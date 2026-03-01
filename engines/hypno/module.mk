MODULE := engines/hypno

MODULE_OBJS := \
	actions.o \
	arcade.o \
	boyz/arcade.o \
	boyz/boyz.o \
	boyz/hard.o \
	boyz/scene.o \
	cursors.o \
	grammar_mis.o \
	grammar_arc.o \
	hypno.o \
	lexer_mis.o \
	lexer_arc.o \
	libfile.o \
	metaengine.o \
	scene.o \
	spider/arcade.o \
	spider/hard.o \
	spider/spider.o \
	spider/talk.o \
	teacher/teacher.o \
	teacher/Animation.o \
	teacher/Character.o \
	teacher/FlagArray.o \
	teacher/GameState.o \
	teacher/Handler.o \
	teacher/demo/Handler1.o \
	teacher/demo/Handler2.o \
	teacher/demo/Handler4.o \
	teacher/demo/Handler6.o \
	teacher/demo/Handler8.o \
	teacher/demo/Handler12.o \
	teacher/demo/Handler13.o \
	teacher/demo/Handler14.o \
	teacher/demo/Handler15.o \
	teacher/demo/Handler16.o \
	teacher/demo/SCI_AfterSchoolMenu.o \
	teacher/demo/SCI_SearchScreen.o \
	teacher/demo/SCI_Dialog.o \
	teacher/CombatEngine.o \
	teacher/CombatSprite.o \
	teacher/SC_Combat1.o \
	teacher/Target.o \
	teacher/Weapon.o \
	teacher/Hotspot.o \
	teacher/IconBar.o \
	teacher/Message.o \
	teacher/MouseControl.o \
	teacher/OptionMenu.o \
	teacher/Palette.o \
	teacher/Parser.o \
	teacher/Sample.o \
	teacher/SC_Question.o \
	teacher/SC_Timer.o \
	teacher/Sprite.o \
	teacher/TeacherFont.o \
	teacher/StringTable.o \
	teacher/TimedEvent.o \
	video.o \
	wet/arcade.o \
	wet/cursors.o \
	wet/hard.o \
	wet/wet.o

MODULE_DIRS += \
	engines/hypno

# HACK: Skip this when including the file for detection objects.
ifeq "$(LOAD_RULES_MK)" "1"
hypno-grammar:
	flex engines/hypno/lexer_arc.l
	bison engines/hypno/grammar_arc.y
	flex engines/hypno/lexer_mis.l
	bison engines/hypno/grammar_mis.y
endif

# This module can be built as a plugin
ifeq ($(ENABLE_HYPNO), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
