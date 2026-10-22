#include <fstream>
#include <iostream>
#include <string>

#include "chime/audio_player.h"
#include "chime/build_version.h"
#include "chime/chime_config.h"
#include "chime/chime_service.h"
#include "chime/config_migrate.h"
#include "chime/wifi_monitor.h"
#include "oc/logging/logger.h"
#include "oc/process/posix_runner.h"
#include "oc/runtime/signal_handler.h"
#include "oc/util/environment.h"
#include "oc/util/platform.h"

namespace {
constexpr const char *kDefaultConfigPath = "/etc/chime.conf";
constexpr const char *kReleaseFilePath = "/etc/openchime-release";

std::string ReadReleaseValue(const std::string &key) {
    std::ifstream release_file(kReleaseFilePath);
    if (!release_file.is_open()) {
        return "";
    }

    const std::string prefix = key + "=";
    std::string line;
    while (std::getline(release_file, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }

    return "";
}

void PrintUsage(const char *program) {
    std::cout << "Usage: " << program << " [--version]\n";
}

void PrintVersion() {
    PrintCompileTimeVersions();

    const std::string runtime_os_version = ReadReleaseValue("OPENCHIME_OS_VERSION");
    if (!runtime_os_version.empty()) {
        std::cout << "RUNTIME_OS_VERSION=" << runtime_os_version << "\n";
    }

    const std::string runtime_kernel_version = ReadReleaseValue("LINUX_KERNEL_RELEASE");
    if (!runtime_kernel_version.empty()) {
        std::cout << "RUNTIME_KERNEL_RELEASE=" << runtime_kernel_version << "\n";
    }

    const std::string runtime_app_build_id = ReadReleaseValue("CHIME_BUILD_ID");
    if (!runtime_app_build_id.empty()) {
        std::cout << "RUNTIME_CHIME_BUILD_ID=" << runtime_app_build_id << "\n";
    }

    const std::string runtime_source_sha = ReadReleaseValue("SOURCE_GIT_SHA");
    if (!runtime_source_sha.empty()) {
        std::cout << "RUNTIME_SOURCE_GIT_SHA=" << runtime_source_sha << "\n";
    }
}
} // namespace

int main(int argc, char *argv[]) {
    if (argc > 1) {
        const std::string arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            PrintVersion();
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

    const std::string config_env = oc::util::GetEnv("CHIME_CONFIG");
    const std::string config_path = config_env.empty() ? kDefaultConfigPath : config_env;

    const auto migrated = chime::MigratePersistedConfig(config_path);
    if (!migrated.success) {
        if (chime::MigrateFailureBlocksStartup(migrated)) {
            logger.Error("chime", migrated.error);
            return chime::kConfigFatalExitCode;
        }
        logger.Warn("chime", "config migration did not rewrite " + config_path + ": " + migrated.error);
    } else if (migrated.rewritten) {
        logger.Info("chime", "migrated config schema from " + std::to_string(migrated.from_version) + " to " +
                                 std::to_string(migrated.to_version));
        for (const auto &value : migrated.replaced_values) {
            logger.Info("chime", "replaced default " + value);
        }
    }

    auto result = chime::LoadConfig(config_path);
    if (!result) {
        logger.Error("chime", result.error);
        return 1;
    }

    const std::string client_id_override = oc::util::GetEnv("CHIME_MQTT_CLIENT_ID");
    if (!client_id_override.empty()) {
        result.config.mqtt_client_id = client_id_override;
        logger.Info("mqtt", "client_id override from CHIME_MQTT_CLIENT_ID");
    }

    const std::string mqtt_username_override = oc::util::GetEnv("CHIME_MQTT_USERNAME");
    if (!mqtt_username_override.empty()) {
        result.config.mqtt_username = mqtt_username_override;
        logger.Info("mqtt", "username override from CHIME_MQTT_USERNAME");
    }

    const std::string mqtt_password_override = oc::util::GetEnv("CHIME_MQTT_PASSWORD");
    if (!mqtt_password_override.empty()) {
        result.config.mqtt_password = mqtt_password_override;
        logger.Info("mqtt", "password override from CHIME_MQTT_PASSWORD");
    }

    logger.Info("chime", "loaded config from " + config_path);

    oc::process::PosixRunner process_runner;
    chime::AplayAudioPlayer audio_player(logger, process_runner, oc::util::IsLinux());
    chime::LinuxWifiMonitor wifi_monitor;
    chime::ChimeService service(result.config, logger, audio_player, wifi_monitor);

    return service.Run(signal_handler);
}
