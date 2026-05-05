MODULE := engines/interspective

MODULE_OBJS := \
	inter.o \
	innocent.o \
	metaengine.o \
	resources.o \
	datafile.o \
	main_dat.o \
	mapfile.o \
	prog_dat.o \
	graphics.o \
	logic.o \
	program.o \
	debugger.o \
	animation.o \
	value.o \
	sprite.o \
	actor.o \
	exit.o \
	room.o \
	eventmanager.o \
	debug.o \
	movie.o \
	musicparser.o \
	sound.o

inter.o: opcode_handlers.cpp

# This module can be built as a plugin
ifeq ($(ENABLE_INTERSPECTIVE), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
