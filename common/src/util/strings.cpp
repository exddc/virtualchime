#include "vc/util/strings.h"

#include <cctype>
#include <sstream>

namespace vc::util {

std::string BoolToString(bool value) { return value ? "true" : "false"; }

std::string Join(const std::vector<std::string>& values,
                 std::string_view separator) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << separator;
    }
    out << values[i];
  }
  return out.str();
}

std::string EscapeShellDoubleQuotes(const std::string& value) {
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

std::string SanitizePayloadForLog(std::string_view payload) {
  std::string clean;
  clean.reserve(payload.size());
  for (const unsigned char c : payload) {
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

}  // namespace vc::util
