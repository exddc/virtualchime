#include <fstream>
#include <iostream>
#include <string>

#include "chime/audio_player.h"
#include "chime/chime_config.h"
#include "chime/chime_service.h"
#include "chime/wifi_monitor.h"
#include "vc/logging/logger.h"
#include "vc/runtime/signal_handler.h"
#include "vc/util/environment.h"

namespace {
constexpr const char* kDefaultConfigPath = "/etc/chime.conf";
constexpr const char* kReleaseFilePath = "/etc/virtualchime-release";

#ifndef CHIME_APP_VERSION
#define CHIME_APP_VERSION "dev"
#endif

#ifndef VIRTUALCHIME_OS_VERSION
#define VIRTUALCHIME_OS_VERSION "dev"
#endif

#ifndef CHIME_CONFIG_VERSION
#define CHIME_CONFIG_VERSION "dev"
#endif

#ifndef CHIME_BUILD_ID
#define CHIME_BUILD_ID "unknown"
#endif

std::string ReadReleaseValue(const std::string& key) {
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

void PrintUsage(const char* program) {
  std::cout << "Usage: " << program << " [--version]\n";
}

void PrintVersion() {
  std::cout << "CHIME_APP_VERSION=" << CHIME_APP_VERSION << "\n";
  std::cout << "CHIME_BUILD_ID=" << CHIME_BUILD_ID << "\n";
  std::cout << "VIRTUALCHIME_OS_VERSION=" << VIRTUALCHIME_OS_VERSION << "\n";
  std::cout << "CHIME_CONFIG_VERSION=" << CHIME_CONFIG_VERSION << "\n";

  const std::string runtime_os_version =
      ReadReleaseValue("VIRTUALCHIME_OS_VERSION");
  if (!runtime_os_version.empty()) {
    std::cout << "RUNTIME_OS_VERSION=" << runtime_os_version << "\n";
  }

  const std::string runtime_kernel_version =
      ReadReleaseValue("LINUX_KERNEL_RELEASE");
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
}

int main(int argc, char* argv[]) {
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

  vc::logging::StderrLogger logger;
  vc::runtime::SignalHandler signal_handler;
  signal_handler.Install();

  const std::string config_env = vc::util::GetEnv("CHIME_CONFIG");
  const std::string config_path =
      config_env.empty() ? kDefaultConfigPath : config_env;

  auto result = chime::LoadConfig(config_path);
  if (!result) {
    logger.Error("chime", result.error);
    return 1;
  }

  const std::string client_id_override = vc::util::GetEnv("CHIME_MQTT_CLIENT_ID");
  if (!client_id_override.empty()) {
    result.config.client_id = client_id_override;
    logger.Info("mqtt", "client_id override from CHIME_MQTT_CLIENT_ID");
  }

  logger.Info("chime", "loaded config from " + config_path);

  chime::AplayAudioPlayer audio_player(logger);
  chime::LinuxWifiMonitor wifi_monitor;
  chime::ChimeService service(result.config, logger, audio_player, wifi_monitor);

  return service.Run(signal_handler);
}
