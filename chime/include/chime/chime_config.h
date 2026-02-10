#ifndef CHIME_CHIME_CONFIG_H
#define CHIME_CHIME_CONFIG_H

#include <string>
#include <vector>

#include "vc/config/kv_config.h"

namespace chime {

struct ChimeConfig {
  std::string host;
  int port = 0;
  std::string client_id = "chime";
  std::vector<std::string> topics;
  int mqtt_subscribe_qos = 0;
  int heartbeat_interval = 60;
  std::string heartbeat_topic = "chime/heartbeat";

  std::string ring_topic = "doorbell/ring";
  std::string sound_path = "/usr/local/share/chime/ring.wav";
  bool audio_enabled = true;

  std::string wifi_interface = "wlan0";
  int wifi_check_interval = 5;
};

vc::config::LoadResult<ChimeConfig> LoadConfig(const std::string& path);

}  // namespace chime

#endif
