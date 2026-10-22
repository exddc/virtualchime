#ifndef CHIME_GENERATED_CONFIG_TYPES_H
#define CHIME_GENERATED_CONFIG_TYPES_H

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "oc/config/kv_config.h"

namespace chime {

constexpr int kConfigSchemaVersion = 7;
constexpr int kLegacyUnversionedSchema = 4;

enum class ConfigValueType { kString, kInt, kBool, kCsv };
enum class ConfigPersist { kNone, kFile, kWpa };

struct ConfigFieldSpec {
    const char *key;
    ConfigValueType type;
    const char *default_text;
    const char *repair_text;
    ConfigPersist persist;
    bool runtime;
    bool api;
    bool ui;
    bool init_only;
    bool secret;
    bool file_required;
    bool api_required;
    bool api_empty_ok;
    bool forbid_whitespace;
    bool forbid_newline;
    int min_value;
    int max_value;
    int min_len;
    int max_len;
};

struct ConfigRenameSpec {
    const char *from;
    const char *to;
};

struct ConfigValueReplaceSpec {
    const char *key;
    const char *from;
    const char *to;
};

struct ConfigMigrationStep {
    int to_version = 0;
    const char *const *remove = nullptr;
    std::size_t remove_count = 0;
    const ConfigRenameSpec *renames = nullptr;
    std::size_t rename_count = 0;
    const ConfigValueReplaceSpec *replaces = nullptr;
    std::size_t replace_count = 0;
};

struct ConfigInvalidValueExample {
    const char *key;
    const char *value;
};

constexpr ConfigFieldSpec kAllConfigFields[] = {
    {"schema_version", ConfigValueType::kInt, "7", "7", ConfigPersist::kFile, false, false, false, false, false, false, false, false, false, false, 1, 1000000, 0, 0},
    {"mqtt_host", ConfigValueType::kString, "", "", ConfigPersist::kFile, true, true, true, false, false, true, true, false, true, true, 0, 0, 0, 256},
    {"mqtt_port", ConfigValueType::kInt, "1883", "1883", ConfigPersist::kFile, true, true, true, false, false, true, true, false, false, false, 1, 65535, 0, 0},
    {"mqtt_client_id", ConfigValueType::kString, "chime", "chime", ConfigPersist::kFile, true, true, true, false, false, false, true, false, false, true, 0, 0, 0, 128},
    {"mqtt_username", ConfigValueType::kString, "", "", ConfigPersist::kFile, true, true, true, false, false, false, true, true, false, true, 0, 0, 0, 128},
    {"mqtt_password", ConfigValueType::kString, "", "", ConfigPersist::kFile, true, true, true, false, true, false, false, false, false, true, 0, 0, 0, 256},
    {"mqtt_tls_enabled", ConfigValueType::kBool, "false", "false", ConfigPersist::kFile, true, true, true, false, false, false, true, false, false, false, 0, 0, 0, 0},
    {"mqtt_tls_validate_certificate", ConfigValueType::kBool, "true", "true", ConfigPersist::kFile, true, true, true, false, false, false, true, false, false, false, 0, 0, 0, 0},
    {"mqtt_tls_ca_file", ConfigValueType::kString, "", "", ConfigPersist::kFile, true, true, true, false, false, false, true, true, false, true, 0, 0, 0, 256},
    {"mqtt_tls_cert_file", ConfigValueType::kString, "", "", ConfigPersist::kFile, true, true, true, false, false, false, true, true, false, true, 0, 0, 0, 256},
    {"mqtt_tls_key_file", ConfigValueType::kString, "", "", ConfigPersist::kFile, true, true, true, false, false, false, true, true, false, true, 0, 0, 0, 256},
    {"mqtt_topics", ConfigValueType::kCsv, "", "ring/+/pressed/#,ring/+/status", ConfigPersist::kFile, true, true, true, false, false, true, true, false, true, true, 0, 0, 0, 0},
    {"mqtt_subscribe_qos", ConfigValueType::kInt, "0", "0", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, false, 0, 2, 0, 0},
    {"heartbeat_interval", ConfigValueType::kInt, "60", "60", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, false, 0, 3600, 0, 0},
    {"heartbeat_topic", ConfigValueType::kString, "chime/heartbeat", "chime/heartbeat", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, true, 0, 0, 0, 256},
    {"ntp_servers", ConfigValueType::kCsv, "time.cloudflare.com,time.google.com,pool.ntp.org", "time.cloudflare.com,time.google.com,pool.ntp.org", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, true, 0, 0, 0, 0},
    {"time_http_urls", ConfigValueType::kCsv, "http://connectivitycheck.gstatic.com/generate_204,http://detectportal.firefox.com/success.txt,http://example.com/", "http://connectivitycheck.gstatic.com/generate_204,http://detectportal.firefox.com/success.txt,http://example.com/", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, true, 0, 0, 0, 0},
    {"time_sync_retries", ConfigValueType::kInt, "6", "6", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, false, 1, 100, 0, 0},
    {"time_sync_retry_delay", ConfigValueType::kInt, "5", "5", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, false, 1, 3600, 0, 0},
    {"time_sync_interval", ConfigValueType::kInt, "3600", "3600", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, false, 0, 86400, 0, 0},
    {"ring_topic", ConfigValueType::kString, "ring/+/pressed/#", "ring/+/pressed/#", ConfigPersist::kFile, true, true, true, false, false, false, true, false, true, true, 0, 0, 0, 256},
    {"sound_path", ConfigValueType::kString, "/usr/local/share/chime/ring.wav", "/usr/local/share/chime/ring.wav", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, true, 0, 0, 0, 256},
    {"notification_success_sound_path", ConfigValueType::kString, "/usr/local/share/chime/test.wav", "/usr/local/share/chime/test.wav", ConfigPersist::kFile, true, true, true, false, false, false, false, false, false, true, 0, 0, 1, 256},
    {"notification_failure_sound_path", ConfigValueType::kString, "/usr/local/share/chime/ring.wav", "/usr/local/share/chime/ring.wav", ConfigPersist::kFile, true, true, true, false, false, false, false, false, false, true, 0, 0, 1, 256},
    {"volume_ring", ConfigValueType::kInt, "80", "80", ConfigPersist::kFile, true, true, true, false, false, false, true, false, false, false, 0, 100, 0, 0},
    {"volume_notifications", ConfigValueType::kInt, "70", "70", ConfigPersist::kFile, true, true, true, false, false, false, true, false, false, false, 0, 100, 0, 0},
    {"audio_enabled", ConfigValueType::kBool, "true", "true", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, false, 0, 0, 0, 0},
    {"wifi_interface", ConfigValueType::kString, "wlan0", "wlan0", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, true, 0, 0, 0, 32},
    {"wifi_check_interval", ConfigValueType::kInt, "5", "5", ConfigPersist::kFile, true, false, false, false, false, false, false, false, false, false, 0, 3600, 0, 0},
    {"log_max_bytes", ConfigValueType::kInt, "262144", "262144", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, false, 1024, 104857600, 0, 0},
    {"log_rotate_keep", ConfigValueType::kInt, "5", "5", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, false, 1, 100, 0, 0},
    {"log_rotate_check_interval", ConfigValueType::kInt, "30", "30", ConfigPersist::kFile, false, false, false, true, false, false, false, false, false, false, 1, 3600, 0, 0},
    {"wifi_ssid", ConfigValueType::kString, "", "", ConfigPersist::kWpa, false, true, true, false, false, false, true, false, false, true, 0, 0, 0, 32},
    {"wifi_password", ConfigValueType::kString, "", "", ConfigPersist::kWpa, false, true, true, false, true, false, false, false, false, true, 0, 0, 8, 63}
};

inline constexpr const char *kRemovedConfigKeys[] = {"volume_other"};

inline constexpr const char *kConfigMigrationRemove5[] = {"volume_other"};
inline constexpr ConfigRenameSpec kConfigMigrationRename6[] = {{"volume_bell", "volume_ring"}};
inline constexpr ConfigValueReplaceSpec kConfigMigrationReplace7[] = {{"mqtt_topics", "ring/pressed,ring/status", "ring/+/pressed/#,ring/+/status"}, {"ring_topic", "ring/pressed", "ring/+/pressed/#"}};

inline constexpr ConfigMigrationStep kConfigMigrationSteps[] = {
    {.to_version = 5, .remove = kConfigMigrationRemove5, .remove_count = 1},
    {.to_version = 6, .renames = kConfigMigrationRename6, .rename_count = 1},
    {.to_version = 7, .replaces = kConfigMigrationReplace7, .replace_count = 2}
};

inline constexpr ConfigInvalidValueExample kConfigInvalidValueExamples[] = {
    {"mqtt_host", "bad host"},
    {"mqtt_client_id", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"},
    {"ring_topic", "ring/+/pressed/#\ninjected"},
    {"mqtt_topics", "bad topic"},
    {"heartbeat_topic", "chime/heartbeat\n"},
    {"notification_success_sound_path", ""}
};

inline const ConfigFieldSpec *FindConfigField(std::string_view key) {
    for (const auto &field : kAllConfigFields) {
        if (key == field.key) {
            return &field;
        }
    }
    return nullptr;
}

inline bool ConfigFieldValueValid(const ConfigFieldSpec &spec, std::string_view value) {
    if (spec.type == ConfigValueType::kInt) {
        int parsed = 0;
        return oc::config::parse_int_value(value, spec.min_value, spec.max_value, &parsed);
    }
    if (spec.type == ConfigValueType::kBool) {
        bool parsed = false;
        return oc::config::parse_bool_value(value, &parsed);
    }
    if (spec.type == ConfigValueType::kCsv) {
        const auto items = oc::config::split_csv(value);
        if (spec.file_required && items.empty()) {
            return false;
        }
        for (const auto &item : items) {
            if (!oc::config::string_value_valid(item, spec.min_len, spec.max_len, spec.forbid_whitespace,
                                                spec.forbid_newline)) {
                return false;
            }
        }
        return true;
    }
    return oc::config::string_value_valid(value, spec.min_len, spec.max_len, spec.forbid_whitespace,
                                         spec.forbid_newline);
}

struct ChimeConfig {
    std::string mqtt_host{};
    int mqtt_port = 1883;
    std::string mqtt_client_id = "chime";
    std::string mqtt_username{};
    std::string mqtt_password{};
    bool mqtt_tls_enabled = false;
    bool mqtt_tls_validate_certificate = true;
    std::string mqtt_tls_ca_file{};
    std::string mqtt_tls_cert_file{};
    std::string mqtt_tls_key_file{};
    std::vector<std::string> mqtt_topics{};
    int mqtt_subscribe_qos = 0;
    int heartbeat_interval = 60;
    std::string heartbeat_topic = "chime/heartbeat";
    std::string ring_topic = "ring/+/pressed/#";
    std::string sound_path = "/usr/local/share/chime/ring.wav";
    std::string notification_success_sound_path = "/usr/local/share/chime/test.wav";
    std::string notification_failure_sound_path = "/usr/local/share/chime/ring.wav";
    int volume_ring = 80;
    int volume_notifications = 70;
    bool audio_enabled = true;
    std::string wifi_interface = "wlan0";
    int wifi_check_interval = 5;
};

inline const oc::config::Field<ChimeConfig> kChimeConfigFields[] = {
    {"mqtt_host", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_host, 0, 256, true, true>, true},
    {"mqtt_port", oc::config::parse_int<ChimeConfig, &ChimeConfig::mqtt_port, 1, 65535>, true},
    {"mqtt_client_id", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_client_id, 0, 128, false, true>, false},
    {"mqtt_username", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_username, 0, 128, false, true>, false},
    {"mqtt_password", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_password, 0, 256, false, true>, false},
    {"mqtt_tls_enabled", oc::config::parse_bool<ChimeConfig, &ChimeConfig::mqtt_tls_enabled>, false},
    {"mqtt_tls_validate_certificate", oc::config::parse_bool<ChimeConfig, &ChimeConfig::mqtt_tls_validate_certificate>, false},
    {"mqtt_tls_ca_file", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_tls_ca_file, 0, 256, false, true>, false},
    {"mqtt_tls_cert_file", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_tls_cert_file, 0, 256, false, true>, false},
    {"mqtt_tls_key_file", oc::config::parse_string<ChimeConfig, &ChimeConfig::mqtt_tls_key_file, 0, 256, false, true>, false},
    {"mqtt_topics", oc::config::parse_csv<ChimeConfig, &ChimeConfig::mqtt_topics, true, true>, true},
    {"mqtt_subscribe_qos", oc::config::parse_int<ChimeConfig, &ChimeConfig::mqtt_subscribe_qos, 0, 2>, false},
    {"heartbeat_interval", oc::config::parse_int<ChimeConfig, &ChimeConfig::heartbeat_interval, 0, 3600>, false},
    {"heartbeat_topic", oc::config::parse_string<ChimeConfig, &ChimeConfig::heartbeat_topic, 0, 256, false, true>, false},
    {"ring_topic", oc::config::parse_string<ChimeConfig, &ChimeConfig::ring_topic, 0, 256, true, true>, false},
    {"sound_path", oc::config::parse_string<ChimeConfig, &ChimeConfig::sound_path, 0, 256, false, true>, false},
    {"notification_success_sound_path", oc::config::parse_string<ChimeConfig, &ChimeConfig::notification_success_sound_path, 1, 256, false, true>, false},
    {"notification_failure_sound_path", oc::config::parse_string<ChimeConfig, &ChimeConfig::notification_failure_sound_path, 1, 256, false, true>, false},
    {"volume_ring", oc::config::parse_int<ChimeConfig, &ChimeConfig::volume_ring, 0, 100>, false},
    {"volume_notifications", oc::config::parse_int<ChimeConfig, &ChimeConfig::volume_notifications, 0, 100>, false},
    {"audio_enabled", oc::config::parse_bool<ChimeConfig, &ChimeConfig::audio_enabled>, false},
    {"wifi_interface", oc::config::parse_string<ChimeConfig, &ChimeConfig::wifi_interface, 0, 32, false, true>, false},
    {"wifi_check_interval", oc::config::parse_int<ChimeConfig, &ChimeConfig::wifi_check_interval, 0, 3600>, false},
};

struct FileConfig {
    int schema_version = 7;
    std::string mqtt_host{};
    int mqtt_port = 1883;
    std::string mqtt_client_id = "chime";
    std::string mqtt_username{};
    std::string mqtt_password{};
    bool mqtt_tls_enabled = false;
    bool mqtt_tls_validate_certificate = true;
    std::string mqtt_tls_ca_file{};
    std::string mqtt_tls_cert_file{};
    std::string mqtt_tls_key_file{};
    std::vector<std::string> mqtt_topics{};
    int mqtt_subscribe_qos = 0;
    int heartbeat_interval = 60;
    std::string heartbeat_topic = "chime/heartbeat";
    std::vector<std::string> ntp_servers{"time.cloudflare.com", "time.google.com", "pool.ntp.org"};
    std::vector<std::string> time_http_urls{"http://connectivitycheck.gstatic.com/generate_204", "http://detectportal.firefox.com/success.txt", "http://example.com/"};
    int time_sync_retries = 6;
    int time_sync_retry_delay = 5;
    int time_sync_interval = 3600;
    std::string ring_topic = "ring/+/pressed/#";
    std::string sound_path = "/usr/local/share/chime/ring.wav";
    std::string notification_success_sound_path = "/usr/local/share/chime/test.wav";
    std::string notification_failure_sound_path = "/usr/local/share/chime/ring.wav";
    int volume_ring = 80;
    int volume_notifications = 70;
    bool audio_enabled = true;
    std::string wifi_interface = "wlan0";
    int wifi_check_interval = 5;
    int log_max_bytes = 262144;
    int log_rotate_keep = 5;
    int log_rotate_check_interval = 30;
};

inline const oc::config::Field<FileConfig> kFileConfigFields[] = {
    {"schema_version", oc::config::parse_int<FileConfig, &FileConfig::schema_version, 1, 1000000>, false},
    {"mqtt_host", oc::config::parse_string<FileConfig, &FileConfig::mqtt_host, 0, 256, true, true>, false},
    {"mqtt_port", oc::config::parse_int<FileConfig, &FileConfig::mqtt_port, 1, 65535>, false},
    {"mqtt_client_id", oc::config::parse_string<FileConfig, &FileConfig::mqtt_client_id, 0, 128, false, true>, false},
    {"mqtt_username", oc::config::parse_string<FileConfig, &FileConfig::mqtt_username, 0, 128, false, true>, false},
    {"mqtt_password", oc::config::parse_string<FileConfig, &FileConfig::mqtt_password, 0, 256, false, true>, false},
    {"mqtt_tls_enabled", oc::config::parse_bool<FileConfig, &FileConfig::mqtt_tls_enabled>, false},
    {"mqtt_tls_validate_certificate", oc::config::parse_bool<FileConfig, &FileConfig::mqtt_tls_validate_certificate>, false},
    {"mqtt_tls_ca_file", oc::config::parse_string<FileConfig, &FileConfig::mqtt_tls_ca_file, 0, 256, false, true>, false},
    {"mqtt_tls_cert_file", oc::config::parse_string<FileConfig, &FileConfig::mqtt_tls_cert_file, 0, 256, false, true>, false},
    {"mqtt_tls_key_file", oc::config::parse_string<FileConfig, &FileConfig::mqtt_tls_key_file, 0, 256, false, true>, false},
    {"mqtt_topics", oc::config::parse_csv<FileConfig, &FileConfig::mqtt_topics, true, true>, false},
    {"mqtt_subscribe_qos", oc::config::parse_int<FileConfig, &FileConfig::mqtt_subscribe_qos, 0, 2>, false},
    {"heartbeat_interval", oc::config::parse_int<FileConfig, &FileConfig::heartbeat_interval, 0, 3600>, false},
    {"heartbeat_topic", oc::config::parse_string<FileConfig, &FileConfig::heartbeat_topic, 0, 256, false, true>, false},
    {"ntp_servers", oc::config::parse_csv<FileConfig, &FileConfig::ntp_servers, false, true>, false},
    {"time_http_urls", oc::config::parse_csv<FileConfig, &FileConfig::time_http_urls, false, true>, false},
    {"time_sync_retries", oc::config::parse_int<FileConfig, &FileConfig::time_sync_retries, 1, 100>, false},
    {"time_sync_retry_delay", oc::config::parse_int<FileConfig, &FileConfig::time_sync_retry_delay, 1, 3600>, false},
    {"time_sync_interval", oc::config::parse_int<FileConfig, &FileConfig::time_sync_interval, 0, 86400>, false},
    {"ring_topic", oc::config::parse_string<FileConfig, &FileConfig::ring_topic, 0, 256, true, true>, false},
    {"sound_path", oc::config::parse_string<FileConfig, &FileConfig::sound_path, 0, 256, false, true>, false},
    {"notification_success_sound_path", oc::config::parse_string<FileConfig, &FileConfig::notification_success_sound_path, 1, 256, false, true>, false},
    {"notification_failure_sound_path", oc::config::parse_string<FileConfig, &FileConfig::notification_failure_sound_path, 1, 256, false, true>, false},
    {"volume_ring", oc::config::parse_int<FileConfig, &FileConfig::volume_ring, 0, 100>, false},
    {"volume_notifications", oc::config::parse_int<FileConfig, &FileConfig::volume_notifications, 0, 100>, false},
    {"audio_enabled", oc::config::parse_bool<FileConfig, &FileConfig::audio_enabled>, false},
    {"wifi_interface", oc::config::parse_string<FileConfig, &FileConfig::wifi_interface, 0, 32, false, true>, false},
    {"wifi_check_interval", oc::config::parse_int<FileConfig, &FileConfig::wifi_check_interval, 0, 3600>, false},
    {"log_max_bytes", oc::config::parse_int<FileConfig, &FileConfig::log_max_bytes, 1024, 104857600>, false},
    {"log_rotate_keep", oc::config::parse_int<FileConfig, &FileConfig::log_rotate_keep, 1, 100>, false},
    {"log_rotate_check_interval", oc::config::parse_int<FileConfig, &FileConfig::log_rotate_check_interval, 1, 3600>, false},
};

namespace webd {

struct CoreConfig {
    std::string mqtt_host{};
    int mqtt_port = 1883;
    std::string mqtt_client_id = "chime";
    std::string mqtt_username{};
    std::string mqtt_password{};
    bool mqtt_tls_enabled = false;
    bool mqtt_tls_validate_certificate = true;
    std::string mqtt_tls_ca_file{};
    std::string mqtt_tls_cert_file{};
    std::string mqtt_tls_key_file{};
    std::vector<std::string> mqtt_topics{};
    std::string ring_topic = "ring/+/pressed/#";
    std::string notification_success_sound_path = "/usr/local/share/chime/test.wav";
    std::string notification_failure_sound_path = "/usr/local/share/chime/ring.wav";
    int volume_ring = 80;
    int volume_notifications = 70;
    std::string wifi_ssid{};
};

} // namespace webd

inline ChimeConfig RuntimeConfigFromFile(const FileConfig &file) {
    ChimeConfig out;
    out.mqtt_host = file.mqtt_host;
    out.mqtt_port = file.mqtt_port;
    out.mqtt_client_id = file.mqtt_client_id;
    out.mqtt_username = file.mqtt_username;
    out.mqtt_password = file.mqtt_password;
    out.mqtt_tls_enabled = file.mqtt_tls_enabled;
    out.mqtt_tls_validate_certificate = file.mqtt_tls_validate_certificate;
    out.mqtt_tls_ca_file = file.mqtt_tls_ca_file;
    out.mqtt_tls_cert_file = file.mqtt_tls_cert_file;
    out.mqtt_tls_key_file = file.mqtt_tls_key_file;
    out.mqtt_topics = file.mqtt_topics;
    out.mqtt_subscribe_qos = file.mqtt_subscribe_qos;
    out.heartbeat_interval = file.heartbeat_interval;
    out.heartbeat_topic = file.heartbeat_topic;
    out.ring_topic = file.ring_topic;
    out.sound_path = file.sound_path;
    out.notification_success_sound_path = file.notification_success_sound_path;
    out.notification_failure_sound_path = file.notification_failure_sound_path;
    out.volume_ring = file.volume_ring;
    out.volume_notifications = file.volume_notifications;
    out.audio_enabled = file.audio_enabled;
    out.wifi_interface = file.wifi_interface;
    out.wifi_check_interval = file.wifi_check_interval;
    return out;
}

inline webd::CoreConfig CoreConfigFromFile(const FileConfig &file) {
    webd::CoreConfig out;
    out.mqtt_host = file.mqtt_host;
    out.mqtt_port = file.mqtt_port;
    out.mqtt_client_id = file.mqtt_client_id;
    out.mqtt_username = file.mqtt_username;
    out.mqtt_password = file.mqtt_password;
    out.mqtt_tls_enabled = file.mqtt_tls_enabled;
    out.mqtt_tls_validate_certificate = file.mqtt_tls_validate_certificate;
    out.mqtt_tls_ca_file = file.mqtt_tls_ca_file;
    out.mqtt_tls_cert_file = file.mqtt_tls_cert_file;
    out.mqtt_tls_key_file = file.mqtt_tls_key_file;
    out.mqtt_topics = file.mqtt_topics;
    out.ring_topic = file.ring_topic;
    out.notification_success_sound_path = file.notification_success_sound_path;
    out.notification_failure_sound_path = file.notification_failure_sound_path;
    out.volume_ring = file.volume_ring;
    out.volume_notifications = file.volume_notifications;
    return out;
}

inline void ApplyCoreConfigToFile(const webd::CoreConfig &core, FileConfig *file) {
    if (file == nullptr) {
        return;
    }
    file->mqtt_host = core.mqtt_host;
    file->mqtt_port = core.mqtt_port;
    file->mqtt_client_id = core.mqtt_client_id;
    file->mqtt_username = core.mqtt_username;
    file->mqtt_password = core.mqtt_password;
    file->mqtt_tls_enabled = core.mqtt_tls_enabled;
    file->mqtt_tls_validate_certificate = core.mqtt_tls_validate_certificate;
    file->mqtt_tls_ca_file = core.mqtt_tls_ca_file;
    file->mqtt_tls_cert_file = core.mqtt_tls_cert_file;
    file->mqtt_tls_key_file = core.mqtt_tls_key_file;
    file->mqtt_topics = core.mqtt_topics;
    file->ring_topic = core.ring_topic;
    file->notification_success_sound_path = core.notification_success_sound_path;
    file->notification_failure_sound_path = core.notification_failure_sound_path;
    file->volume_ring = core.volume_ring;
    file->volume_notifications = core.volume_notifications;
}

inline std::map<std::string, std::string> CoreConfigFileReplacements(const webd::CoreConfig &config,
                                                                    const std::string &mqtt_password) {
    return {
        {"schema_version", std::to_string(kConfigSchemaVersion)},
        {"mqtt_host", config.mqtt_host},
        {"mqtt_port", std::to_string(config.mqtt_port)},
        {"mqtt_client_id", config.mqtt_client_id},
        {"mqtt_username", config.mqtt_username},
        {"mqtt_password", mqtt_password},
        {"mqtt_tls_enabled", oc::config::bool_to_text(config.mqtt_tls_enabled)},
        {"mqtt_tls_validate_certificate", oc::config::bool_to_text(config.mqtt_tls_validate_certificate)},
        {"mqtt_tls_ca_file", config.mqtt_tls_ca_file},
        {"mqtt_tls_cert_file", config.mqtt_tls_cert_file},
        {"mqtt_tls_key_file", config.mqtt_tls_key_file},
        {"mqtt_topics", oc::config::join_csv(config.mqtt_topics)},
        {"ring_topic", config.ring_topic},
        {"notification_success_sound_path", config.notification_success_sound_path},
        {"notification_failure_sound_path", config.notification_failure_sound_path},
        {"volume_ring", std::to_string(config.volume_ring)},
        {"volume_notifications", std::to_string(config.volume_notifications)},
    };
}

} // namespace chime

#endif
