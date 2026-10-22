#include "chime/chime_config.h"
#include "chime/chime_service.h"
#include "chime/generated/config_types.h"
#include "doctest.h"
#include "oc/config/kv_config.h"
#include "oc/mqtt/client.h"
#include "oc/mqtt/topic.h"
#include "test_support.h"

TEST_SUITE("chime_service_ring") {
    TEST_CASE("matching ring topic invokes the audio seam") {
        const ScopedTempDir tmp;
        chime::ChimeConfig config;
        config.audio_enabled = true;
        config.ring_topic = "ring/+/pressed";
        config.sound_path = "/usr/local/share/chime/ring.wav";
        config.volume_ring = 80;

        NullLogger logger;
        RecordingAudioPlayer audio;
        NullWifiMonitor wifi;
        chime::ChimeService service(config, logger, audio, wifi, (tmp.path() / "observed_topics.txt").string());

        oc::mqtt::Message matching;
        matching.topic = "ring/2OG/pressed";
        matching.payload = "ding";
        service.OnMessage(matching);

        REQUIRE(audio.calls().size() == 1);
        CHECK(audio.calls()[0].path == config.sound_path);
        CHECK(audio.calls()[0].volume_percent == 80);

        oc::mqtt::Message non_matching;
        non_matching.topic = "ring/status";
        service.OnMessage(non_matching);
        CHECK(audio.calls().size() == 1);
    }

    TEST_CASE("default ring topic plays for every Ring button") {
        const ScopedTempDir tmp;
        chime::ChimeConfig config;
        config.audio_enabled = true;

        NullLogger logger;
        RecordingAudioPlayer audio;
        NullWifiMonitor wifi;
        chime::ChimeService service(config, logger, audio, wifi, (tmp.path() / "observed_topics.txt").string());

        for (const char *topic : {"ring/front/pressed/1", "ring/back/pressed/4"}) {
            oc::mqtt::Message press;
            press.topic = topic;
            service.OnMessage(press);
        }
        CHECK(audio.calls().size() == 2);

        oc::mqtt::Message status;
        status.topic = "ring/front/status";
        service.OnMessage(status);
        CHECK(audio.calls().size() == 2);
    }

    TEST_CASE("shipped subscriptions deliver every Ring button press") {
        const auto topics = oc::config::split_csv(chime::FindConfigField("mqtt_topics")->repair_text);
        for (const char *press : {"ring/front/pressed/1", "ring/back/pressed/4"}) {
            CHECK(oc::mqtt::TopicMatchesFilter(chime::ChimeConfig{}.ring_topic, press));
            bool subscribed = false;
            for (const auto &filter : topics) {
                subscribed = subscribed || oc::mqtt::TopicMatchesFilter(filter, press);
            }
            CHECK(subscribed);
        }
    }

    TEST_CASE("disabled audio does not play on a matching topic") {
        const ScopedTempDir tmp;
        chime::ChimeConfig config;
        config.audio_enabled = false;
        config.ring_topic = "ring/pressed";

        NullLogger logger;
        RecordingAudioPlayer audio;
        NullWifiMonitor wifi;
        chime::ChimeService service(config, logger, audio, wifi, (tmp.path() / "observed_topics.txt").string());

        oc::mqtt::Message matching;
        matching.topic = "ring/pressed";
        service.OnMessage(matching);
        CHECK(audio.calls().empty());
    }
}
