#include "chime/wifi_monitor.h"

#include "vc/util/filesystem.h"
#include "vc/util/platform.h"

namespace chime {

bool WifiStateChanged(const std::optional<WifiState>& before,
                      const WifiState& after) {
  if (!before.has_value()) {
    return true;
  }
  return before->interface_present != after.interface_present ||
         before->operstate != after.operstate || before->carrier != after.carrier;
}

std::optional<WifiState> LinuxWifiMonitor::ReadState(
    const std::string& interface_name) const {
  if (!vc::util::IsLinux()) {
    return std::nullopt;
  }

  WifiState state;
  const std::string base_path = "/sys/class/net/" + interface_name;
  const std::string operstate_path = base_path + "/operstate";
  const std::string carrier_path = base_path + "/carrier";

  if (!vc::util::FileExists(operstate_path)) {
    state.interface_present = false;
    return state;
  }

  state.interface_present = true;
  state.operstate = vc::util::ReadTrimmedFile(operstate_path);

  const std::string carrier_raw = vc::util::ReadTrimmedFile(carrier_path);
  if (carrier_raw == "0") {
    state.carrier = 0;
  } else if (carrier_raw == "1") {
    state.carrier = 1;
  }

  return state;
}

}  // namespace chime
