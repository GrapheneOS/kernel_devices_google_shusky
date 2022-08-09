M ?= $(shell pwd)

KBASE_PATH_RELATIVE = $(M)

KBUILD_OPTIONS += CONFIG_DRM_PANEL_GOOGLE_HK3=m

EXTRA_CFLAGS += -DDYNAMIC_DEBUG_MODULE=1
EXTRA_CFLAGS += -I$(KERNEL_SRC)/../private/google-modules/display
EXTRA_CFLAGS += -I$(KERNEL_SRC)/../private/google-modules/display/include/uapi
EXTRA_CFLAGS += -Werror

EXTRA_SYMBOLS += $(OUT_DIR)/../private/google-modules/display/Module.symvers

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" \
	$(@)
