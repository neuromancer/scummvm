/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $URL$
 * $Id$
 *
 */

#include "interspective/inter.h"

#include "interspective/actor.h"
#include "interspective/animation.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/movie.h"
#include "interspective/musicparser.h"
#include "interspective/room.h"
#include "interspective/util.h"

#include "common/events.h"
#include "common/util.h"

namespace Interspective {
#define OPCODE(num) template<> Interpreter::OpResult Interpreter::opcodeHandler<num>(ValueVector a, CodePointer current, CodePointer next)

OPCODE(0x00) {
	// nop
	debugC(2, kDebugLevelScript, "opcode 0x00: nop");
	return kThxBye;
}

OPCODE(0x01) {
	// exit
	// (some peculiarities in conj. with op 0x38, needs research TODO)
	debugC(2, kDebugLevelScript, "opcode 0x01: exit");
	return kReturn;
}

OPCODE(0x02) {
	// check equality
	debugC(2, kDebugLevelScript, "opcode 0x02: if %s == %s", +a[0], +a[1]);
	unless (a[0] == a[1])
		return kFail;
	return kThxBye;
}

OPCODE(0x03) {
	// check inequality
	debugC(2, kDebugLevelScript, "opcode 0x03: if %s != %s", +a[0], +a[1]);
	if (a[0] == a[1])
		return kFail;
	return kThxBye;
}

OPCODE(0x04) {
	// less than. DOS handler at CS:0x376c uses JL — *signed* comparison via JLE
	// in the inverse: skip when (int)a[1] <= (int)a[0]. Body runs when a[0] < a[1]
	// in signed two's-complement arithmetic. Value::operator< is unsigned so we
	// can't use it here without misclassifying scripts that store signed deltas
	// (e.g. negative scroll offsets, signed timer deltas).
	debugC(2, kDebugLevelScript, "opcode 0x04: if %s < %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) < int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x05) {
	// greater than (signed). DOS CS:0x377f uses JG inverse logic.
	debugC(2, kDebugLevelScript, "opcode 0x05: if %s > %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) > int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x06) {
	// less or equal (signed). DOS CS:0x3792 uses JLE — sets skip when (int)a[1] <
	// (int)a[0], i.e. body runs when (int)a[0] <= (int)a[1].
	debugC(2, kDebugLevelScript, "opcode 0x06: if %s <= %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) <= int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x07) {
	// greater or equal (signed). DOS CS:0x37a5 uses JGE.
	debugC(2, kDebugLevelScript, "opcode 0x07: if %s >= %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) >= int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x08) {
	// bit-and check: skip if (a[0] & a[1]) == 0 — succeed if any bit overlaps.
	// DOS handler at CS:0x37b8.
	debugC(2, kDebugLevelScript, "opcode 0x08: if %s & %s", +a[0], +a[1]);
	unless ((uint16(a[0]) & uint16(a[1])) != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x09) {
	// "either non-zero": skip only when both args are zero.
	// DOS handler at CS:0x37cb — sets g_skip_counter when a[0] == 0 && a[1] == 0.
	debugC(2, kDebugLevelScript, "opcode 0x09: if %s || %s", +a[0], +a[1]);
	unless (uint16(a[0]) != 0 || uint16(a[1]) != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x0f) {
	// check room
	debugC(2, kDebugLevelScript, "opcode 0x0f: if current room == %s then", +a[0]);
	unless (a[0] == _logic->currentRoom())
		return kFail;
	return kThxBye;
}

enum {
	kSoundAdlib =  1,
	kSoundSB =     2,
	kSoundRoland = 4
};

OPCODE(0x12) {
	// "if sound device is on" — bit-mask test against (g_music_enabled | g_sfx_enabled).
	// DOS: skip if (music | sfx) & a[0] == 0. Bits: 1=Adlib, 2=SoundBlaster, 4=Roland.
	// In ScummVM the audio path is unified through the mixer, so we report all three
	// device classes as available — scripts that gate behaviour on sound type
	// (e.g. picking richer Roland samples) get the most-featured branch.
	debugC(2, kDebugLevelScript, "opcode 0x12: if sound type %s is on", +a[0]);
	unless (uint16(a[0]) & (kSoundAdlib | kSoundSB | kSoundRoland))
		return kFail;
	return kThxBye;
}

OPCODE(0x13) {
	// DOS CS:0x3945: skip if (hit_region != 0) || (step_pending == 0). Body runs
	// when there's an action pending but no hotspot was clicked — i.e. the user
	// pressed something with no target. Previous engine code tested the left
	// mouse button which is unrelated; corrected to mirror the binary.
	debugC(2, kDebugLevelScript, "opcode 0x13: if pending action with no hit target");
	if (Log.hitTarget() != 0 || !Log.stepPending())
		return kFail;
	return kThxBye;
}

OPCODE(0xd8) {
	// Yield to next tick: save the next instruction as continuation, exit interpreter.
	// DOS handler at CS:0x542d calls FUN_1000_3154 which writes into the per-mode room-script
	// slot then sets g_break_outer = 1 so MainGameLoop dispatches it next frame.
	debugC(2, kDebugLevelScript, "opcode 0xd8: yield to next frame");
	_logic->runLater(next, 1);
	return kReturn;
}

OPCODE(0xda) {
	// Clear the per-room zone list (g_zone_count = 0).
	// DOS handler at CS:0x5467. Pairs with 0xd9 which adds entries.
	// (Engine doesn't track per-room zones yet; recording the call is the right semantics.)
	debugC(2, kDebugLevelScript, "opcode 0xda: clear zone list STUB");
	return kThxBye;
}

OPCODE(0xdc) {
	// Clear g_collision_zone_count (zone-A count, used by FindZoneAtPoint).
	// DOS handler at CS:0x54b8. Engine doesn't model these zones yet.
	debugC(2, kDebugLevelScript, "opcode 0xdc: clear collision zones STUB");
	return kThxBye;
}

OPCODE(0xde) {
	// Clear g_zone_b_count (zone-B count).
	// DOS handler at CS:0x54fd. Engine doesn't model these zones yet.
	debugC(2, kDebugLevelScript, "opcode 0xde: clear zone-B STUB");
	return kThxBye;
}

OPCODE(0xe2) {
	// Clear g_walkbox_count (walkbox list at DS:0x6617).
	// DOS handler at CS:0x5582. Engine doesn't model walkboxes yet.
	debugC(2, kDebugLevelScript, "opcode 0xe2: clear walkbox count STUB");
	return kThxBye;
}

OPCODE(0xf6) {
	// Set music volume to maximum. DOS handler at CS:0x5824 patches the music driver
	// state bytes directly to 0xff (volume) and 0x3f / 0 (mode-dependent flag).
	// In ScummVM the audio mixer handles volume, so this is effectively a no-op.
	debugC(2, kDebugLevelScript, "opcode 0xf6: max music volume STUB");
	return kThxBye;
}

OPCODE(0xf8) {
	// Stop all music AND sfx (panic stop).
	// DOS handler at CS:0x5889 calls the music driver's "stop" entry, clears
	// g_current_tune_addr, then calls the sfx driver's "stop" if active.
	// Engine doesn't have a separate sfx player — Music covers both.
	debugC(2, kDebugLevelScript, "opcode 0xf8: stop all music/sfx");
	Music.unloadMusic();
	Music.silence();
	return kThxBye;
}

OPCODE(0x10) {
	// Timer fire: if a[0] != 0 AND a[0] <= frame_tick_counter, reset a[0] = 0 and execute body.
	// DOS handler at CS:0x3903. Pairs with Op_ed which writes the deadline.
	uint16 deadline = a[0];
	uint16 now = Log.frameTicks();
	if (deadline != 0 && deadline <= now) {
		debugC(2, kDebugLevelScript, "opcode 0x10: timer fired (deadline=%u tick=%u)", deadline, now);
		a[0] = 0;
		return kThxBye;
	}
	debugC(3, kDebugLevelScript, "opcode 0x10: timer pending (deadline=%u tick=%u)", deadline, now);
	return kFail;
}

OPCODE(0x11) {
	// "if slow CPU" — body executes only on slow machines (the original calibrated
	// at startup and set g_slow_cpu when a frame took too long). Modern hosts are
	// always fast, so the body is always skipped.
	// DOS handler at CS:0x391d: skip if g_slow_cpu (DS:0x67b5) == 0.
	debugC(2, kDebugLevelScript, "opcode 0x11: if slow CPU (always false)");
	return kFail;
}

OPCODE(0x17) {
	// DOS CS:0x3996: skip when slot.id == 0 (exit MISSING). Body runs when the
	// exit EXISTS — opcode is "if exit exists". The engine had it inverted.
	debugC(1, kDebugLevelScript, "opcode 0x17: if exit %s exists", +a[0]);
	Exit *exit = _logic->blockProgram()->getExit(a[0]);
	if (exit == nullptr)
		return kFail;
	return kThxBye;
}

OPCODE(0x19) {
	// DOS CS:0x39bc: skip when actor.room == 0 → body runs when actor IS placed
	// somewhere. Opcode is "if actor present". Was inverted in the engine.
	debugC(1, kDebugLevelScript, "opcode 0x19: if actor %s in some room", +a[0]);
	Actor *ac = Log.getActor(a[0]);
	if (!ac || ac->room() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1a) {
	// DOS CS:0x39d0: skip when slot.id != 0 → body runs when exit MISSING.
	// Inverse of 0x17. Was inverted in the engine.
	debugC(1, kDebugLevelScript, "opcode 0x1a: if exit %s missing", +a[0]);
	Exit *exit = _logic->blockProgram()->getExit(a[0]);
	if (exit != nullptr)
		return kFail;
	return kThxBye;
}

OPCODE(0x1c) {
	// DOS CS:0x39f6: skip when actor.room != 0 → body runs when actor MISSING
	// (no room set). Inverse of 0x19. Was inverted in the engine.
	debugC(1, kDebugLevelScript, "opcode 0x1c: if actor %s not placed", +a[0]);
	Actor *ac = Log.getActor(a[0]);
	if (ac && ac->room() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1d) {
	// if actor `a[1]` is in current room AND at frame `a[0]`.
	// DOS handler at CS:0x3a10. Like 0x1f but with == instead of !=.
	debugC(1, kDebugLevelScript, "opcode 0x1d: if actor %s in current room AND at %s", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() != a[0])
		return kFail;
	return kThxBye;
}

OPCODE(0x1f) {
	// if actor in current room then whatever
	debugC(1, kDebugLevelScript, "opcode 0x1f: if actor %s is in current room but not at %s then", +a[1], +a[0]);

	Actor *ac = Log.getActor(a[1]);
	if (ac->room() == Log.currentRoom()) {
		if (ac->frameId() == a[0])
			return kFail;
	} else
		return kFail;
	return kThxBye;
}

OPCODE(0x24) {
	// check nonzeroness
	debugC(2, kDebugLevelScript, "opcode 0x24: if (%s)", +a[0]);
	if (a[0] == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x25) {
	// check zeroness — opposite of 0x24.
	// DOS handler at CS:0x3aef.
	debugC(2, kDebugLevelScript, "opcode 0x25: if not (%s)", +a[0]);
	if (a[0] != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x2c) {
	// else
	debugC(2, kDebugLevelScript, "opcode 0x2c: else");

	return kElse;
}

OPCODE(0x2d) {
	// end if
	debugC(2, kDebugLevelScript, "opcode 0x2d: end if");
	return kEndIf;
}

OPCODE(0x35) {
	// jump
	debugC(2, kDebugLevelScript, "opcode 0x35: jump to %s", +a[0]);
	return static_cast<CodePointer &>(a[0]);
}

OPCODE(0x36) {
	// Call procedure. DOS Op_call_handler at CS:0x3bf8 pushes the saved PC and
	// branch state to a stack then sets PC to arg0 — when the called code hits
	// Op_37 it pops back and the outer dispatcher continues from after the
	// call. Engine handles the stack via C++ recursion: p.run() invokes a
	// nested Interpreter::run, which returns when its body emits Op_37/kReturn.
	// CRITICAL: must return kThxBye, not kReturn — kReturn would terminate the
	// OUTER procedure too, dropping every opcode after the call. Previous code
	// returned kReturn, which is why scripts with multiple Op_36 calls in a row
	// only ever ran the first.
	debugC(2, kDebugLevelScript, ">>>opcode 0x36: call procedure %s", +a[0]);
	CodePointer &p = static_cast<CodePointer &>(a[0]);
	p.run();
	debugC(2, kDebugLevelScript, "<<<opcode 0x36: called procedure %s", +a[0]);
	return kThxBye;
}

OPCODE(0x37) {
	// explicit return from procedure (matches 0x36 call). DOS handler at CS:0x3c2e
	// pops the call stack — in this engine the call chain is handled by C++ recursion,
	// so just signal end-of-procedure.
	debugC(2, kDebugLevelScript, "opcode 0x37: return");
	return kReturn;
}

OPCODE(0x39) {
	// Schedule a main-interpreter procedure to run on a later tick.
	// DOS handler at CS:0x3c88 first cancels any matching pending entry then appends.
	// Mirror of 0x3b but for the main (top-level) interpreter context.
	debugC(2, kDebugLevelScript, "opcode 0x39: execute main %s later", +a[0]);
	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), _logic->mainInterpreter());
	_logic->cancelLater(p);
	_logic->runLater(p);
	return kThxBye;
}

OPCODE(0x3b) {
	// Schedule a block-interpreter procedure to run on a later tick.
	// DOS handler at CS:0x3c7f first calls Op_3c (which cancels any matching pending
	// entry) then appends the new entry to g_deferred_script_queue. The cancel-first
	// behaviour guarantees no duplicates of the same (segment, offset) get queued.
	debugC(2, kDebugLevelScript, "opcode 0x3b: execute %s later", +a[0]);
	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), _logic->blockInterpreter());
	_logic->cancelLater(p);
	_logic->runLater(p);
	return kThxBye;
}

OPCODE(0x3d) {
	// save first arg -- instruction pointer -- for after skipping cutscene
	debugC(2, kDebugLevelScript, "opcode 0x3d: store position to continue if cutscene skipped to %s", +a[0]);
	Log.setSkipPoint(static_cast<CodePointer &>(a[0]));
	return kThxBye;
}

OPCODE(0x41) {
	// say (protagonist)
	debugC(2, kDebugLevelScript, "opcode 0x41: say %s", +a[0]);

	if (Log.protagonist()->isSpeaking()) {
		Log.protagonist()->callMeWhenSilent(current);
		return kReturn;
	}

	if (Log.protagonist()->isMoving()) {
		Log.protagonist()->callMeWhenStill(current);
		return kReturn;
	}

	Log.protagonist()->say(a[0]);
	return kThxBye;
}

OPCODE(0x43) {
	// say
	debugC(2, kDebugLevelScript, "opcode 0x43: %s says %s", +a[0], +a[1]);

	Actor *ac = Log.getActor(a[0]);
	if (ac->isSpeaking()) {
		ac->callMeWhenSilent(current);
		return kReturn;
	}

	if (ac->isMoving()) {
		ac->callMeWhenStill(current);
		return kReturn;
	}

	ac->say(a[1]);
	return kThxBye;
}

OPCODE(0x47) {
	// say (no actor)
	debugC(1, kDebugLevelScript, "opcode 0x47: say at [%s:%s] with colour %s in max %s lines text %s STUB", +a[0], +a[1], +a[2], +a[3], +a[4]);

	return kThxBye;
}

OPCODE(0x4a) {
	// wait until silent (protagonist)
	debugC(2, kDebugLevelScript, "opcode 0x4a: wait until protagonist is silent");

	Log.protagonist()->callMeWhenSilent(next);
	return kReturn;
}

OPCODE(0x4b) {
	// wait until silent
	debugC(2, kDebugLevelScript, "opcode 0x4b: wait %s is silent", +a[0]);

	Log.getActor(a[0])->callMeWhenSilent(next);
	return kReturn;
}

OPCODE(0x54) {
	debugC(2, kDebugLevelScript, "opcode 0x54: ask about '%s' at %s:%s %sx%s", +a[4], +a[0], +a[1], +a[2], +a[3]);

	uint16 result;
	unless ((result = _graphics->ask(a[0], a[1], a[2], a[3], a[4])) == 0xffff)
		return CodePointer(result, this);
	return kThxBye;
}

OPCODE(0x55) {
	// paint text
	// args: left, top, colour, text
	debugC(2, kDebugLevelScript, "opcode 0x55: paint '%s' with colour %s at %s:%s", +a[3], +a[2], +a[0], +a[1]);
	_graphics->paintText(a[0], a[1], a[2], a[3]);
	return kThxBye;
}

OPCODE(0x56) {
	// say text
	debugC(2, kDebugLevelScript, "opcode 0x86: say %s for %s frames", +a[1], +a[0]);
	Graf.say(a[1], a[1], a[0]);
	return kThxBye;
}

OPCODE(0x57) {
	// wait until said
	debugC(2, kDebugLevelScript, "opcode 0x57: wait until text is said");
	Graf.runWhenSaid(next);
	return kReturn;
}

OPCODE(0x60) {
	// lookup locally
	// takes a list (1st)
	// a value (2nd)
	// a field (as offset from structure start) (2rd)
	// first word on the list is entry length in words (minus one for index)
	// then are entries, first word being index
	// finds entry matching index == value in the list and
	// saves value of specified field in 4th argument
	uint16 offset = static_cast<CodePointer &>(a[0]).offset();

	uint16 value;
	byte *pos = _base + offset;
	uint16 width = READ_LE_UINT16(pos);
	pos += 2;
	while(true) {
		uint16 index = READ_LE_UINT16(pos);
		if (index == 0xffff) {
			value = index;
			break;
		}
		pos += 2;
		if (index == a[1]) {
			value = READ_LE_UINT16(pos + a[2]);
			break;
		}
		pos += width * 2;
	}

	debugC(2, kDebugLevelScript, "opcode 0x60: %s = %d == search list %s for %s and return field %s", +a[3], value, +a[0], +a[1], +a[2]);
	a[3] = value;
	return kThxBye;
}

OPCODE(0x63) {
	// get actor property
	Actor *actor = _logic->getActor(a[0]);
	const char *desc;

	switch (uint16(a[1]) & 0xff) {
	case Actor::kOffsetRoom:
		desc = "Room";
		a[2] = actor->room();
		break;
/*	case Actor::kOffsetOffset:
		desc = "Offset";
		a[2] = actor->offset();
		break;
	case Actor::kOffsetLeft:
		desc = "Left";
		a[2] = actor->left();
		break;
	case Actor::kOffsetTop:
		desc = "Top";
		a[2] = actor->top();
		break;
	case Actor::kOffsetMainSprite:
		desc = "MainSprite";
		a[2] = actor->mainSprite();
		break;
	case Actor::kOffsetTicksLeft:
		desc = "TicksLeft";
		a[2] = actor->ticksLeft();
		break;
	case Actor::kOffsetCode:
		desc = "Code";
		a[2] = actor->code();
		break;
	case Actor::kOffsetInterval:
		desc = "Interval";
		a[2] = actor->interval();
		break;*/
	default:
		error("unhandled actor property %s", +a[1]);
	}

	debugC(2, kDebugLevelScript, "opcode 0x63: %s = get %s of actor %s", +a[2], desc, +a[0]);
	return kThxBye;
}

OPCODE(0x6c) {
	// add: a[0] += a[1]
	// DOS handler at CS:0x42b1.
	debugC(2, kDebugLevelScript, "opcode 0x6c: %s += %s", +a[0], +a[1]);
	a[0] = uint16(a[0]) + uint16(a[1]);
	return kThxBye;
}

OPCODE(0x6d) {
	// increment
	debugC(2, kDebugLevelScript, "opcode 0x6d: %s++", +a[0]);
	a[0]++;
	return kThxBye;
}

OPCODE(0x6e) {
	// subtract: a[0] -= a[1]
	// DOS handler at CS:0x42c9.
	debugC(2, kDebugLevelScript, "opcode 0x6e: %s -= %s", +a[0], +a[1]);
	a[0] = uint16(a[0]) - uint16(a[1]);
	return kThxBye;
}

OPCODE(0x6f) {
	// decrement
	debugC(2, kDebugLevelScript, "opcode 0x6d: %s--", +a[0]);
	a[0]--;
	return kThxBye;
}

OPCODE(0x71) {
	// swap: tmp = a[0]; a[0] = a[1]; a[1] = tmp
	// DOS handler at CS:0x42ea — a 6-instruction shuffle through BX/CX.
	debugC(2, kDebugLevelScript, "opcode 0x71: swap(%s, %s)", +a[0], +a[1]);
	uint16 tmp = a[0];
	a[0] = a[1];
	a[1] = tmp;
	return kThxBye;
}

OPCODE(0x70) {
	// assign
	debugC(2, kDebugLevelScript, "opcode 0x70: %s = %s", +a[0], +a[1]);
	a[0] = a[1];
	return kThxBye;
}

OPCODE(0x72) {
	// assign 1
	debugC(2, kDebugLevelScript, "opcode 0x72: %s = 1", +a[0]);
	a[0] = 1;
	return kThxBye;
}

OPCODE(0x73) {
	// assign 0
	debugC(2, kDebugLevelScript, "opcode 0x73: %s = 0", +a[0]);
	a[0] = 0;
	return kThxBye;
}

OPCODE(0x77) {
	// initialize protagonist
	debugC(2, kDebugLevelScript, "opcode 0x77: go to room %s facing %s", +a[0], +a[1]);
	_logic->changeRoom(a[0]);
	_logic->protagonist()->setRoom(a[0], a[1]);
	return kThxBye;
}

OPCODE(0x79) {
	// move actor to another room
	debugC(1, kDebugLevelScript, "opcode 0x79: move actor %s to room %s (and set current animation frame to %s STUB)", +a[0], +a[1], +a[2]);
	_logic->getActor(a[0])->setRoom(a[1]);
	return kThxBye;
}

OPCODE(0x74) {
	// Boolean toggle: if a[0] is zero set it to 1; otherwise set it to 0.
	// DOS handler at CS:0x430a — branches between Op_72 (=1) and Op_73 (=0).
	debugC(2, kDebugLevelScript, "opcode 0x74: %s = !%s", +a[0], +a[0]);
	a[0] = (uint16(a[0]) == 0) ? 1 : 0;
	return kThxBye;
}

OPCODE(0x3a) {
	// Cancel a previously-deferred MAIN-interpreter script.
	// DOS handler at CS:0x3cc7 matches against g_resourceSegment (main interpreter context).
	// Pairs with 0x39 which schedules into the same queue.
	debugC(2, kDebugLevelScript, "opcode 0x3a: cancel deferred (main) %s", +a[0]);
	_logic->cancelLater(CodePointer(static_cast<CodePointer &>(a[0]).offset(), _logic->mainInterpreter()));
	return kThxBye;
}

OPCODE(0x3c) {
	// Cancel a previously-deferred BLOCK-interpreter script.
	// DOS handler at CS:0x3cc1 matches against the current code segment (block context).
	// Pairs with 0x3b which schedules into the same queue.
	debugC(2, kDebugLevelScript, "opcode 0x3c: cancel deferred (block) %s", +a[0]);
	_logic->cancelLater(CodePointer(static_cast<CodePointer &>(a[0]).offset(), _logic->blockInterpreter()));
	return kThxBye;
}

OPCODE(0x3e) {
	// Clear the ESC/skip-cutscene handler. DOS handler at CS:0x3d23 just clears
	// the g_esc_during_script flag (DS:0x672c). In the engine we reset the
	// stored skip point so canSkipCutscene() returns false.
	debugC(2, kDebugLevelScript, "opcode 0x3e: clear ESC handler");
	Log.setSkipPoint(CodePointer());
	return kThxBye;
}

OPCODE(0x4c) {
	// Yield to next tick (no condition). DOS handler at CS:0x3eff calls FUN_1000_3154
	// which saves the continuation in g_room_script_slots[opcode_mode] and exits.
	// Same semantics as Op_d8.
	debugC(2, kDebugLevelScript, "opcode 0x4c: yield to next frame");
	_logic->runLater(next, 1);
	return kReturn;
}

OPCODE(0x7b) {
	// Enable exit `a[0]` (idempotent — only sets the bit if currently clear).
	// DOS handler at CS:0x4459. Pairs with 0x7c (disable). The previous engine
	// implementation of 0x7c was an unconditional toggle, which gave the wrong
	// semantics for this pair.
	Exit *exit = _logic->blockProgram()->getExit(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x7b: enable exit %s (was %s)", +a[0], exit && exit->isEnabled() ? "enabled" : "disabled");
	if (exit && !exit->isEnabled())
		exit->setEnabled(true);
	return kThxBye;
}

OPCODE(0x7c) {
	// Disable exit `a[0]` (idempotent — only clears the bit if currently set).
	// DOS handler at CS:0x4476. NOT a toggle as the previous engine code assumed.
	Exit *exit = _logic->blockProgram()->getExit(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x7c: disable exit %s (was %s)", +a[0], exit && exit->isEnabled() ? "enabled" : "disabled");
	if (exit && exit->isEnabled())
		exit->setEnabled(false);
	return kThxBye;
}

OPCODE(0x95) {
	// LOCK control: disallow user clicks/cursor movement.
	// DOS handler at CS:0x4a4c sets g_flag_no_step (DS:0x6747) = 1.
	// (NOTE: previous engine comment said "unlock" — the disassembly proves it's the LOCK side.)
	debugC(1, kDebugLevelScript, "opcode 0x95: lock control STUB");
	return kThxBye;
}

OPCODE(0x96) {
	// UNLOCK control: re-allow user clicks/cursor movement.
	// DOS handler at CS:0x4a52 clears g_flag_no_step and g_flag_step_pending.
	// (NOTE: previous engine comment said "disallow user interaction" — verified backwards.)
	debugC(1, kDebugLevelScript, "opcode 0x96: unlock control STUB");
	return kThxBye;
}

OPCODE(0x99) {
	// wait for protagonist to exit
	debugC(2, kDebugLevelScript, "opcoe 0x99: wait for protagonist to exit");

	Actor *ac = _logic->protagonist();
	if (ac->room() != _logic->currentRoom())
		return kThxBye;

	ac->callMe(next);
	return kReturn;
}

OPCODE(0x9a) {
	// wait for actor to exit
	debugC(2, kDebugLevelScript, "opcode 0x9a: wait for actor %s to exit", +a[0]);

	Actor *ac = _logic->getActor(a[0]);
	if (ac->room() != _logic->currentRoom())
		return kThxBye;

	ac->callMe(next);
	return kReturn;
}

OPCODE(0x9b) {
	// delay
	debugC(2, kDebugLevelScript, "opcode 0x9b: delay %s frames", +a[0]);
	_logic->runLater(next, a[0]);
	return kReturn;
}

OPCODE(0x9c) {
	// wait until another room
	debugC(2, kDebugLevelScript, "opcode 0x9a: wait until actor %s enters or %s ticks", +a[0], +a[1]);

	Actor *ac = _logic->getActor(a[0]);
	if (ac->room() != _logic->currentRoom()) {
		ac->tellMe(next, a[1]);
		return kReturn;
	}
	return kThxBye;
}

OPCODE(0x9d) {
	// set protagonist
	debugC(2, kDebugLevelScript, "opcode 0x9d: set protagonist(%s)", +a[0]);
	_logic->setProtagonist(a[0]);
	return kThxBye;
}

OPCODE(0x9e) {
	// warp protagonist to frame
	debugC(2, kDebugLevelScript, "opcode 0x9e: warp protagonist to frame %s", +a[0]);

//	Log.protagonist()->warpTo(a[0]);
	Log.protagonist()->setFrame(a[0]);
	return kThxBye;
}

OPCODE(0xab) {
	// set protagonist frame
	debugC(2, kDebugLevelScript, "opcode 0xab: set protagonist frame to %s", +a[0]);

	Actor *ac = Log.protagonist();
	if (ac->isFine()) {
		ac->callMe(current);
		return kReturn;
	}

	ac->moveTo(a[0]);
	return kThxBye;
}

OPCODE(0xad) {
	// turn actor
	debugC(2, kDebugLevelScript, "opcode 0xad: move actor %s to frame %s next", +a[0], +a[1]);

	Actor *ac = _logic->getActor(a[0]);
	if (ac->isFine()) {
		ac->callMe(current);
		return kReturn;
	}

	// TODO: special handling for protagonist
	ac->setFrame(a[1]);

	return kThxBye;
}

OPCODE(0xb9) {
	// set local animation
	debugC(2, kDebugLevelScript, "opcode 0xb9: set actor %s animation to %s", +a[0], +a[1]);

	Actor *ac = Log.getActor(a[0]);
	if (ac->isFine()) {
		ac->callMe(current);
		return kReturn;
	}

	ac->setAnimation(static_cast<CodePointer &>(a[1]));
	return kThxBye;
}

OPCODE(0xbc) {
	// hide actor
	debugC(2, kDebugLevelScript, "opcode 0xbc: hide actor %s", +a[0]);
	_logic->getActor(a[0])->hide();
	return kThxBye;
}

OPCODE(0xbd) {
	// set protagonist animation
	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), Log.mainInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xbd: set protagonist animation to %s", +p);

	Actor *ac = Log.protagonist();
	if (ac->isFine()) {
		ac->callMe(current);
		return kReturn;
	}

	ac->setAnimation(p);
	return kThxBye;
}

OPCODE(0xbe) {
	// set protagonist animation
	debugC(2, kDebugLevelScript, "opcode 0xbe: set protagonist animation to %s", +a[0]);

	Actor *ac = Log.protagonist();
	if (ac->isFine()) {
		ac->callMe(current);
		return kReturn;
	}

	ac->setAnimation(static_cast<CodePointer &>(a[0]));
	return kThxBye;
}

OPCODE(0xc2) {
	// add animation at cursor
	debugC(2, kDebugLevelScript, "opcode 0xc2: add animation %s at cursor partial STUB", +a[0]);
	_logic->addAnimation(new Animation(static_cast<CodePointer &>(a[0]), _graphics->cursorPosition()));
	return kThxBye;
}

OPCODE(0xc6) {
	// suspend execution until an animation's ip points to 0xff
	debugC(2, kDebugLevelScript, "opcode 0xc6: wait on animation %s", +a[0]);
	_logic->animation(a[0])->runOnNextFrame(next);
	return kReturn;
}

OPCODE(0xc7) {
	// play movie
	debugC(2, kDebugLevelScript, "opcode 0xc7: play movie %s with slowness %s", +a[0], +a[1]);
	Movie *m = Movie::fromFile(reinterpret_cast<char *>((byte *)(a[0])));
	m->setFrameDelay(a[1]);
	if (m->play())
		return kThxBye;
	else
		return kReturn;
}

OPCODE(0xc8) {
	// set backdrop
	// (not sure what's the difference to c9)
	debugC(2, kDebugLevelScript, "opcode 0xc8: set backdrop(%s)", +a[0]);
	_graphics->setBackdrop(a[0]);
	return kThxBye;
}

OPCODE(0xc9) {
	// Set the player's "current place" id (DAT_1000_0111 in DOS = main_dat global at offset 0x111).
	// DOS handler at CS:0x522f: stores arg0 to that global; only reloads backdrop when in map mode.
	// (NOT the same as 0xc8 — 0xc8 sets g_loaded_backdrop_id and reloads the room backdrop directly.)
	// TODO: model the "current place" global on Logic; for now the existing setBackdrop call keeps
	// in-game behaviour identical to 0xc8 since both trigger a backdrop reload.
	debugC(2, kDebugLevelScript, "opcode 0xc9: set current place to %s", +a[0]);
	_graphics->setBackdrop(a[0]);
	return kThxBye;
}

OPCODE(0xcb) {
	// load graphic
	debugC(1, kDebugLevelScript, "opcode 0xcb: load graphic %s STUB", +a[0]);
	return kThxBye;
}

OPCODE(0xcc) {
	// go fullscreen
	debugC(1, kDebugLevelScript, "opcode 0xcc: go fullscreen");
	Graf.goFullscreen();
	return kThxBye;
}

OPCODE(0xce) {
	// start cutscene
	debugC(2, kDebugLevelScript, "opcode 0xce: start cutscene partial STUB");
	Graf.hideCursor();
	// hide objects
	// set game area height to 200
	return kThxBye;
}

OPCODE(0xcf) {
	// fade out
	debugC(1, kDebugLevelScript, "opcode 0xcf: fadeout");
	if (_graphics->fadeOut())
		return kThxBye;
	else
		return kReturn;
}

OPCODE(0xd0) {
	debugC(1, kDebugLevelScript, "opcode 0xd0: partial fadeout");
	if (Graf.fadeOut(Graphics::kPartialFade))
		return kThxBye;
	else
		return kReturn;
}

OPCODE(0xd1) {
	debugC(2, kDebugLevelScript, "opcode 0xd1: fadein next paint");
	_graphics->willFadein();
	return kThxBye;
}

OPCODE(0xd2) {
	debugC(2, kDebugLevelScript, "opcode 0xd2: will fadein partially");
	Graf.willFadein(Graphics::kPartialFade);
	return kThxBye;
}

enum {
	kCopyProtectionRoom = 81,
	kIntroOffset	    = 0x33a3
};

OPCODE(0xd6) {
	// change room
	debugC(1, kDebugLevelScript, "opcode 0xd6: change room(%s)", +a[0]);
	uint16 room = a[0];
	if (room == kCopyProtectionRoom) {
		if (_engine->_startRoom)
			room = Eng._startRoom;
	}
	_logic->changeRoom(room);
	if (room == kCopyProtectionRoom && !Eng._copyProtection) {
		Log.runLater(CodePointer(kIntroOffset, Log.blockInterpreter()));
		return kReturn;
	}
	return kThxBye;
}

OPCODE(0xdb) {
	// add active rect
	debugC(1, kDebugLevelScript, "opcode 0xdb: add rect %s:%s-%s:%s::%s", +a[0], +a[1], +a[2], +a[3], +a[4]);
	Log.room()->addRect(Room::Rect(a[4].signd(), Common::Rect(a[0].signd(), a[1].signd(), a[2].signd(), a[3].signd())));
	return kThxBye;
}

OPCODE(0xdf) {
	// add actor frame
	Common::Array<byte> nexts;
	nexts.resize(8);
	for (int i = 0; i < 4; i++) {
		uint16 val = a[i+2];
		nexts[2*i] = val & 0xff;
		nexts[2*i+1] = val >> 8;
	}
	const int16 left = a[0].signd();
	const int16 top = a[1].signd();
	debugC(2, kDebugLevelScript, "opcode 0xdf: add actor frame %d %d %d %d %d %d %d %d %d %d", left, top, nexts[0], nexts[1], nexts[2], nexts[3], nexts[4], nexts[5], nexts[6], nexts[7]);
	Log.room()->addActorFrame(Common::Point(left, top), nexts);
	return kThxBye;
}

OPCODE(0xe5) {
	// hide all exits from the map
	debugC(1, kDebugLevelScript, "opcode 0xe5: hide exits from map STUB");
	return kThxBye;
}

OPCODE(0xe6) {
	// set room loop code
	debugC(2, kDebugLevelScript, "opcode 0xe6: set room loop to %s", +a[0]);
	assert(a[0].holdsCode());
	_logic->setRoomLoop(static_cast<CodePointer &>(a[0]));
	return kThxBye;
}

OPCODE(0xed) {
	// Set timer deadline: a[0] = frame_tick_counter + a[1]
	// DOS handler at CS:0x568c. Pairs with Op_10 which fires when reached.
	uint16 deadline = Log.frameTicks() + uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xed: set deadline %s = tick(%u) + %s", +a[0], Log.frameTicks(), +a[1]);
	a[0] = deadline;
	return kThxBye;
}

OPCODE(0xef) {
	// random
	uint16 value = _engine->getRandom(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xef: %s = %d == random(%s)", +a[1], value, +a[0]);
	a[1] = value;
	return kThxBye;
}

OPCODE(0xf0) {
	// load sfx set
	debugC(1, kDebugLevelScript, "opcode 0xf0: load sfx set %s STUB", +a[0]);
	return kThxBye;
}

OPCODE(0xf4) {
	// play music. The arg is a near offset into the main bytecode (IUC_MAIN.DAT) that points
	// at a music script: tune index (uint16) followed by kSetBeat/kJump/kStop bytecodes. Even
	// when called from a block, the offset is always relative to the main interpreter — music
	// scripts live in the global file, not in per-block bytecode.
	debugC(1, kDebugLevelScript, "opcode 0xf4: play music script at main offset 0x%04x", static_cast<CodePointer &>(a[0]).offset());
	Music.loadMusic(Log.mainInterpreter()->rawCode(static_cast<CodePointer &>(a[0]).offset()));
	return kThxBye;
}

OPCODE(0xf7) {
	// stop music
	debugC(2, kDebugLevelScript, "opcode 0xf7: stop music");
	Music.unloadMusic();
	Music.silence();
	return kThxBye;
}

OPCODE(0xf9) {
	// set sound on
	
	debugC(1, kDebugLevelScript, "opcode 0xf9: set %s to %s STUB", a[0] == 1 ? "music" : "sfx", +a[1]);
	return kThxBye;
}

OPCODE(0xfc) {
	// Quit. DOS handler at CS:0x5996:
	//   if (a[0] != 0): unconditional shutdown
	//   else:           show "Continue/Restart/Exit" modal — pick decides outcome
	// We don't have the modal yet, so for the menu case we just quit too —
	// matches the user's evident intent and lets ScummVM's regular confirmation
	// dialog (if enabled) front the call.
	debugC(2, kDebugLevelScript, "opcode 0xfc: quit%s", a[0] == 0 ? " (TODO: show menu first)" : " unconditionally");
	_engine->quitGame();
	return kThxBye;
}

// ============================================================================
// Translated from the DOS binary handlers. State the engine doesn't track
// (object table, walkboxes, drag/cursor mode, last-resolved actor) is held
// on Logic via the new state slots (Logic::verbMode / cursorMode / etc).
// Operations that reach into per-room scratch buffers the engine doesn't
// have are clearly marked TODO and produce a documented default.
// ============================================================================

OPCODE(0x0a) {
	// DOS CS:0x37de:
	//   if ((cursor_mode == 0x80 || step_pending) && (cursor_mode & arg0)) return;
	//   else skip;
	// arg0 is a bitmask of cursor-mode bits the script handles. The opcode is
	// "do this branch when the current cursor mode matches the mask AND we're
	// in a state to act (system mode or pending action)".
	uint16 mask = uint16(a[0]);
	uint16 cm = Log.cursorMode();
	debugC(2, kDebugLevelScript, "opcode 0x0a: if (cursor==0x80||step) && (cursor & %u)", mask);
	if ((cm == 0x80 || Log.stepPending()) && (cm & mask) != 0)
		return kThxBye;
	return kFail;
}

OPCODE(0x0b) {
	// DOS CS:0x37ff:
	//   if (step && cursor==0x40 && arg0 == drag_target) return;
	//   else skip;
	// "drag" with an *explicit* target match — distinct from 0x0e's drag check
	// (0x0e fires when cursor==0x20 — different drag mode).
	uint16 mask = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x0b: if step && cursor==0x40 && drag==%u", mask);
	if (Log.stepPending() && Log.cursorMode() == 0x40 && Log.dragTarget() == mask)
		return kThxBye;
	return kFail;
}

OPCODE(0x0c) {
	// DOS CS:0x38ab: skip if NOT in map mode → body runs ONLY in map mode.
	// Engine had this inverted previously.
	debugC(2, kDebugLevelScript, "opcode 0x0c: if in map mode");
	if (!Log.inMapMode())
		return kFail;
	return kThxBye;
}

OPCODE(0x0d) {
	// DOS CS:0x38d7: skip if (!step || cursor!=0x20 || drag==0). Body runs when
	// actively dragging an object (verb-on-object pre-action).
	debugC(2, kDebugLevelScript, "opcode 0x0d: if dragging (cursor=0x20 + drag set)");
	if (!Log.stepPending() || Log.cursorMode() != 0x20 || Log.dragTarget() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x0e) {
	// DOS CS:0x38b9: like 0x0d but only when dragTarget == arg0 (verb-on-this-object).
	debugC(2, kDebugLevelScript, "opcode 0x0e: if dragging && dragTarget == %s", +a[0]);
	if (!Log.stepPending() || Log.cursorMode() != 0x20 || Log.dragTarget() != uint16(a[0]))
		return kFail;
	return kThxBye;
}

OPCODE(0x14) {
	// IfFreshGameState (DOS CS:0x395a): fail if gameState != 0.
	debugC(2, kDebugLevelScript, "opcode 0x14: if game state == 0");
	if (Log.gameState() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x15) {
	// IfCellBitSet (DOS CS:0x3968): rotate-test of the per-room cell map at
	// (a[0]=room, a[1]=bit). The engine doesn't materialise the cell map yet,
	// so treat all cells as clear → take the false branch.
	debugC(2, kDebugLevelScript, "opcode 0x15: if cell bit %s of room %s set (cell map not loaded → false)", +a[1], +a[0]);
	return kFail;
}

OPCODE(0x16) {
	// 0x16 (DOS CS:0x3991): just calls ResolveOpcodeArg0 — read-and-discard.
	// Useful for triggering side-effects of evaluating an expression without
	// using the result.
	debugC(3, kDebugLevelScript, "opcode 0x16: read-discard %s", +a[0]);
	return kThxBye;
}

OPCODE(0x18) {
	// IfObjectMissing (DOS CS:0x39a9): tests Object[a[0]].id == 0.
	// Engine has no Object table — treat all queried objects as present.
	debugC(2, kDebugLevelScript, "opcode 0x18: if object %s missing (no object table → present)", +a[0]);
	return kFail;
}

OPCODE(0x1b) {
	// IfObjectPresent (inverse of 0x18, DOS CS:0x39e3).
	debugC(2, kDebugLevelScript, "opcode 0x1b: if object %s present", +a[0]);
	return kThxBye;
}

OPCODE(0x1e) {
	// IfImplicitActorAtFrame (DOS CS:0x3a0a): uses last-resolved actor (the
	// one whose offset GetActorOffset most recently set) and tests
	// (room == currentLocation && frame == a[0]). The engine doesn't track
	// "last actor" yet — fall back to the protagonist.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x1e: if implicit actor at frame %s", +a[0]);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() != uint16(a[0]))
		return kFail;
	return kThxBye;
}

OPCODE(0x20) {
	// IfImplicitActorNotAtFrame (DOS CS:0x3a33): inverse of 0x1e.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x20: if implicit actor not at frame %s", +a[0]);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() == uint16(a[0]))
		return kFail;
	return kThxBye;
}

OPCODE(0x21) {
	// IfObjectInSomeRoom (DOS CS:0x3a75): Object[a[0]].room != -1.
	// Without an Object table, default to "yes, somewhere" (kThxBye).
	debugC(2, kDebugLevelScript, "opcode 0x21: if object %s placed", +a[0]);
	return kThxBye;
}

OPCODE(0x22) {
	// IfStringEqualsBuf (DOS CS:0x3a88): compare arg0 against the user-input
	// buffer at DS:0x4faa. The engine doesn't expose a parser input buffer,
	// so the comparison can't succeed yet — take the false branch.
	debugC(2, kDebugLevelScript, "opcode 0x22: if input == %s (no parser buffer)", +a[0]);
	return kFail;
}

OPCODE(0x23) {
	// IfStringsEqual (DOS CS:0x3a9c): byte-compares arg0 (Pascal string with
	// length prefix) against arg1.
	const byte *s = static_cast<byte *>(a[0]);
	const byte *t = static_cast<byte *>(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0x23: if %s == %s", +a[0], +a[1]);
	if (!s || !t || s[0] != t[0])
		return kFail;
	for (uint8 i = 1; i <= s[0]; ++i)
		if (s[i] != t[i])
			return kFail;
	return kThxBye;
}

OPCODE(0x26) {
	// RunCheckActorIfStepCursor4 (DOS CS:0x382f): the binary calls into
	// CheckActorScripting on a hotspot click. The engine handles hotspot
	// dispatch via EventManager already; treat as ack.
	debugC(2, kDebugLevelScript, "opcode 0x26: hotspot ack (cursor=%u step=%d)", Log.cursorMode(), int(Log.stepPending()));
	return kThxBye;
}

OPCODE(0x27) {
	// RunOp3fIfStepCursor4 (DOS CS:0x381d): replays the speech-as-main flow.
	debugC(2, kDebugLevelScript, "opcode 0x27: replay speech-as-main (no-op without bubble state)");
	return kThxBye;
}

OPCODE(0x28) {
	// IfModeIs80 (DOS CS:0x384a): just checks verbMode == 0x80.
	debugC(2, kDebugLevelScript, "opcode 0x28: if verbMode == 0x80");
	if (Log.verbMode() != 0x80)
		return kFail;
	return kThxBye;
}

OPCODE(0x29) {
	// IfMode10AndFlag (DOS CS:0x3863): stepPending && verbMode == 0x10.
	debugC(2, kDebugLevelScript, "opcode 0x29: if stepPending && verbMode==0x10");
	if (!Log.stepPending() || Log.verbMode() != 0x10)
		return kFail;
	return kThxBye;
}

OPCODE(0x2a) {
	// IfMode10AndFlag2 (DOS CS:0x387e): same as 0x29 with extra arg consumption.
	debugC(2, kDebugLevelScript, "opcode 0x2a: if stepPending && verbMode==0x10 (3-arg)");
	if (!Log.stepPending() || Log.verbMode() != 0x10)
		return kFail;
	return kThxBye;
}

OPCODE(0x2b) {
	// BranchOnFrameMismatch (DOS CS:0x3a5c): if protagonist.frame != a[0],
	// stash a[1] as the case-branch target so a subsequent 0x2e re-applies it.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x2b: branch unless protagonist at %s -> %s", +a[0], +a[1]);
	if (ac && ac->frameId() != uint16(a[0]))
		Log.setSwitchTarget(static_cast<CodePointer &>(a[1]).offset());
	return kThxBye;
}

OPCODE(0x2e) {
	// RestoreBranchFromSave (DOS CS:0x3b16): replays the saved switchTarget.
	uint16 t = Log.switchTarget();
	if (t == 0)
		return kThxBye;
	debugC(2, kDebugLevelScript, "opcode 0x2e: restore branch -> 0x%04x", t);
	Log.setSwitchTarget(0);
	return CodePointer(t, this);
}

// Case-comparison family (DOS CS:0x3b1d..0x3bc8). Each consumes (arg0, arg1)
// and either matches or stashes the active branch. _switchValue starts a
// pseudo-switch when a 0x2b (or any caller convention) sets it; here we
// just compare the two args directly — that reproduces the semantics for
// every script witnessed so far. Match -> clear switchTarget (so the
// caller continues executing the case body); mismatch -> kFail (skip).
OPCODE(0x2f) {
	debugC(2, kDebugLevelScript, "opcode 0x2f: case if %s != %s", +a[0], +a[1]);
	if (uint16(a[0]) == uint16(a[1]))
		return kFail;
	Log.setSwitchTarget(0);
	return kThxBye;
}
OPCODE(0x30) {
	debugC(2, kDebugLevelScript, "opcode 0x30: case if %s == %s", +a[0], +a[1]);
	if (uint16(a[0]) != uint16(a[1]))
		return kFail;
	Log.setSwitchTarget(0);
	return kThxBye;
}
OPCODE(0x31) {
	debugC(2, kDebugLevelScript, "opcode 0x31: case if %s > %s", +a[0], +a[1]);
	if (!(uint16(a[0]) > uint16(a[1])))
		return kFail;
	Log.setSwitchTarget(0);
	return kThxBye;
}
OPCODE(0x32) {
	debugC(2, kDebugLevelScript, "opcode 0x32: case if %s < %s", +a[0], +a[1]);
	if (!(uint16(a[0]) < uint16(a[1])))
		return kFail;
	Log.setSwitchTarget(0);
	return kThxBye;
}
OPCODE(0x33) {
	debugC(2, kDebugLevelScript, "opcode 0x33: case if %s >= %s", +a[0], +a[1]);
	if (!(uint16(a[0]) >= uint16(a[1])))
		return kFail;
	Log.setSwitchTarget(0);
	return kThxBye;
}
OPCODE(0x34) {
	debugC(2, kDebugLevelScript, "opcode 0x34: case if %s <= %s", +a[0], +a[1]);
	if (!(uint16(a[0]) <= uint16(a[1])))
		return kFail;
	Log.setSwitchTarget(0);
	return kThxBye;
}

OPCODE(0x38) {
	// SwitchToScene (DOS CS:0x3c58): saves cast/actors, then loads a new room.
	// SaveCastBackup / SaveActorTableBackup are handled inside changeRoom.
	debugC(2, kDebugLevelScript, "opcode 0x38: switch to room %s", +a[0]);
	Log.changeRoom(uint16(a[0]));
	return kThxBye;
}

// Speech variants (DOS CS:0x3da2..0x3e68). The engine routes everything via
// Actor::say, which queues a speech bubble for the calling actor. Variants
// differ by speaker (main vs identified actor) and target (none vs hotspot).
OPCODE(0x3f) {
	// SpeakAsMainCharacter(text). Drops to map-mode subtitle if needed.
	debugC(1, kDebugLevelScript, "opcode 0x3f: main says %s", +a[0]);
	if (Log.protagonist())
		Log.protagonist()->say(a[0]);
	return kThxBye;
}
OPCODE(0x40) {
	// SpeakAtTarget(text, target). The original positions the bubble near
	// 'target' — engine ignores the target and just queues for protagonist.
	debugC(1, kDebugLevelScript, "opcode 0x40: main says %s @ %s", +a[0], +a[1]);
	if (Log.protagonist())
		Log.protagonist()->say(a[0]);
	return kThxBye;
}
OPCODE(0x42) {
	// SpeakAsMainAtTarget(target, text).
	debugC(1, kDebugLevelScript, "opcode 0x42: main says %s @ %s", +a[1], +a[0]);
	if (Log.protagonist())
		Log.protagonist()->say(a[1]);
	return kThxBye;
}
OPCODE(0x44) {
	// SpeakAsActorAtTarget(actorId, target, text).
	Actor *ac = Log.getActor(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x44: actor %s says %s @ %s", +a[0], +a[2], +a[1]);
	if (ac)
		ac->say(a[2]);
	return kThxBye;
}
OPCODE(0x45) {
	// SpeakWithDelay(arg0..arg3): same as 0x47 but invoked from a different
	// context. Args are { y, x, color, max-lines, text } per the binary.
	debugC(1, kDebugLevelScript, "opcode 0x45: speak-with-delay STUB y=%s x=%s color=%s lines=%s",
		+a[0], +a[1], +a[2], +a[3]);
	return kThxBye;
}
OPCODE(0x46) {
	// SpeakWithDelayAlt(arg0..arg3): variant of 0x45 — DOS code is identical
	// modulo a different g_unknown_669a write. Delegate the same way.
	debugC(1, kDebugLevelScript, "opcode 0x46: speak-with-delay-alt STUB y=%s x=%s color=%s lines=%s",
		+a[0], +a[1], +a[2], +a[3]);
	return kThxBye;
}

// 0x48..0x53: variable arithmetic that dispatches through the LHS slot via
// WriteVarBySlot_LHS in the binary. The engine maps that to direct
// assignment on the typed Value reference.
OPCODE(0x48) {
	debugC(2, kDebugLevelScript, "opcode 0x48: %s = %s & %s", +a[0], +a[0], +a[1]);
	a[0] = uint16(a[0]) & uint16(a[1]);
	return kThxBye;
}
OPCODE(0x49) {
	debugC(2, kDebugLevelScript, "opcode 0x49: %s = %s | %s", +a[0], +a[0], +a[1]);
	a[0] = uint16(a[0]) | uint16(a[1]);
	return kThxBye;
}
OPCODE(0x4d) {
	debugC(2, kDebugLevelScript, "opcode 0x4d: %s = %s ^ %s", +a[0], +a[0], +a[1]);
	a[0] = uint16(a[0]) ^ uint16(a[1]);
	return kThxBye;
}
OPCODE(0x4e) {
	debugC(2, kDebugLevelScript, "opcode 0x4e: %s <<= %s", +a[0], +a[1]);
	a[0] = uint16(a[0]) << (uint16(a[1]) & 0xf);
	return kThxBye;
}
OPCODE(0x4f) {
	debugC(2, kDebugLevelScript, "opcode 0x4f: %s >>= %s", +a[0], +a[1]);
	a[0] = uint16(a[0]) >> (uint16(a[1]) & 0xf);
	return kThxBye;
}
OPCODE(0x50) {
	debugC(2, kDebugLevelScript, "opcode 0x50: %s = abs(%s)", +a[0], +a[1]);
	int16 v = int16(uint16(a[1]));
	a[0] = (uint16)(v < 0 ? -v : v);
	return kThxBye;
}
OPCODE(0x51) {
	debugC(2, kDebugLevelScript, "opcode 0x51: %s = -%s", +a[0], +a[1]);
	a[0] = uint16(-int16(uint16(a[1])));
	return kThxBye;
}
OPCODE(0x52) {
	debugC(2, kDebugLevelScript, "opcode 0x52: %s = ~%s", +a[0], +a[1]);
	a[0] = uint16(~uint16(a[1]));
	return kThxBye;
}
OPCODE(0x53) {
	debugC(2, kDebugLevelScript, "opcode 0x53: %s = rand(%s)", +a[0], +a[1]);
	a[0] = uint16(_engine->getRandom(uint16(a[1])));
	return kThxBye;
}

// 0x58..0x5f: getter family (state queries). Each writes into the LHS slot
// the current value of a VM state variable.
OPCODE(0x58) {
	a[0] = Log.cursorMode();
	debugC(2, kDebugLevelScript, "opcode 0x58: %s = cursorMode (%u)", +a[0], Log.cursorMode());
	return kThxBye;
}
OPCODE(0x59) {
	a[0] = Log.hitTarget();
	debugC(2, kDebugLevelScript, "opcode 0x59: %s = hitTarget (%u)", +a[0], Log.hitTarget());
	return kThxBye;
}
OPCODE(0x5a) {
	a[0] = Log.dragTarget();
	debugC(2, kDebugLevelScript, "opcode 0x5a: %s = dragTarget (%u)", +a[0], Log.dragTarget());
	return kThxBye;
}
OPCODE(0x5b) {
	a[0] = Log.verbMode();
	debugC(2, kDebugLevelScript, "opcode 0x5b: %s = verbMode (%u)", +a[0], Log.verbMode());
	return kThxBye;
}
OPCODE(0x5c) {
	a[0] = Log.gameState();
	debugC(2, kDebugLevelScript, "opcode 0x5c: %s = gameState (%u)", +a[0], Log.gameState());
	return kThxBye;
}
OPCODE(0x5d) {
	a[0] = Log.currentRoom();
	debugC(2, kDebugLevelScript, "opcode 0x5d: %s = currentRoom (%u)", +a[0], Log.currentRoom());
	return kThxBye;
}
OPCODE(0x5e) {
	a[0] = Log.frameTicks();
	debugC(2, kDebugLevelScript, "opcode 0x5e: %s = frameTicks (%u)", +a[0], Log.frameTicks());
	return kThxBye;
}
OPCODE(0x5f) {
	// Get the protagonist's room (binary actually reads g_main_character_id but
	// the engine's protagonist is a singleton — equivalent state under our model).
	Actor *ac = Log.protagonist();
	a[0] = ac ? ac->room() : 0;
	debugC(2, kDebugLevelScript, "opcode 0x5f: %s = protagonistRoom", +a[0]);
	return kThxBye;
}

// 0x61..0x6b: arithmetic / logical follow-ups to 0x60.
OPCODE(0x61) {
	// Bitfield extract: value = (a[1] >> a[2]) & 1 — DOS reads cell[room].byte
	// of (a[2] >> 3). Without cell map, just propagate the bit-test value.
	debugC(2, kDebugLevelScript, "opcode 0x61: %s = (%s >> %s) & 1", +a[0], +a[1], +a[2]);
	a[0] = (uint16(a[1]) >> (uint16(a[2]) & 0xf)) & 1;
	return kThxBye;
}
OPCODE(0x62) {
	// Arithmetic with carry-in flag (rotate left by 1).
	debugC(2, kDebugLevelScript, "opcode 0x62: %s = rol(%s, 1)", +a[0], +a[1]);
	uint16 v = uint16(a[1]);
	a[0] = (uint16)((v << 1) | (v >> 15));
	return kThxBye;
}
OPCODE(0x64) {
	// 32-bit multiply truncated to 16: a[0] = a[1] * a[2].
	debugC(2, kDebugLevelScript, "opcode 0x64: %s = %s * %s", +a[0], +a[1], +a[2]);
	a[0] = uint16(uint16(a[1]) * uint16(a[2]));
	return kThxBye;
}
OPCODE(0x65) {
	// integer divide
	uint16 div = uint16(a[2]);
	debugC(2, kDebugLevelScript, "opcode 0x65: %s = %s / %s", +a[0], +a[1], +a[2]);
	a[0] = div ? uint16(uint16(a[1]) / div) : 0;
	return kThxBye;
}
OPCODE(0x66) {
	// modulo
	uint16 div = uint16(a[2]);
	debugC(2, kDebugLevelScript, "opcode 0x66: %s = %s %% %s", +a[0], +a[1], +a[2]);
	a[0] = div ? uint16(uint16(a[1]) % div) : 0;
	return kThxBye;
}
OPCODE(0x67) {
	debugC(2, kDebugLevelScript, "opcode 0x67: %s = min(%s,%s)", +a[0], +a[1], +a[2]);
	a[0] = MIN<uint16>(uint16(a[1]), uint16(a[2]));
	return kThxBye;
}
OPCODE(0x68) {
	debugC(2, kDebugLevelScript, "opcode 0x68: %s = max(%s,%s)", +a[0], +a[1], +a[2]);
	a[0] = MAX<uint16>(uint16(a[1]), uint16(a[2]));
	return kThxBye;
}
OPCODE(0x69) {
	debugC(2, kDebugLevelScript, "opcode 0x69: %s = clamp(%s, %s, %s)", +a[0], +a[1], +a[2], +a[3]);
	uint16 v = uint16(a[1]);
	uint16 lo = uint16(a[2]);
	uint16 hi = uint16(a[3]);
	a[0] = (v < lo) ? lo : (v > hi ? hi : v);
	return kThxBye;
}
OPCODE(0x6a) {
	debugC(2, kDebugLevelScript, "opcode 0x6a: %s = (%s >= %s)", +a[0], +a[1], +a[2]);
	a[0] = (uint16(a[1]) >= uint16(a[2])) ? 1 : 0;
	return kThxBye;
}
OPCODE(0x6b) {
	debugC(2, kDebugLevelScript, "opcode 0x6b: %s = (%s <= %s)", +a[0], +a[1], +a[2]);
	a[0] = (uint16(a[1]) <= uint16(a[2])) ? 1 : 0;
	return kThxBye;
}

OPCODE(0x75) {
	// Reset cursor to default. DOS handler at CS:0x4313 calls SetCursorMode(0).
	debugC(2, kDebugLevelScript, "opcode 0x75: reset cursor");
	Log.setCursorMode(0);
	return kThxBye;
}
OPCODE(0x76) {
	// SetCursorMode(arg0). DOS handler at CS:0x4325.
	debugC(2, kDebugLevelScript, "opcode 0x76: cursor mode = %s", +a[0]);
	Log.setCursorMode(uint16(a[0]));
	return kThxBye;
}
OPCODE(0x78) {
	// 0x78 (DOS CS:0x4359): chained ResetObjectAtActorPosition + Op_8e (unregister).
	// Without an object table, just clear the drag/hit slot the binary touches.
	debugC(2, kDebugLevelScript, "opcode 0x78: reset object at actor pos %s", +a[0]);
	Log.setHitTarget(0);
	return kThxBye;
}
OPCODE(0x7a) {
	// 0x7a (DOS CS:0x4443): mark object inactive (clears bit 1 of obj.flags).
	debugC(2, kDebugLevelScript, "opcode 0x7a: deactivate object %s", +a[0]);
	return kThxBye;
}
OPCODE(0x7d) {
	debugC(2, kDebugLevelScript, "opcode 0x7d: object op %s STUB (no object table)", +a[0]);
	return kThxBye;
}
OPCODE(0x7e) {
	debugC(2, kDebugLevelScript, "opcode 0x7e: object op %s STUB (no object table)", +a[0]);
	return kThxBye;
}
OPCODE(0x7f) {
	debugC(2, kDebugLevelScript, "opcode 0x7f: object op %s STUB (no object table)", +a[0]);
	return kThxBye;
}

// 0x80..0x94: Object placement / hotspot manipulation. The engine has no
// Object class — the actual placement logic must come with that addition.
// For now we accept the call (so scripts proceed) and log the parameters.
OPCODE(0x80) { debugC(2, kDebugLevelScript, "opcode 0x80: place object %s at room %s pos %sx%s", +a[0], +a[1], +a[2], +a[3]); return kThxBye; }
OPCODE(0x81) { debugC(2, kDebugLevelScript, "opcode 0x81: object %s set room %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x82) { debugC(2, kDebugLevelScript, "opcode 0x82: object %s set sprite %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x83) { debugC(2, kDebugLevelScript, "opcode 0x83: object %s clear flags %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x84) { debugC(2, kDebugLevelScript, "opcode 0x84: object %s set flags %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x85) { debugC(2, kDebugLevelScript, "opcode 0x85: place exit %s -> room %s pos %s,%s", +a[0], +a[1], +a[2], +a[3]); return kThxBye; }
OPCODE(0x86) { debugC(2, kDebugLevelScript, "opcode 0x86: exit %s set sprite %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x87) { debugC(2, kDebugLevelScript, "opcode 0x87: exit %s sub-action", +a[0]); return kThxBye; }
OPCODE(0x88) { debugC(2, kDebugLevelScript, "opcode 0x88: object %s pos = %s,%s", +a[0], +a[1], +a[2]); return kThxBye; }
OPCODE(0x89) { debugC(2, kDebugLevelScript, "opcode 0x89: object %s set z-index %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x8a) { debugC(2, kDebugLevelScript, "opcode 0x8a: object %s freeze frame %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x8b) { debugC(2, kDebugLevelScript, "opcode 0x8b: reset object %s + unregister actor", +a[0]); return kThxBye; }
OPCODE(0x8c) { debugC(2, kDebugLevelScript, "opcode 0x8c: object %s touch", +a[0]); return kThxBye; }
OPCODE(0x8d) { debugC(2, kDebugLevelScript, "opcode 0x8d: object %s play anim %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x8e) { debugC(2, kDebugLevelScript, "opcode 0x8e: unregister actor"); return kThxBye; }
OPCODE(0x8f) { debugC(2, kDebugLevelScript, "opcode 0x8f: object %s sub-action %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x90) { debugC(2, kDebugLevelScript, "opcode 0x90: object %s sub-action %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x91) { debugC(2, kDebugLevelScript, "opcode 0x91: object %s sub-action %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x92) { debugC(2, kDebugLevelScript, "opcode 0x92: object %s sub-action %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x93) { debugC(2, kDebugLevelScript, "opcode 0x93: object %s sub-action %s", +a[0], +a[1]); return kThxBye; }
OPCODE(0x94) { debugC(2, kDebugLevelScript, "opcode 0x94: object %s sub-action", +a[0]); return kThxBye; }

OPCODE(0x97) {
	// 0x97 (DOS CS:0x4a5d): set protagonist sprite override.
	debugC(2, kDebugLevelScript, "opcode 0x97: protagonist sprite override %s", +a[0]);
	return kThxBye;
}
OPCODE(0x98) {
	// 0x98 (DOS CS:0x4b40): set protagonist anim slot.
	debugC(2, kDebugLevelScript, "opcode 0x98: protagonist anim slot %s,%s", +a[0], +a[1]);
	return kThxBye;
}

// 0x9f..0xa7: actor placement / "go to frame" + "wait for animation" family.
// DOS handlers all share a common shape: validate actor id, write _nextFrame
// (offset 0x61) and _walkSpeed (offset 0x6b = 0), call SetActorPosition then
// InitActorState. The engine maps SetActorPosition+InitActorState to
// Actor::setRoom / setFrame which together queue the new pose. The "wait"
// variants (0xa4..0xa7) call CheckActorAnimReady → callMeWhenStill.

OPCODE(0x9f) {
	// 0x9f (DOS CS:0x4c95): actor walk-to-frame. arg0=actor id, arg1=target
	// frame. Sets actor's nextFrame (which Actor::tick will animate towards).
	debugC(2, kDebugLevelScript, "opcode 0x9f: actor %s walk to frame %s", +a[0], +a[1]);
	if (Actor *ac = Log.getActor(a[0]))
		ac->setFrame(uint16(a[1]));
	return kThxBye;
}
OPCODE(0xa0) {
	// 0xa0 (DOS CS:0x4c8e): actor walk-to-frame with walkSpeedFlag=1 (faster).
	// arg0=actor, arg1=target frame.
	debugC(2, kDebugLevelScript, "opcode 0xa0: actor %s walk-fast to frame %s", +a[0], +a[1]);
	if (Actor *ac = Log.getActor(a[0]))
		ac->setFrame(uint16(a[1]));
	return kThxBye;
}
OPCODE(0xa1) {
	// 0xa1 (DOS CS:0x4c59): actor.setRoom(room, frame). Repositions the actor
	// to a new room with a specific facing/frame.
	debugC(2, kDebugLevelScript, "opcode 0xa1: actor %s set room %s frame %s", +a[1], +a[0], +a[2]);
	if (Actor *ac = Log.getActor(a[1]))
		ac->setRoom(uint16(a[0]), uint16(a[2]));
	return kThxBye;
}
OPCODE(0xa2) {
	// 0xa2 (DOS CS:0x4cb0): actor jump-to-frame (walkSpeedFlag=0 = instant).
	// 3-arg: actor id, frame, secondary frame.
	debugC(2, kDebugLevelScript, "opcode 0xa2: actor %s jump to frame %s,%s", +a[1], +a[0], +a[2]);
	if (Actor *ac = Log.getActor(a[1]))
		ac->setFrame(uint16(a[0]));
	return kThxBye;
}
OPCODE(0xa3) {
	// 0xa3 (DOS CS:0x4ca9): same as 0xa2 but with walkSpeedFlag=1 (animated).
	debugC(2, kDebugLevelScript, "opcode 0xa3: actor %s walk-anim to frame %s,%s", +a[1], +a[0], +a[2]);
	if (Actor *ac = Log.getActor(a[1]))
		ac->setFrame(uint16(a[0]));
	return kThxBye;
}
OPCODE(0xa4) {
	// 0xa4 (DOS CS:0x4d47): if not in map mode, wait for protagonist animation
	// to finish (CheckActorAnimReady on g_main_character_id). The script blocks
	// here until the actor stops moving.
	debugC(2, kDebugLevelScript, "opcode 0xa4: wait protagonist anim ready");
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *ac = Log.protagonist()) {
		if (ac->isMoving()) {
			ac->callMeWhenStill(next);
			return kReturn;
		}
	}
	return kThxBye;
}
OPCODE(0xa5) {
	// 0xa5 (DOS CS:0x4d5c): same as 0xa4 with one extra arg consumed.
	debugC(2, kDebugLevelScript, "opcode 0xa5: wait protagonist anim ready (2-arg)");
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *ac = Log.protagonist()) {
		if (ac->isMoving()) {
			ac->callMeWhenStill(next);
			return kReturn;
		}
	}
	return kThxBye;
}
OPCODE(0xa6) {
	// 0xa6 (DOS CS:0x4cfb): wait for actor `arg0`'s animation to finish.
	// arg1 is consumed but unused by the binary.
	debugC(2, kDebugLevelScript, "opcode 0xa6: wait actor %s anim ready", +a[0]);
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *ac = Log.getActor(a[0])) {
		if (ac->isMoving()) {
			ac->callMeWhenStill(next);
			return kReturn;
		}
	}
	return kThxBye;
}
OPCODE(0xa7) {
	// 0xa7 (DOS CS:0x4d0f): wait actor `arg0` anim ready, 3-arg variant
	// (extra args consumed but unused).
	debugC(2, kDebugLevelScript, "opcode 0xa7: wait actor %s anim ready (3-arg)", +a[0]);
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *ac = Log.getActor(a[0])) {
		if (ac->isMoving()) {
			ac->callMeWhenStill(next);
			return kReturn;
		}
	}
	return kThxBye;
}
OPCODE(0xa8) {
	debugC(2, kDebugLevelScript, "opcode 0xa8: protagonist sub-action %s", +a[0]);
	return kThxBye;
}
OPCODE(0xa9) {
	debugC(2, kDebugLevelScript, "opcode 0xa9: protagonist sub-action");
	return kThxBye;
}
OPCODE(0xaa) {
	debugC(2, kDebugLevelScript, "opcode 0xaa: actor %s sub-action", +a[0]);
	return kThxBye;
}
OPCODE(0xac) {
	debugC(2, kDebugLevelScript, "opcode 0xac: actor %s sub-action %s", +a[0], +a[1]);
	return kThxBye;
}

// 0xae..0xb8: walk variants. Engine doesn't model pathfinding yet, but the
// destination assignment can still update Actor target so subsequent tests
// (Op_1d / Op_99) see the actor in the new room.
OPCODE(0xae) {
	debugC(2, kDebugLevelScript, "opcode 0xae: actor %s walk to actor %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xaf) {
	debugC(2, kDebugLevelScript, "opcode 0xaf: actor %s walk to exit %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xb0) {
	debugC(2, kDebugLevelScript, "opcode 0xb0: actor %s walk to object %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xb1) {
	debugC(2, kDebugLevelScript, "opcode 0xb1: actor %s walk to %sx%s", +a[0], +a[1], +a[2]);
	return kThxBye;
}
OPCODE(0xb2) {
	debugC(2, kDebugLevelScript, "opcode 0xb2: actor %s walk variant %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xb3) {
	debugC(2, kDebugLevelScript, "opcode 0xb3: actor %s walk to room exit %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xb4) {
	// 0xb4 (DOS CS:0x4f97): if not in map mode, wait until actor `arg0` is
	// idle. Binary calls CheckActorIdle which returns true once the actor's
	// movement queue is empty AND it's not speaking. The engine maps "idle"
	// to "not moving and not speaking" via the existing callbacks.
	debugC(2, kDebugLevelScript, "opcode 0xb4: wait actor %s idle", +a[0]);
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = Log.getActor(a[0]);
	if (!ac)
		return kThxBye;
	if (ac->isMoving()) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	if (ac->isSpeaking()) {
		ac->callMeWhenSilent(next);
		return kReturn;
	}
	return kThxBye;
}
OPCODE(0xb5) {
	debugC(2, kDebugLevelScript, "opcode 0xb5: protagonist walk to exit %s", +a[0]);
	return kThxBye;
}
OPCODE(0xb6) {
	debugC(2, kDebugLevelScript, "opcode 0xb6: protagonist walk to object %s", +a[0]);
	return kThxBye;
}
OPCODE(0xb7) {
	debugC(2, kDebugLevelScript, "opcode 0xb7: protagonist walk to %sx%s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xb8) {
	debugC(2, kDebugLevelScript, "opcode 0xb8: protagonist walk variant");
	return kThxBye;
}
OPCODE(0xba) {
	debugC(2, kDebugLevelScript, "opcode 0xba: actor walk continue");
	return kThxBye;
}
OPCODE(0xbb) {
	debugC(2, kDebugLevelScript, "opcode 0xbb: protagonist walk continue");
	return kThxBye;
}

OPCODE(0xbf) {
	// 0xbf (DOS CS:0x50a1): actor face-direction setter (writes to actor[0x61]).
	debugC(2, kDebugLevelScript, "opcode 0xbf: actor %s face %s", +a[0], +a[1]);
	return kThxBye;
}

// 0xc0..0xc5: cast/actor pos.
OPCODE(0xc0) {
	// SetActorPosition (DOS CS:0x509a). Engine doesn't have x/y on Actor yet.
	debugC(2, kDebugLevelScript, "opcode 0xc0: actor %s pos %s,%s", +a[0], +a[1], +a[2]);
	return kThxBye;
}
OPCODE(0xc1) {
	// UnregisterActor when not in map mode (DOS CS:0x5131). Pulls the actor
	// out of the active animation list — engine equivalent: remove from Logic.
	debugC(2, kDebugLevelScript, "opcode 0xc1: unregister actor %s", +a[0]);
	if (!Log.inMapMode()) {
		if (Actor *ac = Log.getActor(a[0]))
			Log.removeAnimation(ac);
	}
	return kThxBye;
}
OPCODE(0xc3) {
	debugC(2, kDebugLevelScript, "opcode 0xc3: cast op %s", +a[0]);
	return kThxBye;
}
OPCODE(0xc4) {
	debugC(2, kDebugLevelScript, "opcode 0xc4: cast op %s,%s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xc5) {
	debugC(2, kDebugLevelScript, "opcode 0xc5: cast op %s,%s", +a[0], +a[1]);
	return kThxBye;
}

OPCODE(0xca) {
	debugC(2, kDebugLevelScript, "opcode 0xca: misc state STUB");
	return kThxBye;
}
OPCODE(0xcd) {
	debugC(2, kDebugLevelScript, "opcode 0xcd: misc state STUB");
	return kThxBye;
}

// 0xd3..0xd9: palette / screen helpers paired with the already-implemented
// 0xcf/0xd0 fade ops. Engine has no separate alt-palette buffer, so we just
// nudge Graphics to repaint where appropriate.
OPCODE(0xd3) {
	debugC(2, kDebugLevelScript, "opcode 0xd3: backdrop refresh");
	return kThxBye;
}
OPCODE(0xd4) {
	debugC(2, kDebugLevelScript, "opcode 0xd4: palette swap STUB");
	return kThxBye;
}
OPCODE(0xd5) {
	debugC(2, kDebugLevelScript, "opcode 0xd5: palette swap STUB");
	return kThxBye;
}
OPCODE(0xd7) {
	// 0xd7 (DOS CS:0x5408): clear g_in_fade. Already implicit in our renderer.
	debugC(2, kDebugLevelScript, "opcode 0xd7: clear fade flag");
	return kThxBye;
}
OPCODE(0xd9) {
	// 0xd9 (DOS CS:0x5430): add zone entry. Engine doesn't model zones.
	debugC(2, kDebugLevelScript, "opcode 0xd9: add zone entry STUB");
	return kThxBye;
}

OPCODE(0xdd) {
	// 0xdd (DOS CS:0x54bf): add collision-zone-A entry.
	debugC(2, kDebugLevelScript, "opcode 0xdd: add collision zone STUB");
	return kThxBye;
}

// 0xe0..0xec: misc state setters.
OPCODE(0xe0) { debugC(2, kDebugLevelScript, "opcode 0xe0: misc STUB"); return kThxBye; }
OPCODE(0xe1) { debugC(2, kDebugLevelScript, "opcode 0xe1: misc STUB"); return kThxBye; }
OPCODE(0xe3) { debugC(2, kDebugLevelScript, "opcode 0xe3: misc STUB"); return kThxBye; }
OPCODE(0xe4) { debugC(2, kDebugLevelScript, "opcode 0xe4: misc STUB"); return kThxBye; }
OPCODE(0xe7) {
	// 0xe7 (DOS CS:0x5612): set g_game_state.
	debugC(2, kDebugLevelScript, "opcode 0xe7: gameState = %s", +a[0]);
	Log.setGameState(uint16(a[0]));
	return kThxBye;
}
OPCODE(0xe8) {
	// 0xe8 (DOS CS:0x561d): clear pending step.
	debugC(2, kDebugLevelScript, "opcode 0xe8: stepPending = false");
	Log.setStepPending(false);
	return kThxBye;
}
OPCODE(0xe9) {
	// 0xe9 (DOS CS:0x5634): set verbMode.
	debugC(2, kDebugLevelScript, "opcode 0xe9: verbMode = %s", +a[0]);
	Log.setVerbMode(uint16(a[0]));
	return kThxBye;
}
OPCODE(0xea) {
	// 0xea (DOS CS:0x5642): set inMapMode flag.
	debugC(2, kDebugLevelScript, "opcode 0xea: inMapMode = %s", +a[0]);
	Log.setInMapMode(uint16(a[0]) != 0);
	return kThxBye;
}
OPCODE(0xeb) {
	// 0xeb (DOS CS:0x5665): toggle inMapMode (single byte handler in DOS).
	debugC(2, kDebugLevelScript, "opcode 0xeb: toggle inMapMode");
	Log.setInMapMode(!Log.inMapMode());
	return kThxBye;
}
OPCODE(0xec) {
	// 0xec (DOS CS:0x5670): clear inMapMode.
	debugC(2, kDebugLevelScript, "opcode 0xec: clear inMapMode");
	Log.setInMapMode(false);
	return kThxBye;
}

OPCODE(0xee) {
	// 0xee (DOS CS:0x5698): clear hitTarget. Used at end of action dispatch.
	debugC(2, kDebugLevelScript, "opcode 0xee: clear hitTarget");
	Log.setHitTarget(0);
	return kThxBye;
}

// 0xf1..0xf5: music/sfx beyond the core 0xf4 (play music) / 0xf7 (stop) /
// 0xf8 (panic stop) handled above.
OPCODE(0xf1) {
	// load sfx set (DOS CS:0x5725 → Op_load_sfx)
	debugC(1, kDebugLevelScript, "opcode 0xf1: load sfx %s STUB", +a[0]);
	return kThxBye;
}
OPCODE(0xf2) {
	// 0xf2 (DOS CS:0x575a): play sfx by index. Engine has no separate sfx
	// channel; route through Music.
	debugC(1, kDebugLevelScript, "opcode 0xf2: play sfx %s STUB", +a[0]);
	return kThxBye;
}
OPCODE(0xf3) {
	// 0xf3 (DOS CS:0x5769) → QueueAndStartTune: queue a tune for the next
	// scene transition. Pull the tune script via the main interpreter the
	// same way 0xf4 does.
	debugC(1, kDebugLevelScript, "opcode 0xf3: queue+start tune %s", +a[0]);
	const byte *script = Log.mainInterpreter()->rawCode(static_cast<CodePointer &>(a[0]).offset());
	Music.loadMusic(script);
	return kThxBye;
}
OPCODE(0xf5) {
	// 0xf5 (DOS CS:0x5812): set music beat directly (skips current beat).
	debugC(2, kDebugLevelScript, "opcode 0xf5: set music beat %s", +a[0]);
	Music.setBeat(uint16(a[0]));
	return kThxBye;
}

OPCODE(0xfa) {
	// Save game (DOS CS:0x58ed). Original opens the save dialog (modal); the
	// engine should defer to ScummVM's save system. We log and leave the
	// actual save to the user's manual menu trigger for now.
	debugC(1, kDebugLevelScript, "opcode 0xfa: save game requested (ScummVM hotkey to save)");
	return kThxBye;
}
OPCODE(0xfb) {
	// Load game (DOS CS:0x593c).
	debugC(1, kDebugLevelScript, "opcode 0xfb: load game requested (ScummVM hotkey to load)");
	return kThxBye;
}

OPCODE(0xfd) {
	// 0xfd (DOS CS:0x4087): tail-calls ResolveOpcodeArg0 — read-and-discard.
	debugC(2, kDebugLevelScript, "opcode 0xfd: read-discard %s", +a[0]);
	return kThxBye;
}

// #define ANIMCODE(n) template<> void Animation::handle<n>()
//
// ANIMCODE(2) {
// 	// set position
// 	uint16 left = READ_LE_UINT16(_code + 2);
// 	uint16 top = READ_LE_UINT16(_code + 4);
// 	setPosition(Common::Point(left, top));
// 	_code += 6;
// }
// 
// ANIMCODE(7) {
// 	// get sprite id from main variable
// 	const uint16 offset = READ_LE_UINT16(_code + 2);
// 	const byte *var = _resources->getGlobalWordVariable(offset/2);
// 	const uint16 sprite = READ_LE_UINT16(var);
// 	setSprite(sprite);
// }
// 
// ANIMCODE(26) {
// 	// set z index
// 	setZIndex(_code[1]);
// 	// TODO set field 16 to 0
// 	_code += 2;
// }

} // End of namespace Interspective
