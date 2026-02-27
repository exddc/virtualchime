#include "chime/chime_service.h"

#include <mosquitto.h>

#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>

#include "vc/logging/logger.h"
#include "vc/runtime/signal_handler.h"
#include "vc/util/filesystem.h"
#include "vc/util/platform.h"
#include "vc/util/strings.h"
#include "vc/util/time.h"

namespace chime {
namespace {
constexpr int kMqttLoopTimeoutMs = 100;
constexpr int kReconnectDelaySeconds = 1;
constexpr int kHealthLogIntervalSeconds = 60;
constexpr std::time_t kMinimumSaneEpoch = 1704067200;
constexpr std::size_t kMaxPayloadLogBytes = 256;
}  // namespace

ChimeService::ChimeService(const ChimeConfig& config, vc::logging::Logger& logger,
                           AudioPlayer& audio_player,
                           const WifiMonitor& wifi_monitor)
    : config_(config),
      logger_(logger),
      mqtt_client_(logger, *this),
      audio_player_(audio_player),
      wifi_monitor_(wifi_monitor) {}

int ChimeService::Run(vc::runtime::SignalHandler& signal_handler) {
  clock_was_unsynced_ = !vc::util::ClockIsSane(kMinimumSaneEpoch);
  if (clock_was_unsynced_) {
    logger_.Warn("time", "system clock appears unsynchronized (unix=" +
                             std::to_string(
                                 static_cast<long long>(std::time(nullptr))) +
                             "). verify NTP/time sync");
  }

  logger_.Info("chime", "service starting (pid=" + std::to_string(getpid()) + ")");

  logger_.Info("mqtt", "broker=" + config_.host + ":" + std::to_string(config_.port) +
                           " client_id=" + config_.client_id);
  logger_.Info(
      "mqtt", "auth username=" +
                  (config_.mqtt_username.empty() ? "<none>" : config_.mqtt_username) +
                  " password_set=" +
                  vc::util::BoolToString(!config_.mqtt_password.empty()));
  logger_.Info(
      "mqtt", "tls enabled=" + vc::util::BoolToString(config_.mqtt_tls_enabled) +
                  " validate_cert=" +
                  vc::util::BoolToString(config_.mqtt_tls_validate_certificate) +
                  " ca_file=" +
                  (config_.mqtt_tls_ca_file.empty() ? "<default/system>"
                                                    : config_.mqtt_tls_ca_file));
  logger_.Info("mqtt", "subscribe topics=" + vc::util::Join(config_.topics, ",") +
                           " qos=" + std::to_string(config_.mqtt_subscribe_qos));
  logger_.Info("mqtt", "heartbeat interval=" +
                           std::to_string(config_.heartbeat_interval) +
                           "s topic=" + config_.heartbeat_topic);
  logger_.Info("audio", "enabled=" + vc::util::BoolToString(config_.audio_enabled) +
                            " ring_topic=" + config_.ring_topic +
                            " sound_path=" + config_.sound_path);
  logger_.Info("wifi", "monitor interface=" + config_.wifi_interface +
                           " interval=" +
                           std::to_string(config_.wifi_check_interval) + "s");

  if (config_.audio_enabled && vc::util::IsLinux() &&
      !vc::util::FileExists(config_.sound_path)) {
    logger_.Warn("audio", "configured sound file does not exist: " +
                              config_.sound_path);
  }

  vc::mqtt::ConnectOptions options;
  options.client_id = config_.client_id;
  options.username = config_.mqtt_username;
  options.password = config_.mqtt_password;
  options.tls_enabled = config_.mqtt_tls_enabled;
  options.tls_validate_certificate = config_.mqtt_tls_validate_certificate;
  options.tls_ca_file = config_.mqtt_tls_ca_file;
  options.tls_cert_file = config_.mqtt_tls_cert_file;
  options.tls_key_file = config_.mqtt_tls_key_file;
  options.keepalive_seconds = 60;
  options.reconnect_min_seconds = 2;
  options.reconnect_max_seconds = 10;
  options.reconnect_exponential_backoff = true;

  logger_.Info("mqtt", "connecting to broker");
  if (!mqtt_client_.Connect(config_.host, config_.port, options)) {
    logger_.Error("mqtt", mqtt_client_.LastError());
    return 1;
  }

  auto last_heartbeat = std::chrono::steady_clock::now();
  auto last_health = last_heartbeat;
  auto last_wifi_check = last_heartbeat;
  std::optional<WifiState> last_wifi_state;

  if (config_.wifi_check_interval > 0) {
    const auto wifi_state = wifi_monitor_.ReadState(config_.wifi_interface);
    if (wifi_state.has_value()) {
      LogWifiState(*wifi_state);
      last_wifi_state = wifi_state;
    } else {
      logger_.Info("wifi", "monitor disabled on non-Linux platform");
    }
  } else {
    logger_.Info("wifi", "monitor disabled by config");
  }

  while (!signal_handler.ShouldStop()) {
    const int loop_rc = mqtt_client_.Loop(kMqttLoopTimeoutMs, 1);
    if (signal_handler.ShouldStop()) {
      break;
    }

    if (loop_rc != MOSQ_ERR_SUCCESS) {
      loop_errors_.fetch_add(1, std::memory_order_relaxed);
      logger_.Warn("mqtt", mqtt_client_.LastError() + " (reconnecting)");
      std::this_thread::sleep_for(std::chrono::seconds(kReconnectDelaySeconds));
      reconnect_attempts_.fetch_add(1, std::memory_order_relaxed);
      if (mqtt_client_.Reconnect()) {
        logger_.Info("mqtt", "reconnect attempt started");
      } else {
        logger_.Error("mqtt", mqtt_client_.LastError());
      }
    }

    const auto now = std::chrono::steady_clock::now();

    if (config_.heartbeat_interval > 0) {
      const auto heartbeat_elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat)
              .count();
      if (heartbeat_elapsed >= config_.heartbeat_interval) {
        const std::string payload =
            mqtt_connected_.load() ? "alive" : "degraded";
        if (mqtt_client_.Publish(config_.heartbeat_topic, payload, 0, false)) {
          heartbeats_sent_.fetch_add(1, std::memory_order_relaxed);
          logger_.Info("mqtt", "heartbeat topic='" + config_.heartbeat_topic +
                                   "' payload='" + payload + "'");
        } else {
          logger_.Warn("mqtt", mqtt_client_.LastError());
        }
        last_heartbeat = now;
      }
    }

    if (config_.wifi_check_interval > 0) {
      const auto wifi_elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - last_wifi_check)
              .count();
      if (wifi_elapsed >= config_.wifi_check_interval) {
        const auto current_state = wifi_monitor_.ReadState(config_.wifi_interface);
        if (current_state.has_value() &&
            WifiStateChanged(last_wifi_state, *current_state)) {
          LogWifiState(*current_state);
          last_wifi_state = current_state;
        }
        last_wifi_check = now;
      }
    }

    const auto health_elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_health).count();
    if (health_elapsed >= kHealthLogIntervalSeconds) {
      const bool clock_sane = vc::util::ClockIsSane(kMinimumSaneEpoch);
      if (clock_was_unsynced_ && clock_sane) {
        logger_.Info("time", "system clock synchronized");
        clock_was_unsynced_ = false;
      }
      LogHealth(clock_sane);
      last_health = now;
    }
  }

  if (signal_handler.LastSignal() != 0) {
    logger_.Info("chime", "shutdown requested by signal " +
                              vc::runtime::SignalHandler::SignalName(
                                  signal_handler.LastSignal()));
  } else {
    logger_.Info("chime", "shutdown requested");
  }

  if (!mqtt_client_.Disconnect()) {
    logger_.Warn("mqtt", mqtt_client_.LastError());
  }

  logger_.Info("chime", "service stopped");
  return 0;
}

void ChimeService::OnConnect(int rc) {
  if (rc != MOSQ_ERR_SUCCESS) {
    logger_.Error("mqtt", "connect callback failed: code=" + std::to_string(rc) +
                              " '" + mosquitto_connack_string(rc) + "'");
    mqtt_connected_ = false;
    return;
  }

  mqtt_connected_ = true;
  logger_.Info("mqtt", "connected");
  for (const auto& topic : config_.topics) {
    if (mqtt_client_.Subscribe(topic, config_.mqtt_subscribe_qos)) {
      logger_.Info("mqtt", "subscribed topic='" + topic + "' qos=" +
                               std::to_string(config_.mqtt_subscribe_qos));
    } else {
      logger_.Error("mqtt", mqtt_client_.LastError());
    }
  }
}

void ChimeService::OnDisconnect(int rc) {
  mqtt_connected_ = false;
  if (rc == MOSQ_ERR_SUCCESS) {
    logger_.Info("mqtt", "disconnected cleanly");
    return;
  }
  logger_.Warn("mqtt", "unexpected disconnect: code=" + std::to_string(rc) +
                           " '" + mosquitto_strerror(rc) + "'");
}

void ChimeService::OnMessage(const vc::mqtt::Message& message) {
  messages_received_.fetch_add(1, std::memory_order_relaxed);

  std::string payload_for_log = vc::util::SanitizePayloadForLog(message.payload);
  if (payload_for_log.size() > kMaxPayloadLogBytes) {
    payload_for_log = payload_for_log.substr(0, kMaxPayloadLogBytes) + "...";
  }

  logger_.Info(
      "mqtt", "message topic='" + message.topic + "' qos=" +
                  std::to_string(message.qos) + " retain=" +
                  vc::util::BoolToString(message.retain) + " bytes=" +
                  std::to_string(message.payload.size()) + " payload='" +
                  payload_for_log + "'");

  if (config_.audio_enabled && message.topic == config_.ring_topic) {
    ring_messages_received_.fetch_add(1, std::memory_order_relaxed);
    logger_.Info("chime", "ring received");
    audio_player_.Play(config_.sound_path);
  }
}

void ChimeService::LogWifiState(const WifiState& state) const {
  if (!state.interface_present) {
    logger_.Warn("wifi", "interface '" + config_.wifi_interface + "' not found");
    return;
  }

  std::string message =
      "interface=" + config_.wifi_interface + " operstate=" + state.operstate;
  if (state.carrier >= 0) {
    message += " carrier=" + std::to_string(state.carrier);
  }

  if (state.operstate == "up" && state.carrier != 0) {
    logger_.Info("wifi", message);
  } else {
    logger_.Warn("wifi", message + " (connectivity degraded)");
  }
}

void ChimeService::LogHealth(bool clock_sane) {
  logger_.Info(
      "health",
      "clock_sane=" + vc::util::BoolToString(clock_sane) +
          " mqtt_connected=" + vc::util::BoolToString(mqtt_connected_.load()) +
          " messages=" +
          std::to_string(messages_received_.load(std::memory_order_relaxed)) +
          " rings=" +
          std::to_string(ring_messages_received_.load(std::memory_order_relaxed)) +
          " loop_errors=" +
          std::to_string(loop_errors_.load(std::memory_order_relaxed)) +
          " reconnects=" +
          std::to_string(reconnect_attempts_.load(std::memory_order_relaxed)) +
          " heartbeats=" +
          std::to_string(heartbeats_sent_.load(std::memory_order_relaxed)) +
          " audio_playing=" + vc::util::BoolToString(audio_player_.IsPlaying()));
}

}  // namespace chime
