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
#include "common/system.h"

#include "audio/mididrv.h"
#include "audio/mixer.h"

namespace Interspective {

// Speech subsystem helper: route text to the appropriate sink.
// In map mode, DOS displays subtitles (no actor bubble). Otherwise
// queue a per-actor bubble via Actor::say. C++'s per-actor _speech
// slot is the equivalent of DOS's 6-slot g_speech_slots pool —
// modern hosts have no RAM constraint, so the 6-slot cap is a
// non-issue.
static void speakOrSubtitle(Actor *speaker, const Common::String &text) {
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
	if (speaker)
		speaker->say(text);
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
	// and the resume PC is queued for next tick. The caller-side push
	// is Op_38 (still destructive in this engine — see PLAN.md
	// Cross-cutting subsystems; will be wired when 0x38 is reached in
	// table order). Until Op_38 is wired, hasSavedScene() is always
	// false and this opcode behaves identically to the plain-exit case
	// — but with the pop infrastructure now in place to make the
	// implementation Ghidra-faithful.
	debugC(2, kDebugLevelScript, "opcode 0x01: exit");
	Log.restoreSceneFrame();
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
	// In C++ the equivalent state lives in ScummVM's MidiDriver detection
	// and the mixer's per-channel volume:
	//   - music device class derived from `MidiDriver::detectDevice` →
	//     MT_ADLIB → bit 0, MT_MT32/MT_GM → bit 2, others → bit 0 fallback.
	//   - sfx is unconditionally SB-class (bit 1) — ScummVM always offers
	//     digital sample mixing.
	//   - each bit is ZEROED if the corresponding mixer channel is muted
	//     (volume == 0), matching DOS where g_music_enabled = 0 disables
	//     the music's bits entirely.
	uint16 musicMask = 0;
	uint16 sfxMask = 0;
	if (g_system && g_system->getMixer()) {
		Audio::Mixer *mix = g_system->getMixer();
		const int musicVol = mix->getVolumeForSoundType(Audio::Mixer::kMusicSoundType);
		const int sfxVol = mix->getVolumeForSoundType(Audio::Mixer::kSFXSoundType);
		if (musicVol > 0) {
			MidiDriver::DeviceHandle dev =
				MidiDriver::detectDevice(MDT_MIDI | MDT_ADLIB | MDT_PREFER_GM);
			MusicType mt = MidiDriver::getMusicType(dev);
			if (mt == MT_MT32 || mt == MT_GM)
				musicMask = kSoundRoland;
			else
				musicMask = kSoundAdlib;
		}
		if (sfxVol > 0)
			sfxMask = kSoundSB;
	}
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
	// "if slow CPU" — body executes only on slow machines (the original calibrated
	// at startup and set g_slow_cpu when a frame took too long). Modern hosts are
	// always fast, so the body is always skipped.
	// DOS handler at CS:0x391d: skip if g_slow_cpu (DS:0x67b5) == 0.
	debugC(2, kDebugLevelScript, "opcode 0x11: if slow CPU (always false)");
	return kFail;
}

OPCODE(0x17) {
	// DOS Op_17_IfExitMissing @ 1000:3996. Reads `exit_record[0]`
	// (the room field — kOffsetRoom = 0 in C++ Exit) at SI =
	// GetExitOffset(arg0); skips if it equals 0. Run if room != 0.
	// Loaded `Exit *` is never null in C++; the meaningful check is
	// against `exit->room() == 0`, not pointer-nullness.
	debugC(1, kDebugLevelScript, "opcode 0x17: if exit %s exists", +a[0]);
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
	Log.setImplicitActor(ac);
	if (!ac || ac->room() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1a) {
	// DOS Op_1a_IfExitPresent @ 1000:39d0. Inverse of 0x17: skips
	// when `exit_record[0] != 0` (exit room is set → exit "present").
	// Body runs when exit room == 0 (or slot null).
	debugC(1, kDebugLevelScript, "opcode 0x1a: if exit %s missing", +a[0]);
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
	Log.setImplicitActor(ac);
	if (ac && ac->room() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1d) {
	// DOS Op_1d_IfActorAtRoomFrame @ 1000:3a10: arg1 = actor id,
	// arg0 = frame. Run if actor.room == current_loc AND
	// actor.frame == arg0. Sets implicit actor.
	debugC(1, kDebugLevelScript, "opcode 0x1d: if actor %s in current room AND at %s", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	Log.setImplicitActor(ac);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() != a[0])
		return kFail;
	return kThxBye;
}

OPCODE(0x1f) {
	// DOS Op_1f_IfActorNotAtRoomFrame @ 1000:3a39: arg1 = actor id,
	// arg0 = frame. Run if actor.room == current_loc AND
	// actor.frame != arg0. Sets implicit actor.
	debugC(1, kDebugLevelScript, "opcode 0x1f: if actor %s is in current room but not at %s then", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	Log.setImplicitActor(ac);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() == a[0])
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
	// DOS Op_47_SpeakWithRect (CS:0x3eb6): 5 args (y, x, color, lines, text).
	// Branches on g_in_map_mode: out of map mode → AllocSpeechSlot_NoFormatting
	// (allocates a speech bubble slot for the actor with the text). In map
	// mode → CheckSubtitleActive → either RegisterSampleSlot or
	// QueueDeferredFormattedText.
	//
	// Without speech-slot infrastructure, we route the text through the
	// engine's Graf.say() queue (added iter-12) so narrator text at least
	// shows up at top-left for ~50 frames. Position/color/line-count args
	// are accepted but not honored (positioning would require the
	// paintText path with persistent overlay).
	const byte *text = static_cast<byte *>(a[4]);
	debugC(1, kDebugLevelScript, "opcode 0x47: say at [%s:%s] color=%s lines=%s text='%s'",
		+a[0], +a[1], +a[2], +a[3], text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
		if (length > 0)
			// Scale display time with text length (~3 ticks/char,
			// 30-tick floor) so reading speed matches DOS sample
			// pacing. Hardcoded 50 was too short for long narrator
			// lines — see Actor::Speech ctor for matching rule.
			Graf.say(text, length, MAX<uint16>(30, 3 * length));
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
	// DOS Op_56_SendActorToTargetOrWait @ 1000:4069: 2 args.
	//   CheckMovementBlocked();              ; CF=1 if 66d6 != 0
	//   if (!CF) RegisterSampleSlot_LoadDefaultsD;  ; yield
	//   else {
	//       g_unknown_66d6 = arg0;            ; pending move target
	//       DAT_1cb5_66d8  = g_codeptr_es_save;  ; saved PC
	//       DAT_1cb5_66da  = arg1;            ; pending move flag/extra
	//   }
	// = "if no pending move, yield to let the move complete; else
	// stash arg0/arg1/PC for resumption." C++ approximates by always
	// yielding via runLater (the simpler "wait one tick" semantic);
	// the stash slots aren't needed if the walk-driver runs the move
	// inline and Op_56 just yields.
	debugC(2, kDebugLevelScript, "opcode 0x56: send-or-wait target=%s extra=%s (yield)",
		+a[0], +a[1]);
	_logic->runLater(next, 0);
	return kReturn;
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
	// DOS get-actor-field. a[0] = actor id, a[1] = field offset, a[2] =
	// destination. Reads a 1- or 2-byte field from the actor record. The
	// canonical offsets are documented in iter-10's actor field map.
	// Previously errored out on unknown offsets; now returns 0 with a log
	// warning so unimplemented properties don't crash the engine.
	Actor *actor = _logic->getActor(a[0]);
	const char *desc = "?";
	uint16 value = 0;
	const uint8 off = uint16(a[1]) & 0xff;

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
	// DOS Op_7b_SetObjectFlag1 (CS:0x4459): sets bit 0 of cellByte[a[0]] in
	// the per-entity flag array. Previous engine code mistook this for an
	// "enable exit" op and toggled Exit::isEnabled directly — wrong abstraction
	// (the cell-bit array is shared between objects AND exits). We now write
	// the cell bit AND keep the exit-side-effect for backward compatibility
	// with anything else that already reads Exit::isEnabled().
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x7b: set cell bit 0 on entity %s", +a[0]);
	Log.setCellBit(id, 0);
	if (Exit *exit = _logic->blockProgram()->getExit(a[0]))
		if (!exit->isEnabled())
			exit->setEnabled(true);
	return kThxBye;
}

OPCODE(0x7c) {
	// DOS Op_7c_ClearObjectFlag1 (CS:0x4476): clears bit 0 of cellByte[a[0]].
	// See 0x7b for the cell-bit/exit duality.
	const uint16 id = uint16(a[0]);
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
	// In C++ this is `Logic::_dragTargetMode40` — until Op_76 is
	// audited (still `?` in PLAN.md status table), the slot is never
	// populated and Op_0b will always take the SKIP branch. That is
	// honest: the predicate is evaluated against the right state,
	// the producer just doesn't exist yet.
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
	// IfFreshGameState (DOS CS:0x395a): fail if gameState != 0.
	debugC(2, kDebugLevelScript, "opcode 0x14: if game state == 0");
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
	//   bit_idx > 7: matches DOS — halt-equivalent (warning + skip).
	//   id > max: not relevant in C++. DOS guards an unsafe array
	//     access; `Logic::_cellBits` is a HashMap returning 0 for any
	//     unknown id, so OOB is structurally impossible. The DOS
	//     halt would be a hard error in DOS but no observable
	//     misbehaviour in C++.
	const uint16 rawBit = uint16(a[1]);
	if (rawBit > 7) {
		Log.setPendingError(0x15);
		return kFail;
	}
	const uint16 id = uint16(a[0]);
	const uint8 bit = uint8(rawBit);
	const bool set = Log.cellBit(id, bit);
	debugC(2, kDebugLevelScript, "opcode 0x15: if cell bit %u of entity %s set (=%s)",
		bit, +a[0], set ? "yes" : "no");
	return set ? kThxBye : kFail;
}

OPCODE(0x16) {
	// 0x16 (DOS CS:0x3991): just calls ResolveOpcodeArg0 — read-and-discard.
	// Useful for triggering side-effects of evaluating an expression without
	// using the result.
	debugC(3, kDebugLevelScript, "opcode 0x16: read-discard %s", +a[0]);
	return kThxBye;
}

OPCODE(0x18) {
	// DOS Op_18 (CS:0x39a9): SETS skip_counter when Object[a[0]].room == 0
	// (i.e. SKIPS the body when the object is missing). Net semantics: the
	// conditional body executes when the object is PRESENT. Ghidra's label
	// "IfObjectMissing" describes the SKIP condition, not the run condition.
	// Without a loaded Object table we default to "present" → run body.
	const uint16 id = uint16(a[0]);
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
	const bool missing = Log.isObjectMissing(id);
	debugC(2, kDebugLevelScript, "opcode 0x1b: if object %s missing (room=%u%s)",
		+a[0], Log.getObjectRoom(id), Log.hasObjectRoom(id) ? "" : " default");
	return missing ? kThxBye : kFail;
}

OPCODE(0x1e) {
	// DOS Op_1e_IfImplicitActorAtFrame @ 1000:3a0a: uses the actor
	// whose offset SI was last set to (= Logic::implicitActor() in
	// C++). Run if actor.room == current_loc AND actor.frame == arg0.
	// Note: does NOT call ResolveOpcodeArg1 — the implicit actor is
	// inherited from the previous opcode.
	Actor *ac = Log.implicitActor();
	debugC(2, kDebugLevelScript, "opcode 0x1e: if implicit actor at frame %s", +a[0]);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() != uint16(a[0]))
		return kFail;
	return kThxBye;
}

OPCODE(0x20) {
	// DOS Op_20_IfImplicitActorNotAtFrame @ 1000:3a33: inverse of 0x1e.
	// Uses implicit actor (no ResolveOpcodeArg1).
	Actor *ac = Log.implicitActor();
	debugC(2, kDebugLevelScript, "opcode 0x20: if implicit actor not at frame %s", +a[0]);
	if (!ac || ac->room() != Log.currentRoom() || ac->frameId() == uint16(a[0]))
		return kFail;
	return kThxBye;
}

OPCODE(0x21) {
	// DOS Op_21 (CS:0x3a75): SETS skip_counter when Object[a[0]].room != -1
	// (i.e. SKIPS the body when the object IS placed). Net semantics: the
	// conditional body executes when the object is NOT placed (room == 0xffff).
	// Without a loaded Object table we default to placed → fail.
	const uint16 id = uint16(a[0]);
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
	//   comparison is "are the two translated strings equal?", using the
	//   _length field for arg0 (matching DOS's CL counter).
	// Old C++ used `s[0]` as length — a real bug because `s[0]` is the
	// FIRST CHAR of the translated text, not a Pascal length prefix.
	const byte *s = static_cast<byte *>(a[0]);
	const byte *t = static_cast<byte *>(a[1]);
	const uint16 sLen = uint16(a[0]);
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
	// CheckActorScripting: idle iff both field+0x6f (byte) and
	// field+0x6b/0x6c (word) are zero.
	const bool idle = protag->dosField(0x6f) == 0
		&& protag->dosField(0x6b) == 0
		&& protag->dosField(0x6c) == 0;
	debugC(2, kDebugLevelScript, "opcode 0x26: step+cursor==4, protag idle=%d", int(idle));
	if (!idle) return kThxBye;
	// Tail-jump to Op_41: speak as main, no target.
	protag->say(a[0]);
	return kThxBye;
}

OPCODE(0x27) {
	// DOS Op_27_RunOp3fIfStepCursor4 @ 1000:381d:
	//   if (g_flag_step_pending && g_cursor_mode == 4)
	//       Op_3f_SpeakAsMainCharacter();
	// nargs=1 — when the gate fires, dispatches into Op_3f with the same
	// arg list (which Op_3f's ResolveOpcodeArg0 will consume).
	debugC(2, kDebugLevelScript, "opcode 0x27: if step && cursor==4, speak as main");
	if (Log.stepPending() && Log.cursorMode() == 4) {
		if (Log.protagonist())
			Log.protagonist()->say(a[0]);
	}
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
	// line height, centered within a 56-cell bubble. Without the
	// verb-bubble overlay subsystem, paint the text directly via
	// Graphics::paintText centered on screen width 320 — visible
	// approximation, faithful timing/coloring.
	const byte *text = static_cast<byte *>(a[1]);
	if (text) {
		const Common::Rect metrics = _graphics->textMetrics(text);
		const uint16 textWidth = metrics.width();
		const int16 sx = (320 - int16(textWidth)) / 2;
		const uint16 x = sx > 0 ? uint16(sx) : 0;
		_graphics->paintText(x, 0xb4, 0xeb, text);
	}
	return kThxBye;
}

OPCODE(0x29) {
	// DOS Op_29_IfMode10AndFlag @ 1000:3863:
	//   if (step_pending && cursor == 0x10) {
	//       SendActorToTarget(arg1);  ; protag walks to target entity
	//       SetPostMoveCallback;       ; chain after walk completes
	//   }
	// Falls through unconditionally (no skip_counter).
	// SendActorToTarget dispatches on entity type (DX): 1=exit,
	// 2=object, 3=actor. For C++ we use the target's room/position
	// to compute a walk goal; if target is in the protagonist's
	// current room we trigger a walk to its frame.
	if (Log.stepPending() && Log.cursorMode() == 0x10) {
		const uint16 targetId = uint16(a[1]);
		Actor *protag = Log.protagonist();
		if (protag) {
			// Try resolution as actor target first.
			if (Actor *target = Log.getActor(targetId)) {
				if (target->room() == Log.currentRoom()) {
					protag->moveTo(target->frameId());
				}
			}
			// Object/exit-target dispatch needs entity-type info from
			// the arg slot wType field — until exposed in C++ Value,
			// the walk treats unresolved targets as no-ops (closest
			// match to DOS's silent-fail when target lookup fails).
		}
		debugC(2, kDebugLevelScript, "opcode 0x29: send protag to target %s (frame %s)", +a[1], +a[0]);
	}
	return kThxBye;
}

OPCODE(0x2a) {
	// DOS Op_2a_IfMode10AndFlag2 @ 1000:387e: 3-arg variant.
	//   SendActorToTarget(arg2, arg1, arg0) — same dispatch as 0x29
	//   with extra context args (probably for sub-target offset).
	if (Log.stepPending() && Log.cursorMode() == 0x10) {
		const uint16 targetId = uint16(a[1]);
		Actor *protag = Log.protagonist();
		if (protag) {
			if (Actor *target = Log.getActor(targetId)) {
				if (target->room() == Log.currentRoom())
					protag->moveTo(target->frameId());
			}
		}
		debugC(2, kDebugLevelScript, "opcode 0x2a: send protag to target %s extra %s frame %s",
			+a[1], +a[2], +a[0]);
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
	if (ac && ac->frameId() != uint16(a[0]))
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
	// DOS Op_40_SpeakAtTarget @ 1000:3da2: arg0=text, arg1=target.
	// Target positioning uses the target's sprite center; in C++
	// the bubble follows the actor's current sprite — close enough
	// for player reading. arg1 is for sprite-positioning hint only.
	debugC(1, kDebugLevelScript, "opcode 0x40: main says %s @ target %s", +a[0], +a[1]);
	speakOrSubtitle(Log.protagonist(), a[0]);
	return kThxBye;
}
OPCODE(0x42) {
	// DOS Op_42_SpeakAsMainAtTarget @ 1000:3e04: arg0=target, arg1=text.
	debugC(1, kDebugLevelScript, "opcode 0x42: main says %s @ target %s", +a[1], +a[0]);
	speakOrSubtitle(Log.protagonist(), a[1]);
	return kThxBye;
}
OPCODE(0x44) {
	// DOS Op_44_SpeakAsActorAtTarget @ 1000:3e4f: arg0=actor id,
	// arg1=target, arg2=text.
	Actor *ac = Log.getActor(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x44: actor %s says %s @ target %s", +a[0], +a[2], +a[1]);
	speakOrSubtitle(ac, a[2]);
	return kThxBye;
}
OPCODE(0x45) {
	// DOS Op_45_SpeakWithDelay @ 1000:3e68: 4 args (y, x, color, text).
	// Resolves all four; if !map_mode → AllocSpeechSlot_NoFormatting
	// + stash arg2 in g_unknown_669a (display-pos hint). Else map-mode
	// subtitle path. Same allocation as 0x46 modulo the stash slot.
	const byte *text = static_cast<byte *>(a[3]);
	if (!text) return kThxBye;
	const Common::String s(reinterpret_cast<const char *>(text));
	debugC(1, kDebugLevelScript, "opcode 0x45: speak-with-delay text='%s'", s.c_str());
	speakOrSubtitle(Log.protagonist(), s);
	return kThxBye;
}
OPCODE(0x46) {
	// DOS Op_46_SpeakWithDelayAlt @ 1000:3e5e: identical body to 0x45
	// but writes to a different g_unknown_669a-equivalent slot.
	const byte *text = static_cast<byte *>(a[3]);
	if (!text) return kThxBye;
	const Common::String s(reinterpret_cast<const char *>(text));
	debugC(1, kDebugLevelScript, "opcode 0x46: speak-with-delay-alt text='%s'", s.c_str());
	speakOrSubtitle(Log.protagonist(), s);
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
	// DOS Op_48_SpeakWithRectAndPos (CS:0x3ea7): 5 args, tail-jumps to the
	// shared speech-with-delay path (same as 0x47). Route text through
	// Graf.say like 0x47 does (iter-23).
	const byte *text = static_cast<byte *>(a[4]);
	debugC(1, kDebugLevelScript, "opcode 0x48: speak at [%s:%s] color=%s lines=%s text='%s'",
		+a[0], +a[1], +a[2], +a[3], text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
		if (length > 0)
			// Scale display time with text length (~3 ticks/char,
			// 30-tick floor) so reading speed matches DOS sample
			// pacing. Hardcoded 50 was too short for long narrator
			// lines — see Actor::Speech ctor for matching rule.
			Graf.say(text, length, MAX<uint16>(30, 3 * length));
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
	// DOS Op_4d_StashMenuArgs (CS:0x3f0c): stashes (a[0], a[1]) for the next
	// menu/bubble op (0x4f / 0x51 / 0x53 read these). Also clears the stash
	// "consumed" flag at uRam00023291.
	debugC(2, kDebugLevelScript, "opcode 0x4d: stash menu args (%s, %s)", +a[0], +a[1]);
	Log.setMenuStash(uint16(a[0]), uint16(a[1]));
	return kThxBye;
}
OPCODE(0x4e) {
	// DOS Op_4e_DrawTextRectWithChoices @ 1000:3f1e:
	//   ResolveOpcodeArg0 (text); FormatBubbleText_FullPath sets
	//   g_menu_choice_count; g_palette_overridden = 3; stash-flag=0;
	//   g_menu_max_choices = choice_count; SetRectAndApply.
	// C++ renders the text inline via Graf.say (bubble subsystem
	// abstraction). The menu_choice tracking is part of Op_50/0x54
	// which interpret the choices.
	const byte *text = static_cast<byte *>(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x4e: text-rect-with-choices text='%s'",
		text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = uint16(strlen(reinterpret_cast<const char *>(text)));
		if (length > 0)
			Graf.say(text, length, MAX<uint16>(60, 4 * length));
	}
	Log.setMenuStashConsumed(false);
	return kThxBye;
}
OPCODE(0x4f) {
	// DOS Op_4f_DrawTextRectWithChoicesAlt @ 1000:3f45: 2-arg variant
	// using stashed args from Op_4d. Same rendering path.
	const byte *text = static_cast<byte *>(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x4f: text-rect-with-choices (alt) text='%s' extra=%s",
		text ? reinterpret_cast<const char *>(text) : "(null)", +a[1]);
	if (text) {
		const uint16 length = uint16(strlen(reinterpret_cast<const char *>(text)));
		if (length > 0)
			Graf.say(text, length, MAX<uint16>(60, 4 * length));
	}
	Log.setMenuStashConsumed(false);
	return kThxBye;
}
OPCODE(0x50) {
	// DOS Op_50_OpenVerbMenuModal @ 1000:3f61: MODAL verb-menu loop.
	// Runs until user picks a choice. C++ uses Graphics::ask
	// (existing modal mechanism, line 618 Op_54).
	debugC(1, kDebugLevelScript, "opcode 0x50: open verb menu modal text=%s",
		+a[0]);
	const byte *text = static_cast<byte *>(a[0]);
	if (text) {
		const uint16 length = uint16(strlen(reinterpret_cast<const char *>(text)));
		if (length > 0)
			Graf.say(text, length, MAX<uint16>(120, 6 * length));
	}
	Log.setMenuStashConsumed(true);
	return kThxBye;
}
OPCODE(0x51) {
	// DOS Op_51_OpenVerbMenuModalAlt @ 1000:3f99: 2-arg variant of 0x50.
	debugC(1, kDebugLevelScript, "opcode 0x51: open verb menu modal (alt) text=%s extra=%s",
		+a[0], +a[1]);
	const byte *text = static_cast<byte *>(a[0]);
	if (text) {
		const uint16 length = uint16(strlen(reinterpret_cast<const char *>(text)));
		if (length > 0)
			Graf.say(text, length, MAX<uint16>(120, 6 * length));
	}
	Log.setMenuStashConsumed(true);
	return kThxBye;
}
OPCODE(0x52) {
	// DOS Op_52_DrawFixedTextBubble @ 1000:3ff6: draws static text
	// bubble (no choices). palette=2, choice_count=0.
	const byte *text = static_cast<byte *>(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x52: fixed-text-bubble text='%s'",
		text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = uint16(strlen(reinterpret_cast<const char *>(text)));
		if (length > 0)
			Graf.say(text, length, MAX<uint16>(60, 3 * length));
	}
	return kThxBye;
}
OPCODE(0x53) {
	// DOS Op_53_DrawFixedTextBubbleStashed @ 1000:3fb5: when stash
	// flag is set (Op_4d set it, Op_50/0x51 cleared it), enters
	// "stashed mode" (palette=4); else normal bubble (palette=2).
	// Both render the fixed text.
	const byte *text = static_cast<byte *>(a[0]);
	const bool stashed = Log.menuStashConsumed();
	debugC(1, kDebugLevelScript, "opcode 0x53: fixed-text-bubble-stashed=%d text='%s'",
		int(stashed), text ? reinterpret_cast<const char *>(text) : "(null)");
	if (text) {
		const uint16 length = uint16(strlen(reinterpret_cast<const char *>(text)));
		if (length > 0)
			Graf.say(text, length, MAX<uint16>(60, 3 * length));
	}
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
	// DOS Op_5a @ 1000:409d: BX = [DS:0x666e] = g_game_state.
	a[0] = Log.gameState();
	debugC(2, kDebugLevelScript, "opcode 0x5a: %s = g_game_state (%u)", +a[0], Log.gameState());
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
	// DOS Op_61_AssignExitField @ 1000:411b: arg0=exit id, arg1=value,
	// arg2=LHS slot (resolved to exit-record-relative).
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x61: AssignExitField id=%s val=%s", +a[0], +a[1]);
	a[2] = uint16(a[1]);
	return kThxBye;
}
OPCODE(0x62) {
	// DOS Op_62_AssignObjectField @ 1000:412a: arg0=obj id, arg1=value,
	// arg2=LHS slot.
	const uint16 id = uint16(a[0]);
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x62: AssignObjectField id=%s val=%s", +a[0], +a[1]);
	a[2] = uint16(a[1]);
	return kThxBye;
}
OPCODE(0x64) {
	// DOS Op_64_TableLookupAssignMain @ 1000:418c:
	//   arg3=trash, arg1=search, arg2=field offset, arg0=table.
	//   walk_speed_flag = 0 → resource segment.
	//   Search arg0 table for entry whose first word == arg1.
	//   If found, store resource_seg into entry[2 + arg2]. The
	//   entry's modified field is what arg3 LHS points at if that
	//   slot was set up as an entity-record-relative ref.
	uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	byte *base = static_cast<CodePointer &>(a[0]).base();
	if (!base) return kThxBye;
	byte *pos = base + offset;
	const uint16 width = READ_LE_UINT16(pos);
	pos += 2;
	while (true) {
		const uint16 index = READ_LE_UINT16(pos);
		if (index == 0xffff) break;
		pos += 2;
		if (index == uint16(a[1])) {
			// In DOS write resource segment id; in C++ write a sentinel
			// (0xffff) to indicate "this entry was matched". Game scripts
			// usually only check for non-zero/non-ffff afterward.
			WRITE_LE_UINT16(pos + uint16(a[2]), 0xffff);
			break;
		}
		pos += width * 2;
	}
	debugC(2, kDebugLevelScript, "opcode 0x64: TableLookupAssignMain (table @ 0x%04x search=%s)",
		offset, +a[1]);
	return kThxBye;
}
OPCODE(0x65) {
	// DOS Op_65_TableLookupAssignBlock @ 1000:4185: same as 0x64 but
	// walk_speed_flag = 1 (block segment).
	uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	byte *base = static_cast<CodePointer &>(a[0]).base();
	if (!base) return kThxBye;
	byte *pos = base + offset;
	const uint16 width = READ_LE_UINT16(pos);
	pos += 2;
	while (true) {
		const uint16 index = READ_LE_UINT16(pos);
		if (index == 0xffff) break;
		pos += 2;
		if (index == uint16(a[1])) {
			WRITE_LE_UINT16(pos + uint16(a[2]), 0xffff);
			break;
		}
		pos += width * 2;
	}
	debugC(2, kDebugLevelScript, "opcode 0x65: TableLookupAssignBlock (table @ 0x%04x search=%s)",
		offset, +a[1]);
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
	} else if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
	} else {
		warning("Op_66: exit[%u].field[+0x%02x size=%u] write — only +0/sz=2 (room) wired",
			id, off, sz);
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
	} else {
		warning("Op_67: object[%u].field[+0x%02x size=%u] — slot not modeled",
			id, off, sz);
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
	// DOS Op_7d_MoveObjectFlag1 (CS:0x4493): clear flag1 on src obj (a[0]),
	// then set flag1 on dst obj (a[1]). The "flag" lives in a per-id cell-bit
	// array shared between objects/exits — we don't model the cells yet, so
	// log only. (No room mutation — distinct from Op_7f.)
	debugC(2, kDebugLevelScript, "opcode 0x7d: move flag1 %s -> %s STUB (no cell array)",
		+a[0], +a[1]);
	return kThxBye;
}
OPCODE(0x7e) {
	// DOS Op_7e_QueueOverlay (CS:0x44a8): a[0]=entity-type sentinel
	// (1=exit, 2=object, 3=actor), a[1]=entity id. Looks up the entity's
	// (sprite, x, y) and pushes onto a draw-overlay queue capped at 250.
	debugC(2, kDebugLevelScript, "opcode 0x7e: queue overlay type %s id %s STUB (no overlay queue)",
		+a[0], +a[1]);
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
	if (id == 0) {
		debugC(2, kDebugLevelScript, "opcode 0x84: unregister (id=0)");
		Log.setDragTarget(0);
		return kThxBye;
	}
	if (id > _logic->resources()->mainDat()->personsCount()) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x84: begin drag with object %u", id);
	Log.setDragTarget(id);
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
	//   if (arg0 == drag_target) → HandleHotspotInteraction; if !ok
	//     pending-error 0x25; if ok PauseAndLockCursor.
	//   else: bound-check; HandleHotspotInteraction; if !ok pending-error 0x25.
	// = "trigger hotspot interaction for object arg0". The C++
	// EventManager handles hotspot dispatch directly; this opcode is
	// the script-driven equivalent.
	const uint16 id = uint16(a[0]);
	if (id != Log.dragTarget()) {
		if (id > _logic->resources()->mainDat()->personsCount()) {
			Log.setPendingError(0x16);
			return kThxBye;
		}
	}
	debugC(2, kDebugLevelScript, "opcode 0x88: hotspot interaction object %u (drag=%u)",
		id, Log.dragTarget());
	// HandleHotspotInteraction success path: set hit target so
	// downstream opcodes (Op_13/0x59) see the hit. Failure path
	// raises pending-error 0x25 in DOS — we conservatively treat
	// any registered object as a successful hit.
	Log.setHitTarget(id);
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
	// DOS Op_8a_handler @ 1000:47e6: same shape as Op_88 but with 3
	// args (extras unused except for bound resolution side-effect).
	// Triggers hotspot interaction for object arg0.
	const uint16 id = uint16(a[0]);
	if (id != Log.dragTarget()) {
		if (id > _logic->resources()->mainDat()->personsCount()) {
			Log.setPendingError(0x16);
			return kThxBye;
		}
	}
	debugC(2, kDebugLevelScript, "opcode 0x8a: hotspot interaction object %u (3-arg)", id);
	Log.setHitTarget(id);
	return kThxBye;
}
OPCODE(0x8b) {
	// DOS Op_8b_handler @ 1000:482e: 0 args. Calls
	// ResetObjectAtActorPosition + Op_8e (UnregisterActor). The
	// drag_target object is reset to actor's position and the actor
	// table entry is cleared. C++ approximation: clear drag target
	// (= "this drag interaction is done").
	debugC(2, kDebugLevelScript, "opcode 0x8b: reset object at actor pos + unregister");
	Log.setDragTarget(0);
	return kThxBye;
}
OPCODE(0x8c) {
	// DOS Op_8c_handler @ 1000:48c4:
	//   if (arg0 == drag_target) → Op_8b_handler (clear drag);
	//   else: GetObjectOffset(arg0); if obj.room != -1
	//         → ResetObjectAtActorPosition (move obj to actor pos).
	const uint16 id = uint16(a[0]);
	if (id == Log.dragTarget()) {
		debugC(2, kDebugLevelScript, "opcode 0x8c: drag target %u → 8b", id);
		Log.setDragTarget(0);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x8c: reset object %u at actor pos", id);
	// Reposition: place at protag's current location.
	if (Actor *protag = Log.protagonist()) {
		Log.setObjectRoom(id, protag->room());
		Log.setObjectPosition(id, int16(protag->position().x), int16(protag->position().y));
	}
	return kThxBye;
}
OPCODE(0x8d) {
	// DOS Op_8d_handler @ 1000:48df:
	//   bound-check arg0; if obj.room == -1: RemoveExitFromList;
	//   AddExitToList; if not -1 (was placed): reposition obj
	//     using arg1/arg2 + sprite offset; pending-error 0x21 if
	//     was placed (= attempting to add a 2nd time).
	// = "register object as an exit at given position, removing
	// from any previous registration first".
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
	// DOS Op_8f_handler @ 1000:4925: 1 arg.
	//   if (game_state == 1) MovePersonAndDisableObject;
	//   else pending-error 0xe.
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x8f: move person + disable obj %s", +a[0]);
	// Move person: walk-driver-dependent. Use protag.moveTo if target
	// is an actor in the current room.
	if (Actor *protag = Log.protagonist())
		if (Actor *target = Log.getActor(a[0]))
			if (target->room() == Log.currentRoom())
				protag->moveTo(target->frameId());
	return kThxBye;
}
OPCODE(0x90) {
	// DOS Op_90_handler @ 1000:4941: 2-arg variant of Op_8f.
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x90: move person + disable obj %s extra %s", +a[0], +a[1]);
	if (Actor *protag = Log.protagonist())
		if (Actor *target = Log.getActor(a[0]))
			if (target->room() == Log.currentRoom())
				protag->moveTo(target->frameId());
	return kThxBye;
}
OPCODE(0x91) {
	// DOS Op_91_handler @ 1000:4960: gate (step+cursor==1) +
	// game_state==1 → SendActorToTarget + DisableObjectFlag1 +
	// MovePersonToActor + maybe EnableObjectFlag1. Else pending-error 0xe.
	if (!Log.stepPending() || Log.cursorMode() != 1)
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x91: send-and-move %s", +a[0]);
	if (Actor *protag = Log.protagonist())
		if (Actor *target = Log.getActor(a[0]))
			if (target->room() == Log.currentRoom())
				protag->moveTo(target->frameId());
	return kThxBye;
}
OPCODE(0x92) {
	// DOS Op_92_handler @ 1000:499e: 2-arg variant of Op_91.
	if (!Log.stepPending() || Log.cursorMode() != 1)
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x92: send-and-move %s extra %s", +a[0], +a[1]);
	if (Actor *protag = Log.protagonist())
		if (Actor *target = Log.getActor(a[0]))
			if (target->room() == Log.currentRoom())
				protag->moveTo(target->frameId());
	return kThxBye;
}
OPCODE(0x93) {
	// DOS Op_93_handler @ 1000:49f1: gate (step+cursor==0x20 +
	// arg0==drag_target). game_state==1 → SendActorToTarget +
	// DisableObjectFlag1 + EnableObjectFlag1 + Op_8e. Else pending 0xf.
	if (!Log.stepPending() || Log.cursorMode() != 0x20)
		return kThxBye;
	if (uint16(a[0]) != Log.dragTarget())
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0f);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x93: drag-target send + clear (target=%s extra=%s)",
		+a[0], +a[1]);
	Log.setCursorMode(0);
	Log.setDragTarget(0);
	return kThxBye;
}
OPCODE(0x94) {
	// DOS Op_94_handler @ 1000:4a41: 0 args. Just sets
	// g_flag_misc_1 = 1 and g_flag_logic_dirty = 1. Repaint trigger.
	debugC(2, kDebugLevelScript, "opcode 0x94: mark logic dirty");
	return kThxBye;
}

OPCODE(0x97) {
	// 0x97 (DOS CS:0x4a5d, BackupCutscenePCState): nargs=0 per
	// opcodes_nargs.data. Was OOB-reading a[0]. iter-20 fix.
	debugC(2, kDebugLevelScript, "opcode 0x97: BackupCutscenePCState STUB");
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
	debugC(2, kDebugLevelScript, "opcode 0xae: actor %s walk to actor %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xaf) {
	debugC(2, kDebugLevelScript, "opcode 0xaf: actor %s walk to exit %s", +a[0], +a[1]);
	return kThxBye;
}
OPCODE(0xb0) {
	// nargs=1 — was OOB-reading a[1]. iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xb0: actor walk to object %s STUB", +a[0]);
	return kThxBye;
}
OPCODE(0xb1) {
	// nargs=1 — was OOB-reading a[1] and a[2]. iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xb1: actor walk to position %s STUB", +a[0]);
	return kThxBye;
}
OPCODE(0xb2) {
	// nargs=1 — was OOB-reading a[1]. iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xb2: actor walk variant %s STUB", +a[0]);
	return kThxBye;
}
OPCODE(0xb3) {
	// nargs=1 — was OOB-reading a[1]. iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xb3: actor walk to room exit %s STUB", +a[0]);
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
	// DOS Op_c1_UnregisterActor (CS:0x5131): nargs=0 per opcodes_nargs.data.
	// Calls UnregisterActor on the IMPLICIT actor (whichever is being
	// processed by the cast/animation loop), gated on !inMapMode. Was
	// OOB-reading a[0] AND treating it as an actor id. Without an
	// implicit-actor concept in the C++ engine, log + no-op (the C++
	// loop does its own per-actor tick management).
	debugC(2, kDebugLevelScript, "opcode 0xc1: UnregisterActor (implicit, in_map_mode=%d) STUB",
		Log.inMapMode() ? 1 : 0);
	return kThxBye;
}
OPCODE(0xc3) {
	// nargs=3 — was using only a[0]; under-use OK but log all 3.
	debugC(2, kDebugLevelScript, "opcode 0xc3: cast op %s %s %s STUB", +a[0], +a[1], +a[2]);
	return kThxBye;
}
OPCODE(0xc4) {
	// nargs=3 — was using a[0], a[1]; under-use OK.
	debugC(2, kDebugLevelScript, "opcode 0xc4: cast op %s %s %s STUB", +a[0], +a[1], +a[2]);
	return kThxBye;
}
OPCODE(0xc5) {
	// DOS Op_c5_ClearCastEntry (CS:0x51cd): nargs=1 — searches cast table
	// for entry where field+0x02 == arg0, clears it (sets active=0).
	// Was OOB-reading a[1]. iter-21 fix.
	debugC(2, kDebugLevelScript, "opcode 0xc5: ClearCastEntry %s STUB", +a[0]);
	return kThxBye;
}

OPCODE(0xca) {
	debugC(2, kDebugLevelScript, "opcode 0xca: misc state STUB");
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
	debugC(2, kDebugLevelScript, "opcode 0xe3: misc cutscene state %s,%s,%s STUB",
		+a[0], +a[1], +a[2]);
	return kThxBye;
}
OPCODE(0xe4) {
	debugC(2, kDebugLevelScript, "opcode 0xe4: anim-list append %s,%s,%s,%s STUB",
		+a[0], +a[1], +a[2], +a[3]);
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
