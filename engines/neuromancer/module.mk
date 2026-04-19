MODULE := engines/neuromancer

MODULE_OBJS := \
	bih_script.o \
	decompress.o \
	font.o \
	gfx.o \
	inventory.o \
	level_handlers.o \
	menu.o \
	metaengine.o \
	music_data.o \
	music_player.o \
	neuromancer.o \
	neuro_vm.o \
	pax.o \
	resource.o \
	scene.o \
	scene_main_menu.o \
	scene_real_world.o \
	text_scroller.o

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
