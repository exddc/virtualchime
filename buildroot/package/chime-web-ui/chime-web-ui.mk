################################################################################
#
# chime-web-ui - production Svelte bundle for chime-webd
#
################################################################################

CHIME_WEB_UI_SITE = $(BR2_EXTERNAL_OPENCHIME_PATH)/../webui-src
CHIME_WEB_UI_SITE_METHOD = local
CHIME_WEB_UI_VERSION = $(strip $(shell cat $(CHIME_WEB_UI_SITE)/.source-id 2>/dev/null))
CHIME_WEB_UI_LICENSE = MIT
CHIME_WEB_UI_LICENSE_FILES = README.md
CHIME_WEB_UI_SUPPORTS_IN_SOURCE_BUILD = YES
CHIME_WEB_UI_INSTALL_TARGET = YES
CHIME_WEB_UI_TARGET_DIST = /usr/local/share/chime-web-ui/dist
CHIME_WEB_UI_ASSERT = $(BR2_EXTERNAL_OPENCHIME_PATH)/board/common/assert_chime_web_ui_dist.sh
CHIME_WEB_UI_BUN_CACHE = $(CHIME_WEB_UI_DL_DIR)/bun-cache

ifeq ($(CHIME_WEB_UI_VERSION),)
$(error Missing webui source id in $(CHIME_WEB_UI_SITE)/.source-id)
endif

define CHIME_WEB_UI_BUILD_CMDS
	if [ ! -f $(@D)/package.json ]; then \
		echo "ERROR: missing $(@D)/package.json (webui sources were not staged)" >&2; \
		exit 1; \
	fi
	if [ ! -f $(@D)/bun.lock ]; then \
		echo "ERROR: missing $(@D)/bun.lock (locked Bun dependencies are required)" >&2; \
		exit 1; \
	fi
	BUN="bun"; \
	if ! command -v "$$BUN" >/dev/null 2>&1; then \
		if [ -x /usr/local/bin/bun ]; then \
			BUN=/usr/local/bin/bun; \
		else \
			echo "ERROR: bun is required to build chime-web-ui" >&2; \
			exit 1; \
		fi; \
	fi; \
	echo "[chime-web-ui] Using $$BUN ($$("$$BUN" --version))"; \
	cd $(@D) && \
		BUN_INSTALL_CACHE_DIR=$(CHIME_WEB_UI_BUN_CACHE) \
		"$$BUN" install --frozen-lockfile --backend=copyfile && \
		"$$BUN" run build
endef

define CHIME_WEB_UI_INSTALL_TARGET_CMDS
	rm -rf $(TARGET_DIR)$(CHIME_WEB_UI_TARGET_DIST)
	mkdir -p $(TARGET_DIR)$(CHIME_WEB_UI_TARGET_DIST)
	cp -a $(@D)/dist/. $(TARGET_DIR)$(CHIME_WEB_UI_TARGET_DIST)/
	chmod -R a+rX $(TARGET_DIR)/usr/local/share/chime-web-ui
	bash $(CHIME_WEB_UI_ASSERT) $(TARGET_DIR)$(CHIME_WEB_UI_TARGET_DIST)
endef

$(eval $(generic-package))
