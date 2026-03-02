#ifndef CHIME_WEBD_TYPES_H
#define CHIME_WEBD_TYPES_H

#include <optional>
#include <string>
#include <vector>

namespace chime::webd {

struct ValidationError {
  std::string field;
  std::string message;
};

struct CoreConfig {
  std::string wifi_ssid;
  std::string mqtt_host;
  int mqtt_port = 1883;
  std::string mqtt_client_id = "chime";
  std::string mqtt_username;
  std::string mqtt_password;
  bool mqtt_tls_enabled = false;
  bool mqtt_tls_validate_certificate = true;
  std::string mqtt_tls_ca_file;
  std::string mqtt_tls_cert_file;
  std::string mqtt_tls_key_file;
  std::vector<std::string> mqtt_topics;
  std::string ring_topic = "doorbell/ring";
  int volume_bell = 80;
  int volume_notifications = 70;
  int volume_other = 70;
};

struct CoreConfigSnapshot {
  CoreConfig config;
  bool wifi_password_set = false;
  bool mqtt_password_set = false;
};

struct ApplyStatus {
  unsigned long long job_id = 0;
  std::string state = "idle";
  std::string started_at_utc;
  std::string finished_at_utc;
  std::string error;
};

struct SaveRequest {
  CoreConfig config;
  std::optional<std::string> wifi_password;
  std::optional<std::string> mqtt_password;
};

struct SaveResult {
  bool success = false;
  std::string error;
  std::vector<ValidationError> validation_errors;
  CoreConfigSnapshot snapshot;
};

}  // namespace chime::webd

#endif
