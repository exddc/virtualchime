#ifndef CHIME_WEBD_WIFI_SCAN_H
#define CHIME_WEBD_WIFI_SCAN_H

#include <string>
#include <vector>

namespace vc::logging {
class Logger;
}

namespace chime::webd {

struct WifiNetwork {
  std::string ssid;
  int signal_dbm = -1000;
  std::string security;
};

struct WifiScanResult {
  bool success = false;
  std::string error;
  std::vector<WifiNetwork> networks;
};

class WifiScanner {
 public:
  WifiScanner(vc::logging::Logger& logger, std::string interface_name);

  WifiScanResult Scan() const;

 private:
  WifiScanResult ScanWithWpaCli() const;
  WifiScanResult ScanWithIw() const;

  vc::logging::Logger& logger_;
  std::string interface_name_;
};

}  // namespace chime::webd

#endif
