/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2016-2017 XMRig       <support@xmrig.com>
 * Copyright 2017-     BenDr0id    <ben@graef.in>
 *
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cmath>
#include <cc/CCClient.h>
#include <thread>


#include "api/Api.h"
#include "interfaces/IJobResultListener.h"
#include "Mem.h"
#include "log/Log.h"
#include "workers/MultiWorker.h"
#include "workers/Handle.h"
#include "workers/Hashrate.h"
#include "workers/Workers.h"


bool Workers::m_active = false;
bool Workers::m_enabled = true;
Hashrate *Workers::m_hashrate = nullptr;
IJobResultListener *Workers::m_listener = nullptr;
Job Workers::m_job;
std::atomic<int> Workers::m_paused;
std::atomic<uint64_t> Workers::m_sequence;
std::list<JobResult> Workers::m_queue;
std::vector<Handle*> Workers::m_workers;
uint64_t Workers::m_ticks = 0;
uv_async_t Workers::m_async;
uv_mutex_t Workers::m_mutex;
uv_rwlock_t Workers::m_rwlock;
uv_timer_t Workers::m_timer;

uv_rwlock_t Workers::m_rx_dataset_lock;
randomx_cache *Workers::m_rx_cache = nullptr;
randomx_dataset *Workers::m_rx_dataset = nullptr;
uint8_t Workers::m_rx_seed_hash[32] = {};
std::atomic<uint32_t> Workers::m_rx_dataset_init_thread_counter = {};

Job Workers::job()
{
    uv_rwlock_rdlock(&m_rwlock);
    Job job = m_job;
    uv_rwlock_rdunlock(&m_rwlock);

    return job;
}


void Workers::printHashrate(bool detail)
{
    m_hashrate->print();
}


void Workers::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    if (!m_active) {
        return;
    }

    m_paused = enabled ? 0 : 1;
    m_sequence++;
}


void Workers::setJob(const Job &job)
{
    uv_rwlock_wrlock(&m_rwlock);
    m_job = job;
    uv_rwlock_wrunlock(&m_rwlock);

    m_active = true;
    if (!m_enabled) {
        return;
    }

    m_sequence++;
    m_paused = 0;
}


void Workers::start(size_t threads, int64_t affinityMask, int priority)
{
    m_hashrate = new Hashrate(threads);

    uv_mutex_init(&m_mutex);
    uv_rwlock_init(&m_rwlock);
    uv_rwlock_init(&m_rx_dataset_lock);

    m_sequence = 1;
    m_paused   = 1;

    uv_async_init(uv_default_loop(), &m_async, Workers::onResult);
    uv_timer_init(uv_default_loop(), &m_timer);
    uv_timer_start(&m_timer, Workers::onTick, 500, 500);

    for (size_t i = 0; i < threads; ++i) {
        auto handle = new Handle(i, threads, affinityMask, priority);
        m_workers.push_back(handle);
        handle->start(Workers::onReady);
    }
}

void Workers::updateDataset(const uint8_t* seed_hash, const uint32_t num_threads)
{
    // Check if we need to update cache and dataset
    if (memcmp(m_rx_seed_hash, seed_hash, sizeof(m_rx_seed_hash)) == 0)
        return;

    const uint32_t thread_id = m_rx_dataset_init_thread_counter++;
    LOG_NOTICE("Thread %u started updating RandomX dataset", thread_id);

    // Wait for all threads to get here
    do {
        if (m_sequence.load(std::memory_order_relaxed) == 0) {
          // Exit immediately if workers were stopped
          return;
        }
        std::this_thread::yield();
    } while (m_rx_dataset_init_thread_counter.load() != num_threads);

    // One of the threads updates cache
    uv_rwlock_wrlock(&m_rx_dataset_lock);
    if (memcmp(m_rx_seed_hash, seed_hash, sizeof(m_rx_seed_hash)) != 0) {
        memcpy(m_rx_seed_hash, seed_hash, sizeof(m_rx_seed_hash));
        randomx_init_cache(m_rx_cache, m_rx_seed_hash, sizeof(m_rx_seed_hash));
    }
    uv_rwlock_wrunlock(&m_rx_dataset_lock);

    // All threads update dataset
    const uint32_t a = (randomx_dataset_item_count() * thread_id) / num_threads;
    const uint32_t b = (randomx_dataset_item_count() * (thread_id + 1)) / num_threads;
    randomx_init_dataset(m_rx_dataset, m_rx_cache, a, b - a);

    LOG_NOTICE("Thread %u finished updating RandomX dataset", thread_id);

    // Wait for all threads to complete
    --m_rx_dataset_init_thread_counter;
    do {
        if (m_sequence.load(std::memory_order_relaxed) == 0) {
          // Exit immediately if workers were stopped
          return;
        }
        std::this_thread::yield();
    } while (m_rx_dataset_init_thread_counter.load() != 0);
}

randomx_dataset* Workers::getDataset()
{
    if (m_rx_dataset)
        return m_rx_dataset;

    uv_rwlock_wrlock(&m_rx_dataset_lock);
    if (!m_rx_dataset) {
        randomx_dataset* dataset = randomx_alloc_dataset(RANDOMX_FLAG_LARGE_PAGES);
        if (!dataset) {
            dataset = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
        }
        m_rx_cache = randomx_alloc_cache(static_cast<randomx_flags>(RANDOMX_FLAG_JIT | RANDOMX_FLAG_LARGE_PAGES));
        if (!m_rx_cache) {
            m_rx_cache = randomx_alloc_cache(RANDOMX_FLAG_JIT);
        }
        m_rx_dataset = dataset;
    }
    uv_rwlock_wrunlock(&m_rx_dataset_lock);

    return m_rx_dataset;
}

void Workers::stop()
{
    uv_timer_stop(&m_timer);
    m_hashrate->stop();

    uv_close(reinterpret_cast<uv_handle_t*>(&m_async), nullptr);
    m_paused   = 0;
    m_sequence = 0;

    for (auto worker : m_workers) {
        worker->join();
    }
}


void Workers::submit(const JobResult &result, int threadId)
{
    uv_mutex_lock(&m_mutex);
    m_queue.push_back(result);
    uv_mutex_unlock(&m_mutex);

    uv_async_send(&m_async);
}


void Workers::onReady(void *arg)
{
    auto handle = static_cast<Handle*>(arg);
    handle->setWorker(createMultiWorker(handle, Mem::getThreadHashFactor(handle->threadId())));
    handle->worker()->start();
}


void Workers::onResult(uv_async_t *handle)
{
    std::list<JobResult> results;

    uv_mutex_lock(&m_mutex);
    while (!m_queue.empty()) {
        results.push_back(std::move(m_queue.front()));
        m_queue.pop_front();
    }
    uv_mutex_unlock(&m_mutex);

    for (auto result : results) {
        m_listener->onJobResult(result);
    }

    results.clear();
}


void Workers::onTick(uv_timer_t *handle)
{
    for (auto workerHandle : m_workers) {
        if (!workerHandle->worker()) {
            return;
        }

        m_hashrate->add(workerHandle->threadId(), workerHandle->worker()->hashCount(), workerHandle->worker()->timestamp());
    }

    if ((m_ticks++ & 0xF) == 0)  {
        m_hashrate->updateHighest();
    }

#   ifndef XMRIG_NO_API
    Api::tick(m_hashrate);
#   endif

#   ifndef XMRIG_NO_CC
    CCClient::updateHashrate(m_hashrate);
#   endif
}
