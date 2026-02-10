#ifndef CHIME_CHIME_SERVICE_H
#define CHIME_CHIME_SERVICE_H

#include <atomic>
#include <optional>

#include "chime/audio_player.h"
#include "chime/chime_config.h"
#include "chime/wifi_monitor.h"
#include "vc/mqtt/client.h"

namespace vc::logging {
class Logger;
}

namespace vc::runtime {
class SignalHandler;
}

namespace chime {

class ChimeService final : public vc::mqtt::EventHandler {
 public:
  ChimeService(const ChimeConfig& config, vc::logging::Logger& logger,
               AudioPlayer& audio_player, const WifiMonitor& wifi_monitor);

  int Run(vc::runtime::SignalHandler& signal_handler);

  void OnConnect(int rc) override;
  void OnDisconnect(int rc) override;
  void OnMessage(const vc::mqtt::Message& message) override;

 private:
  void LogWifiState(const WifiState& state) const;
  void LogHealth(bool clock_sane);

  const ChimeConfig& config_;
  vc::logging::Logger& logger_;
  vc::mqtt::Client mqtt_client_;
  AudioPlayer& audio_player_;
  const WifiMonitor& wifi_monitor_;

  std::atomic<bool> mqtt_connected_{false};
  std::atomic<unsigned long long> messages_received_{0};
  std::atomic<unsigned long long> ring_messages_received_{0};
  std::atomic<unsigned long long> loop_errors_{0};
  std::atomic<unsigned long long> reconnect_attempts_{0};
  std::atomic<unsigned long long> heartbeats_sent_{0};

  bool clock_was_unsynced_ = false;
};

}  // namespace chime

#endif
