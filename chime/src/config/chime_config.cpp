#include "chime/chime_config.h"

namespace chime {
namespace {
constexpr vc::config::Field<ChimeConfig> kConfigFields[] = {
    {"mqtt_host", vc::config::parse_string<ChimeConfig, &ChimeConfig::host>, true},
    {"mqtt_port", vc::config::parse_int<ChimeConfig, &ChimeConfig::port>, true},
    {"mqtt_client_id",
     vc::config::parse_string<ChimeConfig, &ChimeConfig::client_id>, false},
    {"mqtt_topics", vc::config::parse_csv<ChimeConfig, &ChimeConfig::topics>, true},
    {"mqtt_subscribe_qos",
     vc::config::parse_int<ChimeConfig, &ChimeConfig::mqtt_subscribe_qos, 0, 2>,
     false},
    {"heartbeat_interval",
     vc::config::parse_int<ChimeConfig, &ChimeConfig::heartbeat_interval, 0, 3600>,
     false},
    {"heartbeat_topic",
     vc::config::parse_string<ChimeConfig, &ChimeConfig::heartbeat_topic>, false},
    {"ring_topic",
     vc::config::parse_string<ChimeConfig, &ChimeConfig::ring_topic>, false},
    {"sound_path",
     vc::config::parse_string<ChimeConfig, &ChimeConfig::sound_path>, false},
    {"audio_enabled",
     vc::config::parse_bool<ChimeConfig, &ChimeConfig::audio_enabled>, false},
    {"wifi_interface",
     vc::config::parse_string<ChimeConfig, &ChimeConfig::wifi_interface>, false},
    {"wifi_check_interval",
     vc::config::parse_int<ChimeConfig, &ChimeConfig::wifi_check_interval, 0, 3600>,
     false},
};
}  // namespace

vc::config::LoadResult<ChimeConfig> LoadConfig(const std::string& path) {
  return vc::config::load(path, ChimeConfig{}, kConfigFields);
}

}  // namespace chime
