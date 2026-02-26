#ifndef CHIME_WEBD_CONFIG_STORE_H
#define CHIME_WEBD_CONFIG_STORE_H

#include <string>
#include <vector>

#include "chime/webd_types.h"

namespace vc::logging {
class Logger;
}

namespace chime::webd {

class ConfigStore {
 public:
  ConfigStore(vc::logging::Logger& logger, std::string chime_config_path,
              std::string wpa_supplicant_path);

  SaveResult LoadCoreConfig() const;
  SaveResult SaveCoreConfig(const SaveRequest& request);

 private:
  std::vector<ValidationError> ValidateRequest(const SaveRequest& request) const;

  SaveResult LoadCoreConfigInternal() const;

  bool SaveChimeConfig(const CoreConfig& config, std::string* error) const;
  bool SaveWpaSupplicant(const SaveRequest& request,
                         const CoreConfigSnapshot& existing,
                         std::string* error) const;

  vc::logging::Logger& logger_;
  std::string chime_config_path_;
  std::string wpa_supplicant_path_;
};

}  // namespace chime::webd

#endif
