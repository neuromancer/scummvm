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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#ifndef NEUROMANCER_MUSIC_PLAYER_H
#define NEUROMANCER_MUSIC_PLAYER_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Audio { class PCSpeaker; }

namespace Neuromancer {

// PC-speaker music player.
//
// Role split:
//   - This class is a pure port of the DOS tracker state machine
//     (asm_set_track_on_playback + asm_get_sample + sub_20482 + sub_20513
//     from Reuromancer/LibNeuroRoutines/asm_audio.asm). It consumes the
//     7 KB data table extracted from asm_seg7.asm and emits one 16-bit PIT
//     divisor per tick at ~541 Hz.
//   - Audio synthesis is delegated to Audio::PCSpeaker (the engine-wide
//     ScummVM square/sine/saw/triangle synth in audio/softsynth/pcspk.h),
//     via its playQueue() microsecond-precise queue. No hand-rolled sample
//     generation.
//
// Integration:
//   - The engine creates one Audio::PCSpeaker (via Audio::PCSpeaker::init()
//     which plugs itself into the mixer), and passes it to this class.
//   - Scenes call setTrack(n); the engine calls tick(elapsedMs) each frame
//     which feeds divisors into the speaker.
class MusicPlayer {
public:
	explicit MusicPlayer(Audio::PCSpeaker *speaker);

	// Start playing track `trackNum`. Track 0 = silence. Re-entrant.
	void setTrack(uint16 trackNum);
	uint16 currentTrack() const { return _track; }

	// Advance the player by `elapsedMs` real milliseconds. Emits one
	// queued PCSpeaker instruction per PIT tick (~1849 us).
	void tick(uint32 elapsedMs);

private:
	// MASM ports.
	void   setTrackInternal(uint16 trackNum); // asm_set_track_on_playback
	uint32 getSampleInternal();               // asm_get_sample
	void   stepVoice(uint16 rsi);             // sub_20482
	void   advancePattern(uint16 rsi);        // sub_20513

	uint16 rd16(uint16 off) const {
		return (uint16)_seg[off] | ((uint16)_seg[(uint16)(off + 1)] << 8);
	}
	void wr16(uint16 off, uint16 val) {
		_seg[off]               = (byte)(val & 0xFF);
		_seg[(uint16)(off + 1)] = (byte)(val >> 8);
	}

	Audio::PCSpeaker  *_speaker; // not owned
	Common::Array<byte> _seg;    // mutable copy of kMusicData

	uint16 _track;
	bool   _playing;

	// Accumulator of time not yet spent on PIT ticks, in microseconds.
	uint64 _accumUs;

	uint32 _debugDivisorCount;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_MUSIC_PLAYER_H
