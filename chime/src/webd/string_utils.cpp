#include "chime/webd_string_utils.h"

#include <algorithm>
#include <cctype>

namespace chime::webd {

std::string ToLower(const std::string& value) {
  std::string lowered = value;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowered;
}

}  // namespace chime::webd
