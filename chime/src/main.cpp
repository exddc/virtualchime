#include "config.h"

#include <mosquitto.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr const char* kDefaultConfigPath = "/etc/chime.conf";
constexpr int kKeepaliveSeconds = 60;

volatile std::sig_atomic_t g_should_stop = 0;

// Audio playback state
std::atomic<bool> g_audio_playing{false};

struct ChimeConfig {
  // MQTT settings
  std::string host;
  int port = 0;
  std::string client_id = "chime";
  std::vector<std::string> topics;
  int heartbeat_interval = 60;
  std::string heartbeat_topic = "chime/heartbeat";

  // Audio settings
  std::string ring_topic = "doorbell/ring";
  std::string sound_path = "/usr/local/share/chime/ring.wav";
  bool audio_enabled = true;
};

constexpr config::Field<ChimeConfig> kConfigFields[] = {
    {"mqtt_host", config::parse_string<ChimeConfig, &ChimeConfig::host>, true},
    {"mqtt_port", config::parse_int<ChimeConfig, &ChimeConfig::port>, true},
    {"mqtt_client_id",
     config::parse_string<ChimeConfig, &ChimeConfig::client_id>, false},
    {"mqtt_topics", config::parse_csv<ChimeConfig, &ChimeConfig::topics>, true},
    {"heartbeat_interval",
     config::parse_int<ChimeConfig, &ChimeConfig::heartbeat_interval, 0, 3600>,
     false},
    {"heartbeat_topic",
     config::parse_string<ChimeConfig, &ChimeConfig::heartbeat_topic>, false},
    {"ring_topic", config::parse_string<ChimeConfig, &ChimeConfig::ring_topic>,
     false},
    {"sound_path", config::parse_string<ChimeConfig, &ChimeConfig::sound_path>,
     false},
    {"audio_enabled",
     config::parse_bool<ChimeConfig, &ChimeConfig::audio_enabled>, false},
};

std::string get_env(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return "";
  }
  return std::string(value);
}

bool file_exists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

bool is_linux() {
#ifdef __linux__
  return true;
#else
  return false;
#endif
}

void play_sound(const std::string& path) {
  // Skip if already playing
  bool expected = false;
  if (!g_audio_playing.compare_exchange_strong(expected, true)) {
    std::cerr << "[audio] Already playing, skipping\n";
    return;
  }

  // In testing environment just log
  if (!is_linux()) {
    std::cerr << "[audio] (local) Would play: " << path << "\n";
    g_audio_playing = false;
    return;
  }

  if (!file_exists(path)) {
    std::cerr << "[audio] Sound file not found: " << path << "\n";
    g_audio_playing = false;
    return;
  }

  // Detached thread to play audio
  std::thread([path]() {
    std::cerr << "[audio] Playing: " << path << "\n";
    const std::string cmd = "aplay -q \"" + path + "\" 2>/dev/null";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
      std::cerr << "[audio] aplay failed with code " << rc << "\n";
    } else {
      std::cerr << "[audio] Playback complete\n";
    }
    g_audio_playing = false;
  }).detach();
}

void handle_signal(int) { g_should_stop = 1; }

void on_connect(struct mosquitto* mosq, void* obj, int rc) {
  auto* config = static_cast<ChimeConfig*>(obj);
  if (rc != MOSQ_ERR_SUCCESS) {
    std::cerr << "[mqtt] Connect failed: " << mosquitto_strerror(rc) << "\n";
    return;
  }

  std::cerr << "[mqtt] Connected\n";
  for (const auto& topic : config->topics) {
    const int sub_rc = mosquitto_subscribe(mosq, nullptr, topic.c_str(), 0);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
      std::cerr << "[mqtt] Subscribe failed for " << topic << ": "
                << mosquitto_strerror(sub_rc) << "\n";
    } else {
      std::cerr << "[mqtt] Subscribed to " << topic << "\n";
    }
  }
}

void on_disconnect(struct mosquitto*, void*, int rc) {
  std::cerr << "[mqtt] Disconnected: " << rc << "\n";
}

void on_message(struct mosquitto*, void* obj,
                const struct mosquitto_message* msg) {
  if (msg == nullptr || msg->topic == nullptr) {
    return;
  }

  auto* config = static_cast<ChimeConfig*>(obj);
  std::string payload;
  if (msg->payload != nullptr && msg->payloadlen > 0) {
    payload.assign(static_cast<const char*>(msg->payload), msg->payloadlen);
  }

  std::cout << "[mqtt] topic=" << msg->topic << " payload=" << payload << "\n";

  // Check if this is a ring message
  if (config->audio_enabled && msg->topic == config->ring_topic) {
    std::cerr << "[chime] Ring received!\n";
    play_sound(config->sound_path);
  }
}
}

int main() {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  const std::string config_env = get_env("CHIME_CONFIG");
  const std::string config_path =
      config_env.empty() ? kDefaultConfigPath : config_env;

  auto result = config::load(config_path, ChimeConfig{}, kConfigFields);
  if (!result) {
    std::cerr << "[chime] " << result.error << "\n";
    return 1;
  }
  ChimeConfig& cfg = result.config;

  mosquitto_lib_init();

  mosquitto* mosq = mosquitto_new(cfg.client_id.c_str(), true, &cfg);
  if (mosq == nullptr) {
    std::cerr << "[mqtt] Failed to create client\n";
    mosquitto_lib_cleanup();
    return 1;
  }

  mosquitto_connect_callback_set(mosq, on_connect);
  mosquitto_disconnect_callback_set(mosq, on_disconnect);
  mosquitto_message_callback_set(mosq, on_message);
  mosquitto_reconnect_delay_set(mosq, 2, 10, true);

  const int connect_rc =
      mosquitto_connect(mosq, cfg.host.c_str(), cfg.port, kKeepaliveSeconds);
  if (connect_rc != MOSQ_ERR_SUCCESS) {
    std::cerr << "[mqtt] Connect error: " << mosquitto_strerror(connect_rc)
              << "\n";
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 1;
  }

  std::cerr << "[mqtt] Listening on " << cfg.host << ":" << cfg.port << "\n";
  if (cfg.heartbeat_interval > 0) {
    std::cerr << "[mqtt] Heartbeat every " << cfg.heartbeat_interval << "s\n";
  }

  // Log audio configuration
  if (cfg.audio_enabled) {
    std::cerr << "[audio] Ring topic: " << cfg.ring_topic << "\n";
    std::cerr << "[audio] Sound file: " << cfg.sound_path;
    if (!is_linux()) {
      std::cerr << " (log-only mode on non-Linux)";
    } else if (!file_exists(cfg.sound_path)) {
      std::cerr << " (WARNING: file not found)";
    }
    std::cerr << "\n";
  } else {
    std::cerr << "[audio] Disabled\n";
  }

  auto last_heartbeat = std::chrono::steady_clock::now();

  while (!g_should_stop) {
    const int loop_rc = mosquitto_loop(mosq, 100, 1);  // 100ms for fast response
    if (g_should_stop) {
      break;
    }
    if (loop_rc != MOSQ_ERR_SUCCESS) {
      std::cerr << "[mqtt] Loop error: " << mosquitto_strerror(loop_rc)
                << " (reconnecting)\n";
      std::this_thread::sleep_for(std::chrono::seconds(1));
      mosquitto_reconnect(mosq);
    }

    // Heartbeat: publish to topic
    if (cfg.heartbeat_interval > 0) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - last_heartbeat)
                         .count();
      if (elapsed >= cfg.heartbeat_interval) {
        const std::string payload = "alive";
        mosquitto_publish(mosq, nullptr, cfg.heartbeat_topic.c_str(),
                          static_cast<int>(payload.size()), payload.c_str(), 0,
                          false);
        std::cerr << "[mqtt] heartbeat -> " << cfg.heartbeat_topic << "\n";
        last_heartbeat = now;
      }
    }
  }

  mosquitto_disconnect(mosq);
  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();
  return 0;
}
