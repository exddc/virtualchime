#include "chime/webd_config_store.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chime/chime_config.h"
#include "vc/config/kv_config.h"
#include "vc/logging/logger.h"

namespace chime::webd {
namespace {

constexpr mode_t kChimeConfigMode = 0600;
constexpr mode_t kWpaConfigMode = 0600;

bool ReadAllLines(const std::string& path, std::vector<std::string>* lines,
                  std::string* error) {
  if (lines == nullptr || error == nullptr) {
    return false;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    *error = "failed to open file '" + path + "': " + std::strerror(errno);
    return false;
  }

  std::vector<std::string> output;
  std::string line;
  while (std::getline(file, line)) {
    output.push_back(line);
  }
  lines->swap(output);
  return true;
}

bool ReadAllLinesIfExists(const std::string& path, std::vector<std::string>* lines,
                          std::string* error) {
  if (!std::filesystem::exists(path)) {
    lines->clear();
    return true;
  }
  return ReadAllLines(path, lines, error);
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string content;
  for (const auto& line : lines) {
    content += line;
    content.push_back('\n');
  }
  if (lines.empty()) {
    content.push_back('\n');
  }
  return content;
}

bool AtomicWriteFile(const std::string& path, const std::string& content,
                     mode_t mode, std::string* error) {
  if (error == nullptr) {
    return false;
  }

  std::filesystem::path target(path);
  const std::filesystem::path directory = target.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    *error = "failed to create directory '" + directory.string() + "': " +
             ec.message();
    return false;
  }

  std::string template_path =
      (directory / (target.filename().string() + ".tmpXXXXXX")).string();
  std::vector<char> buffer(template_path.begin(), template_path.end());
  buffer.push_back('\0');

  const int fd = mkstemp(buffer.data());
  if (fd < 0) {
    *error = "mkstemp failed for '" + path + "': " + std::strerror(errno);
    return false;
  }

  if (fchmod(fd, mode) != 0) {
    *error = "fchmod failed for temp file: " + std::string(std::strerror(errno));
    close(fd);
    std::remove(buffer.data());
    return false;
  }

  std::size_t offset = 0;
  while (offset < content.size()) {
    const ssize_t written =
        write(fd, content.data() + offset, content.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      *error = "write failed: " + std::string(std::strerror(errno));
      close(fd);
      std::remove(buffer.data());
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }

  if (fsync(fd) != 0) {
    *error = "fsync failed: " + std::string(std::strerror(errno));
    close(fd);
    std::remove(buffer.data());
    return false;
  }

  if (close(fd) != 0) {
    *error = "close failed: " + std::string(std::strerror(errno));
    std::remove(buffer.data());
    return false;
  }

  if (rename(buffer.data(), path.c_str()) != 0) {
    *error = "rename failed for '" + path + "': " + std::strerror(errno);
    std::remove(buffer.data());
    return false;
  }

  return true;
}

std::string JoinCsv(const std::vector<std::string>& items) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    out += items[i];
  }
  return out;
}

bool ParseInt(std::string_view text, int min_value, int max_value, int* output) {
  if (output == nullptr) {
    return false;
  }

  const std::string trimmed = vc::config::trim(text);
  if (trimmed.empty()) {
    return false;
  }

  char* end = nullptr;
  const long parsed = std::strtol(trimmed.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || parsed < min_value || parsed > max_value) {
    return false;
  }

  *output = static_cast<int>(parsed);
  return true;
}

bool ParseBool(std::string_view text, bool* output) {
  if (output == nullptr) {
    return false;
  }
  std::string normalized = vc::config::trim(text);
  if (normalized.empty()) {
    return false;
  }
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (normalized == "true" || normalized == "yes" || normalized == "1" ||
      normalized == "on") {
    *output = true;
    return true;
  }
  if (normalized == "false" || normalized == "no" || normalized == "0" ||
      normalized == "off") {
    *output = false;
    return true;
  }
  return false;
}

std::string BoolToConfig(bool value) { return value ? "true" : "false"; }

std::string StripQuotes(std::string_view value) {
  const std::string trimmed = vc::config::trim(value);
  if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"') {
    return trimmed;
  }

  std::string output;
  output.reserve(trimmed.size() - 2);
  bool escape = false;
  for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
    const char c = trimmed[i];
    if (escape) {
      output.push_back(c);
      escape = false;
      continue;
    }
    if (c == '\\') {
      escape = true;
      continue;
    }
    output.push_back(c);
  }
  return output;
}

std::string QuoteForWpa(const std::string& value) {
  std::string out;
  out.push_back('"');
  for (const char c : value) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

struct WpaData {
  std::vector<std::string> lines;
  std::string ssid;
  std::string psk;
  bool has_network_block = false;
  std::size_t block_start = 0;
  std::size_t block_end = 0;
};

WpaData ParseWpaData(const std::vector<std::string>& lines) {
  WpaData data;
  data.lines = lines;

  bool in_block = false;
  bool block_closed = false;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string trimmed = vc::config::trim(lines[i]);
    if (!in_block && trimmed == "network={") {
      in_block = true;
      data.has_network_block = true;
      data.block_start = i;
      continue;
    }

    if (!in_block) {
      continue;
    }

    if (trimmed == "}") {
      data.block_end = i;
      block_closed = true;
      break;
    }

    const auto separator = trimmed.find('=');
    if (separator == std::string::npos) {
      continue;
    }

    const std::string key = vc::config::trim(trimmed.substr(0, separator));
    const std::string value = vc::config::trim(trimmed.substr(separator + 1));

    if (key == "ssid") {
      data.ssid = StripQuotes(value);
    } else if (key == "psk") {
      data.psk = StripQuotes(value);
    }
  }

  if (data.has_network_block && !block_closed) {
    data.has_network_block = false;
  }

  return data;
}

std::string ExtractConfigValue(const std::vector<std::string>& lines,
                               const std::string& key) {
  std::string output;
  const std::string prefix = key + "=";

  for (const auto& line : lines) {
    const std::string trimmed = vc::config::trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    if (trimmed.rfind(prefix, 0) == 0) {
      output = vc::config::trim(trimmed.substr(prefix.size()));
    }
  }

  return output;
}

bool IsTopicValid(const std::string& topic) {
  if (topic.empty()) {
    return false;
  }
  if (topic.find(' ') != std::string::npos) {
    return false;
  }
  if (topic.find('\t') != std::string::npos) {
    return false;
  }
  return true;
}

}  // namespace

ConfigStore::ConfigStore(vc::logging::Logger& logger, std::string chime_config_path,
                         std::string wpa_supplicant_path)
    : logger_(logger),
      chime_config_path_(std::move(chime_config_path)),
      wpa_supplicant_path_(std::move(wpa_supplicant_path)) {}

SaveResult ConfigStore::LoadCoreConfig() const { return LoadCoreConfigInternal(); }

SaveResult ConfigStore::SaveCoreConfig(const SaveRequest& request) {
  SaveResult result;
  result.validation_errors = ValidateRequest(request);
  if (!result.validation_errors.empty()) {
    result.error = "validation_failed";
    return result;
  }

  const SaveResult existing = LoadCoreConfigInternal();
  if (!existing.success) {
    return existing;
  }

  std::string error;
  if (!SaveWpaSupplicant(request, existing.snapshot, &error)) {
    result.error = error;
    return result;
  }

  if (!SaveChimeConfig(request, existing.snapshot, &error)) {
    result.error = error;
    return result;
  }

  SaveResult loaded = LoadCoreConfigInternal();
  if (!loaded.success) {
    return loaded;
  }

  loaded.success = true;
  return loaded;
}

std::vector<ValidationError> ConfigStore::ValidateRequest(
    const SaveRequest& request) const {
  std::vector<ValidationError> errors;

  if (request.config.wifi_ssid.empty()) {
    errors.push_back({"wifi_ssid", "wifi_ssid is required"});
  } else if (request.config.wifi_ssid.size() > 32) {
    errors.push_back({"wifi_ssid", "wifi_ssid must be <= 32 chars"});
  }

  if (request.wifi_password.has_value() && !request.wifi_password->empty()) {
    const std::size_t length = request.wifi_password->size();
    if (length < 8 || length > 63) {
      errors.push_back(
          {"wifi_password", "wifi_password must be 8-63 chars when provided"});
    }
  }

  if (request.config.mqtt_host.empty()) {
    errors.push_back({"mqtt_host", "mqtt_host is required"});
  } else if (request.config.mqtt_host.find(' ') != std::string::npos) {
    errors.push_back({"mqtt_host", "mqtt_host must not contain spaces"});
  }

  if (request.config.mqtt_port < 1 || request.config.mqtt_port > 65535) {
    errors.push_back({"mqtt_port", "mqtt_port must be 1-65535"});
  }

  if (request.config.mqtt_client_id.empty()) {
    errors.push_back({"mqtt_client_id", "mqtt_client_id is required"});
  } else if (request.config.mqtt_client_id.size() > 128) {
    errors.push_back({"mqtt_client_id", "mqtt_client_id must be <= 128 chars"});
  }

  if (request.config.mqtt_username.size() > 128) {
    errors.push_back({"mqtt_username", "mqtt_username must be <= 128 chars"});
  }

  if (request.config.mqtt_username.empty() && request.mqtt_password.has_value() &&
      !request.mqtt_password->empty()) {
    errors.push_back(
        {"mqtt_password", "mqtt_password requires mqtt_username to be set"});
  }

  if (request.mqtt_password.has_value() && request.mqtt_password->size() > 256) {
    errors.push_back({"mqtt_password", "mqtt_password must be <= 256 chars"});
  }

  if (request.config.mqtt_tls_ca_file.size() > 256) {
    errors.push_back({"mqtt_tls_ca_file", "mqtt_tls_ca_file must be <= 256 chars"});
  }
  if (request.config.mqtt_tls_cert_file.size() > 256) {
    errors.push_back(
        {"mqtt_tls_cert_file", "mqtt_tls_cert_file must be <= 256 chars"});
  }
  if (request.config.mqtt_tls_key_file.size() > 256) {
    errors.push_back({"mqtt_tls_key_file", "mqtt_tls_key_file must be <= 256 chars"});
  }
  const bool tls_cert_set = !request.config.mqtt_tls_cert_file.empty();
  const bool tls_key_set = !request.config.mqtt_tls_key_file.empty();
  if (tls_cert_set != tls_key_set) {
    errors.push_back({"mqtt_tls_cert_file",
                      "mqtt_tls_cert_file and mqtt_tls_key_file must both be set"});
  }

  if (request.config.mqtt_topics.empty()) {
    errors.push_back({"mqtt_topics", "mqtt_topics must contain at least one topic"});
  } else {
    for (std::size_t i = 0; i < request.config.mqtt_topics.size(); ++i) {
      if (!IsTopicValid(request.config.mqtt_topics[i])) {
        errors.push_back({"mqtt_topics",
                          "mqtt_topics[" + std::to_string(i) + "] is invalid"});
      }
    }
  }

  if (!IsTopicValid(request.config.ring_topic)) {
    errors.push_back({"ring_topic", "ring_topic is invalid"});
  }

  return errors;
}

SaveResult ConfigStore::LoadCoreConfigInternal() const {
  SaveResult result;
  result.success = false;

  std::vector<std::string> chime_lines;
  std::string error;
  if (!ReadAllLines(chime_config_path_, &chime_lines, &error)) {
    result.error = error;
    return result;
  }

  chime::ChimeConfig defaults;
  CoreConfig config;
  config.mqtt_host = ExtractConfigValue(chime_lines, "mqtt_host");
  const std::string port_raw = ExtractConfigValue(chime_lines, "mqtt_port");
  int port_value = 1883;
  if (!ParseInt(port_raw, 1, 65535, &port_value)) {
    port_value = 1883;
  }
  config.mqtt_port = port_value;

  const std::string client_id = ExtractConfigValue(chime_lines, "mqtt_client_id");
  config.mqtt_client_id = client_id.empty() ? defaults.client_id : client_id;

  config.mqtt_username = ExtractConfigValue(chime_lines, "mqtt_username");
  config.mqtt_password = ExtractConfigValue(chime_lines, "mqtt_password");

  const std::string tls_enabled_raw =
      ExtractConfigValue(chime_lines, "mqtt_tls_enabled");
  bool tls_enabled = defaults.mqtt_tls_enabled;
  ParseBool(tls_enabled_raw, &tls_enabled);
  config.mqtt_tls_enabled = tls_enabled;

  const std::string tls_validate_raw =
      ExtractConfigValue(chime_lines, "mqtt_tls_validate_certificate");
  bool tls_validate = defaults.mqtt_tls_validate_certificate;
  ParseBool(tls_validate_raw, &tls_validate);
  config.mqtt_tls_validate_certificate = tls_validate;

  config.mqtt_tls_ca_file = ExtractConfigValue(chime_lines, "mqtt_tls_ca_file");
  config.mqtt_tls_cert_file =
      ExtractConfigValue(chime_lines, "mqtt_tls_cert_file");
  config.mqtt_tls_key_file = ExtractConfigValue(chime_lines, "mqtt_tls_key_file");

  const std::string topics_csv = ExtractConfigValue(chime_lines, "mqtt_topics");
  config.mqtt_topics = vc::config::split_csv(topics_csv);

  const std::string ring_topic = ExtractConfigValue(chime_lines, "ring_topic");
  config.ring_topic = ring_topic.empty() ? defaults.ring_topic : ring_topic;

  std::vector<std::string> wpa_lines;
  if (!ReadAllLinesIfExists(wpa_supplicant_path_, &wpa_lines, &error)) {
    result.error = error;
    return result;
  }

  const WpaData wpa_data = ParseWpaData(wpa_lines);
  config.wifi_ssid = wpa_data.ssid;

  result.snapshot.config = std::move(config);
  result.snapshot.wifi_password_set = !wpa_data.psk.empty();
  result.snapshot.mqtt_password_set = !result.snapshot.config.mqtt_password.empty();
  result.success = true;
  return result;
}

bool ConfigStore::SaveChimeConfig(const SaveRequest& request,
                                  const CoreConfigSnapshot& existing,
                                  std::string* error) const {
  std::vector<std::string> lines;
  if (!ReadAllLines(chime_config_path_, &lines, error)) {
    return false;
  }

  std::string mqtt_password = existing.config.mqtt_password;
  if (request.config.mqtt_username.empty()) {
    mqtt_password.clear();
  } else if (request.mqtt_password.has_value()) {
    if (request.mqtt_password->empty()) {
      if (request.config.mqtt_username != existing.config.mqtt_username) {
        mqtt_password.clear();
      }
    } else {
      mqtt_password = *request.mqtt_password;
    }
  } else if (request.config.mqtt_username != existing.config.mqtt_username) {
    mqtt_password.clear();
  }

  const std::map<std::string, std::string> replacements = {
      {"mqtt_host", request.config.mqtt_host},
      {"mqtt_port", std::to_string(request.config.mqtt_port)},
      {"mqtt_client_id", request.config.mqtt_client_id},
      {"mqtt_username", request.config.mqtt_username},
      {"mqtt_password", mqtt_password},
      {"mqtt_tls_enabled", BoolToConfig(request.config.mqtt_tls_enabled)},
      {"mqtt_tls_validate_certificate",
       BoolToConfig(request.config.mqtt_tls_validate_certificate)},
      {"mqtt_tls_ca_file", request.config.mqtt_tls_ca_file},
      {"mqtt_tls_cert_file", request.config.mqtt_tls_cert_file},
      {"mqtt_tls_key_file", request.config.mqtt_tls_key_file},
      {"mqtt_topics", JoinCsv(request.config.mqtt_topics)},
      {"ring_topic", request.config.ring_topic},
  };

  std::set<std::string> seen;
  for (auto& line : lines) {
    const std::string trimmed = vc::config::trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    const auto separator = trimmed.find('=');
    if (separator == std::string::npos) {
      continue;
    }

    const std::string key = vc::config::trim(trimmed.substr(0, separator));
    const auto it = replacements.find(key);
    if (it == replacements.end()) {
      continue;
    }

    line = key + "=" + it->second;
    seen.insert(key);
  }

  for (const auto& [key, value] : replacements) {
    if (seen.find(key) == seen.end()) {
      lines.push_back(key + "=" + value);
    }
  }

  const std::string content = JoinLines(lines);
  return AtomicWriteFile(chime_config_path_, content, kChimeConfigMode, error);
}

bool ConfigStore::SaveWpaSupplicant(const SaveRequest& request,
                                    const CoreConfigSnapshot&, std::string* error) const {
  std::vector<std::string> lines;
  if (!ReadAllLinesIfExists(wpa_supplicant_path_, &lines, error)) {
    return false;
  }

  if (lines.empty()) {
    lines.push_back("ctrl_interface=/var/run/wpa_supplicant");
    lines.push_back("update_config=1");
    lines.push_back("country=US");
    lines.push_back("");
  }

  WpaData parsed = ParseWpaData(lines);

  std::string password_value;
  if (request.wifi_password.has_value()) {
    if (request.wifi_password->empty()) {
      password_value = parsed.psk;
      if (password_value.empty()) {
        *error = "wifi_password is blank and no existing password is available";
        return false;
      }
    } else {
      password_value = *request.wifi_password;
    }
  } else {
    password_value = parsed.psk;
    if (password_value.empty()) {
      *error = "wifi_password is missing and no existing password is available";
      return false;
    }
  }

  const std::string ssid_line = "    ssid=" + QuoteForWpa(request.config.wifi_ssid);
  const std::string psk_line = "    psk=" + QuoteForWpa(password_value);

  if (!parsed.has_network_block) {
    if (!lines.empty() && !lines.back().empty()) {
      lines.push_back("");
    }
    lines.push_back("network={");
    lines.push_back(ssid_line);
    lines.push_back(psk_line);
    lines.push_back("}");
  } else {
    bool ssid_written = false;
    bool psk_written = false;

    for (std::size_t i = parsed.block_start + 1; i < parsed.block_end; ++i) {
      const std::string trimmed = vc::config::trim(lines[i]);
      const auto separator = trimmed.find('=');
      if (separator == std::string::npos) {
        continue;
      }

      const std::string key = vc::config::trim(trimmed.substr(0, separator));
      if (key == "ssid") {
        lines[i] = ssid_line;
        ssid_written = true;
      } else if (key == "psk") {
        lines[i] = psk_line;
        psk_written = true;
      }
    }

    std::size_t insert_pos = parsed.block_end;
    if (!ssid_written) {
      lines.insert(
          lines.begin() +
              static_cast<std::vector<std::string>::difference_type>(insert_pos),
          ssid_line);
      ++insert_pos;
      ++parsed.block_end;
    }
    if (!psk_written) {
      lines.insert(
          lines.begin() +
              static_cast<std::vector<std::string>::difference_type>(insert_pos),
          psk_line);
      ++parsed.block_end;
    }
  }

  const std::string content = JoinLines(lines);
  return AtomicWriteFile(wpa_supplicant_path_, content, kWpaConfigMode, error);
}

}  // namespace chime::webd
