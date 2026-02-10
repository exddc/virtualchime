#ifndef CHIME_WIFI_MONITOR_H
#define CHIME_WIFI_MONITOR_H

#include <optional>
#include <string>

namespace chime {

struct WifiState {
  bool interface_present = false;
  std::string operstate = "unknown";
  int carrier = -1;
};

bool WifiStateChanged(const std::optional<WifiState>& before,
                      const WifiState& after);

class WifiMonitor {
 public:
  virtual ~WifiMonitor() = default;
  virtual std::optional<WifiState> ReadState(
      const std::string& interface_name) const = 0;
};

class LinuxWifiMonitor final : public WifiMonitor {
 public:
  std::optional<WifiState> ReadState(
      const std::string& interface_name) const override;
};

}  // namespace chime

#endif
