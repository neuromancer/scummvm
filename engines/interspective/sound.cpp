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
#include "common/endian.h"
#include "common/file.h"
#include "common/serializer.h"
#include "common/str.h"
#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
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

static uint32 readUint24LE(const byte *p) {
	return uint32(p[0]) | (uint32(p[1]) << 8) | (uint32(p[2]) << 16);
}

static int vocRateFromTimeConstant(uint8 tc) {
	const int denom = 256 - tc;
	return denom > 0 ? MAX<int>(1000, 1000000 / denom) : 11025;
}

static int vocRateFromExtendedTimeConstant(uint16 tc, uint8 channels) {
	const int denom = 65536 - tc;
	const int channelCount = channels ? channels : 1;
	return denom > 0 ? MAX<int>(1000, 256000000 / denom / channelCount) : 11025;
}

static void appendBytes(Common::Array<byte> &out, const byte *src, uint32 len) {
	if (len == 0)
		return;
	const uint oldSize = out.size();
	out.resize(oldSize + len);
	memcpy(&out[oldSize], src, len);
}

static void appendSilence(Common::Array<byte> &out, uint32 samples) {
	if (samples == 0)
		return;
	const uint oldSize = out.size();
	out.resize(oldSize + samples);
	memset(&out[oldSize], 0x80, samples);
}

static bool parseVocSfxBlocks(const byte *data, uint32 size, Common::Array<byte> &pcm,
		int &rate, bool &loop) {
	uint32 pos = 0;
	uint32 repeatStart = 0;
	uint16 repeatCount = 0;
	bool repeatActive = false;
	bool haveExtendedRate = false;
	int extendedRate = 0;

	rate = 11025;
	loop = false;
	while (pos < size) {
		const byte blockType = data[pos++];
		if (blockType == 0)
			return !pcm.empty();
		if (pos + 3 > size)
			return !pcm.empty();

		const uint32 blockLen = readUint24LE(data + pos);
		pos += 3;
		if (blockLen > size - pos)
			return !pcm.empty();

		const byte *payload = data + pos;
		switch (blockType) {
		case 1:
			if (blockLen >= 2) {
				if (!haveExtendedRate)
					rate = vocRateFromTimeConstant(payload[0]);
				appendBytes(pcm, payload + 2, blockLen - 2);
			}
			haveExtendedRate = false;
			break;
		case 2:
			appendBytes(pcm, payload, blockLen);
			break;
		case 3:
			if (blockLen >= 3) {
				if (!haveExtendedRate)
					rate = vocRateFromTimeConstant(payload[2]);
				appendSilence(pcm, uint32(READ_LE_UINT16(payload)) + 1);
			}
			break;
		case 6:
			if (blockLen >= 2) {
				repeatStart = pcm.size();
				repeatCount = READ_LE_UINT16(payload);
				repeatActive = true;
				if (repeatCount == 0xffff)
					loop = true;
			}
			break;
		case 7:
			if (repeatActive && repeatCount != 0xffff && pcm.size() > repeatStart) {
				Common::Array<byte> repeated;
				appendBytes(repeated, &pcm[repeatStart], pcm.size() - repeatStart);
				for (uint16 i = 0; i < repeatCount; ++i)
					appendBytes(pcm, &repeated[0], repeated.size());
			}
			repeatActive = false;
			break;
		case 8:
			if (blockLen >= 4) {
				const uint8 channels = uint8(payload[3] + 1);
				extendedRate = vocRateFromExtendedTimeConstant(READ_LE_UINT16(payload), channels);
				rate = extendedRate;
				haveExtendedRate = true;
			}
			break;
		case 9:
			if (blockLen >= 12 && payload[6] == 8 && payload[8] == 0) {
				rate = int(READ_LE_UINT32(payload));
				appendBytes(pcm, payload + 12, blockLen - 12);
			}
			break;
		default:
			break;
		}
		pos += blockLen;
		if (haveExtendedRate)
			rate = extendedRate;
	}
	return !pcm.empty();
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

void Sound::synchronize(Common::Serializer &s) {
	uint8 active = _active ? 1 : 0;
	uint16 state66fe = _state66fe;
	uint16 state6700 = _state6700;
	uint16 state6702 = _state6702;
	uint16 state6704 = _state6704;
	uint16 state6706 = _state6706;
	uint16 state6708 = _state6708;
	uint16 state670a = _state670a;
	uint16 state670c = _state670c;

	s.syncAsByte(active);
	s.syncAsUint16LE(state66fe);
	s.syncAsUint16LE(state6700);
	s.syncAsUint16LE(state6702);
	s.syncAsUint16LE(state6704);
	s.syncAsUint16LE(state6706);
	s.syncAsUint16LE(state6708);
	s.syncAsUint16LE(state670a);
	s.syncAsUint16LE(state670c);

	if (s.isLoading()) {
		stopAll();
		_active = active != 0;
		_state66fe = state66fe;
		_state6700 = state6700;
		_state6702 = state6702;
		_state6704 = state6704;
		_state6706 = state6706;
		_state6708 = state6708;
		_state670a = state670a;
		_state670c = state670c;
	}
}

void Sound::loadSfxMetadata() const {
	if (_sfxMetadataLoaded)
		return;
	_sfxMetadataLoaded = true;
	_maxSfxId = 0;
	_sfxBanks.clear();
	_sfxSamples.clear();

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
	_sfxSamples.resize(entries);

	uint16 low = 0;
	uint32 bankIndex = 0;
	uint32 maxOffset = 0;
	for (uint32 i = 1; i < entries; ++i) {
		if (offsets[i] == 0) {
			SfxBankInfo bank;
			bank.bank = uint8(bankIndex + 1);
			bank.low = low;
			bank.high = uint16(i);
			Common::String name = Common::String::format("iuc_s%02u.dat", bankIndex + 1);
			const uint32 fileSize = openFileSize(Common::Path(name));
			bank.size = fileSize;
			if (bank.size <= maxOffset)
				bank.size = openFileSize(Common::Path("iuc_sr.dat"));
			_sfxBanks.push_back(bank);
			for (uint32 j = low; j < i; ++j) {
				const uint32 end = (j + 1 < i) ? offsets[j + 1] : fileSize;
				if (offsets[j] < fileSize && offsets[j] < end && end <= fileSize) {
					_sfxSamples[j].bank = bank.bank;
					_sfxSamples[j].offset = offsets[j];
					_sfxSamples[j].end = end;
					_sfxSamples[j].valid = true;
				}
			}
			low = uint16(i);
			maxOffset = 0;
			++bankIndex;
		} else if (offsets[i] > maxOffset) {
			maxOffset = offsets[i];
		}
	}

	SfxBankInfo bank;
	bank.bank = uint8(bankIndex + 1);
	bank.low = low;
	bank.high = uint16(entries);
	Common::String name = Common::String::format("iuc_s%02u.dat", bankIndex + 1);
	const uint32 fileSize = openFileSize(Common::Path(name));
	bank.size = fileSize;
	if (bank.size <= maxOffset)
		bank.size = openFileSize(Common::Path("iuc_sr.dat"));
	_sfxBanks.push_back(bank);
	for (uint32 j = low; j < entries; ++j) {
		const uint32 end = (j + 1 < entries) ? offsets[j + 1] : fileSize;
		if (offsets[j] < fileSize && offsets[j] < end && end <= fileSize) {
			_sfxSamples[j].bank = bank.bank;
			_sfxSamples[j].offset = offsets[j];
			_sfxSamples[j].end = end;
			_sfxSamples[j].valid = true;
		}
	}
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

bool Sound::loadSfxSample(uint16 id, Common::Array<byte> &pcm, int &rate, bool &loop) const {
	loadSfxMetadata();
	if (id == 0 || id > _sfxSamples.size())
		return false;

	const SfxSampleInfo &sample = _sfxSamples[id - 1];
	if (!sample.valid)
		return false;

	Common::String name = Common::String::format("iuc_s%02u.dat", sample.bank);
	Common::File file;
	if (!file.open(Common::Path(name)))
		return false;

	const uint32 bytes = sample.end - sample.offset;
	Common::Array<byte> raw;
	raw.resize(bytes);
	file.seek(sample.offset);
	if (file.read(&raw[0], bytes) != bytes)
		return false;

	pcm.clear();
	return parseVocSfxBlocks(&raw[0], raw.size(), pcm, rate, loop);
}

bool Sound::playSfxSample(uint16 id, Audio::SoundHandle &handle) {
	if (!g_system || !g_system->getMixer())
		return false;

	Common::Array<byte> pcm;
	int rate = 0;
	bool loop = false;
	if (!loadSfxSample(id, pcm, rate, loop)) {
		debugC(1, kDebugLevelSound, "Sound::playSfxSample(%u) — sample decode failed", id);
		return false;
	}

	byte *buf = (byte *)malloc(pcm.size());
	if (!buf)
		return false;
	memcpy(buf, &pcm[0], pcm.size());

	Audio::SeekableAudioStream *raw = Audio::makeRawStream(buf, pcm.size(),
		rate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	if (!raw) {
		free(buf);
		return false;
	}

	Audio::AudioStream *stream = loop ? Audio::makeLoopingAudioStream(raw, 0) : raw;
	g_system->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &handle,
		stream, -1, Audio::Mixer::kMaxChannelVolume, 0, DisposeAfterUse::YES);
	debugC(1, kDebugLevelSound, "Sound::playSfxSample(%u) — %u bytes @ %d Hz%s",
		id, pcm.size(), rate, loop ? " loop" : "");
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
	// C++ port: track all the script-visible state. DOS only loads the
	// bank here; the later DispatchSfxRangeCheck path starts playback.
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
	// Update state record per DOS 1000:56d9:
	const uint32 alignedSize = alignedEvenSize(size);
	_state670a = uint16(alignedSize >> 16);
	_state670c = uint16(alignedSize);
	_state6702 = low;
	_state6704 = high;
	_state66fe = id;    // cache new last-played id
	_state6700 = 0;     // clear secondary

	debugC(1, kDebugLevelSound,
		"Sound::playSfx(%u) — loaded range [%u..%u] size=%u",
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

	_state6706 = low;
	_state6708 = high;
	_state6700 = secondaryId;

	debugC(1, kDebugLevelSound,
		"Sound::playSecondarySfx(%u) — loaded range [%u..%u] size=%u",
		secondaryId, _state6706, _state6708, size);
}

// DOS Op_f2_handler @ 1000:575a:
//   if (g_sfx_enabled == 0) RET;
//   ResolveOpcodeArg0;
//   DispatchSfxRangeCheck();
// DispatchSfxRangeCheck @ 1000:606d:
//   if (g_sfx_active && g_sfx_enabled) {
	//     if (arg == 0) tail-call driver command 8 (= stop);
//     else if (sfx-mode special path) {
//       if ([0x66fe] == 0) RET;
//       if (arg out of range [0x6702..0x6704] AND
//           arg out of range [0x6706..0x6708] OR [0x6700]==0): RET;
	//       if (g_sfx_active != 0) driver command 8 (stop);
	//       driver command 0xe (play at queued offset).
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
	if (isSfxPlaying())
		stopAll();
	playSfxSample(id, _primaryHandle);
}

void Sound::playQueuedLikeDos() {
	if (!isEnabled() || !_active || _state66fe == 0)
		return;

	// PlayQueuedSfx @ 1000:6103 calls PlaySfxSound, the same non-audible
	// sample-cache loader used by Op_f0/Op_f1. Actual playback happens only
	// through DispatchSfxRangeCheck. Our resource backend loads samples on
	// demand, so the room-change cache refresh is a no-op.
	debugC(1, kDebugLevelSound,
		"Sound::playQueuedLikeDos() — cache retained primary=%u secondary=%u",
		_state66fe, _state6700);
}

void Sound::stopAll() {
	if (!g_system || !g_system->getMixer())
		return;
	g_system->getMixer()->stopHandle(_primaryHandle);
	g_system->getMixer()->stopHandle(_secondaryHandle);
}

} // namespace Interspective
