#ifndef VC_CONFIG_KV_CONFIG_H
#define VC_CONFIG_KV_CONFIG_H

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vc::config {

inline std::string trim(std::string_view input) {
  const auto start = input.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  const auto end = input.find_last_not_of(" \t\r\n");
  return std::string(input.substr(start, end - start + 1));
}

inline std::vector<std::string> split_csv(std::string_view csv) {
  std::vector<std::string> result;
  std::string_view remaining = csv;
  while (!remaining.empty()) {
    const auto pos = remaining.find(',');
    const std::string_view token =
        (pos == std::string_view::npos) ? remaining : remaining.substr(0, pos);
    std::string cleaned = trim(token);
    if (!cleaned.empty()) {
      result.push_back(std::move(cleaned));
    }
    if (pos == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(pos + 1);
  }
  return result;
}

template <typename T>
struct Field {
  const char* key;
  bool (*setter)(T& target, std::string_view value);
  bool required;
};

template <typename T, std::string T::*member>
bool parse_string(T& target, std::string_view value) {
  target.*member = std::string(value);
  return true;
}

template <typename T, int T::*member, int min_val = 1, int max_val = 65535>
bool parse_int(T& target, std::string_view value) {
  char* end = nullptr;
  const long parsed = std::strtol(std::string(value).c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || parsed < min_val || parsed > max_val) {
    return false;
  }
  target.*member = static_cast<int>(parsed);
  return true;
}

template <typename T, std::vector<std::string> T::*member>
bool parse_csv(T& target, std::string_view value) {
  target.*member = split_csv(value);
  return !(target.*member).empty();
}

template <typename T, bool T::*member>
bool parse_bool(T& target, std::string_view value) {
  std::string lower;
  lower.reserve(value.size());
  for (char c : value) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "true" || lower == "yes" || lower == "1" || lower == "on") {
    target.*member = true;
    return true;
  }
  if (lower == "false" || lower == "no" || lower == "0" || lower == "off") {
    target.*member = false;
    return true;
  }
  return false;
}

template <typename T>
struct LoadResult {
  T config;
  bool success;
  std::string error;

  explicit operator bool() const { return success; }
};

template <typename T, std::size_t N>
LoadResult<T> load(const std::string& path, T defaults,
                   const Field<T> (&fields)[N]) {
  LoadResult<T> result{std::move(defaults), false, ""};

  std::ifstream file(path);
  if (!file.is_open()) {
    result.error = "Failed to open config: " + path;
    return result;
  }

  bool seen[N] = {};

  std::string line;
  while (std::getline(file, line)) {
    const std::string cleaned = trim(line);
    if (cleaned.empty() || cleaned[0] == '#') {
      continue;
    }

    const auto sep = cleaned.find('=');
    if (sep == std::string::npos) {
      continue;
    }

    const std::string key = trim(std::string_view(cleaned.data(), sep));
    const std::string value = trim(
        std::string_view(cleaned.data() + sep + 1, cleaned.size() - sep - 1));

    for (std::size_t i = 0; i < N; ++i) {
      if (key == fields[i].key) {
        if (fields[i].setter(result.config, value)) {
          seen[i] = true;
        }
        break;
      }
    }
  }

  for (std::size_t i = 0; i < N; ++i) {
    if (fields[i].required && !seen[i]) {
      result.error = std::string("Missing required config key: ") + fields[i].key;
      return result;
    }
  }

  result.success = true;
  return result;
}

}  // namespace vc::config

#endif
