################################################################################
#
# chime - Virtual Chime doorbell application
#
################################################################################

CHIME_VERSION = 1.0
CHIME_SITE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/../chime-src
CHIME_SITE_METHOD = local
CHIME_LICENSE = MIT
CHIME_LICENSE_FILES = chime/README.md
CHIME_DEPENDENCIES = mosquitto

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
		-I$(@D)/chime/include -I$(@D)/common/include \
		-o $(@D)/chime $(addprefix $(@D)/,$(CHIME_SOURCES)) \
		$(TARGET_LDFLAGS) -lmosquitto
endef

# Install to /usr/local/bin
define CHIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/chime $(TARGET_DIR)/usr/local/bin/chime
endef

$(eval $(generic-package))
