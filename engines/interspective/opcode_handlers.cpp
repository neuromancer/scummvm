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
#include "interspective/sound.h"
#include "interspective/logic.h"
#include "interspective/movie.h"
#include "interspective/musicparser.h"
#include "interspective/room.h"
#include "interspective/util.h"

#include "common/events.h"
#include "common/util.h"

#include "audio/mididrv.h"

namespace Interspective {

// Walk-driver helper: send protagonist toward target entity. Now a
// thin wrapper over Logic::sendActorToTarget which generalizes to any
// actor (used by Op_ae and the actor-walk family). Kept as a helper
// for opcode-side readability.
static inline void sendProtagToTarget(Logic *logic, uint16 targetId) {
	logic->sendActorToTarget(/* walker = */ nullptr, targetId);
}

// Speech subsystem helper: route text to the appropriate sink.
// In map mode, DOS displays subtitles (no actor bubble). Otherwise
// queue a per-actor bubble via Actor::say. C++'s per-actor _speech
// slot is the equivalent of DOS's 6-slot g_speech_slots pool —
// modern hosts have no RAM constraint, so the 6-slot cap is a
// non-issue.
//
// If `target` is non-null, the bubble is anchored at the target's
// position instead of the speaker's sprite — mirrors DOS
// SpeakAtTarget (Op_40/0x42/0x44) where the bubble appears near
// the target entity rather than the speaker.
static void speakOrSubtitle(Actor *speaker, const Common::String &text, Actor *target = 0) {
	if (Log.inMapMode()) {
		// DOS map-mode: CheckSubtitleActive + RegisterSampleSlot_LoadDefaultsB
		// or QueueDeferredFormattedText. Closest C++ analog: render text
		// via Graf.say at top-of-screen, ~3 ticks/char reading speed.
		const uint16 length = uint16(text.size());
		if (length > 0)
			Graf.say(reinterpret_cast<const byte *>(text.c_str()),
				length, MAX<uint16>(30, 3 * length));
		return;
	}
	if (!speaker)
		return;
	if (target) {
		Common::Point anchor = target->getSpeechPosition();
		speaker->sayAtPos(text, anchor);
	} else {
		speaker->say(text);
	}
}

#define OPCODE(num) template<> Interpreter::OpResult Interpreter::opcodeHandler<num>(ValueVector a, CodePointer current, CodePointer next)

OPCODE(0x00) {
	// nop
	debugC(2, kDebugLevelScript, "opcode 0x00: nop");
	return kThxBye;
}

OPCODE(0x01) {
	// DOS Op_01 @ 1000:59a3. Two paths:
	//   if (_g_block_pc_offset != 0)  // Op_38 has pushed a saved PC
	//       restore saved PC, LoadCodeBlock, RestoreCastBackup,
	//       RestoreActorTableBackup, return  (no break_loop — caller
	//       resumes from its saved PC)
	//   else
	//       g_break_loop = 1; return    (plain script exit)
	//
	// In C++ the snapshot is held in Logic::_savedScene (single slot,
	// matching DOS sentinel `_g_block_pc_offset == 0`). When restored,
	// the caller's _blockProgram/_blockInterpreter/Room are reinstated
	// and the returned CodePointer transfers the dispatcher directly to
	// the saved caller PC. No saved scene means the plain-exit path.
	debugC(2, kDebugLevelScript, "opcode 0x01: exit");
	CodePointer resume = Log.restoreSceneFrame();
	if (!resume.isEmpty())
		return resume;
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
	// DOS Op_12_IfSoundDeviceMask @ 1000:392b:
	//   if (((g_music_enabled | g_sfx_enabled) & arg0) == 0) skip;
	// `g_music_enabled` (1000:000d) and `g_sfx_enabled` (1000:000e) are
	// bitmasks set at startup by ParseConfigAndCmdLine + ParseSwitchString
	// based on detected sound hardware: bit 0 = Adlib, bit 1 = SB,
	// bit 2 = Roland MT-32.
	//
	// In C++ the equivalent startup state is derived from ScummVM's
	// selected MIDI device and the always-available digital mixer:
	//   - music device class derived from `MidiDriver::detectDevice` →
	//     MT_ADLIB → bit 0, MT_MT32/MT_GM → bit 2, others → bit 0 fallback.
	//   - sfx is unconditionally SB-class (bit 1) — ScummVM always offers
	//     digital sample mixing.
	// Mixer volume is deliberately not part of this predicate: DOS reads
	// configuration/device-enable bytes, not the current playback volume.
	uint16 musicMask = kSoundAdlib;
	MidiDriver::DeviceHandle dev =
		MidiDriver::detectDevice(MDT_MIDI | MDT_ADLIB | MDT_PREFER_GM);
	MusicType mt = MidiDriver::getMusicType(dev);
	if (mt == MT_MT32 || mt == MT_GM)
		musicMask = kSoundRoland;
	uint16 sfxMask = kSoundSB;
	const uint16 combined = uint16(musicMask | sfxMask);
	const uint16 want = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x12: if sound type %u in mask 0x%02x", want, combined);
	if ((combined & want) == 0)
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
	debugC(2, kDebugLevelScript, "opcode 0xda: clear zone list");
	Log.zonesClear();
	return kThxBye;
}

OPCODE(0xdc) {
	// Clear g_collision_zone_count (zone-A count, used by FindZoneAtPoint).
	// DOS handler at CS:0x54b8.
	debugC(2, kDebugLevelScript, "opcode 0xdc: clear collision zones");
	Log.collisionZonesClear();
	return kThxBye;
}

OPCODE(0xde) {
	// Clear g_zone_b_count (zone-B count).
	// DOS handler at CS:0x54fd.
	debugC(2, kDebugLevelScript, "opcode 0xde: clear zone-B");
	Log.zonesBClear();
	return kThxBye;
}

OPCODE(0xe2) {
	// Clear g_walkbox_count (walkbox list at DS:0x6617).
	// DOS handler at CS:0x5582.
	debugC(2, kDebugLevelScript, "opcode 0xe2: clear walkbox count");
	Log.walkboxesClear();
	return kThxBye;
}

OPCODE(0xf6) {
	// Set music volume to maximum. DOS handler at CS:0x5824 patches the music driver
	// state bytes directly to 0xff (volume) and 0x3f / 0 (mode-dependent flag).
	// In ScummVM the audio mixer handles volume, so this is a no-op.
	debugC(2, kDebugLevelScript, "opcode 0xf6: max music volume (no-op under ScummVM mixer)");
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
	// DOS Op_10_IfTimerExpired @ 1000:3903.
	//   ResolveOpcodeArg0 → AX
	//   if (AX == 0) skip
	//   if (AX > tick (signed JG)) skip
	//   else { StoreOpcodeArg0Value(0); run; }
	// SIGNED comparison. Pairs with Op_ed which writes the deadline.
	// C++ writes 0 back via `a[0] = 0` — works when arg0 is a
	// WordVariable/ByteVariable (reaches _ptr); no-op for Constant.
	int16 deadline = int16(uint16(a[0]));
	int16 now = int16(uint16(Log.frameTicks()));
	if (deadline != 0 && deadline <= now) {
		debugC(2, kDebugLevelScript, "opcode 0x10: timer fired (deadline=%d tick=%d)", deadline, now);
		a[0] = 0;
		return kThxBye;
	}
	debugC(3, kDebugLevelScript, "opcode 0x10: timer pending (deadline=%d tick=%d)", deadline, now);
	return kFail;
}

OPCODE(0x11) {
	// "if slow CPU" — body executes only when the startup calibration
	// set g_slow_cpu. DOS handler at CS:0x391d skips if DS:0x67b5 == 0.
	debugC(2, kDebugLevelScript, "opcode 0x11: if slow CPU");
	if (!Log.slowCpu())
		return kFail;
	return kThxBye;
}

OPCODE(0x17) {
	// DOS Op_17_IfExitMissing @ 1000:3996. Reads `exit_record[0]`
	// (the room field — kOffsetRoom = 0 in C++ Exit) at SI =
	// GetExitOffset(arg0); skips if it equals 0. Run if room != 0.
	// C++ Program::getExit accepts the same 1-based id and returns null
	// instead of walking past the loaded table; valid exits test room==0.
	debugC(1, kDebugLevelScript, "opcode 0x17: if exit %s exists", +a[0]);
	if (uint16(a[0]) == 0) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	Exit *exit = _logic->blockProgram()->getExit(a[0]);
	if (exit == nullptr || exit->room() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x19) {
	// DOS Op_19_IfActorMissing @ 1000:39bc: skip when actor.field+0x59
	// (room) == 0 → body runs when actor IS placed somewhere.
	// Sets implicit actor (SI side-effect of GetActorOffset).
	debugC(1, kDebugLevelScript, "opcode 0x19: if actor %s in some room", +a[0]);
	Actor *ac = Log.getActor(a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1a) {
	// DOS Op_1a_IfExitPresent @ 1000:39d0. Inverse of 0x17: skips
	// when `exit_record[0] != 0` (exit room is set → exit "present").
	// Body runs when exit room == 0 (or slot null).
	debugC(1, kDebugLevelScript, "opcode 0x1a: if exit %s missing", +a[0]);
	if (uint16(a[0]) == 0) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	Exit *exit = _logic->blockProgram()->getExit(a[0]);
	if (exit != nullptr && exit->room() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1c) {
	// DOS Op_1c_IfActorPresent @ 1000:39f6: skip when actor.field+0x59
	// (room) != 0 → body runs when actor is MISSING. Inverse of 0x19.
	// Sets implicit actor (SI side-effect of GetActorOffset).
	debugC(1, kDebugLevelScript, "opcode 0x1c: if actor %s not placed", +a[0]);
	Actor *ac = Log.getActor(a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1d) {
	// DOS Op_1d_IfActorAtRoomFrame @ 1000:3a10: arg1 = actor id,
	// arg0 = frame. Run if actor.room == current_loc AND
	// actor.frame == arg0. Sets implicit actor.
	debugC(1, kDebugLevelScript, "opcode 0x1d: if actor %s in current room AND at %s", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) != uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x1f) {
	// DOS Op_1f_IfActorNotAtRoomFrame @ 1000:3a39: arg1 = actor id,
	// arg0 = frame. Run if actor.room == current_loc AND
	// actor.frame != arg0. Sets implicit actor.
	debugC(1, kDebugLevelScript, "opcode 0x1f: if actor %s is in current room but not at %s then", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) == uint8(uint16(a[0])))
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
	// DOS Op_36_Call @ 1000:3bf8:
	//   if (call_depth < 8) {
	//       stack[depth*4]   = g_codeptr_di_save;
	//       stack[depth*4+2] = g_branch_state;
	//       depth++;
	//       g_branch_state = 0;
	//       g_codeptr_di_save = arg0;
	//   } else g_pendingErrorCode = 5;
	// C++ uses native recursion for PC save/restore; we enforce the
	// same depth limit (and the branch_state save/restore around the
	// inner script).
	if (Log.callDepth() >= 8) {
		Log.setPendingError(0x05);
		return kThxBye;
	}
	const uint16 savedBranch = Log.branchState();
	Log.setBranchState(0);
	Log.setCallDepth(uint8(Log.callDepth() + 1));
	debugC(2, kDebugLevelScript, ">>>opcode 0x36: call procedure %s (depth=%u)", +a[0], Log.callDepth());
	CodePointer &p = static_cast<CodePointer &>(a[0]);
	p.run();
	debugC(2, kDebugLevelScript, "<<<opcode 0x36: returned (depth=%u)", Log.callDepth() - 1);
	Log.setCallDepth(uint8(Log.callDepth() - 1));
	Log.setBranchState(savedBranch);
	return kThxBye;
}

OPCODE(0x37) {
	// DOS Op_37_PopCaseStack @ 1000:3c2e:
	//   if (call_depth != 0) { depth--; PC = saved; branch_state = saved; }
	//   else g_pendingErrorCode = 6;
	// C++ side: depth tracking is symmetric with Op_36 — call_depth was
	// incremented at Op_36 entry and the inner Interpreter::run's
	// kReturn pops out (matching DOS's PC restore). Op_37 itself just
	// signals "end of procedure" via kReturn; the outer Op_36 then
	// decrements the counter and restores branch_state.
	// Underflow (Op_37 with no active call) → pending-error 6.
	if (Log.callDepth() == 0) {
		Log.setPendingError(0x06);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0x37: return");
	return kReturn;
}

OPCODE(0x39) {
	// DOS Op_39_DeferRunMain @ 1000:3c88:
	//   Op_3a_CancelDeferredMain();   // remove any matching slot
	//   for slot in queue[0..7]:
	//       if slot.mode == 0:
	//           slot.mode = 0xb + idx;
	//           slot.code_offset = g_resourceSegment;
	//           slot.code_segment = arg0;
	//           return;
	//   g_pendingErrorCode = 0x1e;    // queue overflow
	// C++ models the queue via `_queued` (Common::List<DelayedRun>,
	// unbounded). The DOS limit of 8 slots is enforced here so we
	// can raise pending-error 0x1e on overflow (rule 2). The mode
	// field (0xb+idx) is a per-slot DOS dispatcher tag — not used
	// by the C++ runner, which calls the queued CodePointer directly.
	if (_logic->queuedCount() >= 8) {
		Log.setPendingError(0x1e);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x39: execute main %s later", +a[0]);
	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), _logic->mainInterpreter());
	_logic->cancelLater(p);
	_logic->runLater(p);
	return kThxBye;
}

OPCODE(0x3b) {
	// DOS Op_3b_DeferRunBlock @ 1000:3c7f: same shape as Op_39 but
	// for block-interpreter scripts. Slot-cap 8; pending-error 0x1e on
	// overflow.
	if (_logic->queuedCount() >= 8) {
		Log.setPendingError(0x1e);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x3b: execute %s later", +a[0]);
	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), _logic->blockInterpreter());
	_logic->cancelLater(p);
	_logic->runLater(p);
	return kThxBye;
}

OPCODE(0x3d) {
	// DOS Op_3d_SetEscapeBreakPoint @ 1000:3d0b:
	//   g_break_target_proc = g_opcode_mode;     ; current dispatch mode
	//   _g_break_target_di  = g_codeptr_es_save;  ; current PC
	//   _g_break_target_es  = arg0;               ; jump-to target
	//   g_esc_during_script = 1;                  ; flag (= !skipPoint.isEmpty in C++)
	// HandleEscDuringScript reads all three to dispatch ESC: mode<0xb
	// runs target inline; mode>=0xb modifies the deferred queue entry
	// of that mode to start at the target on next tick.
	const uint16 srcPC = current.offset();
	debugC(2, kDebugLevelScript, "opcode 0x3d: ESC break (mode=%u srcPC=0x%04x → %s)",
		Log.opcodeMode(), srcPC, +a[0]);
	Log.setEscBreakPoint(Log.opcodeMode(), srcPC, static_cast<CodePointer &>(a[0]));
	return kThxBye;
}

OPCODE(0x41) {
	// DOS Op_41_SpeakAsMainNoTarget @ 1000:3dae. Protagonist speaks;
	// if already speaking or moving, defer the dispatch via the
	// actor's silent/still callback.
	debugC(2, kDebugLevelScript, "opcode 0x41: protag says %s", +a[0]);
	Actor *protag = Log.protagonist();
	if (!protag) return kThxBye;

	if (protag->isSpeaking()) {
		protag->callMeWhenSilent(current);
		return kReturn;
	}
	if (protag->isMoving()) {
		protag->callMeWhenStill(current);
		return kReturn;
	}
	speakOrSubtitle(protag, a[0]);
	return kThxBye;
}

OPCODE(0x43) {
	// DOS Op_43_SpeakAsActor @ 1000:3e10: arg0=actor id, arg1=text.
	debugC(2, kDebugLevelScript, "opcode 0x43: %s says %s", +a[0], +a[1]);

	Actor *ac = Log.getActor(a[0]);
	if (!ac) return kThxBye;
	if (ac->isSpeaking()) {
		ac->callMeWhenSilent(current);
		return kReturn;
	}
	if (ac->isMoving()) {
		ac->callMeWhenStill(current);
		return kReturn;
	}
	speakOrSubtitle(ac, a[1]);
	return kThxBye;
}

OPCODE(0x47) {
	// DOS Op_47_SpeakWithRect @ 1000:3eb6: 5 args (y, x, color, lines, text).
	//   if (g_in_map_mode == 0) AllocSpeechSlot_NoFormatting +
	//       stash arg2 in g_unknown_669a;
	//   else CheckSubtitleActive → RegisterSampleSlot_LoadDefaultsB or
	//       QueueDeferredFormattedText.
	// AllocSpeechSlot_NoFormatting allocates a NARRATOR bubble slot
	// (no actor — bubble at the explicit (x,y) position with the
	// given color). C++ uses Graphics::sayAt to render at (x, y)
	// with the color arg.
	const uint16 y = uint16(a[0]);
	const uint16 x = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	const byte *text = static_cast<byte *>(a[4]);
	debugC(1, kDebugLevelScript, "opcode 0x47: narrator at (%u,%u) color=%u text='%s'",
		x, y, color, text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
		if (length > 0)
			Graf.sayAt(text, length, MAX<uint16>(30, 3 * length), x, y, color);
	}
	return kThxBye;
}

OPCODE(0x4a) {
	// DOS Op_4a_RegisterSampleByMapMode @ 1000:3ed5: dispatches to
	// `RegisterSampleSlot_Bare2` (map mode) or `_Bare9` (non-map),
	// both of which call `RegisterSampleSlot_Common` @ 1000:3154:
	//   if (branch_state == 0 && call_depth == 0) {
	//       room_script_slots[opcode_mode] = (PC, regs);
	//       g_break_outer = 1;  // exits loop, resumes when sample done
	//   } else g_pendingErrorCode = 0x39;
	// C++ approximation: wait until the protagonist's speech bubble
	// (the engine's stand-in for the sample) finishes, then continue.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x4a: wait protag silent (map=%d)", Log.inMapMode() ? 1 : 0);
	if (Actor *ac = Log.protagonist()) {
		if (ac->isSpeaking()) {
			ac->callMeWhenSilent(next);
			return kReturn;
		}
	}
	return kThxBye;
}

OPCODE(0x4b) {
	// DOS Op_4b_RegisterSampleIfMainChar @ 1000:3ee7:
	//   non-map: ResolveOpcodeArg0; RegisterSampleSlot_Bare9 (always).
	//   map: only if AX (input from prior call) == g_main_character_id
	//        → RegisterSampleSlot_Bare2.
	// Both calls land in RegisterSampleSlot_Common with the
	// branch_state/call_depth check.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x4b: wait actor %s silent", +a[0]);
	if (Actor *ac = Log.getActor(a[0])) {
		if (ac->isSpeaking()) {
			ac->callMeWhenSilent(next);
			return kReturn;
		}
	}
	return kThxBye;
}

OPCODE(0x54) {
	// DOS Op_54_RunMenuSelectAndBranch @ 1000:4011:
	//   CALL ReadVarBySlot_RHS;        ; AX = arg0_pointer (LHS slot)
	//   MOV DI, AX
	//   MOV AX, [0x6712]               ; AX = g_modal_es (saved seg)
	//   MOV ES, AX
	//   PUSH ES; PUSH DI                ; save (es:di) for later
	//   CALL ResolveOpcodeArg0; MOV CX, AX   ; arg0 = left
	//   CALL ResolveOpcodeArg1; MOV DX, AX   ; arg1 = top
	//   CALL ResolveOpcodeArg3; MOV BX, AX   ; arg3 = ?
	//   CALL ResolveOpcodeArg2;              ; arg2 = text
	//   POP DI; POP ES                       ; restore es:di
	//   CALL RunModalLoop @ 0x7ea2          ; modal!
	//   MOV AX, [0x66a2]                     ; selected item index
	//   CMP AX, 0xffff
	//   JZ skip                              ; cancelled → don't write
	//   ; AX is valid index → look up g_menu_item_indices[AX]:
	//   PUSH ds; POP DS; MOV SI, 0x4f1b
	//   ADD AX, AX; ADD SI, AX               ; SI = &indices[AX]
	//   MOV AX, [SI]                         ; AX = indices[AX]
	//   MOV [0x6710], AX                     ; write to result-slot (Op_5x reads via Op_55-style)
	//   skip: RET.
	//
	// = "show modal menu with text (arg2) at (arg0, arg1) with width-hint
	// arg3; if user picks an item, write the looked-up index to the
	// global result slot. arg4 is the destination LHS for return value
	// (read via ReadVarBySlot_RHS first to get the address)."
	//
	// C++ port: uses Graphics::ask which already implements a modal
	// bubble-frame text picker (matches DOS RunModalLoop semantics for
	// our purposes — polls events, hit-tests options, returns choice).
	// The result of Graphics::ask is `_optionValues[selected]` which
	// IS already the "looked-up index" — Graphics::ask integrates the
	// indices lookup into its option-rendering path, so we don't need
	// to re-apply the g_menu_item_indices mapping.
	debugC(1, kDebugLevelScript,
		"opcode 0x54: RunMenuSelectAndBranch text='%s' at (%s,%s) size %sx%s",
		+a[4], +a[0], +a[1], +a[2], +a[3]);

	const uint16 result = _graphics->ask(a[0], a[1], a[2], a[3], a[4]);
	Logic::ModalState &ms = Log.modalState();
	if (result == 0xffff) {
		// User cancelled (clicked outside menu). DOS: AX = 0xffff,
		// skip the result-write. C++ matches.
		ms.selectedItemIdx = 0xffff;
		debugC(2, kDebugLevelScript, "opcode 0x54: modal cancelled");
		return kThxBye;
	}
	// Valid selection. Persist the result and branch to the option's
	// CodePointer (DOS does `[0x6710] = indices[AX]`, then later
	// opcodes branch via the result; C++ Graphics::ask returns the
	// CodePointer offset directly — we branch via CodePointer ctor).
	ms.selectedItemIdx = result;
	return CodePointer(result, this);
}

OPCODE(0x55) {
	// paint text
	// args: left, top, colour, text
	debugC(2, kDebugLevelScript, "opcode 0x55: paint '%s' with colour %s at %s:%s", +a[3], +a[2], +a[0], +a[1]);
	_graphics->paintText(a[0], a[1], a[2], a[3]);
	return kThxBye;
}

OPCODE(0x56) {
	// DOS Op_56_SendActorToTargetOrWait @ 1000:4069: 2 args.
	//   CheckMovementBlocked() tests g_unknown_66d6, the countdown used by
	//   UpdateActorMotion's transient text renderer.
	//   if countdown active: RegisterSampleSlot_LoadDefaultsD; RET;
	//   else:
	//       g_unknown_66d6 = arg0;      // frames
	//       DAT_1cb5_66d8  = current code segment
	//       DAT_1cb5_66da  = arg1;      // text/control string
	//
	// Earlier C++ treated this as a protagonist walk request and queued `next`
	// immediately. In the intro that advanced room-84's script through Op_57
	// and Op_d0 in the same queued pass. Model the DOS countdown/text gate
	// instead; queued-pass isolation in Logic::runQueued() handles the
	// RegisterSampleSlot side of the wait path.
	debugC(2, kDebugLevelScript, "opcode 0x56: motion text frames=%s text=%s",
		+a[0], +a[1]);
	if (Log.motionTextActive()) {
		_logic->runLater(current, 0);
		return kReturn;
	}
	Log.startMotionText(uint16(a[0]), static_cast<byte *>(a[1]), uint16(a[1]));
	return kThxBye;
}

OPCODE(0x57) {
	// DOS Op_57_RegisterSampleSlotDirect @ 1000:4084: 0 args.
	// Calls RegisterSampleSlot_Common @ 1000:3154:
	//   if (branch_state == 0 && call_depth == 0) {
	//       room_script_slots[opcode_mode] = (PC, regs);
	//       g_break_outer = 1;
	//   } else g_pendingErrorCode = 0x39;
	// = "yield to next tick, resume from saved PC". C++ uses the
	// generic _queued mechanism: runLater(next, 0) + kReturn —
	// resumes the script on the next runQueued() pass.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x57: yield (resume next tick)");
	_logic->runLater(next, 0);
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
	// DOS Op_63_ReadActorField @ 1000:4139:
	//   1. ResolveOpcodeArg0 (actor id);
	//   2. if (id > g_anim_count_max) → pending-error 0x13;
	//   3. GetActorOffset(id) → ES:SI;
	//   4. ValidateTypeAndWriteVar2: ResolveOpcodeArg1 → AX (low=offset,
	//      high=size); size must be 1/2/4 else pending-error 2;
	//      load sized field from ES:[SI+off] into BX(:CX);
	//      WriteVarBySlot2_LHS → write to arg2 LHS.
	// = "READ a sized field from actor record, store in arg2 LHS".
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 0 && sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	Actor *actor = _logic->getActor(a[0]);
	const char *desc = "?";
	uint16 value = 0;

	if (!actor) {
		debugC(1, kDebugLevelScript, "opcode 0x63: get prop %u of UNKNOWN actor %s", off, +a[0]);
		a[2] = 0;
		return kThxBye;
	}

	switch (off) {
	case Actor::kOffsetLeft:        // +4 (int16 x)
		desc = "Left";
		value = uint16(actor->position().x);
		break;
	case Actor::kOffsetTop:         // +6 (int16 y)
		desc = "Top";
		value = uint16(actor->position().y);
		break;
	case Actor::kOffsetMainSprite:  // +8 (target frame in DOS — _nextFrame in C++)
		desc = "MainSprite";
		value = actor->targetFrameId();
		break;
	case Actor::kOffsetTicksLeft:   // +0xa (per-tick countdown)
		desc = "TicksLeft";
		value = uint16(actor->ticksLeft());
		break;
	case Actor::kOffsetInterval:    // +0x10 (current frame byte)
		desc = "Interval";
		value = actor->frameId();
		break;
	case Actor::kOffsetRoom:        // +0x59 (int16 room)
		desc = "Room";
		value = actor->room();
		break;
	default:
		// DOS fields not yet mirrored in C++ Actor (mood, flag14/15/16,
		// callback, etc.) — fall back to the sparse _dosFields hashmap
		// (which Phase-1 ops 0x1d/0x1e/0x1f/0x25 populate) so scripts
		// reading freshly-set bytes get the correct value, and absent
		// keys read as 0 (matches DOS post-init state).
		desc = "DosField";
		value = actor->dosField(off);
		debugC(2, kDebugLevelScript, "opcode 0x63: get unmodelled prop +0x%02x of actor %s -> %u (sparse)",
			off, +a[0], value);
		break;
	}

	a[2] = value;
	debugC(2, kDebugLevelScript, "opcode 0x63: %s of actor %s = %u", desc, +a[0], value);
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
	// DOS Op_77 @ 1000:433d = GoToRoomWithFrame(room, frame).
	// Ghidra's "Op_77_CheckActorAnimReady" rename is INCOMPLETE — only
	// the first 2 instructions decompiled (likely the no-return-analyzer
	// trap). The actual handler:
	//
	//   1. CALL CheckActorAnimReady(g_main_character_id).
	//   2. If carry clear (anim ready): JMP @ 0x3078 to save the script
	//      PC and defer execution (retry next tick when actor is idle).
	//   3. If carry set (NOT ready — e.g. fresh actor with
	//      actor.room != g_current_location): fall through and place:
	//        - actor.field+0x61 = arg1_lo  (current frame)
	//        - actor.field+0x62 = arg1_lo  (target frame)
	//        - actor.field+0x59 = arg0     (room)
	//        - g_current_location = arg0
	//        - FlushDirtyObjectSlotsToActor + g_flag_restart_room=1 etc.
	//
	// Critical Ghidra naming gotcha: 0x2eef = ResolveOpcodeArg1, 0x2f08 =
	// ResolveOpcodeArg0 (NOT what the names suggest!). Re-checking the
	// chain: CALL 0x2f08(=Arg0) gives AX=arg0, then BX=arg0; CALL
	// 0x2eef(=Arg1) gives AX=arg1. So at the placement: BL=arg0_lo →
	// frame, AX=arg1 → room. WAIT — let me re-verify in Op_77 itself.
	// The Op_77 chain uses 0x434d:CALL 0x2eef (=Arg1) first, MOV BX,AX,
	// MOV CX,AX, then 0x4354:CALL 0x2f08 (=Arg0). So BX=CX=arg1 and
	// AX=arg0 at the placement: BL→frame=arg1, AX→room=arg0.
	//
	// Thus Op_77(arg0=room, arg1=frame). Script Op_77(67, 10) →
	// room=67, frame=10. The OLD C++ port's `changeRoom(a[0]);
	// protagonist->setRoom(a[0], a[1])` happened to match DOS
	// semantics; the iter-24 "fix" reversed it incorrectly.
	debugC(2, kDebugLevelScript, "opcode 0x77: go to room %s frame %s", +a[0], +a[1]);
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *ac = Log.protagonist())
		ac->placeIn(uint16(a[0]), uint16(a[1]));
	_logic->changeRoom(a[0]);
	return kThxBye;
}

OPCODE(0x79) {
	// DOS Op_79_PlaceActorInRoom @ 1000:43d3:
	//   if (in_map_mode) return;
	//   id=arg0, room=arg1, frame=arg2;
	//   UnregisterActor(id);                  ; clear from old room
	//   actor.field+0x61 = (byte)frame;       ; current_frame
	//   actor.field+0x62 = (byte)frame;       ; target_frame (= same as current)
	//   actor.field+0x59 = room;
	//   actor.field+0x6b = 0;                  ; walk-target word cleared
	//   SetActorPosition;                     ; position from frame
	//   FindPlaceById(id); InitActorState(id);
	//   if (room == g_current_location && target!=current_frame)
	//       MoveActorToTargetExit;            ; auto-walk
	debugC(1, kDebugLevelScript, "opcode 0x79: move actor %s to room %s frame %s",
		+a[0], +a[1], +a[2]);
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = _logic->getActor(a[0]);
	if (!ac) return kThxBye;
	ac->placeIn(uint16(a[1]), uint16(a[2]));  // sets _room, _frame, _nextFrame, _position
	// Clear walk-target field (actor.field+0x6b/0x6c, word):
	ac->setDosField(0x6b, 0);
	ac->setDosField(0x6c, 0);
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
	// DOS Op_3a_CancelDeferredMain @ 1000:3cc7: removes the matching
	// entry from the deferred-script queue. Match key = (mode-segment,
	// offset). If the matched entry's mode equals `g_opcode_mode`
	// (= currently-running deferred script's mode), DOS sets
	// `g_break_loop = 1` so the running script exits this tick.
	// C++ `cancelLater` returns true in that self-cancel case;
	// translate to kReturn (= dispatcher exits the loop).
	debugC(2, kDebugLevelScript, "opcode 0x3a: cancel deferred (main) %s", +a[0]);
	const bool selfCancel = _logic->cancelLater(
		CodePointer(static_cast<CodePointer &>(a[0]).offset(), _logic->mainInterpreter()));
	return selfCancel ? kReturn : kThxBye;
}

OPCODE(0x3c) {
	// DOS Op_3c_CancelDeferredBlock @ 1000:3cc1: same shape as Op_3a
	// but for block-interpreter scripts.
	debugC(2, kDebugLevelScript, "opcode 0x3c: cancel deferred (block) %s", +a[0]);
	const bool selfCancel = _logic->cancelLater(
		CodePointer(static_cast<CodePointer &>(a[0]).offset(), _logic->blockInterpreter()));
	return selfCancel ? kReturn : kThxBye;
}

OPCODE(0x3e) {
	// DOS Op_3e_ClearEscapeBreakPoint @ 1000:3d23:
	//   g_esc_during_script = 0;
	// In C++, clearEscBreakPoint() resets all three fields plus the
	// skipPoint flag (mirrors `g_esc_during_script = 0` since the
	// "active" predicate in C++ is `!_skipPoint.isEmpty()`).
	debugC(2, kDebugLevelScript, "opcode 0x3e: clear ESC handler");
	Log.clearEscBreakPoint();
	return kThxBye;
}

OPCODE(0x4c) {
	// DOS Op_4c_RegisterSampleSpeechOrMap @ 1000:3eff: same
	// RegisterSampleSlot_Common epilogue as Op_4a/0x4b. Map-mode
	// uses Bare2; non-map uses Bare3 (which differs from Bare9 in
	// some sound-driver details but the Common save + break is
	// identical). Pending-error 0x39 if active switch / call.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x4c: wait protag silent (speech/map=%d)", Log.inMapMode() ? 1 : 0);
	if (Actor *ac = Log.protagonist()) {
		if (ac->isSpeaking()) {
			ac->callMeWhenSilent(next);
			return kReturn;
		}
	}
	return kThxBye;
}

OPCODE(0x7b) {
	// DOS Op_7b_SetObjectFlag1 @ 1000:4459:
	//   if (arg0 > g_object_count_max) pending-error 0x14;
	//   else if cell bit 0 not set: set cellByte[arg0] |= 1.
	const uint16 id = uint16(a[0]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (id > personsCount) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7b: set cell bit 0 on entity %s", +a[0]);
	Log.setCellBit(id, 0);
	if (Exit *exit = _logic->blockProgram()->getExit(a[0]))
		if (!exit->isEnabled())
			exit->setEnabled(true);
	return kThxBye;
}

OPCODE(0x7c) {
	// DOS Op_7c_ClearObjectFlag1 @ 1000:4476:
	//   if (arg0 > g_object_count_max) pending-error 0x14;
	//   else if cell bit 0 set: clear cellByte[arg0] ^= 1.
	const uint16 id = uint16(a[0]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (id > personsCount) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7c: clear cell bit 0 on entity %s", +a[0]);
	Log.clearCellBit(id, 0);
	if (Exit *exit = _logic->blockProgram()->getExit(a[0]))
		if (exit->isEnabled())
			exit->setEnabled(false);
	return kThxBye;
}

OPCODE(0x95) {
	// LOCK control: disallow user clicks/cursor movement.
	// DOS handler at CS:0x4a4c sets g_flag_no_step (DS:0x6747) = 1.
	debugC(1, kDebugLevelScript, "opcode 0x95: lock control");
	Log.setNoStep(true);
	return kThxBye;
}

OPCODE(0x96) {
	// UNLOCK control: re-allow user clicks/cursor movement.
	// DOS handler at CS:0x4a52 clears g_flag_no_step and g_flag_step_pending.
	debugC(1, kDebugLevelScript, "opcode 0x96: unlock control");
	Log.setNoStep(false);
	Log.setStepPending(false);
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
	if (Log.inMapMode())
		return kThxBye;

	Actor *ac = _logic->getActor(a[0]);
	if (!ac)
		return kThxBye;
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
	debugC(2, kDebugLevelScript, "opcode 0x9c: wait until actor %s enters or %s ticks", +a[0], +a[1]);
	if (Log.inMapMode())
		return kThxBye;

	Actor *ac = _logic->getActor(a[0]);
	if (!ac)
		return kThxBye;
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
	// DOS Op_9e (CS:0x4c4c). nargs=1 in dispatch table. Saves
	// g_main_character_id and uses it as the target actor; arg0 is the
	// frame id. DOS field assignments (after GetActorOffset(prot)):
	//   field+0x61 = arg0   (current frame)
	//   field+0x62 = arg0   (target frame)
	//   field+0x6b = 0      (walk speed)
	// Then SetActorPosition copies frame[arg0]'s X/Y into the actor.
	// FindPlaceById + InitActorState run after, but they only initialise
	// state without touching the script PC. The C++ equivalent is
	// placeIn() into the actor's CURRENT room (no room change here).
	debugC(2, kDebugLevelScript, "opcode 0x9e: warp protagonist to frame %s", +a[0]);
	if (Actor *ac = Log.protagonist())
		ac->placeIn(ac->room(), uint16(a[0]));
	return kThxBye;
}

OPCODE(0xab) {
	// DOS Op_ab_handler @ 1000:4e3e: protag-specific exit transition.
	//   if (g_in_map_mode != 0) RET;
	//   CheckActorIdle(g_main_character_id);
	//   if (CLC ready): RegisterSampleSlot_LoadDefaultsAndMark — saves
	//     g_block_start_es/di (script entry) to the room_script_slots
	//     pool indexed by g_opcode_mode, sets g_break_outer = 1.
	//     Per-tick MainGameLoop's RunScriptByMode for that mode reads
	//     the slot and re-runs from saved entry.
	//   if (STC busy/elsewhere): ResolveOpcodeArg0 + QueueExitTransition
	//     ([0x6609] = target_frame; g_break_inner=1;
	//      g_post_callback_ptr=0; CancelSpeechSlots; MoveActorToRoom).
	//
	// FULL-FIDELITY YIELD requires the room_script_slots subsystem
	// (5-mode slot pool + per-tick dispatcher) which is NOT yet
	// implemented in C++. Direct re-queueing via runLater(current, 0)
	// causes an infinite tight loop (Pass2-14a regression: per game.log,
	// 784k repeated frame-4 calls hung the engine before reaching
	// room 1 render). Re-queueing the script's entry offset via
	// `runLater(CodePointer(this->runEntry(), this), 0)` would
	// re-run the WHOLE script (including room-entry setup ops like
	// changeRoom / addActorFrame), which is also wrong.
	//
	// **Pass2-14b stop-gap**: actor-bound yield via `callMe(current)`.
	// This drains via Actor::callBacks() when isFine becomes false
	// (e.g., actor's animation consumes _attentionNeeded). It's the
	// pre-Pass2-14 working pattern. NOT DOS-faithful in mechanism but
	// produces equivalent end-to-end behavior because the actor's
	// state oscillates over animation ticks. Marked PARTIAL pending
	// proper room_script_slots subsystem implementation (cross-cutting).
	debugC(2, kDebugLevelScript, "opcode 0xab: queue protag exit transition to frame %s", +a[0]);
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = Log.protagonist();
	if (!ac)
		return kThxBye;

	if (ac->isFine()) {
		// Actor-bound yield (Pass2-14b stop-gap; see comment above).
		ac->callMe(current);
		return kReturn;
	}

	// QueueExitTransition equivalent: cancel speech + walk.
	ac->stopSpeaking();
	ac->moveTo(uint16(a[0]));
	return kThxBye;
}

OPCODE(0xad) {
	// DOS Op_ad_handler @ 1000:4e8c: actor-targeted move (any actor id).
	//   ResolveOpcodeArg0 → AX = actor id;
	//   CheckActorIdle(AX);
	//   if (CLC ready): RegisterSampleSlot_LoadDefaultsAndMark (yield
	//     to room_script_slots — see Op_ab note for the full-fidelity
	//     subsystem requirement).
	//   if (STC busy/elsewhere): ResolveOpcodeArg1 → AX = target_frame;
	//                            MoveActorToTargetExit.
	//
	// MoveActorToTargetExit (1000:70da) dispatches by actor type:
	//   - Protagonist: QueueExitTransition (cancel speech + walk +
	//     g_break_inner=1).
	//   - Non-protag IN g_actor_table[20] (active in current room):
	//     setup walk via FindActorPath (pathfinder).
	//   - Non-protag NOT in active table (offscreen):
	//     warp via SI[0x61]=target_frame + SetActorPosition (using
	//     actor.room's frame table).
	//
	// Pass2-14b: dispatch path is full-fidelity (warp/walk based on
	// actor.room == currentRoom). Yield path is actor-bound stop-gap
	// per Op_ab note — same room_script_slots gap.
	debugC(2, kDebugLevelScript, "opcode 0xad: move actor %s to frame %s next",
		+a[0], +a[1]);
	Actor *ac = _logic->getActor(a[0]);
	if (!ac)
		return kThxBye;

	if (ac->isFine()) {
		ac->callMe(current);
		return kReturn;
	}

	const uint16 targetFrame = uint16(a[1]);

	// MoveActorToTargetExit dispatch.
	if (ac == Log.protagonist()) {
		ac->stopSpeaking();
		ac->moveTo(targetFrame);
	} else if (ac->room() != _logic->currentRoom()) {
		// Non-protag offscreen: warp _frame only; _position preserved
		// (current room's frame[N] returns (999,999) sentinel which
		// setFrame ignores). DOS reads from actor.room's frame table
		// which C++ doesn't have loaded.
		ac->setFrame(targetFrame);
	} else {
		// Non-protag in current room: engage walk via Actor::moveTo.
		ac->moveTo(targetFrame);
	}
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
	// DOS Op_c2_handler @ 1000:5140: walks g_cast_table (18 slots),
	// finds first inactive (wActive==0), seeds it with arg0 (script
	// offset), current ES (script segment), and locked cursor x/y as
	// initial position. The animation script then does its own Op_05/06
	// to set the sprite + reposition. Engine equivalent: spawn a fresh
	// Animation registered in Logic::_animations.
	//
	// Differences from DOS that are observable but harmless during the
	// intro: we use the live cursor position (currently hardcoded to
	// 160,100 — graphics.cpp:cursorPosition is a STUB) instead of the
	// frame-start snapshot, and we don't enforce the 18-slot cap. The
	// script always repositions its animation in the first tick, so the
	// initial position only matters for the very first frame.
	debugC(2, kDebugLevelScript, "opcode 0xc2: add animation %s at cursor", +a[0]);
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
	// DOS Op_c8_handler @ 1000:5222: ClearVideoAndPushToScreen +
	// g_loaded_backdrop_id (DS:0x666a) = arg0 + SetBackdropImage. This
	// loads the named backdrop graphic into the room buffer.
	//
	// Difference from Op_c9 (clarified iter-30): Op_c8 immediately loads
	// a backdrop image. Op_c9 sets the savegame "current place" id and
	// only triggers a reload when in map mode.
	debugC(2, kDebugLevelScript, "opcode 0xc8: set backdrop(%s)", +a[0]);
	_graphics->setBackdrop(a[0]);
	return kThxBye;
}

OPCODE(0xc9) {
	// DOS Op_c9_handler @ 1000:522f:
	//   DAT_1000_0111 = arg0           ; set "current place" id (CS:[0x111])
	//   if (g_in_map_mode != 0) {
	//     ClearVideoAndPushToScreen();
	//     g_flag_change_room = 1;     ; trigger room reload
	//   }
	// CS:[0x111] is read only by RestoreBackdrop and Op_fa (savegame
	// path), so the "place" id functions as a save-state identifier.
	// Previous C++ called `setBackdrop(a[0])` which fed arg0 (e.g. "1")
	// into the backdrop slot — completely wrong, that's Op_c8's job.
	// Now we just record the value via Logic::setCurrentPlace and skip
	// the map-mode branch (we don't model map mode).
	debugC(2, kDebugLevelScript, "opcode 0xc9: set current place to %s", +a[0]);
	_logic->setCurrentPlace(uint16(a[0]));
	return kThxBye;
}

OPCODE(0xcb) {
	// DOS Op_cb_handler @ 1000:5275 → calls LoadGraphicToSlot @ 1000:1f49:
	//   if (arg0 > graphic_count) pending-error 0xa;
	//   else: type = graphic_index[(arg0-1)*4].first_dword;
	//     type ∈ {1,2,3} → DecodeImage to small slot (DS:0x676f..0x6773)
	//     type ∈ {4,5}  → DecodeFullScreenImage to slot (DS:0x6779/0x677b)
	//     type 6        → DecodeFullScreenImage to slot (DS:0x6775)
	//     type 7        → ditto + palette read (slot DS:0x6777)
	//     other         → pending-error 0xa.
	//   On match, store arg0 in the corresponding slot global.
	const uint16 id = uint16(a[0]);
	MapFile *map = _logic->resources()->graphicsMap();
	if (!map || id == 0 || id > map->entryCapacity()) {
		Log.setPendingError(0x0a);
		return kThxBye;
	}
	const uint32 entry = map->offsetOfEntry(id);
	const uint8 type = uint8(entry & 0xff);
	debugC(1, kDebugLevelScript, "opcode 0xcb: load graphic %u (type=%u)", id, type);
	switch (type) {
	case 1: Log.setGraphicSlot(0, id); break;
	case 2: Log.setGraphicSlot(1, id); break;
	case 3: Log.setGraphicSlot(2, id); break;
	case 6: Log.setGraphicSlot(5, id); break;
	case 7: Log.setGraphicSlot(6, id); break;
	case 4: Log.setGraphicSlot(3, id); break;
	case 5: Log.setGraphicSlot(4, id); break;
	default:
		Log.setPendingError(0x0a);
		return kThxBye;
	}
	// Decoding happens on demand in C++ Resources::loadImage. The
	// script's contract is satisfied by recording which graphic is
	// active in this slot.
	return kThxBye;
}

OPCODE(0xcc) {
	// go fullscreen
	debugC(1, kDebugLevelScript, "opcode 0xcc: go fullscreen");
	Graf.goFullscreen();
	return kThxBye;
}

OPCODE(0xce) {
	// DOS Op_ce_handler (CS:0x52a4): start cutscene.
	//   1. g_room_active = 0
	//   2. SetBackdropDimensions (fullscreen)
	//   3. g_flag_misc_1 = 1 (dirty flag)
	//   4. Calls Op_95_handler (lock control = setNoStep(true))
	// C++ approximation: hide cursor (visual cue) AND lock control (so the
	// player can't click while the cutscene runs). Backdrop dimensions /
	// room_active still TODO — currently the engine doesn't have a separate
	// "room active" flag distinct from currentRoom.
	debugC(2, kDebugLevelScript, "opcode 0xce: start cutscene");
	Graf.hideCursor();
	Log.setNoStep(true);
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
	// DOS Op_e5_handler @ 1000:5604: 0 args. Clears g_anim_list_count = 0.
	// = reset cutscene anim-list to empty.
	debugC(1, kDebugLevelScript, "opcode 0xe5: anim-list clear");
	Log.animListClear();
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
	// DOS Op_load_sfx @ 1000:56d9: 1 arg = SFX id.
	//   if (g_sfx_enabled) {
	//       if (arg0 != pbRam0002324e) {
	//           PlaySfxSound(arg0);
	//           update slot caches, last_played = arg0, secondary = 0;
	//       }
	//   }
	// Routes through Sound::playSfx which handles the short-circuit
	// and slot-cache state transitions per DOS.
	if (Sound *snd = _engine->sound())
		snd->playSfx(uint16(a[0]));
	debugC(1, kDebugLevelScript, "opcode 0xf0: load_sfx id=%s", +a[0]);
	return kThxBye;
}

OPCODE(0xf4) {
	// play music. The arg is a near offset into the main bytecode (IUC_MAIN.DAT) that points
	// at a music script: tune index (uint16) followed by kSetBeat/kJump/kStop bytecodes. Even
	// when called from a block, the offset is always relative to the main interpreter — music
	// scripts live in the global file, not in per-block bytecode.
	const uint16 scriptOff = static_cast<CodePointer &>(a[0]).offset();
	debugC(1, kDebugLevelScript, "opcode 0xf4: play music script at main offset 0x%04x", scriptOff);
	static int op_f4_calls = 0;
	op_f4_calls++;
	if (op_f4_calls <= 3)
		warning("Interspective music: opcode 0xf4 emitted (call #%d, script offset 0x%04x)",
			op_f4_calls, scriptOff);
	Music.loadMusic(Log.mainInterpreter()->rawCode(scriptOff));
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
	// DOS Op_f9_handler (CS:0x58cc):
	//   arg1 = state byte (0=disable channel, non-zero=enable)
	//   arg0 = which: 1=music, 2=sfx
	// On music-disable: stop the current tune (silence + clear current_tune_addr).
	// On music-disable: also stops any active sfx (cascade in DOS).
	// On sfx-disable: clears g_sfx_active. We don't have a separate sfx mixer
	// channel yet, so treat sfx-disable as a no-op beyond logging.
	const uint8 state = uint8(uint16(a[1]));
	const bool isMusic = (uint16(a[0]) == 1);
	debugC(1, kDebugLevelScript, "opcode 0xf9: set %s to %u", isMusic ? "music" : "sfx", state);
	if (isMusic && state == 0)
		Music.silence();
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
	// DOS Op_0b_IfMode40AndFlag @ 1000:37ff:
	//   if (step_pending && cursor==0x40 && arg0 == g_drag_target_mode40)
	//       return; else skip;
	// Note: DOS reads `g_drag_target_mode40` @ DS:0x667e, NOT the
	// regular `g_drag_target` @ DS:0x667c (which Op_0e uses). The
	// mode-40 slot is set exclusively by Op_76_BeginDragWithTarget
	// (1000:4325) together with `_g_cursor_mode = 0x40`.
	// In C++ this is `Logic::_dragTargetMode40`; Op_76 populates it
	// and sets cursor mode 0x40.
	uint16 mask = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x0b: if step && cursor==0x40 && dragMode40==%u", mask);
	if (Log.stepPending() && Log.cursorMode() == 0x40 && Log.dragTargetMode40() == mask)
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
		// IfFreshGameState (DOS CS:0x395a): fail if current entity type != 0.
		debugC(2, kDebugLevelScript, "opcode 0x14: if current entity type == 0");
	if (Log.gameState() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x15) {
	// DOS Op_15_IfCellBitSet @ 1000:3968.
	//   ResolveOpcodeArg1 → bit_idx; if > 7 → SetError15ArgOutOfRange (halt)
	//   ResolveOpcodeArg0 → id; if > g_object_count_max → SetError14NoExit (halt)
	//   build 9-bit value: ((id < max) << 8) | cellByte[id]
	//   ROR by (bit_idx + 1) mod 9; skip if result bit 8 == 0
	// Net for the common path (id < max, bit ∈ [0,7]): tests
	// cellByte[id] bit `bit_idx` (LSB-indexed). Body runs if bit SET.
	//
	// Bound checks:
	//   bit_idx > 7: SetError15ArgOutOfRange.
	//   id > max: SetError14NoExit.
	const uint16 rawBit = uint16(a[1]);
	if (rawBit > 7) {
		Log.setPendingError(0x15);
		return kThxBye;
	}
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const uint8 bit = uint8(rawBit);
	const bool set = Log.cellBit(id, bit);
	debugC(2, kDebugLevelScript, "opcode 0x15: if cell bit %u of entity %s set (=%s)",
		bit, +a[0], set ? "yes" : "no");
	return set ? kThxBye : kFail;
}

OPCODE(0x16) {
	// DOS Op_16 @ 1000:3991 sets CX=1 then tail-jumps into the same
	// cell-byte tester used by Op_15. The common tail increments CX
	// before `RCR AL,CL`, so this tests bit 1 of cellByte[arg0].
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const bool set = Log.cellBit(id, 1);
	debugC(2, kDebugLevelScript, "opcode 0x16: if cell bit 1 of entity %s set (=%s)",
		+a[0], set ? "yes" : "no");
	return set ? kThxBye : kFail;
}

OPCODE(0x18) {
	// DOS Op_18 (CS:0x39a9): SETS skip_counter when Object[a[0]].room == 0
	// (i.e. SKIPS the body when the object is missing). Net semantics: the
	// conditional body executes when the object is PRESENT. Ghidra's label
	// "IfObjectMissing" describes the SKIP condition, not the run condition.
	// Without a loaded Object table we default to "present" → run body.
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const bool missing = Log.isObjectMissing(id);
	debugC(2, kDebugLevelScript, "opcode 0x18: if object %s present (room=%u%s)",
		+a[0], Log.getObjectRoom(id), Log.hasObjectRoom(id) ? "" : " default");
	return missing ? kFail : kThxBye;
}

OPCODE(0x1b) {
	// DOS Op_1b (CS:0x39e3): SETS skip_counter when Object[a[0]].room != 0
	// (i.e. SKIPS the body when the object is PRESENT). Net semantics: the
	// conditional body executes when the object is MISSING. Inverse of 0x18.
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const bool missing = Log.isObjectMissing(id);
	debugC(2, kDebugLevelScript, "opcode 0x1b: if object %s missing (room=%u%s)",
		+a[0], Log.getObjectRoom(id), Log.hasObjectRoom(id) ? "" : " default");
	return missing ? kThxBye : kFail;
}

OPCODE(0x1e) {
	// DOS Op_1e_IfMainActorAtRoomFrame @ 1000:3a0a:
	//   MOV AX, CS:[0x010f] (g_main_character_id), then tail into
	//   the Op_1d room/frame tester. Run if protagonist.room ==
	//   current_loc AND byte field+0x61 == low(arg0).
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x1e: if protagonist at frame %s", +a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) != uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x20) {
	// DOS Op_20_IfMainActorNotAtRoomFrame @ 1000:3a33:
	// protagonist variant of Op_1f; same room check, byte frame mismatch.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x20: if protagonist not at frame %s", +a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) == uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x21) {
	// DOS Op_21 (CS:0x3a75): SETS skip_counter when Object[a[0]].room != -1
	// (i.e. SKIPS the body when the object IS placed). Net semantics: the
	// conditional body executes when the object is NOT placed (room == 0xffff).
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const uint16 room = Log.getObjectRoom(id);
	const bool unplaced = (room == uint16(0xffff));
	debugC(2, kDebugLevelScript, "opcode 0x21: if object %s unplaced (room=%u%s)",
		+a[0], room, Log.hasObjectRoom(id) ? "" : " default");
	return unplaced ? kThxBye : kFail;
}

OPCODE(0x22) {
	// DOS Op_22_IfStringEqualsBuf @ 1000:3a88:
	//   compare arg0 (null-terminated chars) against parser buffer
	//   (DS:0x4fa9 capacity, 0x4faa length, 0x4fab+ chars). Match if
	//   both end together (all chars equal up to length, then null
	//   on arg0 side). Skip on mismatch.
	// C++ models the parser buffer via `Logic::_parserBuffer`. Filled
	// by Op_e9 (append), cleared by Op_e7, popped by Op_eb.
	const byte *s = static_cast<byte *>(a[0]);
	const Common::String &buf = Log.parserBuffer();
	debugC(2, kDebugLevelScript, "opcode 0x22: if input '%s' == arg0 '%s'",
		buf.c_str(), s ? reinterpret_cast<const char *>(s) : "(null)");
	if (!s) return kFail;
	if (strlen(reinterpret_cast<const char *>(s)) != buf.size()) return kFail;
	if (memcmp(s, buf.c_str(), buf.size()) != 0) return kFail;
	return kThxBye;
}

OPCODE(0x23) {
	// DOS Op_23_IfStringsEqual @ 1000:3a9c.
	//   arg0 has 2-byte header (byte 0 unused, byte 1 = length), chars at +2.
	//   arg1 is null-terminated chars.
	//   Compare byte-by-byte for `length` chars; both must end together.
	// In C++ both arg0 and arg1 are `ParametrizedString` instances:
	//   `static_cast<byte *>(a[i])` returns the translated, null-terminated
	//   `_translateBuf`; `uint16(a[i])` returns the `_length` field. The
	//   DOS arg0/arg1 format asymmetry (length-prefix vs null-term) is
	//   flattened by the C++ argument loader. The Ghidra-faithful
	//   comparison is "are the two translated strings equal?". `_length`
	//   includes the terminating NUL in C++, while DOS's CL counter does
	//   not, so use strlen() for the payload length.
	const byte *s = static_cast<byte *>(a[0]);
	const byte *t = static_cast<byte *>(a[1]);
	const uint16 sLen = s ? uint16(strlen(reinterpret_cast<const char *>(s))) : 0;
	debugC(2, kDebugLevelScript, "opcode 0x23: if %s == %s", +a[0], +a[1]);
	if (!s || !t)
		return kFail;
	for (uint16 i = 0; i < sLen; ++i) {
		if (t[i] == 0 || s[i] != t[i])  // arg1 ended early or mismatch
			return kFail;
	}
	if (t[sLen] != 0)  // arg1 has more chars beyond arg0's length
		return kFail;
	return kThxBye;
}

OPCODE(0x26) {
	// DOS Op_26_RunCheckActorIfStepCursor4 @ 1000:382f:
	//   if (step_pending && cursor == 4) {
	//       AX = g_main_character_id;
	//       CheckActorScripting(AX);    // CF=1 if actor.f6f==0 && actor.f6b==0
	//       if (CF == 1) JMP Op_41_SpeakAsMainNoTarget;
	//   }
	// Net: speak-as-main only if the main char is "idle" (both
	// scripting fields 0). Otherwise no-op.
	if (!Log.stepPending() || Log.cursorMode() != 4)
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag) return kThxBye;
	Log.setImplicitActor(protag);
	// CheckActorScripting: idle iff both field+0x6f (byte) and
	// field+0x6b/0x6c (word) are zero.
	const bool idle = protag->dosField(0x6f) == 0
		&& protag->dosField(0x6b) == 0
		&& protag->dosField(0x6c) == 0;
	debugC(2, kDebugLevelScript, "opcode 0x26: step+cursor==4, protag idle=%d", int(idle));
	if (!idle) return kThxBye;
	// Tail-jump to Op_41: speak as main, no target.
	if (protag->isSpeaking()) {
		protag->callMeWhenSilent(current);
		return kReturn;
	}
	if (protag->isMoving()) {
		protag->callMeWhenStill(current);
		return kReturn;
	}
	speakOrSubtitle(protag, a[0]);
	return kThxBye;
}

OPCODE(0x27) {
	// DOS Op_27_RunOp3fIfStepCursor4 @ 1000:381d:
	//   if (g_flag_step_pending && g_cursor_mode == 4)
	//       Op_3f_SpeakAsMainCharacter();
	// nargs=1 — when the gate fires, dispatches into Op_3f with the same
	// arg list (which Op_3f's ResolveOpcodeArg0 will consume).
	debugC(2, kDebugLevelScript, "opcode 0x27: if step && cursor==4, speak as main");
	if (Log.stepPending() && Log.cursorMode() == 4)
		speakOrSubtitle(Log.protagonist(), a[0]);
	return kThxBye;
}

OPCODE(0x28) {
	// DOS Op_28_IfModeIs80 @ 1000:384a:
	//   if (cursor_mode == 0x80) {
	//       arg0 = anim mask;  CycleAllAnimationsByMask(arg0);
	//       arg1 = text;       DrawCenteredOverlayText(arg1);
	//   }
	// fall through (no skip_counter).
	if (Log.cursorMode() != 0x80)
		return kThxBye;
	debugC(2, kDebugLevelScript, "opcode 0x28: cursor==0x80, anim mask=%s text=%s",
		+a[0], +a[1]);

	// Side effect 1: CycleAllAnimationsByMask @ 1000:c8a1 advances 5
	// cursor-overlay animation slots based on arg0 bits (1, 2, 0x10,
	// 4, 8). DOS slots live at CS:[0xa9..0xb1], populated at runtime
	// when entering cursor mode 0x80. C++ doesn't yet preload these
	// 5 slots; the on-screen verb bubble itself isn't rendered. The
	// closest equivalent in C++: when an animation matching one of
	// the 5 overlay sprites is registered via the regular animation
	// pipeline, it cycles automatically each tick — Op_28's
	// per-bit-gated cycle is a DOS performance optimization (only
	// advance the visible icons) that's a no-op in the C++
	// always-tick model.
	const uint16 mask = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x28: cycle cursor-overlay anims (mask=0x%02x; per-tick auto-cycle in C++)", mask);

	// Side effect 2: DrawCenteredOverlayText @ 1000:c581 renders text
	// at y=0xb4 (180), color 0xeb (foreground), 0xae (shadow), 9px
	// line height, centered within a 56-pixel-wide bubble.
	// DOS centering math: x = (0x38 - textWidth) / 2 + 4. The
	// bubble itself sits at the top-left of a verb overlay (the
	// 5 icons cycled by side-effect 1). C++ uses paintText at the
	// computed (x, 180) with color 0xeb (foreground); the DOS
	// shadow color 0xae would require a second paintText call
	// offset by 1 pixel, which the existing engine font path
	// doesn't yet expose without a font-spec change.
	const byte *text = static_cast<byte *>(a[1]);
	if (text) {
		const Common::Rect metrics = _graphics->textMetrics(text);
		const uint16 textWidth = metrics.width();
		// DOS bubble interior width = 0x38 = 56 pixels. Center text
		// within that span; clamp to 0 if text is wider.
		const int16 bubbleInner = 0x38;
		const int16 sx = (bubbleInner - int16(textWidth)) / 2 + 4;
		const uint16 x = sx > 0 ? uint16(sx) : 0;
		// Shadow first (color 0xae), offset by +1,+1 to match DOS
		// outline rendering.
		_graphics->paintText(x + 1, 0xb5, 0xae, text);
		_graphics->paintText(x, 0xb4, 0xeb, text);
	}
	return kThxBye;
}

OPCODE(0x29) {
	// DOS Op_29_IfMode10AndFlag @ 1000:3863:
	//   if (step_pending && cursor == 0x10) {
	//       SendActorToTarget();       ; protag walks to current entity
	//       SetPostMoveCallback(BP=0x4376, AX=arg0, BX=CX=arg1);
	//   }
	// Falls through unconditionally (no skip_counter).
	if (Log.stepPending() && Log.cursorMode() == 0x10) {
		debugC(2, kDebugLevelScript,
			"opcode 0x29: walk current entity, then place protag room %s frame %s",
			+a[0], +a[1]);
		if (Log.sendActorToCurrentEntity(Log.protagonist())) {
			Log.setPostMoveCallback(
				Logic::PostMoveCallback::kPlaceProtagonistAfterMove,
				uint16(a[0]),
				uint8(uint16(a[1])),
				uint8(uint16(a[1]))
			);
		}
	}
	return kThxBye;
}

OPCODE(0x2a) {
	// DOS Op_2a_IfMode10AndFlag2 @ 1000:387e: 3-arg variant of 0x29.
	if (Log.stepPending() && Log.cursorMode() == 0x10) {
		debugC(2, kDebugLevelScript,
			"opcode 0x2a: walk current entity, then place protag room %s frame %s next %s",
			+a[0], +a[1], +a[2]);
		if (Log.sendActorToCurrentEntity(Log.protagonist())) {
			Log.setPostMoveCallback(
				Logic::PostMoveCallback::kPlaceProtagonistAfterMove,
				uint16(a[0]),
				uint8(uint16(a[1])),
				uint8(uint16(a[2]))
			);
		}
	}
	return kThxBye;
}

OPCODE(0x2b) {
	// DOS Op_2b_BranchOnFrameMismatch @ 1000:3a5c.
	// Reads `g_main_character_id` (CS:[0x10f]), GetActorOffset → SI =
	// protagonist record. If actor.frame != arg0:
	//   `g_codeptr_di_save = arg1` — IMMEDIATE jump to arg1. Body
	//   between Op_2b and the destination is SKIPPED.
	// If equal: fall through.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x2b: jump unless protagonist at %s -> %s", +a[0], +a[1]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (uint8(ac->frameId()) != uint8(uint16(a[0])))
		return static_cast<CodePointer &>(a[1]);
	return kThxBye;
}

OPCODE(0x2e) {
	// DOS Op_2e_RestoreBranchFromSave @ 1000:3b16:
	//   g_branch_state = g_codeptr_di_save;
	// Saves the CURRENT script PC (= start of switch loop body) so
	// Op_2f..Op_34 can jump back here on case mismatch ("try next
	// case"). The dispatcher passes the next-instruction CodePointer
	// as `next` — that's the position right after Op_2e itself, which
	// is exactly what DOS captures.
	debugC(2, kDebugLevelScript, "opcode 0x2e: branch-state = current PC");
	Log.setBranchState(next.offset());
	return kThxBye;
}

// Case-comparison family (DOS Op_2f..Op_34 @ 1000:3b1d..0x3bc8). All
// share the structure:
//   if (g_branch_state == 0) g_pendingErrorCode = 4; return;
//   if (SKIP_COND) g_codeptr_di_save = g_branch_state; return;  // jump back
//   g_branch_state = 0; return;                                  // case match
// SKIP_COND varies per opcode (the case label = SKIP condition):
//   0x2f CaseNotEqual:        arg0 != arg1
//   0x30 CaseEqual:           arg0 == arg1
//   0x31 CaseGreater:         arg1 <= arg0 (signed)  → run if arg0 < arg1
//   0x32 CaseLess:            arg0 <= arg1 (signed)  → run if arg0 > arg1
//   0x33 CaseGreaterOrEqual:  arg1 < arg0 (signed)   → run if arg0 <= arg1
//   0x34 CaseLessOrEqual:     arg0 < arg1 (signed)   → run if arg0 >= arg1
// (Note: Ghidra labels describe the SKIP condition, not the run condition.)
//
// Pending-error (rule 2): code 4 = "no active switch". Until the
// pending-error subsystem lands the C++ logs a warning and falls
// through (the case ops become no-ops without an active switch).

OPCODE(0x2f) {
	debugC(2, kDebugLevelScript, "opcode 0x2f: case-not-equal %s vs %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) { Log.setPendingError(0x04); return kThxBye; }
	if (uint16(a[0]) != uint16(a[1]))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x30) {
	debugC(2, kDebugLevelScript, "opcode 0x30: case-equal %s vs %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) { Log.setPendingError(0x04); return kThxBye; }
	if (uint16(a[0]) == uint16(a[1]))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x31) {
	debugC(2, kDebugLevelScript, "opcode 0x31: case-greater (skip if a0>=a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) { Log.setPendingError(0x04); return kThxBye; }
	if (int16(uint16(a[0])) >= int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x32) {
	debugC(2, kDebugLevelScript, "opcode 0x32: case-less (skip if a0<=a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) { Log.setPendingError(0x04); return kThxBye; }
	if (int16(uint16(a[0])) <= int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x33) {
	debugC(2, kDebugLevelScript, "opcode 0x33: case-ge (skip if a0>a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) { Log.setPendingError(0x04); return kThxBye; }
	if (int16(uint16(a[0])) > int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x34) {
	debugC(2, kDebugLevelScript, "opcode 0x34: case-le (skip if a0<a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) { Log.setPendingError(0x04); return kThxBye; }
	if (int16(uint16(a[0])) < int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}

OPCODE(0x38) {
	// DOS Op_38_SwitchToScene @ 1000:3c58:
	//   SaveCastBackup;  // memcpy cast table (0x642 bytes)
	//   SaveActorTableBackup;
	//   ResolveOpcodeArg0;  // arg0 = new scene id
	//   LoadRoomLevelHeader;
	//   _g_block_pc_offset  = g_codeptr_es_save;  // save caller PC
	//   _g_block_pc_segment = g_codeptr_di_save;
	//   g_codeptr_es_save = g_seg_buffer_e;       // jump to new scene
	//   g_codeptr_di_save = g_room_list_ptr;
	// = "call into a sub-scene". Op_01_handler's nested-pop path
	// then restores the saved PC. C++ uses Logic::saveSceneFrame
	// (built in Pass1-3) to capture the current Program /
	// Interpreter / Room / resume-PC; Logic::changeRoom loads the
	// new scene's bytecode.
	debugC(2, kDebugLevelScript, "opcode 0x38: switch to scene %s (push)", +a[0]);
	Log.saveSceneFrame(next);
	Log.changeRoom(uint16(a[0]));
	return kThxBye;
}

// Speech variants (DOS CS:0x3da2..0x3e68). The engine routes everything via
// Actor::say, which queues a speech bubble for the calling actor. Variants
// differ by speaker (main vs identified actor) and target (none vs hotspot).
OPCODE(0x3f) {
	// DOS Op_3f_SpeakAsMainCharacter @ 1000:3d29: speak text (arg0)
	// as the protagonist. Map-mode → subtitle; else → speech-slot
	// allocation for the main char. The full DOS path also handles
	// hit-region pre-walk and game-state-2 target-wait — those are
	// walk-driver concerns; with no walk pending the protagonist
	// just speaks.
	debugC(1, kDebugLevelScript, "opcode 0x3f: main says %s", +a[0]);
	speakOrSubtitle(Log.protagonist(), a[0]);
	return kThxBye;
}
OPCODE(0x40) {
	// DOS Op_40_SpeakAtTarget @ 1000:3da2: arg0=text, arg1=target id.
	// Target positioning anchors the bubble near the target entity's
	// sprite. C++ resolves arg1 as an actor and uses its
	// getSpeechPosition() as the bubble anchor.
	Actor *target = Log.getActor(a[1]);
	debugC(1, kDebugLevelScript, "opcode 0x40: main says %s @ target %s%s",
		+a[0], +a[1], target ? "" : " (unresolved)");
	speakOrSubtitle(Log.protagonist(), a[0], target);
	return kThxBye;
}
OPCODE(0x42) {
	// DOS Op_42_SpeakAsMainAtTarget @ 1000:3e04: arg0=target id, arg1=text.
	Actor *target = Log.getActor(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x42: main says %s @ target %s%s",
		+a[1], +a[0], target ? "" : " (unresolved)");
	speakOrSubtitle(Log.protagonist(), a[1], target);
	return kThxBye;
}
OPCODE(0x44) {
	// DOS Op_44_SpeakAsActorAtTarget @ 1000:3e4f: arg0=actor id,
	// arg1=target id, arg2=text.
	Actor *ac = Log.getActor(a[0]);
	Actor *target = Log.getActor(a[1]);
	debugC(1, kDebugLevelScript, "opcode 0x44: actor %s says %s @ target %s%s",
		+a[0], +a[2], +a[1], target ? "" : " (unresolved)");
	speakOrSubtitle(ac, a[2], target);
	return kThxBye;
}
OPCODE(0x45) {
	// DOS Op_45_SpeakWithDelay @ 1000:3e68: 4 args (y, x, color, text).
	//   if (!map_mode) AllocSpeechSlot_NoFormatting + stash arg2;
	//   else map-mode subtitle.
	// AllocSpeechSlot_NoFormatting = narrator-style bubble at the
	// explicit (x, y) with color — NOT tied to any actor.
	const byte *text = static_cast<byte *>(a[3]);
	if (!text) return kThxBye;
	const uint16 y = uint16(a[0]);
	const uint16 x = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
	if (length == 0) return kThxBye;
	const uint16 ticks = MAX<uint16>(30, 3 * length);
	debugC(1, kDebugLevelScript, "opcode 0x45: narrator at (%u,%u) color=%u text='%s'",
		x, y, color, reinterpret_cast<const char *>(text));
	if (Log.inMapMode())
		Graf.say(text, length, ticks);
	else
		Graf.sayAt(text, length, ticks, x, y, color);
	return kThxBye;
}
OPCODE(0x46) {
	// DOS Op_46_SpeakWithDelayAlt @ 1000:3e5e: identical body to 0x45
	// but writes to a different g_unknown_669a-equivalent slot. The
	// stash differs (an alternate hint slot) but the visible
	// behaviour is identical from the script's perspective.
	const byte *text = static_cast<byte *>(a[3]);
	if (!text) return kThxBye;
	const uint16 y = uint16(a[0]);
	const uint16 x = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
	if (length == 0) return kThxBye;
	const uint16 ticks = MAX<uint16>(30, 3 * length);
	debugC(1, kDebugLevelScript, "opcode 0x46: narrator (alt) at (%u,%u) color=%u text='%s'",
		x, y, color, reinterpret_cast<const char *>(text));
	if (Log.inMapMode())
		Graf.say(text, length, ticks);
	else
		Graf.sayAt(text, length, ticks, x, y, color);
	return kThxBye;
}

// 0x48..0x53: speech / sample / menu / text-bubble family.
// PREVIOUSLY (incorrectly) implemented as bitwise / arithmetic ops by the
// original ScummVM port — see PLAN.md "P0 finding". Confirmed via Ghidra:
// these are speech, sample-registration, menu, and text-bubble handlers.
// Replaced with faithful (or safe-stub) implementations that preserve DOS
// fall-through semantics (always kThxBye — these handlers never set
// skip_counter). Side effects flagged TODO where infrastructure missing.
OPCODE(0x48) {
	// DOS Op_48_SpeakWithRectAndPos @ 1000:3ea7: 5 args (y, x, color,
	// lines, text). Same shared-tail as Op_47 but reads via
	// ReadVarBySlot_RHS (different argument-resolution path); for
	// the script-observable behaviour the args have the same meaning.
	const uint16 y = uint16(a[0]);
	const uint16 x = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	const byte *text = static_cast<byte *>(a[4]);
	debugC(1, kDebugLevelScript, "opcode 0x48: narrator at (%u,%u) color=%u text='%s'",
		x, y, color, text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
		if (length > 0)
			Graf.sayAt(text, length, MAX<uint16>(30, 3 * length), x, y, color);
	}
	return kThxBye;
}
OPCODE(0x49) {
	// DOS Op_49_SetActorFlag70 (CS:0x3ec5): a[0]=actor id, a[1]=byte.
	// Sets Actor[a[0]].field_0x70 = (byte)a[1]. Logic doesn't track actor
	// flag70 yet — log and stash via Logic helper for future readers.
	const uint16 id = uint16(a[0]);
	const uint8 v = uint8(uint16(a[1]));
	debugC(2, kDebugLevelScript, "opcode 0x49: actor %s flag70 = %u", +a[0], v);
	Log.setActorFlag70(id, v);
	return kThxBye;
}
OPCODE(0x4d) {
	// DOS Op_4d_StashMenuArgs @ 1000:3f0c:
	//   pbRam00023206 = ResolveOpcodeArg0;   // [DS:0x66b6] = arg0
	//   pbRam00023208 = ResolveOpcodeArg1;   // [DS:0x66b8] = arg1
	//   DAT_1cb5_6741 = 0;                   // [DS:0x6741] = 0 (clear stash flag)
	// Stashes the (positioning, hint) pair for the next bubble/menu op
	// (0x4f / 0x51 read these via the secondary-arg path). The
	// stash flag is RESET so Op_53 (DrawFixedTextBubbleStashed) takes
	// its non-stashed branch unless Op_50/0x51 fires in between.
	debugC(2, kDebugLevelScript, "opcode 0x4d: stash menu args (%s, %s)", +a[0], +a[1]);
	Log.setMenuStash(uint16(a[0]), uint16(a[1]));
	// Sync new ModalState.stashFlag (canonical for Op_53's branch).
	Log.modalState().stashFlag = 0;
	return kThxBye;
}
OPCODE(0x4e) {
	// DOS Op_4e_DrawTextRectWithChoices @ 1000:3f1e:
	//   CALL ResolveOpcodeArg0;     ; AX = text ptr
	//   MOV DI, AX                   ; DI = text source for formatter
	//   CALL FormatBubbleText_FullPath; AX = total_height (low),
	//                                   ; DX = ?, CX = max_line_width,
	//                                   ; BX = choice_count
	//   MOV [0x66c2], AX             ; menu_choice_count = formatter result
	//   MOV [0x66c4], AX             ; menu_max_choices  = same
	//   MOV AX, CX                   ; AX = max_line_width (left? rect-width)
	//   MOV BX, DX                   ; BX = total_height (top? rect-height)
	//   PUSH ds; POP ES              ; ES = data segment
	//   MOV DI, 0x40b7               ; DI = formatted-buffer base
	//   MOV [0x66c6], 3              ; palette mode = 3 (text-rect+choices)
	//   MOV [0x6741], 0              ; clear stash flag
	//   JMP SetRectAndApply           ; → 0x3f86 → JMP RunVerbMenuModalLoop.
	const byte *text = static_cast<byte *>(a[0]);
	Logic::FormattedBubble fb = _logic->formatBubbleText(text);
	Logic::ModalState &ms = Log.modalState();
	ms.menuChoiceCount = fb.totalHeight;   // DOS writes formatter AX
	ms.menuMaxChoices  = fb.totalHeight;
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.totalHeight;
	ms.activeEs = 0;                        // C++ has no segment
	ms.activeDi = 0x40b7;                   // formatted-buffer base sentinel
	ms.paletteMode = 3;
	ms.stashFlag = 0;
	debugC(1, kDebugLevelScript, "opcode 0x4e: DrawTextRectWithChoices text='%s' lines=%u h=%u",
		text ? reinterpret_cast<const char *>(text) : "(null)",
		fb.lineCount, fb.totalHeight);
	if (!fb.text.empty()) {
		const byte *out = reinterpret_cast<const byte *>(fb.text.c_str());
		const uint16 length = uint16(fb.text.size());
		Graf.say(out, length, MAX<uint16>(60, 4 * length));
	}
	if (fb.truncated)
		Log.setPendingError(0x11);
	return kThxBye;
}
OPCODE(0x4f) {
	// DOS Op_4f_DrawTextRectWithChoicesAlt @ 1000:3f45:
	//   CALL ResolveOpcodeArg1;      ; AX = text ptr (NOTE: arg1 first!)
	//   MOV DI, AX
	//   CALL FormatBubbleText_FullPath
	//   PUSH AX; PUSH DX             ; save formatter result + height
	//   CALL ResolveOpcodeArg0;      ; AX = arg0 (extra param)
	//   MOV BX, AX
	//   POP DX; POP AX
	//   CALL NoOpStub9bcc            ; (returns AX unchanged in our model)
	//   MOV [0x66c2], AX; MOV [0x66c4], AX
	//   JMP into Op_4e's tail at 0x3f2c (= MOV AX, CX; ... fall through
	//                                    to SetRectAndApply with mode=3,
	//                                    stash=0).
	// = "DrawTextRectWithChoices but using arg1 as text, with arg0 as
	// some extra positioning hint. Final state matches Op_4e."
	const byte *text = static_cast<byte *>(a[1]);
	Logic::FormattedBubble fb = _logic->formatBubbleText(text);
	Logic::ModalState &ms = Log.modalState();
	ms.menuChoiceCount = fb.totalHeight;
	ms.menuMaxChoices  = fb.totalHeight;
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.totalHeight;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.paletteMode = 3;
	ms.stashFlag = 0;
	debugC(1, kDebugLevelScript, "opcode 0x4f: DrawTextRectWithChoicesAlt text='%s' arg0=%s",
		text ? reinterpret_cast<const char *>(text) : "(null)", +a[0]);
	if (!fb.text.empty()) {
		const byte *out = reinterpret_cast<const byte *>(fb.text.c_str());
		const uint16 length = uint16(fb.text.size());
		Graf.say(out, length, MAX<uint16>(60, 4 * length));
	}
	if (fb.truncated)
		Log.setPendingError(0x11);
	return kThxBye;
}
OPCODE(0x50) {
	// DOS Op_50_OpenVerbMenuModal @ 1000:3f61:
	//   CALL ResolveOpcodeArg0;     ; AX = text ptr
	//   MOV DI, AX
	//   CALL FormatBubbleText_FullPath
	//   MOV [0x66c2], AX; MOV [0x66c4], AX
	//   MOV AX, CX; MOV BX, DX
	//   PUSH ds; POP ES; MOV DI, 0x40b7
	//   MOV [0x66c6], 1            ; palette mode = 1 (verb-menu modal)
	//   MOV [0x6741], 1            ; SET stash flag
	//   ; falls through to SetRectAndApply.
	const byte *text = static_cast<byte *>(a[0]);
	Logic::FormattedBubble fb = _logic->formatBubbleText(text);
	Logic::ModalState &ms = Log.modalState();
	ms.menuChoiceCount = fb.totalHeight;
	ms.menuMaxChoices  = fb.totalHeight;
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.totalHeight;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.paletteMode = 1;
	ms.stashFlag = 1;
	debugC(1, kDebugLevelScript, "opcode 0x50: OpenVerbMenuModal text='%s'",
		text ? reinterpret_cast<const char *>(text) : "(null)");
	if (!fb.text.empty()) {
		const byte *out = reinterpret_cast<const byte *>(fb.text.c_str());
		const uint16 length = uint16(fb.text.size());
		Graf.say(out, length, MAX<uint16>(120, 6 * length));
	}
	if (fb.truncated)
		Log.setPendingError(0x11);
	return kThxBye;
}
OPCODE(0x51) {
	// DOS Op_51_OpenVerbMenuModalAlt @ 1000:3f99:
	//   CALL ResolveOpcodeArg1;     ; arg1 = text
	//   MOV DI, AX
	//   CALL FormatBubbleText_FullPath
	//   PUSH AX; PUSH DX
	//   CALL ResolveOpcodeArg0;     ; arg0 = positioning hint
	//   MOV BX, AX
	//   POP DX; POP AX
	//   CALL NoOpStub9bcc
	//   MOV [0x66c2], AX; MOV [0x66c4], AX
	//   JMP 0x3f6f (Op_50's tail: palette=1, stash=1).
	const byte *text = static_cast<byte *>(a[1]);
	Logic::FormattedBubble fb = _logic->formatBubbleText(text);
	Logic::ModalState &ms = Log.modalState();
	ms.menuChoiceCount = fb.totalHeight;
	ms.menuMaxChoices  = fb.totalHeight;
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.totalHeight;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.paletteMode = 1;
	ms.stashFlag = 1;
	debugC(1, kDebugLevelScript, "opcode 0x51: OpenVerbMenuModalAlt text='%s' arg0=%s",
		text ? reinterpret_cast<const char *>(text) : "(null)", +a[0]);
	if (!fb.text.empty()) {
		const byte *out = reinterpret_cast<const byte *>(fb.text.c_str());
		const uint16 length = uint16(fb.text.size());
		Graf.say(out, length, MAX<uint16>(120, 6 * length));
	}
	if (fb.truncated)
		Log.setPendingError(0x11);
	return kThxBye;
}
OPCODE(0x52) {
	// DOS Op_52_DrawFixedTextBubble @ 1000:3ff6:
	//   CALL ResolveOpcodeArg0;     ; AX = text
	//   MOV DI, AX
	//   CALL MeasureVerbBubbleTextHeight @ 0x8eb7
	//                                ; → updates a different metric
	//                                  (uses already-formatted text)
	//   MOV [0x66c6], 2;             ; palette mode = 2 (fixed bubble)
	//   MOV [0x66c2], 0;             ; choice count = 0 (no choices)
	//   MOV [0x6741], 0;             ; clear stash
	//   JMP SetRectAndApply.
	const byte *text = static_cast<byte *>(a[0]);
	Logic::FormattedBubble fb = _logic->formatBubbleText(text);
	Logic::ModalState &ms = Log.modalState();
	ms.menuChoiceCount = 0;       // DOS sets [0x66c2] = 0 explicitly
	// menuMaxChoices is left as previously-set (DOS doesn't write [0x66c4] here)
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.totalHeight;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.paletteMode = 2;
	ms.stashFlag = 0;
	debugC(1, kDebugLevelScript, "opcode 0x52: DrawFixedTextBubble text='%s' h=%u",
		text ? reinterpret_cast<const char *>(text) : "(null)", fb.totalHeight);
	if (!fb.text.empty()) {
		const byte *out = reinterpret_cast<const byte *>(fb.text.c_str());
		const uint16 length = uint16(fb.text.size());
		Graf.say(out, length, MAX<uint16>(60, 3 * length));
	}
	if (fb.truncated)
		Log.setPendingError(0x11);
	return kThxBye;
}
OPCODE(0x53) {
	// DOS Op_53_DrawFixedTextBubbleStashed @ 1000:3fb5:
	//   CALL ResolveOpcodeArg0;     ; AX = text
	//   MOV DI, AX
	//   CMP [0x6741], 0
	//   JZ fallthrough_fixed         ; not stashed → normal bubble (Op_52 path)
	//   ; STASHED PATH:
	//   MOV SI, [0x66be]; MOV [0x66b2], SI  ; saved AX = active AX
	//   MOV SI, [0x66c0]; MOV [0x66b4], SI  ; saved BX = active BX
	//   MOV SI, [0x66bc]; MOV [0x66b0], SI  ; saved ES
	//   MOV SI, [0x66ba]; MOV [0x66ae], SI  ; saved DI
	//   CALL MeasureVerbBubbleTextHeight
	//   MOV [0x66c6], 4              ; palette = 4 (stashed-bubble)
	//   MOV [0x66c2], 0              ; choices = 0
	//   MOV [0x6741], 0              ; clear stash flag
	//   JMP SetRectAndApply
	//   fallthrough_fixed:           ; same as Op_52 but DI already set
	//     CALL MeasureVerbBubbleTextHeight
	//     MOV [0x66c6], 2; MOV [0x66c2], 0; MOV [0x6741], 0
	//     JMP SetRectAndApply
	const byte *text = static_cast<byte *>(a[0]);
	Logic::FormattedBubble fb = _logic->formatBubbleText(text);
	Logic::ModalState &ms = Log.modalState();
	if (ms.stashFlag != 0) {
		// Stash the active modal slot into the saved slot.
		ms.savedAx = ms.activeAx;
		ms.savedBx = ms.activeBx;
		ms.savedEs = ms.activeEs;
		ms.savedDi = ms.activeDi;
		ms.paletteMode = 4;
		ms.menuChoiceCount = 0;
		ms.stashFlag = 0;
		debugC(1, kDebugLevelScript, "opcode 0x53: DrawFixedTextBubbleStashed (STASHED) text='%s'",
			text ? reinterpret_cast<const char *>(text) : "(null)");
	} else {
		// Same as Op_52.
		ms.paletteMode = 2;
		ms.menuChoiceCount = 0;
		ms.stashFlag = 0;
		debugC(1, kDebugLevelScript, "opcode 0x53: DrawFixedTextBubbleStashed (FIXED, no stash) text='%s'",
			text ? reinterpret_cast<const char *>(text) : "(null)");
	}
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.totalHeight;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	if (!fb.text.empty()) {
		const byte *out = reinterpret_cast<const byte *>(fb.text.c_str());
		const uint16 length = uint16(fb.text.size());
		Graf.say(out, length, MAX<uint16>(60, 3 * length));
	}
	if (fb.truncated)
		Log.setPendingError(0x11);
	return kThxBye;
}

// 0x58..0x5e: state-getter family. Each is `MOV BX,[DS:slot]; JMP
// StoreOpcodeArg0Value` — a one-instruction read-and-store. The
// LABELS in Ghidra (Op_58_StoreCursorMode etc.) were AUTO-GENERATED
// and bear no relation to what's actually read. Cross-checked
// against actual disassembly addresses 1000:408f..0x40bd.
OPCODE(0x58) {
	// DOS Op_58 @ 1000:408f: BX = [DS:0x661b] = g_draw_command_count.
	// Count of pending draw commands queued via AddDrawCommand.
	// C++ tracks via Logic::_drawCommandCount; reset per frame and
	// incremented at draw-command queue sites.
	a[0] = Log.drawCommandCount();
	debugC(2, kDebugLevelScript, "opcode 0x58: %s = g_draw_command_count (%u)",
		+a[0], Log.drawCommandCount());
	return kThxBye;
}
OPCODE(0x59) {
	// DOS Op_59 @ 1000:4096: BX = [DS:0x661f] = g_exit_count.
	// = number of exits currently loaded for the active block.
	a[0] = uint16(_logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0);
	debugC(2, kDebugLevelScript, "opcode 0x59: %s = g_exit_count (%u)", +a[0], uint16(a[0]));
	return kThxBye;
}
OPCODE(0x5a) {
	// DOS Op_5a @ 1000:409d: BX = [DS:0x666e] = current entity type.
	a[0] = Log.gameState();
	debugC(2, kDebugLevelScript, "opcode 0x5a: %s = current entity type (%u)", +a[0], Log.gameState());
	return kThxBye;
}
OPCODE(0x5b) {
	// DOS Op_5b @ 1000:40a4: BX = [DS:0x666c] = current-entity-id.
	// Read by entity scripts during dispatch to know which entity
	// "this" script is for. C++ tracks via Logic::_currentEntityId,
	// updated at script-dispatch sites.
	a[0] = Log.currentEntityId();
	debugC(2, kDebugLevelScript, "opcode 0x5b: %s = current-entity-id (%u)",
		+a[0], Log.currentEntityId());
	return kThxBye;
}
OPCODE(0x5c) {
	// DOS Op_5c @ 1000:40ab: BX = [DS:0x6670] = g_game_score.
	a[0] = Log.gameScore();
	debugC(2, kDebugLevelScript, "opcode 0x5c: %s = g_game_score (%u)", +a[0], Log.gameScore());
	return kThxBye;
}
OPCODE(0x5d) {
	// DOS Op_5d @ 1000:40b2: store score percent + tenths.
	//   ComputePercentTenths @ 1000:7e24:
	//     score = g_game_score; max = CS:[0x91];
	//     if score==0 || max==0: BX=0, CX=0;
	//     else: BX = (score*100)/max;        ; integer percent
	//           rem = (score*100)%max;
	//           CX = (rem*10)/max;            ; tenths digit
	//   StoreOpcodeArg0Value(BX);  // write percent to arg0 LHS
	//   WriteVarBySlot_LHS(CX);    // write tenths to arg1 LHS
	const uint16 score = Log.gameScore();
	const uint16 maxScore = Log.maxGameScore();
	uint16 percent = 0;
	uint16 tenths = 0;
	if (score != 0 && maxScore != 0) {
		const uint32 sc100 = uint32(score) * 100;
		percent = uint16(sc100 / maxScore);
		const uint32 rem = sc100 % maxScore;
		if (rem != 0)
			tenths = uint16((rem * 10) / maxScore);
	}
	a[0] = percent;
	a[1] = tenths;
	debugC(2, kDebugLevelScript, "opcode 0x5d: score percent=%u.%u (score=%u/%u)",
		percent, tenths, score, maxScore);
	return kThxBye;
}
OPCODE(0x5e) {
	// DOS Op_5e @ 1000:40bd: BX = [DS:0x667c] = g_drag_target.
	a[0] = Log.dragTarget();
	debugC(2, kDebugLevelScript, "opcode 0x5e: %s = g_drag_target (%u)", +a[0], Log.dragTarget());
	return kThxBye;
}
OPCODE(0x5f) {
	// DOS Op_5f_TableLookupResource @ 1000:40cb:
	//   walk_speed_flag = 0;             ; resource segment
	//   arg1 = search value, arg2 = field offset, arg0 = table ptr,
	//   arg3 = destination LHS.
	//   width_words = arg0[0]; (entry length minus index)
	//   loop entries; if entry[0] matches arg1, BX = entry[2+arg2];
	//   else BX = 0xffff.
	//   WriteVarBySlot3_LHS(BX) → arg3.
	// Sister of Op_60 (same algorithm, different memory bank). The
	// C++ CodePointer abstraction wraps the bank — both Op_5f and
	// Op_60 use the same arg0-resolution which already targets the
	// correct memory area for the calling context.
	uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	byte *base = static_cast<CodePointer &>(a[0]).base();
	uint16 value = 0xffff;
	if (base) {
		byte *pos = base + offset;
		const uint16 width = READ_LE_UINT16(pos);
		pos += 2;
		while (true) {
			const uint16 index = READ_LE_UINT16(pos);
			if (index == 0xffff) break;
			pos += 2;
			if (index == uint16(a[1])) {
				value = READ_LE_UINT16(pos + uint16(a[2]));
				break;
			}
			pos += width * 2;
		}
	}
	a[3] = value;
	debugC(2, kDebugLevelScript, "opcode 0x5f: table lookup arg0=0x%04x search=%s field=%s -> %u",
		offset, +a[1], +a[2], value);
	return kThxBye;
}

// 0x61..0x65: entity-field assign / table-lookup-assign family.
// DOS uses a "the LHS of arg2 was already resolved to an
// entity-record-relative pointer during arg parsing" mechanism that
// the C++ Value system doesn't directly model. The functional effect
// of each: arg2's LHS receives arg1's value (or in 0x64/0x65, the
// matched entry's segment is written to arg2's LHS). C++ implements
// these via the ValueVector's `a[2] = arg1` write, which dispatches
// to the underlying Value's operator= (writes to the variable slot
// when the slot is WordVariable/ByteVariable, no-op for Constant).
OPCODE(0x61) {
	// DOS Op_61_ReadExitField @ 1000:411b:
	//   1. ResolveOpcodeArg0 (exit id);
	//   2. if (id > g_object_count_max) → pending-error 0x13;
	//   3. GetExitOffset(id) → ES:SI;
	//   4. ValidateTypeAndWriteVar2 @ 0x4146:
	//      ResolveOpcodeArg1 → AX (low=offset, high=size);
	//      size==1 → BL = byte ptr ES:[offset + SI];
	//      size==2 → BX = word ptr ES:[offset + SI];
	//      size==4 → BX,CX = dword ptr ES:[offset + SI];
	//      else → pending-error 2;
	//      WriteVarBySlot2_LHS → writes BX (and CX for size 4) to arg2's LHS.
	// = "READ a sized field from the exit record at (arg1.lo) of width
	// (arg1.hi), store in arg2 LHS". STUB: Exit record fields beyond
	// _room/_position aren't first-class; reads of unknown offsets
	// fall through to Logic._exitFields sparse storage (zero-default).
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	uint16 value = 0;
	Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(id) : 0;
	if (exit) {
		if (off == 0) value = exit->room();
		else if (off == 2) value = uint16(exit->position().x);
		else if (off == 4) value = uint16(exit->position().y);
		else value = Log.exitField(id, off) | (sz == 2 ? (uint16(Log.exitField(id, off + 1)) << 8) : 0);
	}
	debugC(2, kDebugLevelScript, "opcode 0x61: ReadExitField id=%u off=0x%02x sz=%u → %u",
		id, off, sz, value);
	a[2] = value;
	return kThxBye;
}
OPCODE(0x62) {
	// DOS Op_62_ReadObjectField @ 1000:412a: same shape as 0x61 with
	// GetObjectOffset and g_persons_count bound. Read object[id]'s
	// sized field at offset (arg1.lo) into arg2 LHS.
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	uint16 value = 0;
	if (off == 0) value = Log.getObjectRoom(id);
	else if (off == 2) value = uint16(Log.getObjectPosX(id));
	else if (off == 4) value = uint16(Log.getObjectPosY(id));
	else value = Log.objectField(id, off) | (sz == 2 ? (uint16(Log.objectField(id, off + 1)) << 8) : 0);
	debugC(2, kDebugLevelScript, "opcode 0x62: ReadObjectField id=%u off=0x%02x sz=%u → %u",
		id, off, sz, value);
	a[2] = value;
	return kThxBye;
}
// Op_64 / Op_65 helper — both opcodes share the same table-iterate +
// match-key + write-segment loop; the only differences are
// (1) which segment value is written (resource vs block), and
// (2) the walk_speed_flag setting before arg resolution. The DOS
// flag is a side-channel that arg-parsing can read; in C++ it's
// captured directly via the segValue parameter.
//
// DOS Op_64 / Op_65 algorithm (1000:418c / 0x4185 → shared body):
//   walk_speed_flag = 0 (Op_64) or 1 (Op_65);
//   ResolveOpcodeArg3, ResolveOpcodeArg1 → CX (search key),
//   ResolveOpcodeArg2 → DX (field offset),
//   ResolveOpcodeArg0 → SI (table base);
//   BX = walk_speed_flag ? g_seg_buffer_e : g_resourceSegment;
//   MOV DS, BX;                            ; switch to source segment
//   DI = [SI];                              ; first word = entry stride (words)
//   SI += 2;
//   if (DI == 0) → pending error 0x1a;
//   DI *= 2;                                ; stride bytes
//   loop:
//     AX = [SI];  SI += 2;                  ; entry's first word = key
//     if (AX == 0xffff) → pending error 0x1a;  ; sentinel before match
//     if (CX == AX) match → goto write;
//     SI += DI;                              ; advance by entry stride
//     loop;
//   write: SI += DX;                          ; SI = entry.field
//          [SI] = BX;                         ; write segment id
//
// C++ algorithm: identical iterate+match. The C++ port writes
// `segValue` into the matched field. `segValue` is taken from the
// DOS-realistic constants below — IUC's runtime DS = 0x1cb5
// (per project memory: "Runtime DS=0x1cb5; globals live in CODE_1
// not CODE_0"), so Op_64 writes 0x1cb5 to literally match DOS
// `g_resourceSegment` for IUC's standard layout. Op_65 writes
// `0x4000 + currentBlock` so each block has a distinct, predictable
// "segment id" value mirroring DOS's per-block dynamic seg
// allocation. Both values are non-zero → scripts checking "is bound"
// see the resource as bound; scripts comparing against 0x1cb5
// specifically (Op_64) get a literal DOS-segment-value match.
static void doTableLookupAssign(Logic *logic, ValueVector &a, uint16 segValue,
                                const char *opname, uint8 dbgOpcode) {
	uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	byte *base = static_cast<CodePointer &>(a[0]).base();
	if (!base) {
		logic->setPendingError(0x1a);
		return;
	}
	byte *pos = base + offset;
	const uint16 width = READ_LE_UINT16(pos);
	if (width == 0) {
		logic->setPendingError(0x1a);
		return;
	}
	pos += 2;
	bool matched = false;
	while (true) {
		const uint16 index = READ_LE_UINT16(pos);
		if (index == 0xffff) break;
		pos += 2;
		if (index == uint16(a[1])) {
			WRITE_LE_UINT16(pos + uint16(a[2]), segValue);
			matched = true;
			break;
		}
		pos += width * 2;
	}
	if (!matched)
		logic->setPendingError(0x1a);
	debugC(2, kDebugLevelScript, "opcode 0x%02x: %s (table @ 0x%04x search=%s segValue=0x%04x match=%d)",
		dbgOpcode, opname, offset, +a[1], segValue, int(matched));
}

OPCODE(0x64) {
	// DOS Op_64_TableLookupAssignMain @ 1000:418c. Writes
	// g_resourceSegment (DOS DS register, = IUC runtime data segment
	// 0x1cb5) into the matched table entry's field-at-arg2 offset.
	// Scripts later read this field as "is this resource bound to
	// the main bank?" — non-zero answer = bound; specific 0x1cb5 =
	// "main bank" tag matches DOS literally.
	doTableLookupAssign(_logic, a, /* segValue = */ 0x1cb5,
		"TableLookupAssignMain", 0x64);
	return kThxBye;
}
OPCODE(0x65) {
	// DOS Op_65_TableLookupAssignBlock @ 1000:4185. Writes
	// g_seg_buffer_e (DOS dynamic per-block buffer segment) into the
	// matched table entry. C++ uses `0x4000 + currentBlock` so each
	// block has a distinct, stable, non-zero "segment id". This
	// matches DOS's per-block-distinct value semantics; the absolute
	// value differs but scripts checking "is this resource bound
	// AND in the current block" see the same yes/no.
	const uint16 blockSeg = uint16(0x4000 + (Log.currentBlock() & 0x3fff));
	doTableLookupAssign(_logic, a, /* segValue = */ blockSeg,
		"TableLookupAssignBlock", 0x65);
	return kThxBye;
}
OPCODE(0x66) {
	// DOS Op_66_WriteExitFieldSized @ 1000:41ee:
	//   if arg0 > g_object_count_max → pending-error 0x13;
	//   else GetExitOffset(arg0); WriteSizedFieldAtSi:
	//     arg1 = (size << 8) | offset; arg2 = value.
	//     size==1 byte; size==2 word; size==4 dword.
	// Exit field offsets in C++ Exit class (per kOffset* enums):
	//   0 = _room (word), 2 = _position (word*2), 6 = _sprite (word) etc.
	// For now, support writes to the exit's room field (offset 0,
	// size 2). Other fields warn and skip (rule-2 documented gap
	// pending Exit data-model extension).
	const uint16 id = uint16(a[0]);
	const uint16 fieldEnc = uint16(a[1]);
	const uint16 value = uint16(a[2]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(id) : 0;
	if (!exit) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	if (off == 0 && sz == 2) {
		exit->setRoom(value);
		debugC(2, kDebugLevelScript, "opcode 0x66: exit[%s].room = %s", +a[0], +a[2]);
	} else if (sz == 1) {
		Log.setExitField(id, off, uint8(value & 0xff));
		debugC(2, kDebugLevelScript, "opcode 0x66: exit[%u].field[+0x%02x] = %u (sparse byte)",
			id, off, value & 0xff);
	} else if (sz == 2) {
		Log.setExitField(id, off, uint8(value & 0xff));
		Log.setExitField(id, uint8(off + 1), uint8(value >> 8));
		debugC(2, kDebugLevelScript, "opcode 0x66: exit[%u].field[+0x%02x] = %u (sparse word)",
			id, off, value);
	} else {
		// sz == 4 not supported — DOS uses BX leftover from prior arg resolve.
		Log.setPendingError(0x02);
	}
	return kThxBye;
}
OPCODE(0x67) {
	// DOS Op_67_WriteObjectFieldSized @ 1000:41fe: same shape as 0x66
	// but via GetObjectOffset and g_persons_count bound. Object record
	// fields (per data-file layout): 0 = room (word), 2 = x (word),
	// 4 = y (word). C++ stores these in Logic._objectRoom/Pos*.
	const uint16 id = uint16(a[0]);
	const uint16 fieldEnc = uint16(a[1]);
	const uint16 value = uint16(a[2]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x67: object[%u].field[+0x%02x size=%u] = %u",
		id, off, sz, value);
	if (off == 0 && sz == 2) {
		Log.setObjectRoom(id, value);
	} else if (off == 2 && sz == 2) {
		Log.setObjectPosition(id, int16(value), Log.getObjectPosY(id));
	} else if (off == 4 && sz == 2) {
		Log.setObjectPosition(id, Log.getObjectPosX(id), int16(value));
	} else if (sz == 1) {
		Log.setObjectField(id, off, uint8(value & 0xff));
	} else if (sz == 2) {
		// Sparse 2-byte write: store low/high bytes at off and off+1.
		Log.setObjectField(id, off, uint8(value & 0xff));
		Log.setObjectField(id, uint8(off + 1), uint8(value >> 8));
	} else {
		// sz == 4 not supported (DOS uses BX leftover from prior arg
		// resolve). Mirror DOS error.
		Log.setPendingError(0x02);
	}
	return kThxBye;
}
OPCODE(0x68) {
	// DOS Op_68_WriteActorFieldSized @ 1000:4211:
	//   if arg0 > g_anim_count_max → pending-error 0x13;
	//   GetActorOffset(arg0); resolve arg2 (value), arg1 (field+size);
	//   if size==1: actor[off] = (byte)value;
	//   if size==2: actor[off..off+1] = value;
	//   if size==4: actor[off..off+3] = value (high word from BX leftover);
	//   else pending-error 2.
	const uint16 id = uint16(a[0]);
	const uint16 fieldEnc = uint16(a[1]);
	const uint16 value = uint16(a[2]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	if (sz == 1) {
		ac->setDosField(off, uint8(value & 0xff));
	} else if (sz == 2) {
		ac->setDosField(off, uint8(value & 0xff));
		ac->setDosField(uint8(off + 1), uint8(value >> 8));
	} else if (sz == 4) {
		// 4-byte writes use BX leftover from prior arg resolution
		// in DOS — C++ doesn't track that high word. Raise pending
		// error 2 like DOS for unsupported size on the C++ side.
		Log.setPendingError(0x02);
	} else {
		Log.setPendingError(0x02);
	}
	debugC(2, kDebugLevelScript, "opcode 0x68: actor[%s].field[%u] = %u (size=%u)",
		+a[0], off, value, sz);
	return kThxBye;
}
OPCODE(0x69) {
	// DOS Op_69_SetCellBitDefault (CS:0x425c): 1 arg. Sets BIT 1 (the "default
	// bit") on cellByte[a[0]]. The original ScummVM port misclassified this as
	// 4-arg clamp(), which read 3 garbage Value slots past the end of the
	// dispatched arg vector.
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x69: set cell bit 1 (default) on entity %s",
		+a[0]);
	Log.setCellBit(id, 1);
	return kThxBye;
}
OPCODE(0x6a) {
	// DOS Op_6a_SetCellBit @ 1000:4261: 2 args.
	//   if (arg1 > 7) pending-error 0x15;
	//   if (arg0 > g_object_count_max) pending-error 0x14;
	//   sets bit `arg1+1` of the 9-bit value (id<8 | cell_byte) =
	//   cell_byte bit `arg1` (LSB-indexed). HashMap bounds-safe in C++.
	const uint16 raw = uint16(a[1]);
	if (raw > 7) {
		Log.setPendingError(0x15);
		return kThxBye;
	}
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x6a: set cell bit %u on entity %s", raw, +a[0]);
	Log.setCellBit(id, uint8(raw));
	return kThxBye;
}
OPCODE(0x6b) {
	// DOS Op_6b_ClearCellBit @ 1000:4281: 2 args. Same bound checks
	// as 0x6a, then clears bit `arg1` of cellByte[arg0].
	const uint16 raw = uint16(a[1]);
	if (raw > 7) {
		Log.setPendingError(0x15);
		return kThxBye;
	}
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x6b: clear cell bit %u on entity %s", raw, +a[0]);
	Log.clearCellBit(id, uint8(raw));
	return kThxBye;
}

OPCODE(0x75) {
	// Reset cursor to default. DOS handler at CS:0x4313 calls SetCursorMode(0).
	debugC(2, kDebugLevelScript, "opcode 0x75: reset cursor");
	Log.setCursorMode(0);
	return kThxBye;
}
OPCODE(0x76) {
	// DOS Op_76_BeginDragWithTarget @ 1000:4325:
	//   pbRam000231ce = arg0;       ; g_drag_target_mode40 = arg0
	//   _g_drag_step_idx = 0;
	//   _g_cursor_mode = 0x40;      ; HARDCODED 0x40 (not arg0!)
	//   g_flag_step_pending = 0;
	// Old C++ set cursor=arg0 — WRONG. Op_76 always enters cursor
	// mode 0x40 and stores arg0 as the drag target for Op_0b's
	// later check.
	debugC(2, kDebugLevelScript, "opcode 0x76: begin drag mode=0x40 target=%s", +a[0]);
	Log.setDragTargetMode40(uint16(a[0]));
	Log.setCursorMode(0x40);
	Log.setStepPending(false);
	return kThxBye;
}
OPCODE(0x78) {
	// DOS Op_78_CheckActorAnimReadyAlt @ 1000:4359:
	//   CheckActorAnimReady(g_main_character_id);
	//   if (!CF) yield (RegisterSampleSlot_LoadDefaultsAndMark);
	//   else: GetActorOffset(main_char), place protagonist:
	//     actor.field+0x61 = arg1 (current frame)
	//     actor.field+0x62 = arg2 (target frame)
	//     actor.field+0x59 = arg0 (room)
	//     g_current_location = arg0 (room transition)
	//     uRam00023159 = arg2 (rendering hint)
	// Args: arg0=room, arg1=current frame, arg2=target frame.
	// C++: always-ready model (Actor::isMoving stub = false), so
	// place + change room directly.
	debugC(2, kDebugLevelScript, "opcode 0x78: go to room %s frame curr=%s target=%s",
		+a[0], +a[1], +a[2]);
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag) return kThxBye;
	protag->placeIn(uint16(a[0]), uint16(a[1]), uint16(a[2]));
	_logic->changeRoom(uint16(a[0]));
	return kThxBye;
}
OPCODE(0x7a) {
	// DOS Op_7a_PlaceActorInRoomXY (CS:0x4443). nargs=4 per dispatch
	// table. Same shape as Op_79 but with a separate target frame:
	//   a[0] = actor id
	//   a[1] = room
	//   a[2] = current frame (-> field+0x61)
	//   a[3] = target frame  (-> field+0x62)
	// DOS sequence: UnregisterActor, set fields, SetActorPosition (X/Y
	// from frame[a[2]]), FindPlaceById, InitActorState. If the new room
	// matches g_current_location and target!=current, MoveActorToTargetExit.
	// Previous C++ was a logging stub mislabelled as "deactivate object".
	debugC(1, kDebugLevelScript, "opcode 0x7a: place actor %s in room %s frame %s target %s",
		+a[0], +a[1], +a[2], +a[3]);
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *ac = _logic->getActor(a[0]))
		ac->placeIn(uint16(a[1]), uint16(a[2]), uint16(a[3]));
	return kThxBye;
}
OPCODE(0x7d) {
	// DOS Op_7d_MoveObjectFlag1 @ 1000:4493:
	//   ResolveOpcodeArg0; DisableObjectFlag1 (clear arg0's bit 0);
	//   ResolveOpcodeArg1; if arg1 > max → error 0x14;
	//     else EnableObjectFlag1 (set arg1's bit 0).
	const uint16 src = uint16(a[0]);
	const uint16 dst = uint16(a[1]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (dst > personsCount) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7d: move cell bit 0: %u → %u", src, dst);
	Log.clearCellBit(src, 0);
	Log.setCellBit(dst, 0);
	return kThxBye;
}
OPCODE(0x7e) {
	// DOS Op_7e_QueueOverlay @ 1000:44a8:
	//   arg0 = entity type (1=exit, 2=object, 3=actor);
	//   arg1 = entity id;
	//   look up (sprite, x, y):
	//     type 1 (exit):  GetExitOffset; sprite=[+6], x=[+2], y=[+4]
	//     type 2 (object): GetObjectOffset; sprite=[+6], x=[+2], y=[+4]
	//     type 3 (actor): GetActorOffset; sprite=[+8], x=[+4], y=[+6]
	//     else: pending-error 0x34.
	//   push (sprite, x, y) to overlay queue at DS:0x37b7. Cap 250
	//   entries (counter at DS:0x6621). Overflow → pending-error 0x35.
	//   set g_flag_misc_3 = 1, call DrawBackdropTile (immediate draw).
	const uint16 type = uint16(a[0]);
	const uint16 id = uint16(a[1]);
	uint16 sprite = 0;
	int16 x = 0, y = 0;
	switch (type) {
	case 1: { // exit
		Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(id) : 0;
		if (exit) {
			x = int16(exit->position().x);
			y = int16(exit->position().y);
			// exit sprite id not directly exposed — use room as proxy
			sprite = exit->room();
		}
		break;
	}
	case 2: { // object
		x = Log.getObjectPosX(id);
		y = Log.getObjectPosY(id);
		sprite = Log.getObjectRoom(id);
		break;
	}
	case 3: { // actor
		if (Actor *ac = Log.getActor(id)) {
			x = int16(ac->position().x);
			y = int16(ac->position().y);
			sprite = ac->frameId();
		}
		break;
	}
	default:
		Log.setPendingError(0x34);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7e: queue overlay type=%u id=%u sprite=%u pos=%d,%d",
		type, id, sprite, x, y);
	if (!Log.overlayQueuePush(sprite, x, y)) {
		Log.setPendingError(0x35);
		return kThxBye;
	}
	return kThxBye;
}
OPCODE(0x7f) {
	// DOS Op_7f_PlaceObjectInRoom (CS:0x452f): set Object[a[0]].room = a[1],
	// Object[a[0]].position = -1, Object[a[0]].field4 = 0. Marks logic dirty.
	// Used both to place an object in a scene AND to add it to the player's
	// inventory (room == kInventoryRoom). Op_18 / Op_1b / Op_21 read this.
	const uint16 id = uint16(a[0]);
	const uint16 room = uint16(a[1]);
	debugC(1, kDebugLevelScript, "opcode 0x7f: place object %s in room %s", +a[0], +a[1]);
	Log.setObjectRoom(id, room);
	return kThxBye;
}

// 0x80..0x94: Object placement / hotspot manipulation. iter-20 audit per
// opcodes_nargs.data discovered SEVEN OOB-read bugs in this range — the
// C++ opcode bodies were accessing args past the count the dispatcher
// fetches, reading garbage from past the end of the ValueVector. All
// fixed below: each handler now respects its declared nargs.
OPCODE(0x80) {
	// DOS Op_80_handler @ 1000:457f:
	//   if (arg0 > g_persons_count) pending-error 0x16;
	//   else GetObjectOffset(arg0); object[+0] = arg1 (room),
	//        object[+2] = arg2 (x), object[+4] = arg3 (y);
	//        if room == 0xffff: AddExitToList + pending-error 0x21;
	//        else ClampSpriteOnScreen + g_flag_logic_dirty = 1;
	//        g_flag_misc_1 = 1.
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const uint16 room = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0x80: place object %s at room %s pos %sx%s",
		+a[0], +a[1], +a[2], +a[3]);
	Log.setObjectRoom(id, room);
	Log.setObjectPosition(id, int16(uint16(a[2])), int16(uint16(a[3])));
	if (room == 0xffff)
		Log.setPendingError(0x21);
	return kThxBye;
}
OPCODE(0x81) {
	// DOS Op_81 @ 1000:45ce: same as Op_80 but room = current_location.
	// nargs=3 (id, x, y).
	debugC(2, kDebugLevelScript, "opcode 0x81: place object %s at current room pos %s,%s",
		+a[0], +a[1], +a[2]);
	const uint16 id = uint16(a[0]);
	Log.setObjectRoom(id, Log.currentRoom());
	Log.setObjectPosition(id, int16(uint16(a[1])), int16(uint16(a[2])));
	return kThxBye;
}
OPCODE(0x82) {
	// DOS Op_82_handler @ 1000:45f0: SWAP two objects' first 3 fields
	// (room, x, y) atomically. Bound checks both ids; if either is
	// the drag target → PrepareDragInteraction. If either object's
	// room is -1 (unplaced) → RemapEntityRefById to fix references.
	const uint16 a0 = uint16(a[0]);
	const uint16 b0 = uint16(a[1]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (a0 > personsCount || b0 > personsCount) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const uint16 ra = Log.getObjectRoom(a0);
	const uint16 rb = Log.getObjectRoom(b0);
	const int16 xa = Log.getObjectPosX(a0);
	const int16 ya = Log.getObjectPosY(a0);
	const int16 xb = Log.getObjectPosX(b0);
	const int16 yb = Log.getObjectPosY(b0);
	debugC(2, kDebugLevelScript, "opcode 0x82: swap objects %u<->%u (room+pos)", a0, b0);
	Log.setObjectRoom(a0, rb);
	Log.setObjectRoom(b0, ra);
	Log.setObjectPosition(a0, xb, yb);
	Log.setObjectPosition(b0, xa, ya);
	return kThxBye;
}
OPCODE(0x83) {
	// DOS Op_83_handler @ 1000:4684:
	//   bound-check arg0 (object id); if drag target → drag handling;
	//   else: ensure both arg0 and arg1 are placed (remap unplaced
	//   refs); then write arg1.x/y to arg1.x/y (no-op trip) and zero
	//   arg1.room. Net effect: object arg1's room cleared (= missing).
	const uint16 a0 = uint16(a[0]);
	const uint16 a1 = uint16(a[1]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (a0 > personsCount || a1 > personsCount) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x83: clear object %u room (context obj %u)", a1, a0);
	Log.setObjectRoom(a1, 0);
	return kThxBye;
}
OPCODE(0x84) {
	// DOS Op_84_handler @ 1000:4703:
	//   if (arg0 == 0) Op_8e (UnregisterActor); return;
	//   if (arg0 > persons_count) pending-error 0x16;
	//   else:
	//     if (cursor==0x20) ResetObjectAtActorPosition;
	//     g_drag_target = arg0;
	//     GetObjectOffset(arg0); if (obj.room != current_loc &&
	//       obj.room != -1) obj.x/y = camera+offset;
	//     BeginDrag_AfterRemoveExit.
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x84: begin drag with object %u (movePersonToActor)", id);
	Log.movePersonToActor(id);
	return kThxBye;
}
OPCODE(0x85) {
	// DOS Op_85 (CS:0x4762): SEARCH for first object whose room == arg0,
	// write its 1-based index to a[1] (destination var slot). 2 args.
	// Was previously claiming "place exit" with 4 args and OOB-reading
	// a[2]/a[3].
	const uint16 searchRoom = uint16(a[0]);
	uint16 found = 0;
	const uint16 personsCount = Log.engine()->resources()->mainDat()->personsCount();
	for (uint16 i = 1; i <= personsCount; ++i) {
		if (Log.getObjectRoom(i) == searchRoom) {
			found = i;
			break;
		}
	}
	debugC(2, kDebugLevelScript, "opcode 0x85: find object in room %u → id %u (writing to %s)",
		searchRoom, found, +a[1]);
	a[1] = found;
	return kThxBye;
}
OPCODE(0x86) {
	// DOS Op_86_handler @ 1000:4789:
	//   start = arg0 ? arg0 : 1;
	//   if (start > persons_count) write start to arg1 LHS, return;
	//   for (id = start; id <= persons_count && obj[id].room != arg2; id++);
	//   write id to arg1 LHS.
	// = "find next object (starting id arg0) with room == arg2".
	const uint16 startId = uint16(a[0]) == 0 ? 1 : uint16(a[0]);
	const uint16 searchRoom = uint16(a[2]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	uint16 found = startId;
	if (startId <= personsCount) {
		while (found <= personsCount) {
			if (Log.getObjectRoom(found) == searchRoom)
				break;
			found++;
		}
	}
	a[1] = found;
	debugC(2, kDebugLevelScript, "opcode 0x86: search obj room=%u from %u → id=%u", searchRoom, startId, found);
	return kThxBye;
}
OPCODE(0x87) {
	// nargs=0 — was OOB-reading a[0]. DOS does some sub-action with no
	// script args.
	debugC(2, kDebugLevelScript, "opcode 0x87: exit/object sub-action (no args) STUB");
	return kThxBye;
}
OPCODE(0x88) {
	// DOS Op_88_handler @ 1000:47bd:
	//   RetEmpty;  arg0 = ResolveOpcodeArg0;
	//   if (arg0 == g_drag_target):
	//     RetEmpty;  result = HandleHotspotInteraction();
	//     if (result != 0): PauseAndLockCursor; return;
	//   else:
	//     if (arg0 > g_persons_count): pending error 0x16; return;
	//     result = HandleHotspotInteraction();
	//     if (result != 0): g_flag_misc_1 = 1; return;
	//   pending error 0x25.
	// HandleHotspotInteraction (1000:3353): looks up the object's
	// click handler (FindHotspotByPoint / FindHotspotByCursor),
	// invokes its bytecode, returns 0 on no-handler / failure.
	//
	// C++ port: "object has a registered hotspot" maps to
	// `_objectRoom[id] != 0xffff` (the click handler iterates by
	// room match). We treat the in-engine click-handler dispatch as
	// "set hit target so downstream Op_13/0x59 see it" — the actual
	// click-handler script runs through the normal EventManager path.
	const uint16 id = uint16(a[0]);
	const bool isDragTarget = (id == Log.dragTarget());
	if (!isDragTarget) {
		if (id > _logic->resources()->mainDat()->personsCount()) {
			Log.setPendingError(0x16);
			return kThxBye;
		}
	}
	// Approximate "HandleHotspotInteraction returned non-zero" =
	// "object id is bound to a click handler in the current scene".
	// In our model, the proxy is `obj.room != 0xffff` (registered) OR
	// drag-target (id matches the drag in flight).
	const bool registered = isDragTarget || Log.getObjectRoom(id) != 0xffff;
	if (!registered) {
		Log.setPendingError(0x25);
		debugC(2, kDebugLevelScript, "opcode 0x88: hotspot interaction object %u → not registered (pending 0x25)", id);
		return kThxBye;
	}
	Log.setHitTarget(id);
	debugC(2, kDebugLevelScript, "opcode 0x88: hotspot interaction object %u (drag=%u) → hit", id, Log.dragTarget());
	return kThxBye;
}
OPCODE(0x89) {
	// DOS Op_89_handler @ 1000:47f7:
	//   bound-check arg0; obj[arg0].room = arg1; obj[arg0].x = -1;
	//   obj[arg0].y = -1; if (arg0 == drag_target) PauseAndLockCursor.
	// = "place object in room, mark position as 'sentinel' (-1,-1)".
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x89: place object %u in room %s (sentinel pos)", id, +a[1]);
	Log.setObjectRoom(id, uint16(a[1]));
	Log.setObjectPosition(id, -1, -1);
	return kThxBye;
}
OPCODE(0x8a) {
	// DOS Op_8a_handler @ 1000:47e6: 3-arg variant of Op_88. Same
	// HandleHotspotInteraction dispatch; arg1/arg2 are passed through
	// to the click handler context (cursor coords).
	const uint16 id = uint16(a[0]);
	const bool isDragTarget = (id == Log.dragTarget());
	if (!isDragTarget) {
		if (id > _logic->resources()->mainDat()->personsCount()) {
			Log.setPendingError(0x16);
			return kThxBye;
		}
	}
	const bool registered = isDragTarget || Log.getObjectRoom(id) != 0xffff;
	if (!registered) {
		Log.setPendingError(0x25);
		debugC(2, kDebugLevelScript, "opcode 0x8a: hotspot interaction object %u (3-arg) → not registered (pending 0x25)", id);
		return kThxBye;
	}
	Log.setHitTarget(id);
	debugC(2, kDebugLevelScript, "opcode 0x8a: hotspot interaction object %u (3-arg) → hit", id);
	return kThxBye;
}
OPCODE(0x8b) {
	// DOS Op_8b_handler @ 1000:482e: 0 args.
	//   ResetObjectAtActorPosition(g_drag_target);  // place currently-
	//                                                  dragged obj at
	//                                                  actor's spot
	//   Op_8e (cursor=0, drag=0).                  // unregister
	const uint16 dragId = Log.dragTarget();
	if (dragId != 0)
		Log.resetObjectAtActorPosition(dragId);
	Log.setCursorMode(0);
	Log.setDragTarget(0);
	debugC(2, kDebugLevelScript, "opcode 0x8b: reset drag obj %u at actor pos + unregister", dragId);
	return kThxBye;
}
OPCODE(0x8c) {
	// DOS Op_8c_handler @ 1000:48c4:
	//   if (arg0 == drag_target) → Op_8b_handler (reset + unregister);
	//   else: GetObjectOffset(arg0); if obj.room != -1
	//         → ResetObjectAtActorPosition(arg0).
	const uint16 id = uint16(a[0]);
	if (id == Log.dragTarget()) {
		debugC(2, kDebugLevelScript, "opcode 0x8c: drag target %u → reset + unregister", id);
		if (id != 0)
			Log.resetObjectAtActorPosition(id);
		Log.setCursorMode(0);
		Log.setDragTarget(0);
		return kThxBye;
	}
	if (Log.getObjectRoom(id) != 0xffff) {
		debugC(2, kDebugLevelScript, "opcode 0x8c: reset object %u at actor pos", id);
		Log.resetObjectAtActorPosition(id);
	} else {
		debugC(2, kDebugLevelScript, "opcode 0x8c: object %u room=-1, skip reset", id);
	}
	return kThxBye;
}
OPCODE(0x8d) {
	// DOS Op_8d_handler @ 1000:48df:
	//   ResolveOpcodeArg0; GetObjectOffset(id) → ES:SI;
	//   uVar5 = (obj.room != -1);
	//   if (obj.room == -1) RemoveExitFromList;     // was an exit
	//   AddExitToList;                               // re-register
	//   if (!uVar5):                                  // was -1 (= exit)
	//     ResolveOpcodeArg1, ResolveOpcodeArg2;
	//     obj.room = -1;
	//     CalcSpriteOffsetInGraphic;
	//     obj.x = arg1 + sprite_x_off;
	//     obj.y = arg2 + sprite_y_off;
	//     ClampSpriteOnScreen;
	//     // mark dirty
	//   else: pending error 0x21.
	// = "register object as an exit (hotspot) at (arg1, arg2). Fails
	// (pending-error 0x21) if the object is already placed in a
	// regular room — you can't repurpose a placed obj as an exit".
	//
	// C++ port: "0xffff in _objectRoom" maps to DOS "-1 = is-exit /
	// available-for-exit-registration" (the C++ port has no separate
	// "is in exit list" state; click handler treats obj-in-current-room
	// as the hotspot). RemoveExitFromList / AddExitToList have no
	// dynamic-list analog (exits are loaded statically per block);
	// the script-observable effect is captured by setObjectRoom +
	// setObjectPosition. Sprite offset / clamp are renderer concerns.
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const uint16 oldRoom = Log.getObjectRoom(id);
	if (oldRoom != 0xffff) {
		Log.setPendingError(0x21);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x8d: register obj %u as exit at pos %s,%s",
		id, +a[1], +a[2]);
	Log.setObjectRoom(id, Log.currentRoom());
	Log.setObjectPosition(id, int16(uint16(a[1])), int16(uint16(a[2])));
	return kThxBye;
}
OPCODE(0x8e) {
	// DOS Op_8e @ 1000:490e: 0 args. Sets g_flag_paused=1,
	// g_flag_misc_1=1, SetCursorMode(0), g_drag_target=0.
	// = "unregister current drag/cursor interaction".
	debugC(2, kDebugLevelScript, "opcode 0x8e: unregister actor / drag");
	Log.setCursorMode(0);
	Log.setDragTarget(0);
	return kThxBye;
}
OPCODE(0x8f) {
	// DOS Op_8f_handler @ 1000:4925:
	//   if (g_game_state != 1): pending error 0xe;
	//   else JMP trampoline @ 0x49df with
	//        AX = [0x666c] (currentEntityId), BX = arg0, CX = 0.
	// Trampoline @ 0x49df (executed inline, NOT post-move):
	//   PUSH CX; PUSH BX;
	//   CALL DisableObjectFlag1(AX = currentEntityId);
	//   POP AX; CALL MovePersonToActor(AX = arg0);
	//   POP AX; if (AX != 0) JMP EnableObjectFlag1(arg1).
	// = clearCellBit(currentEntityId) + movePersonToActor(arg0).
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	Log.clearCellBit(Log.currentEntityId(), 0);
	Log.movePersonToActor(uint16(a[0]));
	debugC(2, kDebugLevelScript, "opcode 0x8f: disable cell %u + movePersonToActor %s",
		Log.currentEntityId(), +a[0]);
	return kThxBye;
}
OPCODE(0x90) {
	// DOS Op_90_handler @ 1000:4941: 2-arg variant of Op_8f. Same
	// trampoline at 0x49df but CX = arg1 → trampoline runs
	// EnableObjectFlag1(arg1) after move when arg1 != 0.
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	Log.clearCellBit(Log.currentEntityId(), 0);
	Log.movePersonToActor(uint16(a[0]));
	if (uint16(a[1]) != 0)
		Log.setCellBit(uint16(a[1]), 0);
	debugC(2, kDebugLevelScript, "opcode 0x90: disable cell %u + movePersonToActor %s + enable cell %s",
		Log.currentEntityId(), +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0x91) {
	// DOS Op_91_handler @ 1000:4960: gate (g_flag_step_pending +
	// g_cursor_mode==1). game==1 →
	//   CALL SendActorToTarget(arg0)            — start walk;
	//   SetActorTarget(BP=cs:[0xbb], AX=cs:[0x10f]) — actor.field+0x69
	//                                                callback addr;
	//   SetPostMoveCallback(BP=0x49df, BX=arg0, CX=0,
	//                       AX=currentEntityId) — fires when actor's
	//                                              frame == target.
	// Trampoline @ 0x49df: DisableObjectFlag1(currentEntityId) +
	// MovePersonToActor(arg0).
	// else g_pendingErrorCode = 0xe. C++ uses Logic::PostMoveCallback
	// (kDisableMoveOptionalEnable kind) — fires when protagonist's
	// _framequeue empties (Actor::isMoving() goes false).
	if (!Log.stepPending() || Log.cursorMode() != 1)
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	sendProtagToTarget(_logic, uint16(a[0]));
	Log.setPostMoveCallback(
		Logic::PostMoveCallback::kDisableMoveOptionalEnable,
		Log.currentEntityId(),  // cellId (DOS AX)
		uint16(a[0]),            // arg0 (DOS BX)
		0                        // arg1 (DOS CX = 0 → no enable)
	);
	debugC(2, kDebugLevelScript, "opcode 0x91: walk %s + arm post-move callback (cellId=%u)",
		+a[0], Log.currentEntityId());
	return kThxBye;
}
OPCODE(0x92) {
	// DOS Op_92_handler @ 1000:499e: 2-arg variant of Op_91 (CX = arg1
	// → trampoline branches to EnableObjectFlag1(arg1) after move).
	if (!Log.stepPending() || Log.cursorMode() != 1)
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	sendProtagToTarget(_logic, uint16(a[0]));
	Log.setPostMoveCallback(
		Logic::PostMoveCallback::kDisableMoveOptionalEnable,
		Log.currentEntityId(),
		uint16(a[0]),
		uint16(a[1])  // CX = arg1 → fire EnableObjectFlag1(arg1) after move
	);
	debugC(2, kDebugLevelScript, "opcode 0x92: walk %s + arm post-move callback (cellId=%u enable=%s)",
		+a[0], Log.currentEntityId(), +a[1]);
	return kThxBye;
}
OPCODE(0x93) {
	// DOS Op_93_handler @ 1000:49f1: gate (step + cursor==0x20 + arg0 ==
	// g_drag_target). game==1 →
	//   CALL SendActorToTarget(arg0);
	//   SetActorTarget(BP=cs:[0xbd], AX=cs:[0x10f]);
	//   SetPostMoveCallback(BP=0x4a36, BX=arg1, AX=currentEntityId).
	// Trampoline @ 0x4a36: DisableObjectFlag1(currentEntityId) +
	// EnableObjectFlag1(arg1) + Op_8e (cursor=0, drag=0).
	// else g_pendingErrorCode = 0xf.
	if (!Log.stepPending() || Log.cursorMode() != 0x20)
		return kThxBye;
	if (uint16(a[0]) != Log.dragTarget())
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0f);
		return kThxBye;
	}
	sendProtagToTarget(_logic, uint16(a[0]));
	Log.setPostMoveCallback(
		Logic::PostMoveCallback::kDisableEnableUnregister,
		Log.currentEntityId(),
		uint16(a[0]),    // arg0 (unused by the kDisableEnableUnregister handler but kept for trace)
		uint16(a[1])     // arg1 = cell to enable after disable
	);
	debugC(2, kDebugLevelScript, "opcode 0x93: walk %s + arm post-move drag-swap (cellId=%u enable=%s)",
		+a[0], Log.currentEntityId(), +a[1]);
	return kThxBye;
}
OPCODE(0x94) {
	// DOS Op_94_handler @ 1000:4a41: 0 args. Just sets
	// g_flag_misc_1 = 1 and g_flag_logic_dirty = 1. Repaint trigger.
	debugC(2, kDebugLevelScript, "opcode 0x94: mark logic dirty");
	return kThxBye;
}

OPCODE(0x97) {
	// DOS Op_97_BackupCutscenePCState @ 1000:4a5d:
	//   GetActorOffset(g_main_character_id) → ES:SI;
	//   [0x5ef1] = [0x6609];                  // target frame mirror
	//   [0x5ee9] = ES:[SI+0x69];               // walk callback (word)
	//   [0x5ef2] = ES:[SI+0x62];               // walk-step state (byte)
	//   [0x5ef3] = ES:[SI+0x67];               // walk-callback flag (byte)
	//   ES:[SI+0x67] = 0; ES:[SI+0x6b] = 0; ES:[SI+0x62] = 0;
	//   memcpy([0x5ef4], [0x65ab], 20);        // post_callback_ptr block
	//   [0x65ab] = 0; [0x5f08] = 0;
	//   for slot in g_speech_slots[6]:         // find main char's slot
	//     if (slot.frames_left != 0 && slot.owner == main_char) {
	//        memcpy([0x5f08], slot, 17); slot.owner = 0xffff; break;
	//     }
	//   for slot in g_room_script_slots[19]:    // find main char's room script
	//     if (slot[0] != 0 && slot[4] == main_char && slot[6] == 0) {
	//        [0x5eed] = slot[0]; [0x5eef] = slot[2];
	//        slot[0] = 0; [0x5eeb] = index; break;
	//     }
	//   g_break_inner = 1.
	//
	// C++: capture the modeled state subset on Logic::_cutsceneBackup.
	// Speech-slot pool (DOS [0x4e63..]) is replaced by per-actor
	// Actor::_speech in the C++ port; the protag's slot is captured
	// directly via Actor::speechText() / stopSpeaking(). The
	// room-script-slot pool (DOS [0x5471..]) has no C++ analog.
	Actor *protag = Log.protagonist();
	if (!protag) {
		debugC(1, kDebugLevelScript, "opcode 0x97: no protagonist, skipping backup");
		return kThxBye;
	}
	Logic::CutsceneBackup &b = Log.cutsceneBackup();
	if (b.active) {
		// Re-entry without intervening Op_98. DOS would just overwrite
		// the slot; we match.
		warning("opcode 0x97: cutscene backup already active — overwriting");
	}
	b.active = true;
	b.actorField62 = protag->dosField(0x62);
	b.actorField67 = protag->dosField(0x67);
	b.actorField69 = uint16(protag->dosField(0x69)) | (uint16(protag->dosField(0x6a)) << 8);
	b.targetFrameMirror = uint8(protag->targetFrameId() & 0xff);
	// Clear protag fields the way DOS does (field+0x67/+0x6b/+0x62).
	// 0x6b is a word in DOS (`MOV word ptr ES:[SI+0x6b], 0`); we clear
	// both bytes via the sparse map.
	protag->setDosField(0x67, 0);
	protag->setDosField(0x6b, 0);
	protag->setDosField(0x6c, 0);
	protag->setDosField(0x62, 0);
	// Capture and clear post-move callback ([0x65ab..0x65bb]).
	b.savedCallback = Log.postMoveCallback();
	Log.clearPostMoveCallback();
	// Capture protag speech (DOS speech-slot pool entry) and clear.
	b.hadSpeech = protag->isSpeaking();
	b.speechText = b.hadSpeech ? protag->speechText() : Common::String();
	if (b.hadSpeech)
		protag->stopSpeaking();
	debugC(2, kDebugLevelScript,
		"opcode 0x97: BackupCutscenePCState — fields(69=0x%04x 62=0x%02x 67=0x%02x) callback=%d speech='%s'",
		b.actorField69, b.actorField62, b.actorField67,
		int(b.savedCallback.kind), b.speechText.c_str());
	// DOS `g_break_inner = 1` exits the inner dispatch loop. C++
	// equivalent: return kReturn so the caller's dispatch resumes
	// from its own pending state.
	return kReturn;
}
OPCODE(0x98) {
	// DOS Op_98_RestoreCutscenePCState @ 1000:4b40: reverse of Op_97.
	//   GetActorOffset(g_main_character_id) → ES:SI;
	//   [0x6609] = [0x5ef1];  ES:[SI+0x69] = [0x5ee9];
	//   ES:[SI+0x67] = [0x5ef3]; ES:[SI+0x62] = [0x5ef2];
	//   CALL LookupActorAndStartPath();         // re-engage walk
	//   memcpy([0x65ab], [0x5ef4], 20);         // restore post_callback
	//   for slot in g_speech_slots[6]:           // find FREE slot
	//     if (slot.frames_left == 0) { memcpy(slot, [0x5f08], 17); break; }
	//   if ([0x5eeb] != 0xffff):                 // restore room script slot
	//     g_room_script_slots[index].word0 = [0x5eed];
	//     g_room_script_slots[index].word2 = [0x5eef];
	//     g_room_script_slots[index].word4 = main_char;
	//     g_room_script_slots[index].word6 = 0;
	//   g_break_inner = 1.
	Actor *protag = Log.protagonist();
	if (!protag) {
		debugC(1, kDebugLevelScript, "opcode 0x98: no protagonist, skipping restore");
		return kThxBye;
	}
	Logic::CutsceneBackup &b = Log.cutsceneBackup();
	if (!b.active) {
		// DOS would still execute the load-from-zeroed-memory path;
		// we no-op to avoid spurious clears of valid state.
		debugC(1, kDebugLevelScript, "opcode 0x98: no backup active — skipping");
		return kThxBye;
	}
	// Restore protag fields.
	protag->setDosField(0x69, uint8(b.actorField69 & 0xff));
	protag->setDosField(0x6a, uint8(b.actorField69 >> 8));
	protag->setDosField(0x67, b.actorField67);
	protag->setDosField(0x62, b.actorField62);
	// LookupActorAndStartPath re-engages the walk script. Without a
	// modeled walk callback in C++, this is a no-op; the post-move
	// callback restoration below is what actually reactivates the
	// pending action.
	// Restore post-move callback record.
	Log.setPostMoveCallback(b.savedCallback);
	// Restore speech (find first free slot in DOS; here the protag's
	// _speech is single-slot and was cleared by Op_97).
	if (b.hadSpeech)
		protag->say(b.speechText);
	debugC(2, kDebugLevelScript,
		"opcode 0x98: RestoreCutscenePCState — fields(69=0x%04x 62=0x%02x 67=0x%02x) callback=%d speech='%s'",
		b.actorField69, b.actorField62, b.actorField67,
		int(b.savedCallback.kind), b.speechText.c_str());
	b.active = false;
	// DOS sets g_break_inner = 1 — exit inner dispatch. C++ matches
	// by returning kReturn (paired with Op_97's kReturn).
	return kReturn;
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
	// DOS Op_a1 (CS:0x4c59). nargs=2. Disassembly:
	//   AX = arg0 (BX), AX = arg1; PUSH BX, PUSH AX; AX = BX (= arg0)
	//   GetActorOffset(arg0)        ; SI = actor for arg0
	//   POP AX                      ; AX = arg1 (the frame)
	//   actor.field+0x61 = AL       ; current frame = arg1
	//   actor.field+0x62 = AL       ; target frame  = arg1
	//   actor.field+0x6b = 0        ; walk speed
	//   SetActorPosition()          ; X/Y from frame[arg1]
	//   POP BX → FindPlaceById(arg0)
	//   InitActorState()
	// So a[0] is the ACTOR ID and a[1] is the FRAME ID. The previous C++
	// had it backwards (treated a[0] as room, a[1] as actor) and called
	// setRoom which jumped the actor's PC to puppeteer.mainCode.
	debugC(2, kDebugLevelScript, "opcode 0xa1: warp actor %s to frame %s", +a[0], +a[1]);
	if (Actor *ac = Log.getActor(a[0]))
		ac->placeIn(ac->room(), uint16(a[1]));
	return kThxBye;
}
OPCODE(0xa2) {
	// DOS Op_a2 (CS:0x4cb0). nargs=3. Disassembly order:
	//   ResolveOpcodeArg2 → CX                ; arg2 = code offset
	//   ResolveOpcodeArg0 → BX                ; arg0 = actor id
	//   ResolveOpcodeArg1 → AX                ; arg1 = frame
	//   PUSH CX, PUSH BX, PUSH AX
	//   AX = BX (= arg0); GetActorOffset      ; SI = actor for arg0
	//   POP AX                                 ; AX = arg1 (frame)
	//   field+0x61 = AL  (current frame)
	//   field+0x62 = AL  (target frame)
	//   field+0x6b = 0
	//   SetActorPosition                       ; X/Y from frame[arg1]
	//   POP AX (arg0) → CS:[0x37 or 0x35]
	//   POP DI (arg2) → InitActorState         ; sets actor.code_offset = arg2
	// Critical: the previous C++ misread a[0] as the frame (was passing
	// arg0 to setFrame, but arg0 is the actor id).
	debugC(2, kDebugLevelScript, "opcode 0xa2: warp actor %s to frame %s code-offset %s",
		+a[0], +a[1], +a[2]);
	if (Actor *ac = Log.getActor(a[0])) {
		ac->placeIn(ac->room(), uint16(a[1]));
		// arg2 is a code offset for the actor's animation script. DOS
		// stores it at field+0x2 via InitActorState (which loads DI from
		// the stack). Engine-side: rebase the animation pointer.
		if (uint16(a[2]) != 0)
			ac->setAnimation(uint16(a[2]));
	}
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
	// DOS Op_ae_WaitActorIdleByArg @ 1000:4ea2:
	//   arg0 = actor id;
	//   CheckActorIdle(id);
	//   if (NOT idle) RegisterSampleSlot...; RET;  // yield
	//   arg1 = target frame;  MoveActorToTargetExit(id, frame);
	//   GetActorOffset(id) → ES:SI;
	//   arg2 = callback BP;  ES:[SI + 0x69] = arg2.  // walk-callback
	const uint16 id = uint16(a[0]);
	Actor *ac = Log.getActor(id);
	if (!ac)
		return kThxBye;
	if (!Log.actorIdle(ac)) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xae: actor %s walk to frame %s + callback %s",
		+a[0], +a[1], +a[2]);
	const uint16 targetFrame = uint16(a[1]);
	if (targetFrame)
		ac->moveTo(targetFrame);
	// Store callback offset in actor.field+0x69 / +0x6a.
	const uint16 cb = uint16(a[2]);
	ac->setDosField(0x69, uint8(cb & 0xff));
	ac->setDosField(0x6a, uint8(cb >> 8));
	return kThxBye;
}
OPCODE(0xaf) {
	// DOS Op_af_WaitActorIdle @ 1000:4f7c:
	//   if (in_map_mode) RET;
	//   arg0 = actor id;  CheckActorIdle(id);
	//   if (NOT idle) RegisterSampleSlot...; RET;
	//   SendActorToTarget();    // uses globals — no explicit target arg
	// SendActorToTarget without arg => DOS expects a previously-set target
	// (likely from a prior SetActorTarget @ 0x7087). C++ has no global
	// "current target" for actors; the closest thing is "do nothing
	// until the in-flight walk completes", which this opcode already does.
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = Log.getActor(a[0]);
	if (!ac)
		return kThxBye;
	if (!Log.actorIdle(ac)) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xaf: actor %s wait idle (no explicit target)", +a[0]);
	return kThxBye;
}
OPCODE(0xb0) {
	// DOS Op_b0_WaitActorIdle2 @ 1000:4fb1: same as Op_af but ALSO
	// writes arg0's value to actor.field+0x69 (walk-callback target).
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = Log.getActor(a[0]);
	if (!ac)
		return kThxBye;
	if (!Log.actorIdle(ac)) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	const uint16 cb = uint16(a[0]);
	ac->setDosField(0x69, uint8(cb & 0xff));
	ac->setDosField(0x6a, uint8(cb >> 8));
	debugC(2, kDebugLevelScript, "opcode 0xb0: actor %s wait idle + cb=%u", +a[0], cb);
	return kThxBye;
}
OPCODE(0xb1) {
	// DOS Op_b1_WaitActorIdle3 @ 1000:4eee:
	//   if (in_map_mode) RET;
	//   CheckActorIdle(<implicit>);
	//   if (NOT idle) yield;
	//   arg0 resolved (target id);  MoveProtagonistToEntity.
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb1: protag wait idle + walk target %s", +a[0]);
	Log.sendActorToTarget(protag, uint16(a[0]));
	return kThxBye;
}
OPCODE(0xb2) {
	// DOS Op_b2_WaitActorIdle4 @ 1000:4ec8: same as Op_b1.
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb2: protag wait idle + walk target %s", +a[0]);
	Log.sendActorToTarget(protag, uint16(a[0]));
	return kThxBye;
}
OPCODE(0xb3) {
	// DOS Op_b3_WaitActorIdle5 @ 1000:4f0b: same as Op_b1.
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb3: protag wait idle + walk target %s", +a[0]);
	Log.sendActorToTarget(protag, uint16(a[0]));
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
	// DOS Op_b5_handler @ 1000:4f48: protag walk to EXIT.
	//   if (in_map_mode) RET;
	//   arg0 = exit id;  CheckActorIdle(arg0);
	//   if (NOT idle) RegisterSampleSlot_LoadDefaultsAndMark; RET;  // yield
	//   arg1 = ?;  DX = 1 (exit type);  BX = arg0;
	//   MoveProtagonistToEntity (resolves entity → walkbox → frame).
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb5: protagonist walk to exit %s", +a[0]);
	Log.sendActorToTarget(protag, uint16(a[0]));
	return kThxBye;
}
OPCODE(0xb6) {
	// DOS Op_b6_handler @ 1000:4f28: protag walk to OBJECT (DX=2). Same
	// gate/yield shape as 0xb5; differs only in MoveProtagonistToEntity's
	// type tag, which our `sendActorToTarget` derives from the id by
	// trying actor → exit → object lookup.
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb6: protagonist walk to object %s", +a[0]);
	Log.sendActorToTarget(protag, uint16(a[0]));
	return kThxBye;
}
OPCODE(0xb7) {
	// DOS Op_b7_handler @ 1000:4f62: protag walk to ACTOR (DX=3). NOT
	// "walk to (x,y)" as the previous comment claimed.
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb7: protagonist walk to actor %s", +a[0]);
	Log.sendActorToTarget(protag, uint16(a[0]));
	return kThxBye;
}
OPCODE(0xb8) {
	// DOS Op_b8_WalkActorWaitWithBreakFast @ 1000:502d:
	//   g_walk_speed_flag = 0;
	//   if (in_map_mode) RET;
	//   arg0 = actor_id;
	//   if (id > g_anim_count_max) pending error 0x17;
	//   if (id == g_main_character_id) g_break_inner = 1;
	//   CheckActorAnimReady(id);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   arg1 = anim selector; InitActorState(id) — re-run actor's main code.
	const uint16 id = uint16(a[0]);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (Log.inMapMode())
		return kThxBye;
	if (!Log.actorIdle(ac)) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb8: actor %s walk-wait-break (fast) anim=%s", +a[0], +a[1]);
	// InitActorState equivalent: re-engage actor's main animation script.
	if (a[1].holdsCode())
		ac->setAnimation(static_cast<CodePointer &>(a[1]));
	// DOS sets g_break_inner = 1 when id == g_main_character_id; mirror
	// by returning kReturn so the inner dispatch yields.
	if (Log.protagonist() && ac == Log.protagonist())
		return kReturn;
	return kThxBye;
}
OPCODE(0xba) {
	// DOS Op_ba_WalkActorAnimFast @ 1000:4fe5:
	//   g_walk_speed_flag = 0;
	//   if (in_map_mode) RET;
	//   arg2 = screen_x; arg3 = screen_y;  (resolved BEFORE id check)
	//   arg0 = actor_id;
	//   if (id > g_anim_count_max) pending error 0x17;
	//   CheckActorAnimReady(id);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   GetActorOffset(id) → ES:SI;
	//   ES:[SI + 0x4] = arg2;  ES:[SI + 0x6] = arg3;  ES:[SI + 0x61] = 0;
	//   if (in_map_mode) RET;
	//   arg0 = id (re-resolved);  CheckActorAnimReady(id);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   arg1 = anim;  InitActorState(id).
	const uint16 id = uint16(a[0]);
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!Log.actorIdle(ac)) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	const int16 destX = int16(uint16(a[2]));
	const int16 destY = int16(uint16(a[3]));
	debugC(2, kDebugLevelScript, "opcode 0xba: actor %s walk-anim-fast to (%d,%d) anim=%s",
		+a[0], destX, destY, +a[1]);
	Log.walkActorAnim(id, destX, destY, /* slowSpeed = */ false);
	if (a[1].holdsCode())
		ac->setAnimation(static_cast<CodePointer &>(a[1]));
	return kThxBye;
}
OPCODE(0xbb) {
	// DOS Op_bb_WalkActorAnimSlow @ 1000:4fde: identical to 0xba but
	// g_walk_speed_flag = 1 (slow stride). The C++ port doesn't model
	// per-walk stride; behaviour is the same.
	const uint16 id = uint16(a[0]);
	if (Log.inMapMode())
		return kThxBye;
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!Log.actorIdle(ac)) {
		ac->callMeWhenStill(next);
		return kReturn;
	}
	const int16 destX = int16(uint16(a[2]));
	const int16 destY = int16(uint16(a[3]));
	debugC(2, kDebugLevelScript, "opcode 0xbb: actor %s walk-anim-slow to (%d,%d) anim=%s",
		+a[0], destX, destY, +a[1]);
	Log.walkActorAnim(id, destX, destY, /* slowSpeed = */ true);
	if (a[1].holdsCode())
		ac->setAnimation(static_cast<CodePointer &>(a[1]));
	return kThxBye;
}

OPCODE(0xbf) {
	// DOS Op_bf_WaitProtagonistAnimBreak @ 1000:50a1:
	//   g_walk_speed_flag = 0;
	//   if (in_map_mode) RET;
	//   g_break_inner = 1;
	//   CheckActorAnimReady(<implicit = main_char>);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   arg1, arg2 = screen x/y;
	//   GetActorOffset(main_char) → ES:SI;
	//   ES:[SI + 0x61] = 0;
	//   ES:[SI + 0x4] = arg1;  ES:[SI + 0x6] = arg2;
	//   if (in_map_mode) RET;
	//   g_break_inner = 1;  CheckActorAnimReady; if NOT ready yield;
	//   arg0 = anim selector;  InitActorState(main_char).
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xbf: protag wait + walk to (%s,%s) anim=%s",
		+a[1], +a[2], +a[0]);
	const int16 destX = int16(uint16(a[1]));
	const int16 destY = int16(uint16(a[2]));
	if (Log.room()) {
		const uint16 frame = Log.room()->nearestFrameTo(destX, destY);
		if (frame)
			protag->moveTo(frame);
	}
	if (a[0].holdsCode())
		protag->setAnimation(static_cast<CodePointer &>(a[0]));
	return kReturn;  // DOS g_break_inner = 1 — yield
}

// 0xc0..0xc5: cast/actor pos.
OPCODE(0xc0) {
	// DOS Op_c0_WaitProtagonistAnimBreakFast @ 1000:509a: same as Op_bf
	// but g_walk_speed_flag = 0 (this opcode entry is just 7 bytes
	// before Op_bf @ 0x50a1, falling through into the same body).
	if (Log.inMapMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag)
		return kThxBye;
	if (!Log.actorIdle(protag)) {
		protag->callMeWhenStill(next);
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xc0: protag wait + walk to (%s,%s) anim=%s (fast)",
		+a[1], +a[2], +a[0]);
	const int16 destX = int16(uint16(a[1]));
	const int16 destY = int16(uint16(a[2]));
	if (Log.room()) {
		const uint16 frame = Log.room()->nearestFrameTo(destX, destY);
		if (frame)
			protag->moveTo(frame);
	}
	if (a[0].holdsCode())
		protag->setAnimation(static_cast<CodePointer &>(a[0]));
	return kReturn;
}
OPCODE(0xc1) {
	// DOS Op_c1_UnregisterActor @ 1000:5131:
	//   if (g_in_map_mode != 0) RET;
	//   AX = g_main_character_id;
	//   CALL UnregisterActor(AX);   // 0x66ed
	//
	// UnregisterActor (0x66ed) — full disassembly:
	//   GetActorOffset(AX) → ES:SI;
	//   ES:[SI + 0]  = 0;        // clear actor.field+0 (script segment)
	//   ES:[SI + 2]  = 0;        // clear actor.field+2 (script offset)
	//   CX = 0x14;  DI = 0x25fb; // g_actor_table base (20 slots × 0x2e)
	//   loop:
	//     if ([DI] == AX) { [DI] = 0; RET; }   // clear matching wId
	//     DI += 0x2e;  LOOP;
	//   RET;  // no match
	//
	// = "stop processing the protagonist's animation script + remove
	// from the active-actor list".
	//
	// C++ mapping:
	//   Step 1 (clear field+0/+2): Actor::hide() sets _base=0/_offset=0,
	//     which is the C++ analog of DOS's seg+off script PC. After this
	//     Animation::tick early-exits at `while (status==kOk && _base)`.
	//   Step 2 (g_actor_table[i].wId = 0): no direct C++ analog. The
	//     active-actor list in C++ is `Logic::_animations`, populated
	//     when the actor is loaded; entries are removed only on actor
	//     destruction. The renderer-side observable effect of "actor is
	//     no longer in the active list" — namely, that it stops drawing
	//     and stops being ticked — is captured by Step 1: a hidden actor
	//     with _base=0 has Animation::tick return immediately and
	//     Animation::paint skip (via the same _base check). So the
	//     user-observable behavior matches DOS even though the internal
	//     mechanism differs.
	if (Log.inMapMode())
		return kThxBye;
	if (Actor *protag = Log.protagonist()) {
		protag->hide();
		debugC(2, kDebugLevelScript, "opcode 0xc1: UnregisterActor — protagonist hidden (script PC cleared)");
	}
	return kThxBye;
}
OPCODE(0xc3) {
	// DOS Op_c3_RegisterCastEntry @ 1000:514a:
	//   Resolve args 1, 2, 0;
	//   Find first slot where wActive == 0 in g_cast_table[18];
	//   slot.w_unk_02 = arg0 (id);  slot.wActive = caller_seg;
	//   slot.wX = arg1;  slot.wY = arg2;
	//   Init bookkeeping bytes (frame=1, sprite_idx=0xff, rect=0xffff…);
	//   else: pending error 0x2a.
	const uint16 id = uint16(a[0]);
	const int16 x = int16(uint16(a[1]));
	const int16 y = int16(uint16(a[2]));
	const bool ok = Log.castTableRegister(id, x, y);
	debugC(2, kDebugLevelScript, "opcode 0xc3: RegisterCastEntry id=%u pos=(%d,%d) %s",
		id, x, y, ok ? "ok" : "FAIL (table full → pending 0x2a)");
	return kThxBye;
}
OPCODE(0xc4) {
	// DOS Op_c4_SetCastEntryPosition @ 1000:51a8 — BUG-ACCURATE port.
	// DOS clobbers arg1 (saved in CX) with the loop counter immediately
	// before the search loop, so the matched slot's wX is overwritten
	// with (kCastTableCap - matched_index), not arg1. arg2 (in DX) is
	// preserved and written correctly to wY. See Logic::castTableSetPos
	// for the full disassembly trace and reproduction note.
	const uint16 id = uint16(a[0]);
	const int16 x = int16(uint16(a[1]));   // resolved + passed for trace; DOS discards
	const int16 y = int16(uint16(a[2]));
	debugC(2, kDebugLevelScript, "opcode 0xc4: SetCastEntryPosition id=%u (arg1=%d ignored — DOS bug, wY=%d)",
		id, x, y);
	Log.castTableSetPos(id, x, y);
	return kThxBye;
}
OPCODE(0xc5) {
	// DOS Op_c5_ClearCastEntry @ 1000:51cd:
	//   Resolve arg0 (id);
	//   Find slot where w_unk_02 == arg0;
	//   if found: w_unk_02 = 0; wActive = 0;  // free slot
	//   else: silent no-op.
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc5: ClearCastEntry id=%u", id);
	Log.castTableClear(id);
	return kThxBye;
}

OPCODE(0xca) {
	// DOS Op_ca_PatchGraphicEntry @ 1000:5246:
	//   if (arg0 > g_graphic_count) pending-error 0xa;
	//   else: graphic_index[(arg0-1)*4].lowWord = arg1;
	// Patches an entry in the graphic-index table (iuc_graf.dat
	// header). DOS modifies only the low 16 bits of the 4-byte
	// entry — the offset portion. Used to swap a graphic at runtime.
	const uint16 id = uint16(a[0]);
	const uint16 newOffsetLow = uint16(a[1]);
	MapFile *map = _logic->resources()->graphicsMap();
	if (!map || id == 0 || id > map->entryCapacity()) {
		Log.setPendingError(0x0a);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0xca: patch graphic[%u].offset_low = 0x%04x", id, newOffsetLow);
	map->patchEntryLow16(id, newOffsetLow);
	return kThxBye;
}
OPCODE(0xcd) {
	// DOS Op_cd_RestoreRoomActive (CS:0x52b7): end cutscene — mirror of 0xce.
	//   1. g_room_active = 1
	//   2. SetBackdropDimensions (restore)
	//   3. g_flag_misc_1 = 1
	//   4. Calls Op_96_handler (unlock control = setNoStep(false) +
	//      setStepPending(false))
	// C++: show cursor + unlock control to mirror Op_ce.
	debugC(2, kDebugLevelScript, "opcode 0xcd: end cutscene / restore room active");
	Graf.showCursor();
	Log.setNoStep(false);
	Log.setStepPending(false);
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
	// DOS Op_d4_SetCameraTarget @ 1000:53d3:
	//   _g_target_x = arg0; _g_target_y = arg1;
	//   g_input_enabled = 0;
	// Sets camera scroll TARGET (engine smoothly pans toward it).
	const uint16 tx = uint16(a[0]);
	const uint16 ty = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xd4: camera target = (%u, %u)", tx, ty);
	Log.setCameraTarget(tx, ty);
	Log.setInputEnabled(false);
	return kThxBye;
}
OPCODE(0xd5) {
	// DOS Op_d5_SetCameraInstant @ 1000:53e5:
	//   g_camera_x = arg0; g_camera_y = arg1;
	//   g_input_enabled = 0;
	//   _g_target_x = 0xffff; _g_target_y = 0xffff;
	//   g_flag_misc_3 = 1; (mark for redraw)
	// Sets camera position INSTANTLY (no scroll).
	const int16 cx = int16(uint16(a[0]));
	const int16 cy = int16(uint16(a[1]));
	debugC(2, kDebugLevelScript, "opcode 0xd5: camera instant (%d, %d)", cx, cy);
	Log.setCameraXY(cx, cy);
	Log.setCameraTarget(0xffff, 0xffff);
	Log.setInputEnabled(false);
	return kThxBye;
}
OPCODE(0xd7) {
	// 0xd7 (DOS CS:0x5408): clear g_in_fade. Already implicit in our renderer.
	debugC(2, kDebugLevelScript, "opcode 0xd7: clear fade flag");
	return kThxBye;
}
OPCODE(0xd9) {
	// 0xd9 (DOS CS:0x5430): add zone entry to g_zone[8] (4 uint16 args, 8-byte
	// stride). Overflow sets g_pendingErrorCode = 0x27; we silently drop.
	debugC(2, kDebugLevelScript, "opcode 0xd9: add zone (%s,%s,%s,%s)",
		+a[0], +a[1], +a[2], +a[3]);
	Logic::Zone z = { uint16(a[0]), uint16(a[1]), uint16(a[2]), uint16(a[3]) };
	Log.zonesAdd(z);
	return kThxBye;
}

OPCODE(0xdd) {
	// 0xdd (DOS CS:0x54bf): add zone-B entry. 4 uint16 args + 1 var slot value
	// (ReadVarBySlot_RHS) at offset +0x679. Overflow at 30 sets error 0x32.
	// VM args 0..3 + arg 4 (variable). Stride 10 bytes per entry.
	debugC(2, kDebugLevelScript, "opcode 0xdd: add zone-B (%s,%s,%s,%s, var=%s)",
		+a[0], +a[1], +a[2], +a[3], +a[4]);
	Logic::ZoneB z = {
		uint16(a[0]), uint16(a[1]), uint16(a[2]), uint16(a[3]), uint16(a[4])
	};
	Log.zonesBAdd(z);
	return kThxBye;
}

// 0xe0..0xec: frame-table mutators / misc state setters. iter-17:
// re-decompiled from DOS — these mutate the room's per-frame data table:
//   Op_e0 (1 arg): InvalidateFrame(arg0) — set frame[arg0].x = frame.y = 999
//                  (the "placeholder" sentinel that findPath skips per
//                  iter-15's BFS guard).
//   Op_e1 (3 args): SetFramePosition(arg0, x=arg1, y=arg2) — overwrite the
//                   frame's screen position.
//   Op_e3 (3 args): stash 3 globals + set logic dirty (palette/font region?)
//   Op_e4 (4 args): append entry to g_anim_list (max 8) — a deferred draw
//                   queue. C++ has no overlay queue yet (see Op_7e).
// All are scene/cutscene-specific. Without a frame-override mechanism on
// Room, log the call with all args so any anomalies are traceable.
OPCODE(0xe0) {
	// DOS Op_e0 (CS:0x5548): InvalidateFrame. Sets frame[arg0].x = .y = 999.
	// findPath skips frames with this sentinel — used to remove a frame
	// from the walkable graph mid-cutscene (e.g. blocking a path).
	const uint16 frame = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xe0: InvalidateFrame %u", frame);
	if (Room *room = Log.room())
		room->invalidateFrame(frame);
	return kThxBye;
}
OPCODE(0xe1) {
	// DOS Op_e1 (CS:0x5564): SetFramePosition. Overwrites frame[arg0]'s
	// (x, y) with arg1, arg2. Used to dynamically move a walkable point.
	const uint16 frame = uint16(a[0]);
	const int16 x = int16(uint16(a[1]));
	const int16 y = int16(uint16(a[2]));
	debugC(2, kDebugLevelScript, "opcode 0xe1: SetFramePosition frame=%u (%d,%d)", frame, x, y);
	if (Room *room = Log.room())
		room->setFramePosition(frame, x, y);
	return kThxBye;
}
OPCODE(0xe3) {
	// DOS Op_e3_handler @ 1000:5589:
	//   pbRam000231b2 = arg0 + 3;       // DS:0x6662
	//   pbRam000231b4 = arg1 + 0x9b;    // DS:0x6664
	//   _g_unknown_6660 = arg2;         // DS:0x6660 (gate for DispatchDialogClick)
	//   g_flag_logic_dirty = 1;
	// Stashes anim-list cursor pointers used by DispatchDialogClick @
	// 1000:b316 when iterating g_anim_list (per Op_e4 entries). Reader
	// not yet implemented in C++; storage matches DOS field layout.
	const uint16 cursor0 = uint16(uint16(a[0]) + 3);
	const uint16 cursor1 = uint16(uint16(a[1]) + 0x9b);
	const uint16 gate = uint16(a[2]);
	Log.setDialogCursors(cursor0, cursor1, gate);
	debugC(2, kDebugLevelScript, "opcode 0xe3: stash anim-list cursor (cursor0=0x%04x cursor1=0x%04x gate=%u)",
		cursor0, cursor1, gate);
	return kThxBye;
}
OPCODE(0xe4) {
	// DOS Op_e4_handler @ 1000:55a7:
	//   if (anim_list_count >= 8) pending-error 0xb;
	//   else: append (arg3, arg2, arg0+3, arg0+9, arg1+0x9b, arg1+0xa1, 0xffff)
	//         to anim_list[anim_list_count]; ++count.
	// = "queue cutscene anim entry". Args are pose / position deltas.
	if (Log.animListCount() >= 8) {
		Log.setPendingError(0x0b);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0xe4: anim-list append (%s, %s, %s, %s)",
		+a[0], +a[1], +a[2], +a[3]);
	Log.animListAppend(uint16(a[0]), uint16(a[1]), uint16(a[2]), uint16(a[3]));
	return kThxBye;
}
OPCODE(0xe7) {
	// DOS Op_e7 @ 1000:5612: 0 args. Calls ClearBytesUntilWrap on
	// the parser buffer at DS:0x4fa9. Clears the buffer (sets
	// length=0 and zeroes chars).
	debugC(2, kDebugLevelScript, "opcode 0xe7: parser buffer cleared");
	Log.parserBufferClear();
	return kThxBye;
}
OPCODE(0xe8) {
	// 0xe8 (DOS CS:0x561d): clear pending step.
	debugC(2, kDebugLevelScript, "opcode 0xe8: stepPending = false");
	Log.setStepPending(false);
	return kThxBye;
}
OPCODE(0xe9) {
	// DOS Op_e9 @ 1000:5634: 1 arg (char). Appends arg0 byte to the
	// parser buffer at DS:0x4fa9 if length < capacity. C++ comment
	// previously said "set verbMode" — WRONG; that was a misrouting
	// from a different opcode. The disassembly clearly shows
	// `*(byte *)0x4faa` length increment + char store.
	const byte ch = uint8(uint16(a[0]) & 0xff);
	debugC(2, kDebugLevelScript, "opcode 0xe9: parser append '%c' (0x%02x)",
		ch >= 0x20 && ch < 0x7f ? ch : '.', ch);
	Log.parserBufferAppend(ch);
	return kThxBye;
}
OPCODE(0xea) {
	// DOS Op_ea (CS:0x5642): Pascal-string append-byte. arg0 = string ptr,
	// arg1 = byte to append. If string.length < string.capacity, increments
	// length and writes byte at end. Falls through.
	// PREVIOUSLY misclassified as `setInMapMode(arg0 != 0)` — would corrupt
	// map-vs-scene state if 0xea ever emitted. Now safe-stub.
	debugC(2, kDebugLevelScript, "opcode 0xea: pstring append byte STUB (str=%s, byte=%s)",
		+a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xeb) {
	// DOS Op_eb @ 1000:5665: 0 args. Calls PopLastCharOfPascalString
	// on the parser buffer (length-- if length > 0, zero last char).
	debugC(2, kDebugLevelScript, "opcode 0xeb: parser pop last char");
	Log.parserBufferPop();
	return kThxBye;
}
OPCODE(0xec) {
	// DOS Op_ec (CS:0x5670): Pascal-string truncate-by-length. arg0 = string
	// ptr; if length > 0, decrements length and zeroes last char.
	// PREVIOUSLY misclassified as `clear inMapMode` — would force out of map
	// mode if 0xec ever emitted. Now safe-stub.
	debugC(2, kDebugLevelScript, "opcode 0xec: pstring truncate STUB (str=%s)",
		+a[0]);
	return kThxBye;
}

OPCODE(0xee) {
	// DOS Op_ee_handler @ 1000:5698:
	//   if (arg0 >= g_score_event_count [CS:0x93]) pending-error 0x2f;
	//   else:
	//     entry = score_table[arg0*2]   ; CS:[0x95 + arg0*2]
	//     if (entry+1 byte == 0):       ; not yet claimed
	//         g_game_score += entry word
	//         entry+1 byte = 1          ; mark claimed
	// = "claim a score event". Score-table data lives at CS:[0x95]
	// loaded from iuc_main.dat by LoadCodeBlock — not yet wired in
	// C++. As a faithful approximation, increment by 1 per event on
	// first claim; the real DOS values vary per event but the
	// "score increases when achievement triggers" semantic holds.
	const uint16 eventId = uint16(a[0]);
	if (Log.isScoreEventClaimed(eventId)) {
		debugC(3, kDebugLevelScript, "opcode 0xee: score event %u already claimed", eventId);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0xee: claim score event %u (score %u → %u)",
		eventId, Log.gameScore(), Log.gameScore() + 1);
	Log.markScoreEventClaimed(eventId);
	Log.addGameScore(1);
	return kThxBye;
}

// 0xf1..0xf5: music/sfx beyond the core 0xf4 (play music) / 0xf7 (stop) /
// 0xf8 (panic stop) handled above.
OPCODE(0xf1) {
	// DOS Op_f1_handler @ 1000:5725: 2 args.
	//   if (g_sfx_enabled) {
	//       Op_load_sfx(arg0);          // primary play (Op_f0 inline)
	//       if (arg1 != pbRam00023250) {
	//           PlaySfxSound(arg1);
	//           cache arg1 at [0x6700], slot at [0x6706/0x6708].
	//       }
	//   }
	// Routes through Sound::playSfxPair which chains Op_f0 + secondary.
	if (Sound *snd = _engine->sound())
		snd->playSfxPair(uint16(a[0]), uint16(a[1]));
	debugC(1, kDebugLevelScript, "opcode 0xf1: load_sfx pair primary=%s secondary=%s",
		+a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xf2) {
	// DOS Op_f2_handler @ 1000:575a: 1 arg.
	//   if (g_sfx_enabled) DispatchSfxRangeCheck(arg0).
	// DispatchSfxRangeCheck @ 1000:606d: validates arg0 against the
	// active slot range [0x6702..0x6704] and [0x6706..0x6708]; if in
	// range, replays via driver dispatch. Routes through
	// Sound::rangeCheck.
	if (Sound *snd = _engine->sound())
		snd->rangeCheck(uint16(a[0]));
	debugC(1, kDebugLevelScript, "opcode 0xf2: sfx range check id=%s", +a[0]);
	return kThxBye;
}
OPCODE(0xf3) {
	// DOS Op_f3 (CS:0x5769): nargs=0 per opcodes_nargs.data. Calls
	// RegisterSampleSlot_Bare8 (or _Bare5 if SFX inactive). NOT a music
	// load. The original C++ port had it as `Music.loadMusic(a[0])` —
	// reading a[0] (which doesn't exist with 0 nargs) as a CodePointer
	// offset, then loading "music" from a garbage address. Could crash
	// if the script ever emitted Op_f3. iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xf3: RegisterSampleSlot (no args) STUB");
	return kThxBye;
}
OPCODE(0xf5) {
	// DOS Op_f5 (CS:0x5812): nargs=0 per opcodes_nargs.data. Calls
	// RegisterSampleSlot_Bare6 (music) or _Bare5 (sfx). NOT a beat-set.
	// Original C++ called Music.setBeat(uint16(a[0])) — reading a[0]
	// (which doesn't exist with 0 nargs) as a beat number, then setting
	// the music to a garbage beat. Could index past the beat array
	// (Tune::setBeat is now bound-checked iter-12, so it'd just no-op,
	// but cleaner to not invoke at all). iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xf5: RegisterSampleSlot (music, no args) STUB");
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
