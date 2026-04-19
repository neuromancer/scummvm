MODULE := engines/neuromancer

MODULE_OBJS := \
	decompress.o \
	level_handlers.o \
	metaengine.o \
	neuromancer.o \
	neuro_vm.o \
	resource.o

MODULE_DIRS += \
	engines/neuromancer

# This module can be built as a plugin
ifeq ($(ENABLE_NEUROMANCER), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
