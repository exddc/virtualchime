#include "chime/chime_config.h"
#include "doctest.h"
#include "test_support.h"

namespace {

std::string RequiredKeys() {
    return "mqtt_host=broker.local\nmqtt_port=1883\nmqtt_topics=ring/+/pressed/#,ring/+/status\n";
}

} // namespace

TEST_SUITE("chime_config") {
    TEST_CASE("applies documented defaults when optional keys are omitted") {
        const ScopedTempDir tmp;
        const auto path = tmp.WriteFile("defaults.conf", RequiredKeys());
        const auto result = chime::LoadConfig(path.string());
        REQUIRE(result);
        CHECK(result.config.mqtt_host == "broker.local");
        CHECK(result.config.mqtt_port == 1883);
        REQUIRE(result.config.mqtt_topics.size() == 2);
        CHECK(result.config.mqtt_topics[0] == "ring/+/pressed/#");
        CHECK(result.config.mqtt_topics[1] == "ring/+/status");
        CHECK(result.config.mqtt_client_id == "chime");
        CHECK(result.config.mqtt_username.empty());
        CHECK(result.config.mqtt_password.empty());
        CHECK(result.config.mqtt_tls_enabled == false);
        CHECK(result.config.mqtt_tls_validate_certificate == true);
        CHECK(result.config.mqtt_subscribe_qos == 0);
        CHECK(result.config.heartbeat_interval == 60);
        CHECK(result.config.heartbeat_topic == "chime/heartbeat");
        CHECK(result.config.ring_topic == "ring/+/pressed/#");
        CHECK(result.config.sound_path == "/usr/local/share/chime/ring.wav");
        CHECK(result.config.notification_success_sound_path == "/usr/local/share/chime/test.wav");
        CHECK(result.config.notification_failure_sound_path == "/usr/local/share/chime/ring.wav");
        CHECK(result.config.volume_ring == 80);
        CHECK(result.config.volume_notifications == 70);
        CHECK(result.config.audio_enabled == true);
        CHECK(result.config.wifi_interface == "wlan0");
        CHECK(result.config.wifi_check_interval == 5);
    }

    TEST_CASE("loads optional overrides") {
        const ScopedTempDir tmp;
        const auto path = tmp.WriteFile("overrides.conf", RequiredKeys() + R"(
mqtt_client_id=chime-lab
mqtt_username=user
mqtt_password=secret
mqtt_tls_enabled=true
mqtt_subscribe_qos=2
heartbeat_interval=0
ring_topic=ring/+/pressed
volume_ring=0
audio_enabled=false
wifi_check_interval=0
)");
        const auto result = chime::LoadConfig(path.string());
        REQUIRE(result);
        CHECK(result.config.mqtt_client_id == "chime-lab");
        CHECK(result.config.mqtt_username == "user");
        CHECK(result.config.mqtt_password == "secret");
        CHECK(result.config.mqtt_tls_enabled == true);
        CHECK(result.config.mqtt_subscribe_qos == 2);
        CHECK(result.config.heartbeat_interval == 0);
        CHECK(result.config.ring_topic == "ring/+/pressed");
        CHECK(result.config.volume_ring == 0);
        CHECK(result.config.audio_enabled == false);
        CHECK(result.config.wifi_check_interval == 0);
    }

    TEST_CASE("requires host, port, and topics") {
        const ScopedTempDir tmp;
        CHECK_FALSE(chime::LoadConfig(tmp.WriteFile("no-host.conf", "mqtt_port=1883\nmqtt_topics=a\n").string()));
        CHECK_FALSE(chime::LoadConfig(tmp.WriteFile("no-port.conf", "mqtt_host=h\nmqtt_topics=a\n").string()));
        CHECK_FALSE(chime::LoadConfig(tmp.WriteFile("no-topics.conf", "mqtt_host=h\nmqtt_port=1883\n").string()));
    }

    TEST_CASE("accepts empty mqtt_host as not configured") {
        const ScopedTempDir tmp;
        const auto path = tmp.WriteFile("empty-host.conf", "mqtt_host=\nmqtt_port=1883\nmqtt_topics=ring/pressed\n");
        const auto result = chime::LoadConfig(path.string());
        REQUIRE(result);
        CHECK(result.config.mqtt_host.empty());
        CHECK_FALSE(chime::MqttBrokerConfigured(result.config));
        CHECK(result.config.mqtt_port == 1883);
    }

    TEST_CASE("treats a non-empty mqtt_host as configured") {
        chime::ChimeConfig configured;
        configured.mqtt_host = "broker.local";
        CHECK(chime::MqttBrokerConfigured(configured));
        CHECK_FALSE(chime::MqttBrokerConfigured(chime::ChimeConfig{}));
    }

    TEST_CASE("ignores invalid optional values and keeps defaults") {
        const ScopedTempDir tmp;
        const auto path = tmp.WriteFile(
            "invalid-optional.conf", RequiredKeys() + "volume_ring=101\nmqtt_subscribe_qos=3\naudio_enabled=maybe\n");
        const auto result = chime::LoadConfig(path.string());
        REQUIRE(result);
        CHECK(result.config.volume_ring == 80);
        CHECK(result.config.mqtt_subscribe_qos == 0);
        CHECK(result.config.audio_enabled == true);
    }

    TEST_CASE("ignores init-only, unknown, and removed keys") {
        const ScopedTempDir tmp;
        const auto path = tmp.WriteFile(
            "legacy.conf",
            RequiredKeys() + "volume_other=12\nvolume_bell=12\nntp_servers=example.invalid\nlab_flag=1\n");
        const auto result = chime::LoadConfig(path.string());
        REQUIRE(result);
        CHECK(result.config.mqtt_host == "broker.local");
        CHECK(result.config.volume_ring == 80);
    }

    TEST_CASE("rejects an invalid required port") {
        const ScopedTempDir tmp;
        const auto path = tmp.WriteFile("bad-port.conf", "mqtt_host=h\nmqtt_port=not-a-port\nmqtt_topics=a\n");
        const auto result = chime::LoadConfig(path.string());
        CHECK_FALSE(result);
        CHECK(result.error == "Missing required config key: mqtt_port");
    }
}
