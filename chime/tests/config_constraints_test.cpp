#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>

#include "chime/chime_config.h"
#include "chime/config_migrate.h"
#include "chime/generated/config_json.h"
#include "chime/generated/config_types.h"
#include "chime/webd_types.h"
#include "doctest.h"
#include "oc/config/kv_config.h"
#include "oc/config/kv_document.h"
#include "test_support.h"

namespace {

std::string FileWithOverride(std::string_view key, std::string_view value) {
    std::string text;
    if (key != "mqtt_host") {
        text += "mqtt_host=broker\n";
    }
    if (key != "mqtt_port") {
        text += "mqtt_port=1883\n";
    }
    if (key != "mqtt_topics") {
        text += "mqtt_topics=ring/pressed\n";
    }
    text += std::string(key) + "=" + std::string(value) + "\n";
    return text;
}

void ApplyFixtureToSaveRequest(chime::webd::SaveRequest *request, std::string_view key, std::string_view value) {
    REQUIRE(request != nullptr);
    if (key == "mqtt_host") {
        request->config.mqtt_host = std::string(value);
    } else if (key == "mqtt_client_id") {
        request->config.mqtt_client_id = std::string(value);
    } else if (key == "ring_topic") {
        request->config.ring_topic = std::string(value);
    } else if (key == "mqtt_topics") {
        request->config.mqtt_topics = oc::config::split_csv(value);
    } else if (key == "notification_success_sound_path") {
        request->config.notification_success_sound_path = std::string(value);
    } else if (key == "heartbeat_topic") {
        return;
    } else {
        FAIL("unhandled invalid fixture key");
    }
}

bool HasFieldError(const std::vector<chime::webd::ValidationError> &errors, const std::string &field) {
    for (const auto &error : errors) {
        if (error.field == field) {
            return true;
        }
    }
    return false;
}

chime::webd::SaveRequest ValidApiSaveRequest() {
    chime::webd::SaveRequest request;
    request.config.wifi_ssid = "net";
    request.config.mqtt_host = "broker";
    request.config.mqtt_topics = {"ring/pressed"};
    return request;
}

} // namespace

TEST_SUITE("config_constraints") {
    TEST_CASE("runtime, migration, and API share generated invalid-value fixtures") {
        const ScopedTempDir tmp;
        for (const auto &example : chime::kConfigInvalidValueExamples) {
            const auto *spec = chime::FindConfigField(example.key);
            REQUIRE(spec != nullptr);
            CHECK_FALSE(chime::ConfigFieldValueValid(*spec, example.value));

            const auto path =
                tmp.WriteFile(std::string(example.key) + ".conf", FileWithOverride(example.key, example.value));
            const auto loaded = chime::LoadConfig(path.string());
            if (spec->file_required) {
                CHECK_FALSE(loaded);
            } else if (spec->runtime) {
                REQUIRE(loaded);
                if (std::string(example.key) == "mqtt_client_id") {
                    CHECK(loaded.config.mqtt_client_id == "chime");
                } else if (std::string(example.key) == "ring_topic") {
                    CHECK(loaded.config.ring_topic == "ring/+/pressed/#");
                } else if (std::string(example.key) == "heartbeat_topic") {
                    CHECK(loaded.config.heartbeat_topic == "chime/heartbeat");
                } else if (std::string(example.key) == "notification_success_sound_path") {
                    CHECK(loaded.config.notification_success_sound_path == "/usr/local/share/chime/test.wav");
                }
            }

            REQUIRE(chime::MigratePersistedConfig(path.string()).success);
            const auto migrated = oc::config::ParseKvDocument([&] {
                std::ifstream file(path);
                REQUIRE(file.is_open());
                return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            }());
            CHECK(oc::config::KvDocumentValue(migrated, example.key) == spec->repair_text);

            if (!spec->api) {
                continue;
            }
            auto request = ValidApiSaveRequest();
            ApplyFixtureToSaveRequest(&request, example.key, example.value);
            std::vector<chime::webd::ValidationError> errors;
            chime::webd::generated_config_json::ValidateSaveRequest(request, &errors);
            CHECK(HasFieldError(errors, example.key));
        }
    }
}
