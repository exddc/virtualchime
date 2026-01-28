################################################################################
#
# chime - Virtual Chime doorbell application
#
################################################################################

CHIME_VERSION = 1.0
CHIME_SITE = $(BR2_EXTERNAL_VIRTUALCHIME_PATH)/../chime
CHIME_SITE_METHOD = local
CHIME_LICENSE = MIT
CHIME_LICENSE_FILES = LICENSE
CHIME_DEPENDENCIES = mosquitto

define CHIME_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++20 -Wall -Wextra \
		-o $(@D)/chime $(@D)/src/main.cpp \
		$(TARGET_LDFLAGS) -lmosquitto
endef

# Install to /usr/local/bin
define CHIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/chime $(TARGET_DIR)/usr/local/bin/chime
endef

$(eval $(generic-package))
