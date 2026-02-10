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
}

int main() {
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
