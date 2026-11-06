LOCAL_PATH := $(call my-dir)

# mruby as a static library, compiled from the exact source list the host
# mruby build used (so desktop and Android have the same gems).
# That list is generated on the host by `make mruby` (see Makefile.common).
MRUBY_SOURCES_MK := $(LOCAL_PATH)/build/host/mruby_sources.mk
ifeq ($(wildcard $(MRUBY_SOURCES_MK)),)
$(error $(MRUBY_SOURCES_MK) is missing - run `make mruby` on the host first)
endif
include $(MRUBY_SOURCES_MK)

include $(CLEAR_VARS)

LOCAL_MODULE := mruby
LOCAL_SRC_FILES := $(MRUBY_SRC_FILES)
LOCAL_C_INCLUDES := $(addprefix $(LOCAL_PATH)/,$(MRUBY_INCLUDES))
LOCAL_CFLAGS := -std=gnu99 $(addprefix -D,$(MRUBY_DEFINES))

LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/../mruby-cpp/mruby/include \
	$(LOCAL_PATH)/build/host/include \
	$(LOCAL_PATH)/../mruby-cpp
# Old bionic <stdint.h> only defines INT64_MAX etc. for C++ when these are set
# before its first inclusion, which SDL.h does long before mruby.h.
LOCAL_EXPORT_CFLAGS := $(addprefix -D,$(MRUBY_APP_DEFINES)) \
	-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS
LOCAL_EXPORT_LDLIBS := -lm

include $(BUILD_STATIC_LIBRARY)
