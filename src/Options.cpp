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


#include <cstring>
#include <uv.h>

#ifdef _MSC_VER
#   include "getopt/getopt.h"
#else
#   include <getopt.h>
#include <sstream>

#endif


#ifndef XMRIG_NO_HTTPD
#   include <microhttpd.h>
#endif


#include "Cpu.h"
#include "donate.h"
#include "net/Url.h"
#include "Options.h"
#include "Platform.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "version.h"


#ifndef ARRAY_SIZE
#   define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif


Options *Options::m_self = nullptr;

static char const usage[] = "\
Usage: " APP_ID " [OPTIONS]\n\
Options:\n"
# ifndef XMRIG_CC_SERVER
"\
  -a, --algo=ALGO                       cryptonight (default), cryptonight-lite, cryptonight-ultralite or cryptonight-heavy\n\
  -o, --url=URL                         URL of mining server\n\
  -O, --userpass=U:P                    username:password pair for mining server\n\
  -u, --user=USERNAME                   username for mining server\n\
  -p, --pass=PASSWORD                   password for mining server\n\
  -t, --threads=N                       number of miner threads\n\
  -A, --aesni=N                         selection of AES-NI mode (0 auto, 1 on, 2 off)\n\
  -k, --keepalive                       send keepalived for prevent timeout (need pool support)\n\
  -r, --retries=N                       number of times to retry before switch to backup server (default: 5)\n\
  -R, --retry-pause=N                   time to pause between retries (default: 5)\n\
      --pow-variant=V                   specificy the PoW variat to use: \n'auto' (default), '0', '1', '2', 'ipbc', 'xao', 'xtl', 'rto', 'xfh', 'upx', 'turtle', 'hosp', 'r', 'wow', 'double (xcash)', 'zls' (zelerius), 'rwz' (graft)\n\
                                        for further help see: https://github.com/Bendr0id/xmrigCC/wiki/Coin-configurations\n\
      --asm-optimization=V              specificy the ASM optimization to use: -> 'auto' (default), 'intel', 'ryzen', 'bulldozer', 'off' \n\
      --multihash-factor=N              number of hash blocks to process at a time (don't set or 0 enables automatic selection of optimal number of hash blocks)\n\
      --multihash-thread-mask=MASK      limits multihash to given threads (mask), (default: all threads)\n\
      --cpu-affinity                    set process affinity to CPU core(s), mask 0x3 for cores 0 and 1\n\
      --cpu-priority                    set process priority (0 idle, 2 normal to 5 highest)\n\
      --no-huge-pages                   disable huge pages support\n\
      --donate-level=N                  donate level, default 5%% (5 minutes in 100 minutes)\n\
      --user-agent                      set custom user-agent string for pool\n\
      --max-cpu-usage=N                 maximum CPU usage for automatic threads mode (default 75)\n\
      --safe                            safe adjust threads and av settings for current CPU\n\
      --nicehash                        enable nicehash/xmrig-proxy support\n\
      --use-tls                         enable tls on pool communication\n\
      --print-time=N                    print hashrate report every N seconds\n\
      --api-port=N                      port for the miner API\n\
      --api-access-token=T              access token for API\n\
      --api-worker-id=ID                custom worker-id for API\n\
      --reboot-cmd                      command/bat to execute to Reboot miner\n\
      --force-pow-variant               skip pow/variant parsing from pool\n\
      --skip-self-check                 skip self check on startup\n"
# ifndef XMRIG_NO_CC
"\
      --cc-url=URL                      url of the CC Server\n\
      --cc-use-tls                      enable tls encryption for CC communication\n\
      --cc-access-token=T               access token for CC Server\n\
      --cc-worker-id=ID                 custom worker-id for CC Server\n\
      --cc-update-interval-s=N          status update interval in seconds (default: 10 min: 1)\n\
      --cc-use-remote-logging           enable remote logging on CC Server\n\
      --cc-upload-config-on-startup     upload current miner config to CC Server on startup\n"
# endif
# endif

# ifdef XMRIG_CC_SERVER
"\
      --cc-user=USERNAME                CC Server admin user\n\
      --cc-pass=PASSWORD                CC Server admin pass\n\
      --cc-access-token=T               CC Server access token for CC Client\n\
      --cc-port=N                       CC Server port\n\
      --cc-use-tls                      enable tls encryption for CC communication\n\
      --cc-cert-file=FILE               when tls is turned on, use this to point to the right cert file (default: server.pem) \n\
      --cc-key-file=FILE                when tls is turned on, use this to point to the right key file (default: server.key) \n\
      --cc-client-log-lines-history=N   maximum lines of log history kept per miner (default: 100)\n\
      --cc-client-config-folder=FOLDER  Folder contains the client config files\n\
      --cc-pushover-user-key            your user key for pushover notifications\n\
      --cc-pushover-api-token           api token/keytoken of the application for pushover notifications\n\
      --cc-telegram-bot-token           your bot token for telegram notifications\n\
      --cc-telegram-chat-id             your chat-id for telegram notifications\n\
      --cc-push-miner-offline-info      push notification for offline miners and recover push\n\
      --cc-push-miner-zero-hash-info    push notification when miner reports 0 hashrate and recover push\n\
      --cc-push-periodic-mining-status  push periodic status notification (every hour)\n\
      --cc-custom-dashboard=FILE        loads a custom dashboard and serve it to '/'\n"
# endif
"\
      --no-color                        disable colored output\n"
# ifdef HAVE_SYSLOG_H
"\
  -S, --syslog                          use system log for output messages\n"
# endif
"\
  -B, --background                      run the miner in the background\n\
  -c, --config=FILE                     load a JSON-format configuration file\n\
  -l, --log-file=FILE                   log all output to a file\n\
  -h, --help                            display this help and exit\n\
  -V, --version                         output version information and exit\n\
";


static char const short_options[] = "a:c:khBp:Px:r:R:s:t:T:o:u:O:v:Vl:S";


static struct option const options[] = {
    { "algo",             1, nullptr, 'a'  },
    { "api-access-token", 1, nullptr, 4001 },
    { "api-port",         1, nullptr, 4000 },
    { "api-worker-id",    1, nullptr, 4002 },
    { "av",               1, nullptr, 'v'  },
    { "aesni",            1, nullptr, 'A'  },
    { "multihash-factor", 1, nullptr, 'm'  },
    { "background",       0, nullptr, 'B'  },
    { "config",           1, nullptr, 'c'  },
    { "cpu-affinity",     1, nullptr, 1020 },
    { "cpu-priority",     1, nullptr, 1021 },
    { "donate-level",     1, nullptr, 1003 },
    { "help",             0, nullptr, 'h'  },
    { "keepalive",        0, nullptr ,'k'  },
    { "log-file",         1, nullptr, 'l'  },
    { "max-cpu-usage",    1, nullptr, 1004 },
    { "nicehash",         0, nullptr, 1006 },
    { "no-color",         0, nullptr, 1002 },
    { "no-huge-pages",    0, nullptr, 1009 },
    { "pass",             1, nullptr, 'p'  },
    { "print-time",       1, nullptr, 1007 },
    { "retries",          1, nullptr, 'r'  },
    { "retry-pause",      1, nullptr, 'R'  },
    { "safe",             0, nullptr, 1005 },
    { "syslog",           0, nullptr, 'S'  },
    { "threads",          1, nullptr, 't'  },
    { "url",              1, nullptr, 'o'  },
    { "user",             1, nullptr, 'u'  },
    { "user-agent",       1, nullptr, 1008 },
    { "userpass",         1, nullptr, 'O'  },
    { "version",          0, nullptr, 'V'  },
    { "use-tls",          0, nullptr, 1015 },
    { "force-pow-variant", 0, nullptr, 1016 },
    { "pow-variant",      1, nullptr, 1017 },
    { "variant",          1, nullptr, 1017 },
    { "skip-self-check",  0, nullptr, 1018 },
    { "api-port",         1, nullptr, 4000 },
    { "api-access-token", 1, nullptr, 4001 },
    { "api-worker-id",    1, nullptr, 4002 },
    { "reboot-cmd",       1, nullptr, 4021 },
    { "cc-url",           1, nullptr, 4003 },
    { "cc-access-token",  1, nullptr, 4004 },
    { "cc-worker-id",     1, nullptr, 4005 },
    { "cc-update-interval-s",       1, nullptr, 4012 },
    { "cc-port",          1, nullptr, 4006 },
    { "cc-user",          1, nullptr, 4007 },
    { "cc-pass",          1, nullptr, 4008 },
    { "cc-client-config-folder",    1, nullptr, 4009 },
    { "cc-custom-dashboard",        1, nullptr, 4010 },
    { "cc-cert-file",     1, nullptr, 4014 },
    { "cc-key-file",      1, nullptr, 4015 },
    { "cc-use-tls",       0, nullptr, 4016 },
    { "cc-use-remote-logging",          0, nullptr, 4017 },
    { "cc-client-log-lines-history",    1, nullptr, 4018 },
    { "cc-upload-config-on-startup",    0, nullptr, 4019 },
    { "cc-reboot-cmd",       1, nullptr, 4021 },
    { "cc-pushover-user-key",    1, nullptr, 4022 },
    { "cc-pushover-api-token",   1, nullptr, 4023 },
    { "cc-telegram-bot-token",   1, nullptr, 4027 },
    { "cc-telegram-chat-id",     1, nullptr, 4028 },
    { "cc-push-miner-offline-info",        0, nullptr, 4024 },
    { "cc-push-periodic-mining-status",    0, nullptr, 4025 },
    { "cc-push-miner-zero-hash-info",      0, nullptr, 4026 },
    { "daemonized",       0, nullptr, 4011 },
    { "doublehash-thread-mask",     1, nullptr, 4013 },
    { "multihash-thread-mask",      1, nullptr, 4013 },
    { "asm-optimization", 1, nullptr, 4020 },
    { nullptr, 0, nullptr, 0 }
};


static struct option const config_options[] = {
    { "algo",          1, nullptr, 'a'  },
    { "av",            1, nullptr, 'v'  },
    { "aesni",         1, nullptr, 'A'  },
    { "multihash-factor",    1, nullptr, 'm'  },
    { "background",    0, nullptr, 'B'  },
    { "colors",        0, nullptr, 2000 },
    { "cpu-affinity",  1, nullptr, 1020 },
    { "cpu-priority",  1, nullptr, 1021 },
    { "donate-level",  1, nullptr, 1003 },
    { "huge-pages",    0, nullptr, 1009 },
    { "log-file",      1, nullptr, 'l'  },
    { "max-cpu-usage", 1, nullptr, 1004 },
    { "print-time",    1, nullptr, 1007 },
    { "retries",       1, nullptr, 'r'  },
    { "retry-pause",   1, nullptr, 'R'  },
    { "safe",          0, nullptr, 1005 },
    { "syslog",        0, nullptr, 'S'  },
    { "threads",       1, nullptr, 't'  },
    { "user-agent",    1, nullptr, 1008 },
    { "force-pow-variant", 0, nullptr, 1016 },
    { "pow-variant",   1, nullptr, 1017 },
    { "variant",       1, nullptr, 1017 },
    { "skip-self-check", 0, nullptr, 1018 },
    { "doublehash-thread-mask",     1, nullptr, 4013 },
    { "multihash-thread-mask",     1, nullptr, 4013 },
    { "asm-optimization", 1, nullptr, 4020 },
    { "reboot-cmd",       1, nullptr, 4021 },
    { nullptr, 0, nullptr, 0 }
};


static struct option const pool_options[] = {
    { "url",           1, nullptr, 'o'  },
    { "pass",          1, nullptr, 'p'  },
    { "user",          1, nullptr, 'u'  },
    { "userpass",      1, nullptr, 'O'  },
    { "keepalive",     0, nullptr ,'k'  },
    { "nicehash",      0, nullptr, 1006 },
    { "use-tls",       0, nullptr, 1015 },
    { "pow-variant",   1, nullptr, 1017 },
    { "variant",       1, nullptr, 1017 },
    { nullptr, 0, nullptr, 0 }
};


static struct option const api_options[] = {
    { "port",          1, nullptr, 4000 },
    { "access-token",  1, nullptr, 4001 },
    { "worker-id",     1, nullptr, 4002 },
    { nullptr, 0, nullptr, 0 }
};


static struct option const cc_client_options[] = {
    { "url",                    1, nullptr, 4003 },
    { "access-token",           1, nullptr, 4004 },
    { "worker-id",              1, nullptr, 4005 },
    { "update-interval-s",      1, nullptr, 4012 },
    { "use-tls",                0, nullptr, 4016 },
    { "use-remote-logging",     0, nullptr, 4017 },
    { "upload-config-on-startup",  0, nullptr, 4019 },
    { "reboot-cmd",             1, nullptr, 4021 },
    { nullptr, 0, nullptr, 0 }
};

static struct option const cc_server_options[] = {
    { "port",                   1, nullptr, 4006 },
    { "access-token",           1, nullptr, 4004 },
    { "user",                   1, nullptr, 4007 },
    { "pass",                   1, nullptr, 4008 },
    { "client-config-folder",   1, nullptr, 4009 },
    { "custom-dashboard",       1, nullptr, 4010 },
    { "cert-file",              1, nullptr, 4014 },
    { "key-file",               1, nullptr, 4015 },
    { "use-tls",                0, nullptr, 4016 },
    { "client-log-lines-history",   1, nullptr, 4018 },
    { "pushover-user-key",          1, nullptr, 4022 },
    { "pushover-api-token",         1, nullptr, 4023 },
    { "telegram-bot-token",         1, nullptr, 4027 },
    { "telegram-chat-id",           1, nullptr, 4028 },
    { "push-miner-offline-info",        0, nullptr, 4024 },
    { "push-miner-zero-hash-info",      0, nullptr, 4026 },
    { "push-periodic-mining-status",    0, nullptr, 4025 },
    { nullptr, 0, nullptr, 0 }
};

static const char *algo_names[] = {
    "cryptonight",
    "cryptonight-lite",
    "cryptonight-superlite",
    "cryptonight-ultralite",
    "cryptonight-extremelite",
    "cryptonight-heavy"
};

static const char *algo_short_names[] = {
        "cn",
        "cn-lite",
        "cn-superlite",
        "cn-ultralite",
        "cn-extremelite",
        "cn-heavy"
};

constexpr static const char *pow_variant_names[] = {
        "auto",
        "0",
        "1",
        "2",
        "tube",
        "xao",
        "xtl",
        "msr",
        "xhv",
        "rto",
        "xfh",
        "fast2",
        "upx",
        "turtle",
        "hosp",
        "wow",
        "r",
        "xcash",
        "zls",
        "graft",
        "upx2"
};

constexpr static const char *asm_optimization_names[] = {
    "auto",
    "intel",
    "ryzen",
    "bulldozer",
    "off"
};

Options *Options::parse(int argc, char **argv)
{
    auto options = new Options(argc, argv);
    if (options->isReady()) {
        m_self = options;
        return m_self;
    }

    delete options;
    return nullptr;
}


const char *Options::algoName() const
{
    return algo_names[m_algo];
}

const char *Options::algoShortName() const
{
    return algo_short_names[m_algo];
}

Options::Options(int argc, char **argv) :
    m_background(false),
    m_colors(true),
    m_hugePages(true),
    m_ready(false),
    m_safe(false),
    m_syslog(false),
    m_daemonized(false),
    m_ccUseTls(false),
    m_ccUseRemoteLogging(true),
    m_ccUploadConfigOnStartup(true),
    m_ccPushOfflineMiners(false),
    m_ccPushPeriodicStatus(false),
    m_ccPushZeroHashrateMiners(false),
    m_forcePowVariant(false),
    m_skipSelfCheck(false),
    m_fileName(Platform::defaultConfigName()),
    m_apiToken(nullptr),
    m_apiWorkerId(nullptr),
    m_logFile(nullptr),
    m_userAgent(nullptr),
    m_ccHost(nullptr),
    m_ccToken(nullptr),
    m_ccWorkerId(nullptr),
    m_ccAdminUser(nullptr),
    m_ccAdminPass(nullptr),
    m_ccClientConfigFolder(nullptr),
    m_ccCustomDashboard(nullptr),
    m_ccKeyFile(nullptr),
    m_ccCertFile(nullptr),
    m_ccRebootCmd(nullptr),
    m_ccPushoverUser(nullptr),
    m_ccPushoverToken(nullptr),
    m_ccTelegramBotToken(nullptr),
    m_ccTelegramChatId(nullptr),
    m_algo(ALGO_CRYPTONIGHT),
    m_algoVariant(AV0_AUTO),
    m_aesni(AESNI_AUTO),
    m_powVariant(POW_AUTODETECT),
    m_asmOptimization(ASM_AUTODETECT),
    m_hashFactor(0),
    m_apiPort(0),
    m_donateLevel(kDonateLevel),
    m_maxCpuUsage(75),
    m_printTime(60),
    m_priority(-1),
    m_retries(5),
    m_retryPause(5),
    m_threads(0),
    m_ccUpdateInterval(10),
    m_ccPort(0),
    m_ccClientLogLinesHistory(100),
    m_affinity(-1L),
    m_multiHashThreadMask(-1L)
{
    m_pools.push_back(new Url());

    int key;
    while (true) {
        key = getopt_long(argc, argv, short_options, options, nullptr);
        if (key < 0) {
            break;
        }

        if (!parseArg(key, optarg)) {
            return;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unsupported non-option argument '%s'\n", argv[0], argv[optind]);
        return;
    }

    if (!m_pools[0]->isValid() && (!m_ccHost || m_ccPort == 0)) {
        parseConfig(Platform::defaultConfigName());
    }

#ifdef XMRIG_CC_SERVER
    if (m_ccPort == 0) {
        fprintf(stderr, "No CC Server Port supplied. Exiting.\n");
        return;
    }
#else
    #ifndef XMRIG_NO_CC
        if (!m_daemonized) {
            fprintf(stderr, "\"" APP_ID "\" is compiled with CC support, please start the daemon instead.\n");
            return;
        }
    #endif

    if (!m_pools[0]->isValid() && (!m_ccHost || m_ccPort == 0)) {
        fprintf(stderr, "Neither pool nor CCServer URL supplied. Exiting.\n");
        return;
    }
#endif

    optimizeAlgorithmConfiguration();

    if (m_asmOptimization == AsmOptimization::ASM_AUTODETECT) {
        m_asmOptimization = Cpu::asmOptimization();
    }

    for (Url *url : m_pools) {
        url->applyExceptions();
    }

    m_ready = true;
}


Options::~Options()
{
}


bool Options::getJSON(const char *fileName, rapidjson::Document &doc)
{
    uv_fs_t req;
    const int fd = uv_fs_open(uv_default_loop(), &req, fileName, O_RDONLY, 0644, nullptr);
    if (fd < 0) {
        fprintf(stderr, "unable to open %s: %s\n", fileName, uv_strerror(fd));
        return false;
    }

    uv_fs_req_cleanup(&req);

    FILE *fp = fdopen(fd, "rb");
    char buf[8192];
    rapidjson::FileReadStream is(fp, buf, sizeof(buf));

    doc.ParseStream(is);

    uv_fs_close(uv_default_loop(), &req, fd, nullptr);
    uv_fs_req_cleanup(&req);

    if (doc.HasParseError()) {
        printf("%s:%d: %s\n", fileName, (int) doc.GetErrorOffset(), rapidjson::GetParseError_En(doc.GetParseError()));
        return false;
    }

    return doc.IsObject();
}


bool Options::parseArg(int key, const char *arg)
{
    switch (key) {
    case 'a': /* --algo */
        if (!setAlgo(arg)) {
            return false;
        }
        break;

    case 'o': /* --url */
        if (m_pools.size() > 1 || m_pools[0]->isValid()) {
            auto url = new Url(arg);
            if (url->isValid()) {
                m_pools.push_back(url);
            }
            else {
                delete url;
            }
        }
        else {
            m_pools[0]->parse(arg);
        }

        if (!m_pools.back()->isValid()) {
            return false;
        }
        break;

    case 'O': /* --userpass */
        if (!m_pools.back()->setUserpass(arg)) {
            return false;
        }
        break;

    case 'u': /* --user */
        m_pools.back()->setUser(arg);
        break;

    case 'p': /* --pass */
        m_pools.back()->setPassword(arg);
        break;

    case 'l': /* --log-file */
        free(m_logFile);
        m_logFile = strdup(arg);
        break;

    case 4001: /* --access-token */
        free(m_apiToken);
        m_apiToken = strdup(arg);
        break;

    case 4002: /* --worker-id */
        free(m_apiWorkerId);
        m_apiWorkerId = strdup(arg);
        break;

    case 4003: /* --cc-url */
        return parseCCUrl(arg);

    case 4004: /* --cc-access-token */
        free(m_ccToken);
        m_ccToken = strdup(arg);
        break;

    case 4005: /* --cc-worker-id */
        free(m_ccWorkerId);
        m_ccWorkerId = strdup(arg);
        break;

    case 4007: /* --cc-user */
        free(m_ccAdminUser);
        m_ccAdminUser = strdup(arg);
        break;

    case 4008: /* --cc-pass */
        free(m_ccAdminPass);
        m_ccAdminPass = strdup(arg);
        break;

    case 4009: /* --cc-client-config-folder */
        free(m_ccClientConfigFolder);
        m_ccClientConfigFolder = strdup(arg);
        break;

    case 4010: /* --cc-custom-dashboard */
        free(m_ccCustomDashboard);
        m_ccCustomDashboard = strdup(arg);
        break;

    case 4014: /* --cc-cert-file */
        free(m_ccCertFile);
        m_ccCertFile = strdup(arg);
        break;

    case 4015: /* --cc-key-file */
        free(m_ccKeyFile);
        m_ccKeyFile = strdup(arg);
        break;

    case 4011: /* --daemonized */
        m_daemonized = true;
        break;

    case 'r':  /* --retries */
    case 'R':  /* --retry-pause */
    case 'v':  /* --av */
    case 'A':  /* --aesni */
    case 'm':  /* --multihash-factor */
    case 1003: /* --donate-level */
    case 1004: /* --max-cpu-usage */
    case 1007: /* --print-time */
    case 1021: /* --cpu-priority */
    case 4000: /* --api-port */
    case 4006: /* --cc-port */
    case 4012: /* --cc-update-interval-c */
        return parseArg(key, strtol(arg, nullptr, 10));
    case 4018: /* --cc-client-log-lines-history */
        return parseArg(key, strtol(arg, nullptr, 25));

    case 'B':  /* --background */
    case 'k':  /* --keepalive */
    case 'S':  /* --syslog */
    case 1005: /* --safe */
    case 1006: /* --nicehash */

    case 1002: /* --no-color */
    case 1009: /* --no-huge-pages */
        return parseBoolean(key, false);

    case 1015: /* --use-tls */
        return parseBoolean(key, true);

    case 1016: /* --force-pow-variant */
        return parseBoolean(key, true);

    case 1017: /* --pow-variant/--variant */
        return parsePowVariant(arg);

    case 1018: /* --skip-self-check */
        return parseBoolean(key, true);

    case 4016: /* --cc-use-tls */
        return parseBoolean(key, true);

    case 4017: /* --cc-use-remote-logging */
        return parseBoolean(key, true);

    case 4019: /* --cc-upload-config-on-startup */
        return parseBoolean(key, true);

    case 4020: /* --asm-optimization */
        return parseAsmOptimization(arg);

    case 4021: /* --cc-reboot-cmd || --reboot-cmd */
        free(m_ccRebootCmd);
        m_ccRebootCmd = strdup(arg);
        break;

    case 4022: /* --cc-pushover-user-key */
        free(m_ccPushoverUser);
        m_ccPushoverUser = strdup(arg);
        break;

    case 4023: /* --cc-pushover-api-token */
        free(m_ccPushoverToken);
        m_ccPushoverToken = strdup(arg);
        break;

    case 4027: /* --cc-telegram-bot-token */
        free(m_ccTelegramBotToken);
        m_ccTelegramBotToken = strdup(arg);
        break;

    case 4028: /* --cc-telegram-chat-id */
        free(m_ccTelegramChatId);
        m_ccTelegramChatId = strdup(arg);
        break;

    case 4024: /* --cc-push-miner-offline-info */
        return parseBoolean(key, false);

    case 4025: /* --cc-push-periodic-mining-status */
        return parseBoolean(key, false);

    case 4026: /* --cc-push-miner-zero-hash-info */
        return parseBoolean(key, false);

    case 't':  /* --threads */
        if (strncmp(arg, "all", 3) == 0) {
            m_threads = Cpu::threads();
            return true;
        }

        return parseArg(key, strtol(arg, nullptr, 10));

    case 'V': /* --version */
        showVersion();
        return false;

    case 'h': /* --help */
        showUsage(0);
        return false;

    case 'c': /* --config */
        parseConfig(arg);
        break;

    case 1020: { /* --cpu-affinity */
        const char *p  = strstr(arg, "0x");
        return parseArg(key, p ? strtoull(p, nullptr, 16) : strtoull(arg, nullptr, 10));
    }

    case 4013: { /* --multihash-thread-mask */
        const char *p  = strstr(arg, "0x");
        return parseArg(key, p ? strtoull(p, nullptr, 16) : strtoull(arg, nullptr, 10));
    }

    case 1008: /* --user-agent */
        free(m_userAgent);
        m_userAgent = strdup(arg);
        break;

    default:
        showUsage(1);
        return false;
    }

    return true;
}


bool Options::parseArg(int key, uint64_t arg)
{
    switch (key) {
        case 'r': /* --retries */
        if (arg < 1 || arg > 1000) {
            showUsage(1);
            return false;
        }

        m_retries = (int) arg;
        break;

    case 'R': /* --retry-pause */
        if (arg < 1 || arg > 3600) {
            showUsage(1);
            return false;
        }

        m_retryPause = (int) arg;
        break;

    case 't': /* --threads */
        if (arg < 0 || arg > 1024) {
            showUsage(1);
            return false;
        }

        m_threads = (int) arg;
        break;

    case 'v': /* --av */
        showDeprecateWarning("av", "aesni");
        if (arg > 1000) {
            showUsage(1);
            return false;
        }

        m_algoVariant = static_cast<AlgoVariant>(arg);
        break;

    case 'A':  /* --aesni */
        if (arg < AESNI_AUTO || arg > AESNI_OFF) {
            showUsage(1);
            return false;
        }
        m_aesni = static_cast<AesNi>(arg);
        break;

    case 'm':  /* --multihash-factor */
        if (arg > MAX_NUM_HASH_BLOCKS) {
            showUsage(1);
            return false;
        }
        m_hashFactor = arg;
        break;

    case 1003: /* --donate-level */
        if (arg < 1 || arg > 99) {
            return true;
        }

        m_donateLevel = (int) arg;
        break;

    case 1004: /* --max-cpu-usage */
        if (arg < 1 || arg > 100) {
            showUsage(1);
            return false;
        }

        m_maxCpuUsage = (int) arg;
        break;

    case 1007: /* --print-time */
        if (arg > 1000) {
            showUsage(1);
            return false;
        }

        m_printTime = (int) arg;
        break;

    case 1020: /* --cpu-affinity */
        if (arg) {
            m_affinity = arg;
        }
        break;

    case 1021: /* --cpu-priority */
        if (arg <= 5) {
            m_priority = (int) arg;
        }
        break;

    case 4000: /* --api-port */
        if (arg <= 65536) {
            m_apiPort = (int) arg;
        }
        break;

    case 4006: /* --cc-port */
        if (arg <= 65536) {
            m_ccPort = (int) arg;
        }
        break;

    case 4012: /* --cc-update-interval-s */
        if (arg < 1) {
            showUsage(1);
            return false;
        }

        m_ccUpdateInterval = (int) arg;
        break;

    case 4013: /* --multihash-thread-mask */
        if (arg) {
            m_multiHashThreadMask = arg;
        }
        break;

    case 4018: /* --cc-client-log-lines-history */
        m_ccClientLogLinesHistory = (int) arg;
        break;

    default:
        break;
    }

    return true;
}


bool Options::parseBoolean(int key, bool enable)
{
    switch (key) {
    case 'k': /* --keepalive */
        m_pools.back()->setKeepAlive(enable);
        break;

    case 'B': /* --background */
        m_background = enable;
        m_colors = enable ? false : m_colors;
        break;

    case 'S': /* --syslog */
        m_syslog = enable;
        m_colors = enable ? false : m_colors;
        break;

    case 1002: /* --no-color */
        m_colors = enable;
        break;

    case 1005: /* --safe */
        m_safe = enable;
        break;

    case 1006: /* --nicehash */
        m_pools.back()->setNicehash(enable);
        break;

    case 1009: /* --no-huge-pages */
        m_hugePages = enable;
        break;

    case 1015: /* --use-tls */
        m_pools.back()->setUseTls(enable);
        break;

    case 1016: /* --force-pow-variant */
        m_forcePowVariant = enable;
        break;

    case 1018: /* --skip-self-check */
        m_skipSelfCheck = enable;
        break;

    case 2000: /* --colors */
        m_colors = enable;
        break;

    case 4016: /* --cc-use-tls */
        m_ccUseTls = enable;
        break;

    case 4017: /* --cc-use-remote-logging */
        m_ccUseRemoteLogging = enable;
        break;

    case 4019: /* --cc-upload-config-on-startup */
        m_ccUploadConfigOnStartup = enable;
        break;

    case 4024: /* --cc-push-miner-offline-info */
        m_ccPushOfflineMiners = enable;
        break;

    case 4026: /* --cc-push-miner-zero-hash-info */
        m_ccPushZeroHashrateMiners = enable;
        break;

    case 4025: /* --cc-push-periodic-mining-status */
        m_ccPushPeriodicStatus = enable;
        break;

    default:
        break;
    }

    return true;
}


Url *Options::parseUrl(const char *arg) const
{
    auto url = new Url(arg);
    if (!url->isValid()) {
        delete url;
        return nullptr;
    }

    return url;
}


void Options::parseConfig(const char *fileName)
{
    m_fileName = fileName;

    rapidjson::Document doc;
    if (!getJSON(fileName, doc)) {
        return;
    }

    for (auto option : config_options) {
        parseJSON(&option, doc);
    }

    const rapidjson::Value &pools = doc["pools"];
    if (pools.IsArray()) {
        for (const rapidjson::Value &value : pools.GetArray()) {
            if (!value.IsObject()) {
                continue;
            }

            for (auto option : pool_options) {
                parseJSON(&option, value);
            }
        }
    }

    const rapidjson::Value &api = doc["api"];
    if (api.IsObject()) {
        for (auto api_option : api_options) {
            parseJSON(&api_option, api);
        }
    }

    const rapidjson::Value &ccClient = doc["cc-client"];
    if (ccClient.IsObject()) {
        for (auto cc_client_option : cc_client_options) {
            parseJSON(&cc_client_option, ccClient);
        }
    }

    const rapidjson::Value &ccServer = doc["cc-server"];
    if (ccServer.IsObject()) {
        for (auto cc_server_option : cc_server_options) {
            parseJSON(&cc_server_option, ccServer);
        }
    }
}


void Options::parseJSON(const struct option *option, const rapidjson::Value &object)
{
    if (!option->name || !object.HasMember(option->name)) {
        return;
    }

    const rapidjson::Value &value = object[option->name];

    if (option->has_arg && value.IsString()) {
        parseArg(option->val, value.GetString());
    }
    else if (option->has_arg && value.IsUint64()) {
        parseArg(option->val, value.GetUint64());
    }
    else if (!option->has_arg && value.IsBool()) {
        parseBoolean(option->val, value.IsTrue());
    }
}


void Options::showUsage(int status) const
{
    if (status) {
        fprintf(stderr, "Try \"" APP_ID "\" --help' for more information.\n");
    }
    else {
        printf(usage);
    }
}

void Options::showDeprecateWarning(const char* deprecated, const char* newParam) const
{
    fprintf(stderr, "Parameter \"%s\" is deprecated, please used \"%s\" instead.\n", deprecated, newParam);
}

void Options::showVersion()
{
    printf(APP_NAME " " APP_VERSION "\n built on " __DATE__

#   if defined(__clang__)
    " with clang " __clang_version__);
#   elif defined(__GNUC__)
    " with GCC");
    printf(" %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#   elif defined(_MSC_VER)
    " with MSVC");
    printf(" %d", MSVC_VERSION);
#   else
    );
#   endif

    printf("\n features:"
#   if defined(__i386__) || defined(_M_IX86)
    " i386"
#   elif defined(__x86_64__) || defined(_M_AMD64)
    " x86_64"
#   endif

#   if defined(__AES__) || defined(_MSC_VER)
    " AES-NI"
#   endif
    "\n");

    printf("\nlibuv/%s\n", uv_version_string());

#   ifndef XMRIG_NO_HTTPD
    printf("libmicrohttpd/%s\n", MHD_get_version());
#   endif
}


bool Options::setAlgo(const char *algo)
{
    for (size_t i = 0; i < ARRAY_SIZE(algo_names); i++) {
        if (algo_names[i] && !strcmp(algo, algo_names[i])) {
            m_algo = static_cast<Algo>(i);
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cn-lite") || !strcmp(algo, "cryptonight-light"))) {
            m_algo = ALGO_CRYPTONIGHT_LITE;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cn-super-lite") || !strcmp(algo, "cryptonight-super-lite") || !strcmp(algo, "cryptonight-superlight"))) {
            m_algo = ALGO_CRYPTONIGHT_SUPERLITE;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cn-ultra-lite") || !strcmp(algo, "cryptonight-ultra-lite") || !strcmp(algo, "cryptonight-ultralight") || !strcmp(algo, "cryptonight-turtle") || !strcmp(algo, "cn-turtle") || !strcmp(algo, "cryptonight-pico") || !strcmp(algo, "cn-pico"))) {
            m_algo = ALGO_CRYPTONIGHT_ULTRALITE;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cn-extreme-lite") || !strcmp(algo, "cryptonight-extreme-lite") || !strcmp(algo, "cryptonight-extremelight") || !strcmp(algo, "cryptonight-upx2") || !strcmp(algo, "cn-upx2") || !strcmp(algo, "cryptonight-femto") || !strcmp(algo, "cn-femto"))) {
            m_algo = ALGO_CRYPTONIGHT_EXTREMELITE;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cryptonight-lite-ipbc") || !strcmp(algo, "cryptonight-light-ipbc") || !strcmp(algo, "cn-lite-ipbc"))) {
            showDeprecateWarning("cryptonight-light-ipbc", "cryptonight-light (with variant \"ipbc\")");
            m_algo = ALGO_CRYPTONIGHT_LITE;
            m_powVariant = POW_TUBE;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cryptonight-heavy") || !strcmp(algo, "cn-heavy"))) {
            m_algo = ALGO_CRYPTONIGHT_HEAVY;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1 && (!strcmp(algo, "cryptonight-hospital") || !strcmp(algo, "cryptonight-hosp"))) {
            m_algo = ALGO_CRYPTONIGHT;
            m_powVariant = POW_HOSP;
            break;
        }

        if (i == ARRAY_SIZE(algo_names) - 1) {
            showUsage(1);
            return false;
        }
    }

    return true;
}

bool Options::parsePowVariant(const char *powVariant)
{
    for (size_t i = 0; i < ARRAY_SIZE(pow_variant_names); i++) {
        if (pow_variant_names[i] && !strcmp(powVariant, pow_variant_names[i])) {
            m_powVariant = static_cast<PowVariant>(i);
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "auto") || !strcmp(powVariant, "-1"))) {
            m_powVariant = POW_AUTODETECT;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "cnv1") || !strcmp(powVariant, "monerov7") || !strcmp(powVariant, "aeonv7") || !strcmp(powVariant, "v7"))) {
            m_powVariant = POW_V1;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "cnv2") || !strcmp(powVariant, "monerov8") || !strcmp(powVariant, "aeonv8") || !strcmp(powVariant, "v8"))) {
            m_powVariant = POW_V2;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && !strcmp(powVariant, "stellite")) {
            m_powVariant = POW_XTL;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "xao") || !strcmp(powVariant, "alloy"))) {
            m_powVariant = POW_ALLOY;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "ipbc") || !strcmp(powVariant, "bittube"))) {
            m_powVariant = POW_TUBE;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && !strcmp(powVariant, "haven")) {
            m_powVariant = POW_XHV;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && !strcmp(powVariant, "arto")) {
            m_powVariant = POW_RTO;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "freehaven") || !strcmp(powVariant, "faven") || !strcmp(powVariant, "swap"))) {
            m_powVariant = POW_XFH;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "stellitev9") || !strcmp(powVariant, "xtlv2") ||
                                                       !strcmp(powVariant, "half") || !strcmp(powVariant, "msr2") ||
                                                       !strcmp(powVariant, "xtlv9"))) {
            m_powVariant = POW_FAST_2;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && !strcmp(powVariant, "uplexa")) {
            m_powVariant = POW_UPX;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "trtl") || !strcmp(powVariant, "turtlev2") || !strcmp(powVariant, "pico"))) {
            m_powVariant = POW_TURTLE;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "upx2") || !strcmp(powVariant, "upxv2") || !strcmp(powVariant, "femto"))) {
            m_powVariant = POW_UPX2;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "hosp") || !strcmp(powVariant, "hospital"))) {
            m_powVariant = POW_HOSP;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && !strcmp(powVariant, "wow")) {
            m_powVariant = POW_WOW;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "4") || !strcmp(powVariant, "r") || !strcmp(powVariant, "cnv4") || !strcmp(powVariant, "cnv5"))) {
            m_powVariant = POW_V4;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "xcash") || !strcmp(powVariant, "heavyx") || !strcmp(powVariant, "double"))) {
            m_powVariant = POW_DOUBLE;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "zelerius") || !strcmp(powVariant, "zls") || !strcmp(powVariant, "zlx"))) {
            m_powVariant = POW_ZELERIUS;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1 && (!strcmp(powVariant, "rwz") || !strcmp(powVariant, "graft"))) {
            m_powVariant = POW_RWZ;
            break;
        }

        if (i == ARRAY_SIZE(pow_variant_names) - 1) {
            showUsage(1);
            return false;
        }
    }

    return true;
}


bool Options::parseAsmOptimization(const char *asmOptimization)
{
    for (size_t i = 0; i < ARRAY_SIZE(pow_variant_names); i++) {
        if (pow_variant_names[i] && !strcmp(asmOptimization, asm_optimization_names[i])) {
            m_asmOptimization = static_cast<AsmOptimization>(i);
            break;
        }

        if (i == ARRAY_SIZE(asm_optimization_names) - 1 && (!strcmp(asmOptimization, "none") || !strcmp(asmOptimization, "0"))) {
            m_asmOptimization = ASM_OFF;
            break;
        }

        if (i == ARRAY_SIZE(asm_optimization_names) - 1) {
            showUsage(1);
            return false;
        }
    }

    return true;
}


void Options::optimizeAlgorithmConfiguration()
{
    // backwards compatibility for configs still setting algo variant (av)
    // av overrides mutli-hash and aesni when they are either not set or when they are set to auto
    if (m_algoVariant != AV0_AUTO) {
        size_t hashFactor = m_hashFactor;
        AesNi aesni = m_aesni;
        switch (m_algoVariant) {
            case AV1_AESNI:
                hashFactor = 1;
                aesni = AESNI_ON;
                break;
            case AV2_AESNI_DOUBLE:
                hashFactor = 2;
                aesni = AESNI_ON;
                break;
            case AV3_SOFT_AES:
                hashFactor = 1;
                aesni = AESNI_OFF;
                break;
            case AV4_SOFT_AES_DOUBLE:
                hashFactor = 2;
                aesni = AESNI_OFF;
                break;
            case AV0_AUTO:
            default:
                // no change
                break;
        }
        if (m_hashFactor == 0) {
            m_hashFactor = hashFactor;
        }
        if (m_aesni == AESNI_AUTO) {
            m_aesni = aesni;
        }
    }

    AesNi aesniFromCpu = Cpu::hasAES() ? AESNI_ON : AESNI_OFF;
    if (m_aesni == AESNI_AUTO || m_safe) {
        m_aesni = aesniFromCpu;
    }

    if ((m_algo == Options::ALGO_CRYPTONIGHT_HEAVY || m_powVariant ==  PowVariant::POW_XFH) && m_hashFactor > 3) {
        fprintf(stderr, "Maximum supported hashfactor for cryptonight-heavy and XFH is: 3\n");
        m_hashFactor = 3;
    }

    Cpu::optimizeParameters(m_threads, m_hashFactor, m_algo, m_powVariant, m_maxCpuUsage, m_safe);
}

bool Options::parseCCUrl(const char* url)
{
    free(m_ccHost);

    const char *port = strchr(url, ':');
    if (!port) {
        m_ccHost = strdup(url);
        m_ccPort = kDefaultCCPort;
    } else {
        const size_t size = port++ - url + 1;
        m_ccHost = static_cast<char*>(malloc(size));
        memcpy(m_ccHost, url, size - 1);
        m_ccHost[size - 1] = '\0';

        m_ccPort = (uint16_t) strtol(port, nullptr, 10);

        if (m_ccPort < 0 || m_ccPort > 65536) {
            m_ccPort = kDefaultCCPort;
            return false;
        }
    }

    return true;
}
