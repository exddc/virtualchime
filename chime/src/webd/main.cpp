#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "chime/build_version.h"
#include "chime/chime_config.h"
#include "chime/config_migrate.h"
#include "chime/webd_api.h"
#include "chime/webd_apply_manager.h"
#include "chime/webd_auth.h"
#include "chime/webd_config_store.h"
#include "oc/config/kv_config.h"
#include "oc/http/tls_server.h"
#include "oc/logging/logger.h"
#include "oc/mdns/responder.h"
#include "oc/process/posix_runner.h"
#include "oc/runtime/signal_handler.h"
#include "oc/util/environment.h"
#include "oc/wifi/scan.h"

namespace {

constexpr const char *kChimeConfigPath = "/etc/chime.conf";
constexpr const char *kWpaSupplicantPath = "/etc/wpa_supplicant/wpa_supplicant.conf";
constexpr const char *kTlsCertPath = "/etc/chime-web/tls/cert.pem";
constexpr const char *kTlsKeyPath = "/etc/chime-web/tls/key.pem";
constexpr const char *kUiDistDir = "/usr/local/share/chime-web-ui/dist";
constexpr const char *kBindAddress = "0.0.0.0";
constexpr int kListenPort = 8443;
constexpr const char *kHostLabel = "chime";
constexpr const char *kNetworkRestartCommand = "/etc/init.d/S40network restart";
constexpr const char *kChimeRestartCommand = "/etc/init.d/S99chime restart";
constexpr const char *kObservedTopicsPath = "/var/lib/chime/observed_topics.txt";
constexpr const char *kRingSoundsDir = "/var/lib/chime/ring_sounds";
constexpr const char *kActiveRingSoundPath = "/usr/local/share/chime/ring.wav";
constexpr const char *kAuthDir = "/var/lib/chime/auth";

std::string EnvOrDefault(const char *key, const char *fallback) {
    const std::string value = oc::util::GetEnv(key);
    if (!value.empty()) {
        return value;
    }
    return fallback;
}

int EnvIntOrDefault(const char *key, int fallback) {
    const std::string value = oc::util::GetEnv(key);
    if (value.empty()) {
        return fallback;
    }

    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 1 || parsed > 65535) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool EnvBoolOrDefault(const char *key, bool fallback) {
    const std::string value = oc::config::trim(oc::util::GetEnv(key));
    if (value.empty()) {
        return fallback;
    }
    std::string lowered;
    lowered.reserve(value.size());
    for (const char c : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return fallback;
}

std::string ReadWifiInterfaceOrDefault(const std::string &path) {
    const std::string fallback = chime::ChimeConfig{}.wifi_interface;
    std::ifstream file(path);
    if (!file.is_open()) {
        return fallback;
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = oc::config::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.rfind("wifi_interface=", 0) == 0) {
            const std::string value = oc::config::trim(trimmed.substr(15));
            if (!value.empty()) {
                return value;
            }
        }
    }
    return fallback;
}

void PrintUsage(const char *program) {
    std::cout << "Usage: " << program << " [--version] [--help]\n";
    std::cout << "Environment overrides:\n";
    std::cout << "  CHIME_WEBD_CHIME_CONFIG\n";
    std::cout << "  CHIME_WEBD_WPA_SUPPLICANT\n";
    std::cout << "  CHIME_WEBD_TLS_CERT\n";
    std::cout << "  CHIME_WEBD_TLS_KEY\n";
    std::cout << "  CHIME_WEBD_BIND_ADDRESS\n";
    std::cout << "  CHIME_WEBD_PORT\n";
    std::cout << "  CHIME_WEBD_HOST_LABEL\n";
    std::cout << "  CHIME_WEBD_WIFI_INTERFACE\n";
    std::cout << "  CHIME_WEBD_NETWORK_RESTART_CMD  (executable + argv, no shell)\n";
    std::cout << "  CHIME_WEBD_CHIME_RESTART_CMD    (executable + argv, no shell)\n";
    std::cout << "  CHIME_WEBD_MDNS_ENABLED\n";
    std::cout << "  CHIME_WEBD_UI_DIST_DIR\n";
    std::cout << "  CHIME_WEBD_OBSERVED_TOPICS_PATH\n";
    std::cout << "  CHIME_WEBD_RING_SOUNDS_DIR\n";
    std::cout << "  CHIME_WEBD_ACTIVE_RING_SOUND\n";
    std::cout << "  CHIME_WEBD_AUTH_DIR\n";
    std::cout << "  CHIME_WEBD_BOOTSTRAP_PASSWORD\n";
    std::cout << "  CHIME_WEBD_PAIRING_CODE\n";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc > 1) {
        const std::string arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            PrintCompileTimeVersions();
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        std::cerr << "Unknown option: " << arg << "\n";
        PrintUsage(argv[0]);
        return 2;
    }

    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    oc::logging::StderrLogger logger;
    oc::runtime::SignalHandler signal_handler;
    signal_handler.Install();
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const std::string chime_config_path = EnvOrDefault("CHIME_WEBD_CHIME_CONFIG", kChimeConfigPath);
    const auto migrated = chime::MigratePersistedConfig(chime_config_path);
    if (!migrated.success) {
        if (chime::MigrateFailureBlocksStartup(migrated)) {
            logger.Error("webd", migrated.error);
            return chime::kConfigFatalExitCode;
        }
        logger.Warn("webd", "config migration did not rewrite " + chime_config_path + ": " + migrated.error);
    } else if (migrated.rewritten) {
        logger.Info("webd", "migrated config schema from " + std::to_string(migrated.from_version) + " to " +
                                std::to_string(migrated.to_version));
    }

    const std::string wpa_supplicant_path = EnvOrDefault("CHIME_WEBD_WPA_SUPPLICANT", kWpaSupplicantPath);
    const std::string tls_cert_path = EnvOrDefault("CHIME_WEBD_TLS_CERT", kTlsCertPath);
    const std::string tls_key_path = EnvOrDefault("CHIME_WEBD_TLS_KEY", kTlsKeyPath);
    const std::string ui_dist_dir = EnvOrDefault("CHIME_WEBD_UI_DIST_DIR", kUiDistDir);
    const std::string observed_topics_path = EnvOrDefault("CHIME_WEBD_OBSERVED_TOPICS_PATH", kObservedTopicsPath);
    const std::string ring_sounds_dir = EnvOrDefault("CHIME_WEBD_RING_SOUNDS_DIR", kRingSoundsDir);
    const std::string active_ring_sound_path = EnvOrDefault("CHIME_WEBD_ACTIVE_RING_SOUND", kActiveRingSoundPath);
    const std::string auth_dir = EnvOrDefault("CHIME_WEBD_AUTH_DIR", kAuthDir);
    const std::string bind_address = EnvOrDefault("CHIME_WEBD_BIND_ADDRESS", kBindAddress);
    const int listen_port = EnvIntOrDefault("CHIME_WEBD_PORT", kListenPort);
    const std::string host_label = EnvOrDefault("CHIME_WEBD_HOST_LABEL", kHostLabel);
    const std::string wifi_interface_override = oc::util::GetEnv("CHIME_WEBD_WIFI_INTERFACE");
    const std::string wifi_interface =
        wifi_interface_override.empty() ? ReadWifiInterfaceOrDefault(chime_config_path) : wifi_interface_override;
    const std::string network_restart_command = EnvOrDefault("CHIME_WEBD_NETWORK_RESTART_CMD", kNetworkRestartCommand);
    const std::string chime_restart_command = EnvOrDefault("CHIME_WEBD_CHIME_RESTART_CMD", kChimeRestartCommand);
    const bool mdns_enabled = EnvBoolOrDefault("CHIME_WEBD_MDNS_ENABLED", true);

    chime::webd::ConfigStore config_store(logger, chime_config_path, wpa_supplicant_path);
    oc::wifi::WifiScanner wifi_scanner(logger, wifi_interface);
    oc::process::PosixRunner process_runner;
    oc::process::Command network_restart;
    oc::process::Command chime_restart;
    std::string command_error;
    if (!oc::process::ParseCommand(network_restart_command, &network_restart, &command_error)) {
        logger.Error("webd", "CHIME_WEBD_NETWORK_RESTART_CMD: " + command_error);
        return 1;
    }
    if (!oc::process::ParseCommand(chime_restart_command, &chime_restart, &command_error)) {
        logger.Error("webd", "CHIME_WEBD_CHIME_RESTART_CMD: " + command_error);
        return 1;
    }
    chime::webd::ApplyManager apply_manager(logger, process_runner, std::move(network_restart),
                                            std::move(chime_restart));
    chime::webd::AuthStoreOptions auth_options;
    auth_options.auth_dir = auth_dir;
    auth_options.bootstrap_password = oc::util::GetEnv("CHIME_WEBD_BOOTSTRAP_PASSWORD");
    auth_options.pairing_code_override = oc::util::GetEnv("CHIME_WEBD_PAIRING_CODE");
    chime::webd::AuthStore auth_store(logger, auth_options);
    if (!auth_store.Ready()) {
        logger.Error("webd", "failed to initialize authentication state");
        return 1;
    }
    auth_store.AnnounceSetupSecret();
    chime::webd::WebApi api(logger, config_store, wifi_scanner, apply_manager, auth_store, ui_dist_dir,
                            observed_topics_path, ring_sounds_dir, active_ring_sound_path);
    oc::http::TlsServerConfig tls;
    tls.bind_address = bind_address;
    tls.port = listen_port;
    tls.cert_path = tls_cert_path;
    tls.key_path = tls_key_path;
    tls.cert_organization = "OpenChime";
    tls.cert_common_name = host_label + ".local";
    tls.log_component = "webd";
    oc::http::TlsServer web_server(
        logger, std::move(tls), [&api](const oc::http::HttpRequest &request) { return api.Handle(request); },
        chime::webd::WebApi::OffloadToSlowWorker);
    oc::mdns::MdnsResponder mdns(logger, host_label, wifi_interface);

    if (!web_server.Start()) {
        logger.Error("webd", "failed to start web server");
        return 1;
    }

    if (mdns_enabled && !mdns.Start()) {
        logger.Warn("webd", "mDNS responder failed to start");
    } else if (!mdns_enabled) {
        logger.Info("webd", "mDNS responder disabled by CHIME_WEBD_MDNS_ENABLED");
    }

    logger.Info("webd", "chime-webd started");

    while (!signal_handler.ShouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    mdns.Stop();
    web_server.Stop();
    apply_manager.Stop();

    logger.Info("webd", "chime-webd stopped");
    return 0;
}
