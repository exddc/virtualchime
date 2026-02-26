################################################################################
#
# chime - Virtual Chime doorbell application
#
################################################################################

CHIME_SITE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/../chime-src
CHIME_SITE_METHOD = local
CHIME_VERSION_FILE = $(CHIME_SITE)/chime/VERSION
VIRTUALCHIME_VERSION_FILE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/version.env
CHIME_BUILD_META_FILE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/build_meta.env
CHIME_VERSION = $(strip $(shell head -n 1 $(CHIME_VERSION_FILE) 2>/dev/null))
CHIME_OS_VERSION = $(strip $(shell sed -n 's/^VIRTUALCHIME_OS_VERSION=//p' $(VIRTUALCHIME_VERSION_FILE)))
CHIME_CONFIG_VERSION = $(strip $(shell sed -n 's/^CHIME_CONFIG_VERSION=//p' $(VIRTUALCHIME_VERSION_FILE)))
CHIME_BUILD_ID = $(strip $(shell sed -n 's/^CHIME_BUILD_ID=//p' $(CHIME_BUILD_META_FILE) 2>/dev/null))
CHIME_LICENSE = MIT
CHIME_LICENSE_FILES = chime/README.md
CHIME_DEPENDENCIES = mosquitto openssl

ifeq ($(CHIME_VERSION),)
$(error Missing chime app version in $(CHIME_VERSION_FILE))
endif

ifeq ($(CHIME_OS_VERSION),)
$(error Missing VIRTUALCHIME_OS_VERSION in $(VIRTUALCHIME_VERSION_FILE))
endif

ifeq ($(CHIME_CONFIG_VERSION),)
$(error Missing CHIME_CONFIG_VERSION in $(VIRTUALCHIME_VERSION_FILE))
endif

ifeq ($(CHIME_BUILD_ID),)
CHIME_BUILD_ID = unknown
endif

CHIME_COMMON_SOURCES = \
	common/src/logging/logger.cpp \
	common/src/runtime/signal_handler.cpp \
	common/src/util/environment.cpp \
	common/src/util/filesystem.cpp \
	common/src/util/platform.cpp \
	common/src/util/strings.cpp \
	common/src/util/time.cpp

CHIME_DAEMON_SOURCES = \
	chime/src/main.cpp \
	chime/src/audio/aplay_audio_player.cpp \
	chime/src/config/chime_config.cpp \
	chime/src/network/linux_wifi_monitor.cpp \
	chime/src/service/chime_service.cpp \
	common/src/mqtt/client.cpp

CHIME_WEBD_SOURCES = \
	chime/src/webd/main.cpp \
	chime/src/webd/apply_manager.cpp \
	chime/src/webd/config_store.cpp \
	chime/src/webd/json.cpp \
	chime/src/webd/mdns.cpp \
	chime/src/webd/ui_assets.cpp \
	chime/src/webd/web_server.cpp \
	chime/src/webd/wifi_scan.cpp

define CHIME_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++20 -Wall -Wextra \
		-DCHIME_APP_VERSION=\"$(CHIME_VERSION)\" \
		-DVIRTUALCHIME_OS_VERSION=\"$(CHIME_OS_VERSION)\" \
		-DCHIME_CONFIG_VERSION=\"$(CHIME_CONFIG_VERSION)\" \
		-DCHIME_BUILD_ID=\"$(CHIME_BUILD_ID)\" \
		-I$(@D)/chime/include -I$(@D)/common/include \
		-o $(@D)/chime/chime \
		$(addprefix $(@D)/,$(CHIME_COMMON_SOURCES) $(CHIME_DAEMON_SOURCES)) \
		$(TARGET_LDFLAGS) -lmosquitto
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++20 -Wall -Wextra \
		-I$(@D)/chime/include -I$(@D)/common/include \
		-o $(@D)/chime/chime-webd \
		$(addprefix $(@D)/,$(CHIME_COMMON_SOURCES) $(CHIME_WEBD_SOURCES)) \
		$(TARGET_LDFLAGS) -lssl -lcrypto -lpthread
endef

# Install to /usr/local/bin
define CHIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/chime/chime $(TARGET_DIR)/usr/local/bin/chime
	$(INSTALL) -D -m 0755 $(@D)/chime/chime-webd $(TARGET_DIR)/usr/local/bin/chime-webd
	mkdir -p $(TARGET_DIR)/etc/chime-web/tls
	mkdir -p $(TARGET_DIR)/etc
	printf '%s\n' "$(CHIME_VERSION)" > $(TARGET_DIR)/etc/chime-app-version
	printf '%s\n' "$(CHIME_BUILD_ID)" > $(TARGET_DIR)/etc/chime-build-id
endef

$(eval $(generic-package))
