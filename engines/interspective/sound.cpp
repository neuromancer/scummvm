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

#include "common/system.h"
#include "common/debug.h"
#include "audio/mixer.h"

#include "interspective/sound.h"
#include "interspective/debug.h"
#include "interspective/innocent.h"
#include "interspective/util.h"

namespace Interspective {

Sound::Sound(Engine *engine) :
	_engine(engine),
	_state66fe(0),
	_state6700(0),
	_state6702(0),
	_state6704(0),
	_state6706(0),
	_state6708(0),
	_state670a(0),
	_state670c(0) {
}

Sound::~Sound() {
	stopAll();
}

bool Sound::isEnabled() const {
	if (!g_system || !g_system->getMixer())
		return false;
	// Mirror DOS g_sfx_enabled — non-zero = some driver loaded.
	// In ScummVM we proxy via the mixer's sfx-volume slider: volume > 0
	// means the user wants sfx playback.
	return g_system->getMixer()->getVolumeForSoundType(Audio::Mixer::kSFXSoundType) > 0;
}

bool Sound::isSfxPlaying() const {
	if (!g_system || !g_system->getMixer())
		return false;
	return g_system->getMixer()->isSoundHandleActive(_primaryHandle) ||
	       g_system->getMixer()->isSoundHandleActive(_secondaryHandle);
}

// DOS Op_load_sfx @ 1000:56d9 — full state-transition port.
//
// Disassembly trace:
//   if (g_sfx_enabled == 0) RET;
//   AX = ResolveOpcodeArg0;             ; AX = sfx id requested
//   if (AX == [0x66fe]) RET;             ; same as last played → short-circuit
//   uVar3 = 0; bVar5 = false;
//   uVar6 = PlaySfxSound();              ; actual loader
//   iVar4 = uVar6 >> 16;
//   if (!bVar5) {
//     if (uVar6 & 0x10000) iVar4++;
//     [0x670a] = uVar3;                  ; = 0
//     [0x670c] = iVar4;                  ; slot hi
//     [0x6702] = uVar6 low;              ; slot lo
//     [0x6704] = BX;                     ; slot mid
//     pbVar2 = ResolveOpcodeArg0;        ; (re-resolve)
//     [0x66fe] = arg0;                   ; cache last-played id
//     [0x6700] = 0;                      ; clear secondary
//   }
//
// C++ port: track all the script-visible state. Audio playback
// dispatches through ScummVM Audio::Mixer (loading the actual
// sample data is part of the iuc_s*.dat format RE — separate
// data-loading task; the script-state transitions match DOS exactly
// regardless of whether actual audio is produced).
void Sound::playSfx(uint16 id) {
	if (!isEnabled())
		return;
	if (id == _state66fe) {
		// Short-circuit: same id as last played — DOS skips the entire
		// PlaySfxSound + slot-update block.
		debugC(2, kDebugLevelSound, "Sound::playSfx(%u) — short-circuit (== last_played)", id);
		return;
	}
	// Stop any prior primary playback before starting the new sample
	// (matches DOS slot-replacement model — only one primary slot).
	if (g_system && g_system->getMixer())
		g_system->getMixer()->stopHandle(_primaryHandle);

	// Update state record per DOS 1000:56d9:
	_state670a = 0;
	_state670c = 0;     // would be slot hi from PlaySfxSound; stub returns 0
	_state6702 = 0;     // would be slot lo
	_state6704 = 0;     // would be slot mid
	_state66fe = id;    // cache new last-played id
	_state6700 = 0;     // clear secondary

	// TODO: Load sample id from iuc_s*.dat banks via iuc_sdfx.dat index
	// and play through Audio::Mixer. Pending iuc_s*.dat sample-format
	// reverse-engineering (header layout: 1-byte flag + 2-byte length +
	// 2 unidentified bytes + raw 8-bit unsigned PCM).
	debugC(1, kDebugLevelSound, "Sound::playSfx(%u) — id stored, sample loader pending", id);
}

// DOS Op_f1_handler @ 1000:5725:
//   if (g_sfx_enabled == 0) RET;
//   Op_load_sfx();                       ; primary play (Op_f0 inline)
//   AX = ResolveOpcodeArg1;              ; secondary id
//   if (AX == [0x6700]) RET;              ; same as last secondary → short-circuit
//   uVar3 = PlaySfxSound();
//   if (!bVar4) {
//     [0x6706] = uVar3 lo;
//     [0x6708] = BX;
//     [0x6700] = arg1;                    ; cache secondary id
//   }
void Sound::playSfxPair(uint16 primaryId, uint16 secondaryId) {
	if (!isEnabled())
		return;
	playSfx(primaryId);  // Op_f0 inline — handles primary

	if (secondaryId == _state6700) {
		debugC(2, kDebugLevelSound,
			"Sound::playSfxPair secondary %u — short-circuit", secondaryId);
		return;
	}
	if (g_system && g_system->getMixer())
		g_system->getMixer()->stopHandle(_secondaryHandle);

	_state6706 = 0;
	_state6708 = 0;
	_state6700 = secondaryId;

	debugC(1, kDebugLevelSound,
		"Sound::playSfxPair(%u, %u) — secondary stored, sample loader pending",
		primaryId, secondaryId);
}

// DOS Op_f2_handler @ 1000:575a:
//   if (g_sfx_enabled == 0) RET;
//   ResolveOpcodeArg0;
//   DispatchSfxRangeCheck();
// DispatchSfxRangeCheck @ 1000:606d:
//   if (g_sfx_active && g_sfx_enabled) {
//     if (arg == 0) tail-call driver-table[0xc] (= sfx stop?);
//     else if (sfx-mode special path) {
//       if ([0x66fe] == 0) RET;
//       if (arg out of range [0x6702..0x6704] AND
//           arg out of range [0x6706..0x6708] OR [0x6700]==0): RET;
//       if (g_sfx_active != 0) driver-table[0xc] (stop);
//       driver-table[0xc] (play queued).
//     }
//   }
//
// = "issue a range-check play request — replay the queued slot if the
// id falls in the active-slot range, else short-circuit". Used by
// engine code to retrigger an already-loaded sample.
void Sound::rangeCheck(uint16 id) {
	if (!isEnabled())
		return;
	if (!isSfxPlaying())
		return;
	if (id == 0) {
		stopAll();
		return;
	}
	// Range check matching DOS `arg in [0x6702, 0x6704]` / `arg in
	// [0x6706, 0x6708]` (with [0x6700] != 0). The state vars hold
	// slot-ID bounds; if the requested id falls outside both ranges,
	// short-circuit per DOS.
	const bool inPrimaryRange = (id >= _state6702 && id <= _state6704);
	const bool inSecondaryRange = (_state6700 != 0 &&
	                               id >= _state6706 && id <= _state6708);
	if (!inPrimaryRange && !inSecondaryRange) {
		debugC(2, kDebugLevelSound,
			"Sound::rangeCheck(%u) — out of range [%u..%u] / [%u..%u]; no replay",
			id, _state6702, _state6704, _state6706, _state6708);
		return;
	}
	// Replay: DOS stops current playback then re-issues. Mixer
	// stopHandle + (when sample loader is wired) playStream.
	if (g_system && g_system->getMixer()) {
		g_system->getMixer()->stopHandle(_primaryHandle);
		// TODO: resume primary sample at offset 0 once loader is wired.
	}
	debugC(1, kDebugLevelSound, "Sound::rangeCheck(%u) — replay request", id);
}

void Sound::stopAll() {
	if (!g_system || !g_system->getMixer())
		return;
	g_system->getMixer()->stopHandle(_primaryHandle);
	g_system->getMixer()->stopHandle(_secondaryHandle);
}

} // namespace Interspective
