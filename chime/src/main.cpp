#include "config.h"

#include <mosquitto.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
constexpr const char* kDefaultConfigPath = "/etc/chime.conf";
constexpr int kKeepaliveSeconds = 60;
constexpr int kMqttLoopTimeoutMs = 100;
constexpr int kReconnectDelaySeconds = 1;
constexpr int kHealthLogIntervalSeconds = 60;
constexpr std::time_t kMinimumSaneEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC

volatile std::sig_atomic_t g_should_stop = 0;
volatile std::sig_atomic_t g_last_signal = 0;

// Audio playback state
std::atomic<bool> g_audio_playing{false};
std::atomic<bool> g_mqtt_connected{false};
std::atomic<unsigned long long> g_messages_received{0};
std::atomic<unsigned long long> g_ring_messages_received{0};
std::atomic<unsigned long long> g_loop_errors{0};
std::atomic<unsigned long long> g_reconnect_attempts{0};
std::atomic<unsigned long long> g_heartbeats_sent{0};
std::mutex g_log_mutex;

enum class LogLevel { kInfo, kWarn, kError };

struct ChimeConfig {
  // MQTT settings
  std::string host;
  int port = 0;
  std::string client_id = "chime";
  std::vector<std::string> topics;
  int mqtt_subscribe_qos = 0;
  int heartbeat_interval = 60;
  std::string heartbeat_topic = "chime/heartbeat";

  // Audio settings
  std::string ring_topic = "doorbell/ring";
  std::string sound_path = "/usr/local/share/chime/ring.wav";
  bool audio_enabled = true;

  // Connectivity monitoring
  std::string wifi_interface = "wlan0";
  int wifi_check_interval = 5;
};

constexpr config::Field<ChimeConfig> kConfigFields[] = {
    {"mqtt_host", config::parse_string<ChimeConfig, &ChimeConfig::host>, true},
    {"mqtt_port", config::parse_int<ChimeConfig, &ChimeConfig::port>, true},
    {"mqtt_client_id",
     config::parse_string<ChimeConfig, &ChimeConfig::client_id>, false},
    {"mqtt_topics", config::parse_csv<ChimeConfig, &ChimeConfig::topics>, true},
    {"mqtt_subscribe_qos",
     config::parse_int<ChimeConfig, &ChimeConfig::mqtt_subscribe_qos, 0, 2>,
     false},
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
    {"wifi_interface",
     config::parse_string<ChimeConfig, &ChimeConfig::wifi_interface>, false},
    {"wifi_check_interval",
     config::parse_int<ChimeConfig, &ChimeConfig::wifi_check_interval, 0, 3600>,
     false},
};

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "INFO";
}

std::string now_string() {
  const auto now = std::chrono::system_clock::now();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
      1000;
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
#if defined(_WIN32)
  localtime_s(&local_tm, &now_time);
#else
  localtime_r(&now_time, &local_tm);
#endif

  std::ostringstream out;
  out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << '.'
      << std::setw(3) << std::setfill('0') << millis.count();
  return out.str();
}

void log(LogLevel level, std::string_view component, const std::string& message) {
  const std::lock_guard<std::mutex> lock(g_log_mutex);
  std::cerr << now_string() << " [" << level_name(level) << "] [" << component
            << "] " << message << "\n";
}

void log_info(std::string_view component, const std::string& message) {
  log(LogLevel::kInfo, component, message);
}

void log_warn(std::string_view component, const std::string& message) {
  log(LogLevel::kWarn, component, message);
}

void log_error(std::string_view component, const std::string& message) {
  log(LogLevel::kError, component, message);
}

std::string bool_to_string(bool value) { return value ? "true" : "false"; }

std::string signal_name(int signal) {
  switch (signal) {
    case SIGINT:
      return "SIGINT";
    case SIGTERM:
      return "SIGTERM";
    default:
      return std::to_string(signal);
  }
}

bool clock_is_sane() { return std::time(nullptr) >= kMinimumSaneEpoch; }

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

std::string read_trimmed_file(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::string line;
  std::getline(file, line);
  return config::trim(line);
}

bool is_linux() {
#ifdef __linux__
  return true;
#else
  return false;
#endif
}

std::string join(const std::vector<std::string>& values, std::string_view separator) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << separator;
    }
    out << values[i];
  }
  return out.str();
}

std::string escape_shell_double_quotes(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    if (c == '"' || c == '\\' || c == '$' || c == '`') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

std::string sanitize_payload_for_log(std::string_view payload) {
  std::string clean;
  clean.reserve(payload.size());
  for (unsigned char c : payload) {
    if (c == '\n') {
      clean.append("\\n");
    } else if (c == '\r') {
      clean.append("\\r");
    } else if (c == '\t') {
      clean.append("\\t");
    } else if (std::isprint(c) != 0) {
      clean.push_back(static_cast<char>(c));
    } else {
      clean.push_back('?');
    }
  }
  return clean;
}

struct WifiState {
  bool interface_present = false;
  std::string operstate = "unknown";
  int carrier = -1;
};

std::optional<WifiState> read_wifi_state(const std::string& interface_name) {
  if (!is_linux()) {
    return std::nullopt;
  }

  WifiState state;
  const std::string base_path = "/sys/class/net/" + interface_name;
  const std::string operstate_path = base_path + "/operstate";
  const std::string carrier_path = base_path + "/carrier";

  if (!file_exists(operstate_path)) {
    state.interface_present = false;
    return state;
  }

  state.interface_present = true;
  state.operstate = read_trimmed_file(operstate_path);
  const std::string carrier_raw = read_trimmed_file(carrier_path);
  if (carrier_raw == "0") {
    state.carrier = 0;
  } else if (carrier_raw == "1") {
    state.carrier = 1;
  }
  return state;
}

bool wifi_state_changed(const std::optional<WifiState>& before,
                        const WifiState& after) {
  if (!before.has_value()) {
    return true;
  }
  return before->interface_present != after.interface_present ||
         before->operstate != after.operstate || before->carrier != after.carrier;
}

void log_wifi_state(const std::string& interface_name, const WifiState& state) {
  if (!state.interface_present) {
    log_warn("wifi", "interface '" + interface_name + "' not found");
    return;
  }

  std::string message =
      "interface=" + interface_name + " operstate=" + state.operstate;
  if (state.carrier >= 0) {
    message += " carrier=" + std::to_string(state.carrier);
  }

  if (state.operstate == "up" && state.carrier != 0) {
    log_info("wifi", message);
  } else {
    log_warn("wifi", message + " (connectivity degraded)");
  }
}

void play_sound(const std::string& path) {
  // Skip if already playing
  bool expected = false;
  if (!g_audio_playing.compare_exchange_strong(expected, true)) {
    log_warn("audio", "already playing, skipping new request");
    return;
  }

  // In testing environment just log
  if (!is_linux()) {
    log_info("audio", "(local) would play '" + path + "'");
    g_audio_playing = false;
    return;
  }

  if (!file_exists(path)) {
    log_error("audio", "sound file not found: " + path);
    g_audio_playing = false;
    return;
  }

  // Detached thread to play audio
  std::thread([path]() {
    const auto started = std::chrono::steady_clock::now();
    log_info("audio", "playing '" + path + "'");
    const std::string cmd =
        "aplay -q \"" + escape_shell_double_quotes(path) + "\" 2>/dev/null";
    const int rc = std::system(cmd.c_str());
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    if (rc != 0) {
      log_error("audio", "aplay failed with code " + std::to_string(rc));
    } else {
      log_info("audio", "playback complete in " + std::to_string(elapsed_ms) +
                            "ms");
    }
    g_audio_playing = false;
  }).detach();
}

void handle_signal(int signal) {
  g_last_signal = signal;
  g_should_stop = 1;
}

void on_connect(struct mosquitto* mosq, void* obj, int rc) {
  auto* config = static_cast<ChimeConfig*>(obj);
  if (rc != MOSQ_ERR_SUCCESS) {
    log_error("mqtt",
              "connect callback failed: code=" + std::to_string(rc) + " '" +
                  mosquitto_connack_string(rc) + "'");
    g_mqtt_connected = false;
    return;
  }

  g_mqtt_connected = true;
  log_info("mqtt", "connected");
  for (const auto& topic : config->topics) {
    const int sub_rc = mosquitto_subscribe(
        mosq, nullptr, topic.c_str(), config->mqtt_subscribe_qos);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
      log_error("mqtt", "subscribe failed topic='" + topic + "': " +
                            mosquitto_strerror(sub_rc));
    } else {
      log_info("mqtt", "subscribed topic='" + topic + "' qos=" +
                           std::to_string(config->mqtt_subscribe_qos));
    }
  }
}

void on_disconnect(struct mosquitto*, void*, int rc) {
  g_mqtt_connected = false;
  if (rc == MOSQ_ERR_SUCCESS) {
    log_info("mqtt", "disconnected cleanly");
    return;
  }
  log_warn("mqtt", "unexpected disconnect: code=" + std::to_string(rc) + " '" +
                       mosquitto_strerror(rc) + "'");
}

void on_message(struct mosquitto*, void* obj,
                const struct mosquitto_message* msg) {
  if (msg == nullptr || msg->topic == nullptr) {
    return;
  }
  g_messages_received.fetch_add(1, std::memory_order_relaxed);

  auto* config = static_cast<ChimeConfig*>(obj);
  const std::string topic = msg->topic;
  std::string payload;
  if (msg->payload != nullptr && msg->payloadlen > 0) {
    payload.assign(static_cast<const char*>(msg->payload), msg->payloadlen);
  }

  std::string payload_for_log = sanitize_payload_for_log(payload);
  constexpr std::size_t kMaxPayloadLogBytes = 256;
  if (payload_for_log.size() > kMaxPayloadLogBytes) {
    payload_for_log = payload_for_log.substr(0, kMaxPayloadLogBytes) + "...";
  }
  log_info("mqtt", "message topic='" + topic + "' qos=" + std::to_string(msg->qos) +
                       " retain=" + bool_to_string(msg->retain != 0) +
                       " bytes=" + std::to_string(msg->payloadlen) +
                       " payload='" + payload_for_log + "'");

  // Check if this is a ring message
  if (config->audio_enabled && topic == config->ring_topic) {
    g_ring_messages_received.fetch_add(1, std::memory_order_relaxed);
    log_info("chime", "ring received");
    play_sound(config->sound_path);
  }
}
}

int main() {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  bool clock_was_unsynced = false;
  if (!clock_is_sane()) {
    clock_was_unsynced = true;
    log_warn("time",
             "system clock appears unsynchronized (unix=" +
                 std::to_string(static_cast<long long>(std::time(nullptr))) +
                 "). verify NTP/time sync");
  }
  log_info("chime", "service starting (pid=" + std::to_string(getpid()) + ")");

  const std::string config_env = get_env("CHIME_CONFIG");
  const std::string config_path =
      config_env.empty() ? kDefaultConfigPath : config_env;

  auto result = config::load(config_path, ChimeConfig{}, kConfigFields);
  if (!result) {
    log_error("chime", result.error);
    return 1;
  }
  ChimeConfig& cfg = result.config;

  log_info("chime", "loaded config from " + config_path);
  log_info("mqtt", "broker=" + cfg.host + ":" + std::to_string(cfg.port) +
                       " client_id=" + cfg.client_id);
  log_info("mqtt", "subscribe topics=" + join(cfg.topics, ",") +
                       " qos=" + std::to_string(cfg.mqtt_subscribe_qos));
  log_info("mqtt", "heartbeat interval=" + std::to_string(cfg.heartbeat_interval) +
                       "s topic=" + cfg.heartbeat_topic);
  log_info("audio", "enabled=" + bool_to_string(cfg.audio_enabled) +
                        " ring_topic=" + cfg.ring_topic +
                        " sound_path=" + cfg.sound_path);
  log_info("wifi", "monitor interface=" + cfg.wifi_interface +
                       " interval=" + std::to_string(cfg.wifi_check_interval) +
                       "s");

  const int lib_init_rc = mosquitto_lib_init();
  if (lib_init_rc != MOSQ_ERR_SUCCESS) {
    log_error("mqtt",
              "mosquitto_lib_init failed: " + std::string(mosquitto_strerror(lib_init_rc)));
    return 1;
  }

  mosquitto* mosq = mosquitto_new(cfg.client_id.c_str(), true, &cfg);
  if (mosq == nullptr) {
    log_error("mqtt", "failed to create client");
    mosquitto_lib_cleanup();
    return 1;
  }

  mosquitto_connect_callback_set(mosq, on_connect);
  mosquitto_disconnect_callback_set(mosq, on_disconnect);
  mosquitto_message_callback_set(mosq, on_message);
  mosquitto_reconnect_delay_set(mosq, 2, 10, true);

  log_info("mqtt", "connecting to broker");
  const int connect_rc =
      mosquitto_connect(mosq, cfg.host.c_str(), cfg.port, kKeepaliveSeconds);
  if (connect_rc != MOSQ_ERR_SUCCESS) {
    log_error("mqtt", "connect failed: " + std::string(mosquitto_strerror(connect_rc)));
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 1;
  }

  if (cfg.audio_enabled && is_linux() && !file_exists(cfg.sound_path)) {
    log_warn("audio", "configured sound file does not exist: " + cfg.sound_path);
  }

  auto last_heartbeat = std::chrono::steady_clock::now();
  auto last_health = last_heartbeat;
  auto last_wifi_check = last_heartbeat;
  std::optional<WifiState> last_wifi_state;

  if (cfg.wifi_check_interval > 0) {
    const auto wifi_state = read_wifi_state(cfg.wifi_interface);
    if (wifi_state.has_value()) {
      log_wifi_state(cfg.wifi_interface, *wifi_state);
      last_wifi_state = wifi_state;
    } else {
      log_info("wifi", "monitor disabled on non-Linux platform");
    }
  } else {
    log_info("wifi", "monitor disabled by config");
  }

  while (!g_should_stop) {
    const int loop_rc = mosquitto_loop(mosq, kMqttLoopTimeoutMs, 1);
    if (g_should_stop) {
      break;
    }
    if (loop_rc != MOSQ_ERR_SUCCESS) {
      g_loop_errors.fetch_add(1, std::memory_order_relaxed);
      log_warn("mqtt", "loop error: " + std::string(mosquitto_strerror(loop_rc)) +
                           " (reconnecting)");
      std::this_thread::sleep_for(std::chrono::seconds(kReconnectDelaySeconds));
      g_reconnect_attempts.fetch_add(1, std::memory_order_relaxed);
      const int reconnect_rc = mosquitto_reconnect(mosq);
      if (reconnect_rc == MOSQ_ERR_SUCCESS) {
        log_info("mqtt", "reconnect attempt started");
      } else {
        log_error("mqtt",
                  "reconnect failed: " + std::string(mosquitto_strerror(reconnect_rc)));
      }
    }

    const auto now = std::chrono::steady_clock::now();

    // Heartbeat publish
    if (cfg.heartbeat_interval > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - last_heartbeat)
                         .count();
      if (elapsed >= cfg.heartbeat_interval) {
        const std::string payload = g_mqtt_connected ? "alive" : "degraded";
        const int publish_rc =
            mosquitto_publish(mosq, nullptr, cfg.heartbeat_topic.c_str(),
                              static_cast<int>(payload.size()), payload.c_str(), 0,
                              false);
        if (publish_rc == MOSQ_ERR_SUCCESS) {
          g_heartbeats_sent.fetch_add(1, std::memory_order_relaxed);
          log_info("mqtt", "heartbeat topic='" + cfg.heartbeat_topic +
                               "' payload='" + payload + "'");
        } else {
          log_warn("mqtt", "heartbeat publish failed: " +
                               std::string(mosquitto_strerror(publish_rc)));
        }
        last_heartbeat = now;
      }
    }

    if (cfg.wifi_check_interval > 0) {
      const auto wifi_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                    now - last_wifi_check)
                                    .count();
      if (wifi_elapsed >= cfg.wifi_check_interval) {
        const auto current_state = read_wifi_state(cfg.wifi_interface);
        if (current_state.has_value() &&
            wifi_state_changed(last_wifi_state, *current_state)) {
          log_wifi_state(cfg.wifi_interface, *current_state);
          last_wifi_state = current_state;
        }
        last_wifi_check = now;
      }
    }

    const auto health_elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_health).count();
    if (health_elapsed >= kHealthLogIntervalSeconds) {
      const bool clock_sane = clock_is_sane();
      if (clock_was_unsynced && clock_sane) {
        log_info("time", "system clock synchronized");
        clock_was_unsynced = false;
      }
      log_info("health",
               "clock_sane=" + bool_to_string(clock_sane) +
                   " mqtt_connected=" + bool_to_string(g_mqtt_connected.load()) +
                   " messages=" +
                   std::to_string(g_messages_received.load(std::memory_order_relaxed)) +
                   " rings=" +
                   std::to_string(
                       g_ring_messages_received.load(std::memory_order_relaxed)) +
                   " loop_errors=" +
                   std::to_string(g_loop_errors.load(std::memory_order_relaxed)) +
                   " reconnects=" +
                   std::to_string(
                       g_reconnect_attempts.load(std::memory_order_relaxed)) +
                   " heartbeats=" +
                   std::to_string(g_heartbeats_sent.load(std::memory_order_relaxed)) +
                   " audio_playing=" + bool_to_string(g_audio_playing.load()));
      last_health = now;
    }
  }

  if (g_last_signal != 0) {
    log_info("chime",
             "shutdown requested by signal " + signal_name(g_last_signal));
  } else {
    log_info("chime", "shutdown requested");
  }

  const int disconnect_rc = mosquitto_disconnect(mosq);
  if (disconnect_rc != MOSQ_ERR_SUCCESS) {
    log_warn("mqtt",
             "disconnect returned: " + std::string(mosquitto_strerror(disconnect_rc)));
  }
  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();
  log_info("chime", "service stopped");
  return 0;
}
