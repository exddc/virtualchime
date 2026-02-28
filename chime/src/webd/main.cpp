#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "chime/webd_apply_manager.h"
#include "chime/webd_config_store.h"
#include "chime/webd_mdns.h"
#include "chime/webd_web_server.h"
#include "chime/webd_wifi_scan.h"
#include "vc/config/kv_config.h"
#include "vc/logging/logger.h"
#include "vc/runtime/signal_handler.h"
#include "vc/util/environment.h"

namespace {

constexpr const char *kChimeConfigPath = "/etc/chime.conf";
constexpr const char *kWpaSupplicantPath = "/etc/wpa_supplicant/wpa_supplicant.conf";
constexpr const char *kTlsCertPath = "/etc/chime-web/tls/cert.pem";
constexpr const char *kTlsKeyPath = "/etc/chime-web/tls/key.pem";
constexpr const char *kUiDistDir = "/usr/local/share/chime-web-ui/dist";
constexpr const char *kBindAddress = "0.0.0.0";
constexpr int kListenPort = 8443;
constexpr const char *kHostLabel = "chime";
constexpr const char *kNetworkRestartCommand = "/etc/init.d/S40network restart >/dev/null 2>&1";
constexpr const char *kChimeRestartCommand = "/etc/init.d/S99chime restart >/dev/null 2>&1";
constexpr const char *kObservedTopicsPath = "/var/lib/chime/observed_topics.txt";
constexpr const char *kRingSoundsDir = "/var/lib/chime/ring_sounds";
constexpr const char *kActiveRingSoundPath = "/usr/local/share/chime/ring.wav";

std::string EnvOrDefault(const char *key, const char *fallback) {
    const std::string value = vc::util::GetEnv(key);
    if (!value.empty()) {
        return value;
    }
    return fallback;
}

int EnvIntOrDefault(const char *key, int fallback) {
    const std::string value = vc::util::GetEnv(key);
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
    const std::string value = vc::config::trim(vc::util::GetEnv(key));
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
    std::ifstream file(path);
    if (!file.is_open()) {
        return "wlan0";
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = vc::config::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.rfind("wifi_interface=", 0) == 0) {
            const std::string value = vc::config::trim(trimmed.substr(15));
            if (!value.empty()) {
                return value;
            }
        }
    }
    return "wlan0";
}

void PrintUsage(const char *program) {
    std::cout << "Usage: " << program << " [--help]\n";
    std::cout << "Environment overrides:\n";
    std::cout << "  CHIME_WEBD_CHIME_CONFIG\n";
    std::cout << "  CHIME_WEBD_WPA_SUPPLICANT\n";
    std::cout << "  CHIME_WEBD_TLS_CERT\n";
    std::cout << "  CHIME_WEBD_TLS_KEY\n";
    std::cout << "  CHIME_WEBD_BIND_ADDRESS\n";
    std::cout << "  CHIME_WEBD_PORT\n";
    std::cout << "  CHIME_WEBD_HOST_LABEL\n";
    std::cout << "  CHIME_WEBD_WIFI_INTERFACE\n";
    std::cout << "  CHIME_WEBD_NETWORK_RESTART_CMD\n";
    std::cout << "  CHIME_WEBD_CHIME_RESTART_CMD\n";
    std::cout << "  CHIME_WEBD_MDNS_ENABLED\n";
    std::cout << "  CHIME_WEBD_UI_DIST_DIR\n";
    std::cout << "  CHIME_WEBD_OBSERVED_TOPICS_PATH\n";
    std::cout << "  CHIME_WEBD_RING_SOUNDS_DIR\n";
    std::cout << "  CHIME_WEBD_ACTIVE_RING_SOUND\n";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc > 1) {
        const std::string arg = argv[1];
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

    vc::logging::StderrLogger logger;
    vc::runtime::SignalHandler signal_handler;
    signal_handler.Install();
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const std::string chime_config_path = EnvOrDefault("CHIME_WEBD_CHIME_CONFIG", kChimeConfigPath);
    const std::string wpa_supplicant_path = EnvOrDefault("CHIME_WEBD_WPA_SUPPLICANT", kWpaSupplicantPath);
    const std::string tls_cert_path = EnvOrDefault("CHIME_WEBD_TLS_CERT", kTlsCertPath);
    const std::string tls_key_path = EnvOrDefault("CHIME_WEBD_TLS_KEY", kTlsKeyPath);
    const std::string ui_dist_dir = EnvOrDefault("CHIME_WEBD_UI_DIST_DIR", kUiDistDir);
    const std::string observed_topics_path = EnvOrDefault("CHIME_WEBD_OBSERVED_TOPICS_PATH", kObservedTopicsPath);
    const std::string ring_sounds_dir = EnvOrDefault("CHIME_WEBD_RING_SOUNDS_DIR", kRingSoundsDir);
    const std::string active_ring_sound_path = EnvOrDefault("CHIME_WEBD_ACTIVE_RING_SOUND", kActiveRingSoundPath);
    const std::string bind_address = EnvOrDefault("CHIME_WEBD_BIND_ADDRESS", kBindAddress);
    const int listen_port = EnvIntOrDefault("CHIME_WEBD_PORT", kListenPort);
    const std::string host_label = EnvOrDefault("CHIME_WEBD_HOST_LABEL", kHostLabel);
    const std::string wifi_interface_override = vc::util::GetEnv("CHIME_WEBD_WIFI_INTERFACE");
    const std::string wifi_interface =
        wifi_interface_override.empty() ? ReadWifiInterfaceOrDefault(chime_config_path) : wifi_interface_override;
    const std::string network_restart_command = EnvOrDefault("CHIME_WEBD_NETWORK_RESTART_CMD", kNetworkRestartCommand);
    const std::string chime_restart_command = EnvOrDefault("CHIME_WEBD_CHIME_RESTART_CMD", kChimeRestartCommand);
    const bool mdns_enabled = EnvBoolOrDefault("CHIME_WEBD_MDNS_ENABLED", true);

    chime::webd::ConfigStore config_store(logger, chime_config_path, wpa_supplicant_path);
    chime::webd::WifiScanner wifi_scanner(logger, wifi_interface);
    chime::webd::ApplyManager apply_manager(logger, network_restart_command, chime_restart_command);
    chime::webd::WebServer web_server(logger, config_store, wifi_scanner, apply_manager, bind_address, listen_port,
                                      tls_cert_path, tls_key_path, ui_dist_dir, observed_topics_path, ring_sounds_dir,
                                      active_ring_sound_path);
    chime::webd::MdnsResponder mdns(logger, host_label, wifi_interface);

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

    logger.Info("webd", "chime-webd stopped");
    return 0;
}
