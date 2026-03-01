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
  std::string mqtt_username;
  std::string mqtt_password;
  bool mqtt_tls_enabled = false;
  bool mqtt_tls_validate_certificate = true;
  std::string mqtt_tls_ca_file;
  std::string mqtt_tls_cert_file;
  std::string mqtt_tls_key_file;
  std::vector<std::string> topics;
  int mqtt_subscribe_qos = 0;
  int heartbeat_interval = 60;
  std::string heartbeat_topic = "chime/heartbeat";

  std::string ring_topic = "doorbell/ring";
  std::string sound_path = "/usr/local/share/chime/ring.wav";
  int volume_bell = 80;
  int volume_notifications = 70;
  int volume_other = 70;
  bool audio_enabled = true;

  std::string wifi_interface = "wlan0";
  int wifi_check_interval = 5;
};

vc::config::LoadResult<ChimeConfig> LoadConfig(const std::string& path);

}  // namespace chime

#endif
