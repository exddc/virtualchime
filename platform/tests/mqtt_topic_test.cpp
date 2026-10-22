#include "doctest.h"
#include "oc/mqtt/topic.h"

TEST_SUITE("mqtt_topic") {
    TEST_CASE("exact matches, wildcards, empty levels, length, and invalid placement") {
        struct Case {
            const char *filter;
            const char *topic;
            bool expected;
        };

        const Case cases[] = {
            {"a/b", "a/b", true},
            {"a/b", "a/c", false},
            {"ring/pressed", "ring/pressed", true},
            {"ring/pressed", "ring/status", false},

            {"a/+/c", "a/b/c", true},
            {"a/+/c", "a/x/c", true},
            {"a/+/c", "a/b/d", false},
            {"a/+/c", "a/b/c/d", false},
            {"a/+/c", "a/c", false},
            {"+/b", "a/b", true},
            {"a/+", "a/b", true},
            {"+", "a", true},
            {"+", "a/b", false},
            {"a/+/+", "a/b/c", true},
            {"ring/+/pressed", "ring/2OG/pressed", true},
            {"ring/+/pressed", "ring/pressed", false},

            {"#", "a", true},
            {"#", "a/b/c", true},
            {"#", "", true},
            {"a/#", "a", true},
            {"a/#", "a/b", true},
            {"a/#", "a/b/c", true},
            {"a/#", "b", false},
            {"a/b/#", "a/b", true},
            {"a/b/#", "a/b/c", true},
            {"sport/tennis/#", "sport/tennis/player1/ranking", true},
            {"ring/2OG/#", "ring/2OG/pressed", true},
            {"ring/2OG/#", "ring/1OG/pressed", false},
            {"ring/+/pressed/#", "ring/front/pressed/1", true},
            {"ring/+/pressed/#", "ring/front/status", false},

            {"a//b", "a//b", true},
            {"a/+/b", "a//b", true},
            {"a//b", "a/x/b", false},
            {"a/", "a/", true},
            {"/a", "/a", true},
            {"+", "", true},
            {"", "", true},
            {"a/b/", "a/b/", true},
            {"a/b", "a/b/", false},
            {"a/b/", "a/b", false},

            {"a/b", "a/b/c", false},
            {"a/b/c", "a/b", false},
            {"a/+", "a/b/c", false},
            {"a/b/c", "a/+/c", false},

            {"a/#/b", "a/x/b", false},
            {"a/#/b", "a/b", false},
            {"#/a", "x/a", false},
            {"#/a", "/a", false},
            {"a/b/#/c", "a/b/x/c", false},
            {"a#", "a#", true},
            {"a#", "abc", false},
            {"a/b#", "a/b#", true},
            {"a/b#", "a/b", false},
            {"a/+b/c", "a/+b/c", true},
            {"a/+b/c", "a/xb/c", false},
        };

        for (const auto &test_case : cases) {
            CAPTURE(test_case.filter);
            CAPTURE(test_case.topic);
            CAPTURE(test_case.expected);
            CHECK(oc::mqtt::TopicMatchesFilter(test_case.filter, test_case.topic) == test_case.expected);
        }
    }
}
