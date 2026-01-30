#ifndef CHIME_CONFIG_H
#define CHIME_CONFIG_H

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace config {

// Trim strings
inline std::string trim(std::string_view input) {
  const auto start = input.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  const auto end = input.find_last_not_of(" \t\r\n");
  return std::string(input.substr(start, end - start + 1));
}

// Split a comma-separated string into a vector of trimmed strings
inline std::vector<std::string> split_csv(std::string_view csv) {
  std::vector<std::string> result;
  std::string_view remaining = csv;
  while (!remaining.empty()) {
    const auto pos = remaining.find(',');
    std::string_view token =
        (pos == std::string_view::npos) ? remaining : remaining.substr(0, pos);
    std::string trimmed = trim(token);
    if (!trimmed.empty()) {
      result.push_back(std::move(trimmed));
    }
    if (pos == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(pos + 1);
  }
  return result;
}

// Field descriptor: maps a config key to a setter on the target struct
template <typename T>
struct Field {
  const char* key;
  bool (*setter)(T& target, std::string_view value);
  bool required;
};

// Parse a string field
template <typename T, std::string T::*member>
bool parse_string(T& target, std::string_view value) {
  target.*member = std::string(value);
  return true;
}

// Parse an int field with range validation
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

// Parse a CSV list into a vector<string> field
template <typename T, std::vector<std::string> T::*member>
bool parse_csv(T& target, std::string_view value) {
  target.*member = split_csv(value);
  return !((target.*member).empty());
}

// Parse a boolean field (true/false, yes/no, 1/0, on/off)
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

// Result of loading a config file
template <typename T>
struct LoadResult {
  T config;
  bool success;
  std::string error;

  explicit operator bool() const { return success; }
};

// Load config from file using field specs
// Returns LoadResult with success=false if file can't be opened or required
// fields are missing
template <typename T, std::size_t N>
LoadResult<T> load(const std::string& path, T defaults,
                   const Field<T> (&fields)[N]) {
  LoadResult<T> result{std::move(defaults), false, ""};

  std::ifstream file(path);
  if (!file.is_open()) {
    result.error = "Failed to open config: " + path;
    return result;
  }

  // Track which required fields have been handled
  bool seen[N] = {};

  std::string line;
  while (std::getline(file, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    const auto sep = trimmed.find('=');
    if (sep == std::string::npos) {
      continue;
    }

    std::string key = trim(std::string_view(trimmed.data(), sep));
    std::string value = trim(std::string_view(trimmed.data() + sep + 1,
                                               trimmed.size() - sep - 1));

    // Find matching field
    for (std::size_t i = 0; i < N; ++i) {
      if (key == fields[i].key) {
        if (fields[i].setter(result.config, value)) {
          seen[i] = true;
        }
        break;
      }
    }
  }

  // Check required fields
  for (std::size_t i = 0; i < N; ++i) {
    if (fields[i].required && !seen[i]) {
      result.error = std::string("Missing required config key: ") + fields[i].key;
      return result;
    }
  }

  result.success = true;
  return result;
}

}

#endif
