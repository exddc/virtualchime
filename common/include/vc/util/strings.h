#ifndef VC_UTIL_STRINGS_H
#define VC_UTIL_STRINGS_H

#include <string>
#include <string_view>
#include <vector>

namespace vc::util {

std::string BoolToString(bool value);
std::string Join(const std::vector<std::string>& values, std::string_view separator);
std::string EscapeShellDoubleQuotes(const std::string& value);
std::string SanitizePayloadForLog(std::string_view payload);

}  // namespace vc::util

#endif
