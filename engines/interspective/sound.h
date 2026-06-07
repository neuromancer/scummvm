/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef INTERSPECTIVE_SOUND_H
#define INTERSPECTIVE_SOUND_H

#include "audio/mixer.h"
#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Common {
class Serializer;
}

namespace Interspective {

class Engine;

// Sound subsystem — DOS SFX driver port.
//
// DOS architecture (per Ghidra):
// - `g_sfx_enabled` (CS:[0xe]): sfx driver mode (0=off, 2=SoundBlaster/XMS
//   sampled effects, 4=Roland effects through the music driver).
// - `g_sfx_active` (DS:0x67b7): bit-flag set while a sample is playing;
//   read by `CheckSfxPlaying @ 1000:5eb3`.
// - `pbRam0002324e` (DS:0x66fe): "last played sfx id" cache used by
//   Op_load_sfx (0xf0) for short-circuit (same arg = no replay).
// - `pbRam00023250` (DS:0x6700): secondary slot id cache for Op_f1.
// - `g_sfx_count` / `g_sfx_count_alt` (CS:[0x83]/[0x87]): sample-count
//   and SFX file-list count from iuc_main.dat footer offsets 0x24/0x28.
//
// State globals at DS:0x66fe..0x670c form a 14-byte SFX-engine record
// modified by Op_load_sfx / Op_f1 (per DOS 1000:56d9 / 0x5725):
//   [0x66fe] = current sfx id (pbRam0002324e backing)
//   [0x6700] = secondary sfx id (pbRam00023250 backing)
//   [0x6702] = primary slot lo word
//   [0x6704] = primary slot hi word
//   [0x6706] = secondary slot lo word
//   [0x6708] = secondary slot hi word
//   [0x670a] = ?
//   [0x670c] = ?
//
// File layout (IUC_SDFX.DAT + IUC_S*.DAT banks):
// - IUC_MAIN.DAT footer +0x26: SFX file-list entries used by OpenSfxFile:
//   uint16 strict-low sample id, uint8 driver mode, ASCIIZ filename.
// - IUC_SDFX.DAT: 144-byte index, 36 entries × 4-byte uint32 offsets into
//   the file selected by the footer list.
// - IUC_S01.DAT..IUC_S10.DAT: SoundBlaster Creative VOC-style sample banks.
// - IUC_SR.DAT: Roland effect tune records for mode 4.
//
// C++ port: Audio::Mixer integration — range-check playback decodes the
// VOC-style bank entry and plays it as unsigned 8-bit PCM on kSFXSoundType.
class Sound {
public:
	Sound(Engine *engine);
	~Sound();

	// DOS Op_f0_load_sfx (1000:56d9):
	//   if (sfx_enabled) {
	//     if (arg0 != last_played) {
	//       slot = PlaySfxSound(arg0);
	//       update [0x6702..0x670c] state;
	//       [0x66fe] = arg0;  [0x6700] = 0;
	//     }
	//   }
	void playSfx(uint16 id);
	void playSecondarySfx(uint16 id);

	// DOS Op_f1_handler (1000:5725):
	//   if (sfx_enabled) {
	//     Op_f0(arg0);         // primary
	//     if (arg1 != [0x6700]) {
	//       slot2 = PlaySfxSound(arg1);
	//       [0x6706/0x6708] = slot2;  [0x6700] = arg1;
	//     }
	//   }
	void playSfxPair(uint16 primaryId, uint16 secondaryId);

	// DOS Op_f2_handler (1000:575a):
	//   if (sfx_enabled) DispatchSfxRangeCheck(arg0).
	// The range check decides whether to play, replay, or short-circuit
	// based on slot bookkeeping at [0x6702..0x6708].
	void rangeCheck(uint16 id);

	// DOS PlayQueuedSfx @ 1000:6103: when a block transition reloads the
	// backdrop, replay the loaded primary/secondary SFX slots if present.
	void playQueued();

	// "Is this SFX subsystem enabled?" — gates all play paths. Mirrors
	// DOS `g_sfx_enabled` (CS:[0xe]) from the parsed DOS switch config.
	bool isEnabled() const;
	bool isActive() const { return _active; }
	void setActive(bool active) { _active = active; }

	// Currently-playing flag (DOS `g_sfx_active` bit / [0x67b7]).
	bool isSfxPlaying() const;

	// Last-played sfx id cache (DOS pbRam0002324e at [0x66fe]).
	// Op_f0 short-circuits when the same id is requested twice.
	uint16 lastPlayedId() const { return _state66fe; }
	uint16 secondaryId() const { return _state6700; }

	// Stop any currently-playing sample. DOS calls this through the
	// driver-table dispatch when transitioning rooms or restarting.
	void stopAll();
	void synchronize(Common::Serializer &s);

private:
	struct SfxBankInfo {
		uint8 bank;
		uint8 mode;
		uint16 low;
		uint16 high;
		uint32 size;
		Common::String filename;
	};
	struct SfxSampleInfo {
		SfxSampleInfo() : bank(0), offset(0), end(0), valid(false) {}
		uint8 bank;
		uint32 offset;
		uint32 end;
		bool valid;
	};

	void loadSfxMetadata() const;
	uint16 maxSfxId() const;
	bool validateSfxId(uint16 id) const;
	uint16 maxSfxBankId() const;
	bool validateSfxBankId(uint16 id) const;
	const SfxBankInfo *sfxBankForLoadId(uint16 id) const;
	bool resolveSfxSlot(uint16 id, uint32 baseBytes, uint16 &low, uint16 &high, uint32 &size) const;
	bool loadSfxSample(uint16 id, Common::Array<byte> &pcm, int &rate, bool &loop) const;
	bool loadRolandSfxTune(uint16 id, Common::Array<byte> &tune) const;
	bool playSfxSample(uint16 id, Audio::SoundHandle &handle);

	Engine *_engine;
	Audio::SoundHandle _primaryHandle;
	Audio::SoundHandle _secondaryHandle;
	// DOS state record at DS:0x66fe..0x670c:
	uint16 _state66fe; // current/last-played sfx id (pbRam0002324e)
	uint16 _state6700; // secondary sfx id (pbRam00023250)
	uint16 _state6702; // primary slot lo
	uint16 _state6704; // primary slot hi
	uint16 _state6706; // secondary slot lo
	uint16 _state6708; // secondary slot hi
	uint16 _state670a; // (unidentified)
	uint16 _state670c; // (unidentified)
	mutable Common::Array<SfxBankInfo> _sfxBanks;
	mutable Common::Array<SfxSampleInfo> _sfxSamples;
	mutable uint16 _maxSfxId;
	mutable uint16 _maxSfxBankId;
	mutable bool _sfxMetadataLoaded;
	bool _active;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_SOUND_H
