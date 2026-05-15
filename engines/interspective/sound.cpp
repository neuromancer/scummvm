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
#include "common/file.h"
#include "common/str.h"
#include "audio/mixer.h"

#include "interspective/sound.h"
#include "interspective/debug.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/util.h"

namespace Interspective {

static uint32 openFileSize(const Common::Path &path) {
	Common::File file;
	if (!file.open(path))
		return 0;
	return uint32(file.size());
}

static uint32 alignedEvenSize(uint32 size) {
	return (size + 1) & ~uint32(1);
}

Sound::Sound(Engine *engine) :
	_engine(engine),
	_state66fe(0),
	_state6700(0),
	_state6702(0),
	_state6704(0),
	_state6706(0),
	_state6708(0),
	_state670a(0),
	_state670c(0),
	_maxSfxId(0),
	_sfxMetadataLoaded(false),
	_active(true) {
}

Sound::~Sound() {
	stopAll();
}

bool Sound::isEnabled() const {
	if (!_engine)
		return false;
	// Mirror DOS g_sfx_enabled — non-zero = some driver loaded. Mixer
	// volume affects audibility, not script-visible opcode gating.
	return _engine->dosSfxEnabled() != 0;
}

bool Sound::isSfxPlaying() const {
	if (!g_system || !g_system->getMixer())
		return false;
	return g_system->getMixer()->isSoundHandleActive(_primaryHandle) ||
	       g_system->getMixer()->isSoundHandleActive(_secondaryHandle);
}

void Sound::loadSfxMetadata() const {
	if (_sfxMetadataLoaded)
		return;
	_sfxMetadataLoaded = true;
	_maxSfxId = 0;
	_sfxBanks.clear();

	Common::File index;
	if (!index.open(Common::Path("iuc_sdfx.dat")))
		return;

	const uint32 entries = uint32(index.size() / 4);
	if (entries == 0 || entries > 0xffff)
		return;

	Common::Array<uint32> offsets;
	offsets.reserve(entries);
	for (uint32 i = 0; i < entries; ++i)
		offsets.push_back(index.readUint32LE());

	_maxSfxId = uint16(entries);

	uint16 low = 0;
	uint32 bankIndex = 0;
	uint32 maxOffset = 0;
	for (uint32 i = 1; i < entries; ++i) {
		if (offsets[i] == 0) {
			SfxBankInfo bank;
			bank.low = low;
			bank.high = uint16(i);
			Common::String name = Common::String::format("iuc_s%02u.dat", bankIndex + 1);
			bank.size = openFileSize(Common::Path(name));
			if (bank.size <= maxOffset)
				bank.size = openFileSize(Common::Path("iuc_sr.dat"));
			_sfxBanks.push_back(bank);
			low = uint16(i);
			maxOffset = 0;
			++bankIndex;
		} else if (offsets[i] > maxOffset) {
			maxOffset = offsets[i];
		}
	}

	SfxBankInfo bank;
	bank.low = low;
	bank.high = uint16(entries);
	Common::String name = Common::String::format("iuc_s%02u.dat", bankIndex + 1);
	bank.size = openFileSize(Common::Path(name));
	if (bank.size <= maxOffset)
		bank.size = openFileSize(Common::Path("iuc_sr.dat"));
	_sfxBanks.push_back(bank);
}

uint16 Sound::maxSfxId() const {
	loadSfxMetadata();
	return _maxSfxId;
}

bool Sound::validateSfxId(uint16 id) const {
	if (id <= maxSfxId())
		return true;
	Log.setPendingError(0x3f);
	return false;
}

const Sound::SfxBankInfo *Sound::sfxBankForId(uint16 id) const {
	loadSfxMetadata();
	for (uint i = 0; i < _sfxBanks.size(); ++i) {
		const SfxBankInfo &bank = _sfxBanks[i];
		if (id > bank.low && id <= bank.high)
			return &bank;
	}
	return nullptr;
}

bool Sound::resolveSfxSlot(uint16 id, uint32 baseBytes, uint16 &low, uint16 &high, uint32 &size) const {
	if (!validateSfxId(id))
		return false;

	const SfxBankInfo *bank = sfxBankForId(id);
	if (!bank)
		return false;

	const uint8 mode = _engine ? _engine->dosSfxEnabled() : 0;
	if (mode == 2) {
		if (baseBytes + bank->size > 0x40000) {
			Log.setPendingError(0x40);
			return false;
		}
	} else if (mode == 4) {
		if (baseBytes + bank->size >= 0x320) {
			Log.setPendingError(0x40);
			return false;
		}
	} else {
		return false;
	}

	low = bank->low;
	high = bank->high;
	size = bank->size;
	return true;
}

// DOS Op_load_sfx @ 1000:56d9 — full state-transition port.
//
// Disassembly trace:
//   if (g_sfx_enabled == 0) RET;
//   AX = ResolveOpcodeArg0;             ; AX = sfx id requested
//   if (AX == [0x66fe]) RET;             ; same as last played → short-circuit
//   CX = 0; DX = 0;
//   PlaySfxSound();                      ; AX/BX=bank id bounds, CX/DX=size
//   if (carry clear) {
//     if (DX & 1) { DX++; ADC CX,0; }     ; align loaded primary byte size
//     [0x670a] = CX;
//     [0x670c] = DX;
//     [0x6702] = AX;
//     [0x6704] = BX;
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
	uint16 low = 0;
	uint16 high = 0;
	uint32 size = 0;
	if (!resolveSfxSlot(id, 0, low, high, size))
		return;
	// Stop any prior primary playback before starting the new sample
	// (matches DOS slot-replacement model — only one primary slot).
	if (g_system && g_system->getMixer())
		g_system->getMixer()->stopHandle(_primaryHandle);

	// Update state record per DOS 1000:56d9:
	const uint32 alignedSize = alignedEvenSize(size);
	_state670a = uint16(alignedSize >> 16);
	_state670c = uint16(alignedSize);
	_state6702 = low;
	_state6704 = high;
	_state66fe = id;    // cache new last-played id
	_state6700 = 0;     // clear secondary

	// TODO: Load sample id from iuc_s*.dat banks via iuc_sdfx.dat index
	// and play through Audio::Mixer. Pending iuc_s*.dat sample-format
	// reverse-engineering (header layout: 1-byte flag + 2-byte length +
	// 2 unidentified bytes + raw 8-bit unsigned PCM).
	debugC(1, kDebugLevelSound,
		"Sound::playSfx(%u) — range [%u..%u] size=%u, sample loader pending",
		id, _state6702, _state6704, size);
}

// DOS Op_f1_handler @ 1000:5725:
//   if (g_sfx_enabled == 0) RET;
//   Op_load_sfx();                       ; primary play (Op_f0 inline)
//   AX = ResolveOpcodeArg1;              ; secondary id
//   if (AX == [0x6700]) RET;              ; same as last secondary → short-circuit
//   CX = [0x670a]; DX = [0x670c];
//   PlaySfxSound();                      ; AX/BX=secondary bank id bounds
//   if (carry clear) {
//     [0x6706] = AX;
//     [0x6708] = BX;
//     [0x6700] = arg1;                    ; cache secondary id
//   }
void Sound::playSfxPair(uint16 primaryId, uint16 secondaryId) {
	if (!isEnabled())
		return;
	playSfx(primaryId);  // Op_f0 inline — handles primary
	playSecondarySfx(secondaryId);
}

void Sound::playSecondarySfx(uint16 secondaryId) {
	if (!isEnabled())
		return;
	if (secondaryId == _state6700) {
		debugC(2, kDebugLevelSound,
			"Sound::playSfxPair secondary %u — short-circuit", secondaryId);
		return;
	}
	uint16 low = 0;
	uint16 high = 0;
	uint32 size = 0;
	const uint32 primarySize = (uint32(_state670a) << 16) | _state670c;
	if (!resolveSfxSlot(secondaryId, primarySize, low, high, size))
		return;
	if (g_system && g_system->getMixer())
		g_system->getMixer()->stopHandle(_secondaryHandle);

	_state6706 = low;
	_state6708 = high;
	_state6700 = secondaryId;

	debugC(1, kDebugLevelSound,
		"Sound::playSecondarySfx(%u) — range [%u..%u] size=%u, sample loader pending",
		secondaryId, _state6706, _state6708, size);
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
	if (!_active)
		return;
	if (id == 0) {
		stopAll();
		return;
	}
	if (_state66fe == 0)
		return;
	// Range check matching DOS `arg in [0x6702, 0x6704]` / `arg in
	// [0x6706, 0x6708]` (with [0x6700] != 0). DOS uses strict lower
	// bounds and inclusive upper bounds: `arg > low && arg <= high`.
	// The state vars hold
	// slot-ID bounds; if the requested id falls outside both ranges,
	// short-circuit per DOS.
	const bool inPrimaryRange = (id > _state6702 && id <= _state6704);
	const bool inSecondaryRange = (_state6700 != 0 &&
	                               id > _state6706 && id <= _state6708);
	if (!inPrimaryRange && !inSecondaryRange) {
		debugC(2, kDebugLevelSound,
			"Sound::rangeCheck(%u) — out of range [%u..%u] / [%u..%u]; no replay",
			id, _state6702, _state6704, _state6706, _state6708);
		return;
	}
	// Replay: DOS stops current playback only when [0x67b7] is nonzero,
	// then issues the driver play request.
	if (isSfxPlaying() && g_system && g_system->getMixer()) {
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
