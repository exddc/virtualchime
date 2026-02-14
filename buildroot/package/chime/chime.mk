################################################################################
#
# chime - Virtual Chime doorbell application
#
################################################################################

CHIME_SITE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/../chime-src
CHIME_SITE_METHOD = local
CHIME_VERSION_FILE = $(CHIME_SITE)/chime/VERSION
VIRTUALCHIME_VERSION_FILE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/version.env
CHIME_VERSION = $(strip $(shell head -n 1 $(CHIME_VERSION_FILE) 2>/dev/null))
CHIME_OS_VERSION = $(strip $(shell sed -n 's/^VIRTUALCHIME_OS_VERSION=//p' $(VIRTUALCHIME_VERSION_FILE)))
CHIME_CONFIG_VERSION = $(strip $(shell sed -n 's/^CHIME_CONFIG_VERSION=//p' $(VIRTUALCHIME_VERSION_FILE)))
CHIME_LICENSE = MIT
CHIME_LICENSE_FILES = chime/README.md
CHIME_DEPENDENCIES = mosquitto

ifeq ($(CHIME_VERSION),)
$(error Missing chime app version in $(CHIME_VERSION_FILE))
endif

ifeq ($(CHIME_OS_VERSION),)
$(error Missing VIRTUALCHIME_OS_VERSION in $(VIRTUALCHIME_VERSION_FILE))
endif

ifeq ($(CHIME_CONFIG_VERSION),)
$(error Missing CHIME_CONFIG_VERSION in $(VIRTUALCHIME_VERSION_FILE))
endif

CHIME_SOURCES = \
	chime/src/main.cpp \
	chime/src/audio/aplay_audio_player.cpp \
	chime/src/config/chime_config.cpp \
	chime/src/network/linux_wifi_monitor.cpp \
	chime/src/service/chime_service.cpp \
	common/src/logging/logger.cpp \
	common/src/mqtt/client.cpp \
	common/src/runtime/signal_handler.cpp \
	common/src/util/environment.cpp \
	common/src/util/filesystem.cpp \
	common/src/util/platform.cpp \
	common/src/util/strings.cpp \
	common/src/util/time.cpp

define CHIME_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++20 -Wall -Wextra \
		-DCHIME_APP_VERSION=\"$(CHIME_VERSION)\" \
		-DVIRTUALCHIME_OS_VERSION=\"$(CHIME_OS_VERSION)\" \
		-DCHIME_CONFIG_VERSION=\"$(CHIME_CONFIG_VERSION)\" \
		-I$(@D)/chime/include -I$(@D)/common/include \
		-o $(@D)/chime/chime $(addprefix $(@D)/,$(CHIME_SOURCES)) \
		$(TARGET_LDFLAGS) -lmosquitto
endef

# Install to /usr/local/bin
define CHIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/chime/chime $(TARGET_DIR)/usr/local/bin/chime
	mkdir -p $(TARGET_DIR)/etc
	printf '%s\n' "$(CHIME_VERSION)" > $(TARGET_DIR)/etc/chime-app-version
endef

$(eval $(generic-package))
