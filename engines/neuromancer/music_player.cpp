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

#include "neuromancer/music_player.h"

#include "neuromancer/detection.h"
#include "neuromancer/music_data.h"

#include "audio/softsynth/pcspk.h"
#include "common/debug.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// PIT timing. The DOS player reprograms the 8253 timer at divisor 0x13B1
// from a 1.193180 MHz clock -- one music tick every 1.193180e6 / 0x13B1
// ~= 540.9 Hz, i.e. 1849 microseconds. Each tick produces exactly one PIT
// divisor that (when non-zero) represents a square-wave period.
constexpr double kPitClockHz    = 1193180.0;
constexpr double kPitDivisor    = 0x13B1;
constexpr uint32 kTickPeriodUs  = (uint32)((1e6 * kPitDivisor / kPitClockHz) + 0.5); // 1849
} // anonymous namespace

MusicPlayer::MusicPlayer(Audio::PCSpeaker *speaker)
	: _speaker(speaker),
	  _track(0),
	  _playing(false),
	  _accumUs(0),
	  _debugDivisorCount(0) {
	_seg.resize(kMusicDataSize);
	memcpy(_seg.data(), kMusicData, kMusicDataSize);
}

void MusicPlayer::setTrack(uint16 trackNum) {
	if (trackNum == _track && _playing)
		return;
	_track = trackNum;
	_playing = (trackNum != 0);
	setTrackInternal(trackNum);
	_accumUs = 0;
	_debugDivisorCount = 0;

	// Diagnostic: which voices got armed?
	for (int v = 0; v < 4; v++) {
		uint16 voiceBase = (uint16)(0x2D + v * 0x30);
		uint16 pattern   = rd16((uint16)(voiceBase + 2));
		uint16 period    = rd16(voiceBase);
		debugC(1, kDebugGeneral,
		       "MusicPlayer::setTrack(%u): voice %d period=0x%04X pattern=0x%04X",
		       trackNum, v, period, pattern);
	}
}

void MusicPlayer::tick(uint32 elapsedMs) {
	if (!_playing || !_speaker)
		return;

	_accumUs += (uint64)elapsedMs * 1000;

	// Guard against falling far behind (e.g. after a debugger break):
	// cap the catch-up window at ~100 ms so we don't spin-queue thousands
	// of notes at once.
	if (_accumUs > 100000)
		_accumUs = 100000;

	while (_accumUs >= kTickPeriodUs) {
		_accumUs -= kTickPeriodUs;

		uint32 div = getSampleInternal() & 0xFFFF;
		if (_debugDivisorCount < 16) {
			debugC(1, kDebugGeneral, "MusicPlayer: divisor[%u] = 0x%04X",
			       _debugDivisorCount, div);
			_debugDivisorCount++;
		}

		if (div != 0) {
			float freq = (float)(kPitClockHz / (double)div);
			_speaker->playQueue(Audio::PCSpeaker::kWaveFormSquare, freq, kTickPeriodUs);
		} else {
			_speaker->playQueue(Audio::PCSpeaker::kWaveFormSilence, 0.0f, kTickPeriodUs);
		}
	}
}

// --------------------------------------------------------------------------
// MASM ports. Each procedure below mirrors the corresponding MASM proc in
// Reuromancer/LibNeuroRoutines/asm_audio.asm; offsets are preserved verbatim.
// --------------------------------------------------------------------------

void MusicPlayer::setTrackInternal(uint16 trackNum) {
	uint16 si = trackNum;
	wr16(0x04, si);
	si <<= 3;
	_seg[0x01] = 1;
	_seg[0x02] = 4;
	uint16 di = 0x2D;

	while (_seg[0x02] > 0) {
		uint16 entry = rd16((uint16)(si + 0x1D0));
		if (entry != 0) {
			for (int bx = 0x2E; bx >= 0; bx -= 2)
				wr16((uint16)(di + bx), 0);
			wr16((uint16)(di + 2), entry);
			wr16(di, 1);
		}
		di += 0x30;
		si += 2;
		_seg[0x02]--;
	}
	_seg[0x01] = 0;
}

uint32 MusicPlayer::getSampleInternal() {
	if (_seg[0x01] != 0)
		return 0;
	_seg[0x01] = 1;

	uint32 eax = 0;

	if (rd16(0x04) != 0) {
		wr16(0x1C5, 0);
		_seg[0x02] = 4;
		wr16(0x0A, 0x1AD);
		uint16 si = 0x2D;
		wr16(0x08, si);

		for (int i = 0; i < 4; i++) {
			if (rd16(si) != 0) {
				stepVoice(si);
				if (rd16(0x1C5) == 0 &&
				    rd16((uint16)(si + 0x0A)) != 0 &&
				    rd16(si) != 0) {
					wr16(0x1C5, si);
				}
			}
			si = (uint16)(si + 0x30);
		}

		uint16 activeSi = rd16(0x1C5);
		if (activeSi != 0) {
			uint16 off = (uint16)(activeSi + 8);
			eax =  (uint32)_seg[off]
			    | ((uint32)_seg[(uint16)(off + 1)] << 8)
			    | ((uint32)_seg[(uint16)(off + 2)] << 16)
			    | ((uint32)_seg[(uint16)(off + 3)] << 24);
		}
	}

	_seg[0x01] = 0;
	return eax;
}

void MusicPlayer::stepVoice(uint16 rsi) {
	// (+0xA) += (+0xC)
	wr16((uint16)(rsi + 0x0A),
	     (uint16)(rd16((uint16)(rsi + 0x0A)) + rd16((uint16)(rsi + 0x0C))));

	// (+0x4) += (+0x6)
	wr16((uint16)(rsi + 0x4),
	     (uint16)(rd16((uint16)(rsi + 0x4)) + rd16((uint16)(rsi + 0x6))));

	int32 dxFull = 0;

	uint16 ax = (uint16)(rd16((uint16)(rsi + 0x1E)) + rd16((uint16)(rsi + 0x20)));
	if (ax != 0) {
		uint16 mod = rd16((uint16)(rsi + 0x24));
		if (ax >= mod) ax -= mod;
		wr16((uint16)(rsi + 0x1E), ax);

		int16 axSigned = (int16)ax;
		axSigned >>= 4;
		axSigned = (int16)(axSigned + (int16)rd16((uint16)(rsi + 0x1C)));

		uint16 addr = (uint16)axSigned;
		uint8  b    = _seg[addr];

		int32 product = (int32)((int16)((uint16)b << 8)) *
		                (int32)(int16)rd16((uint16)(rsi + 0x22));
		dxFull = (product >> 16);
	}

	uint16 dx = (uint16)(dxFull + (int16)rd16((uint16)(rsi + 0x4)));
	wr16((uint16)(rsi + 0x8), dx);

	{
		uint16 decay = rd16((uint16)(rsi + 0x14));
		if (decay != 0) {
			decay--;
			wr16((uint16)(rsi + 0x14), decay);
			if (decay == 0) {
				wr16((uint16)(rsi + 0x18), 0x10);
				wr16((uint16)(rsi + 0x1A), 1);
			}
		}
	}

	{
		uint16 period = (uint16)(rd16(rsi) - 1);
		wr16(rsi, period);
		if (period == 0)
			advancePattern(rsi);
	}

	{
		uint16 env = rd16((uint16)(rsi + 0x1A));
		if (env == 0)
			return;
		env--;
		wr16((uint16)(rsi + 0x1A), env);
		if (env != 0)
			return;
	}

	for (int safety = 0; safety < 1024; safety++) {
		uint16 bx = (uint16)(rd16((uint16)(rsi + 0x16)) +
		                     rd16((uint16)(rsi + 0x18)));
		uint16 nextDur = rd16((uint16)(bx + 2));
		if (nextDur != 0xFFFF) {
			uint16 v1 = rd16(bx);
			wr16((uint16)(rsi + 0x0C), v1);
			wr16((uint16)(rsi + 0x1A), nextDur);
			wr16((uint16)(rsi + 0x18),
			     (uint16)(rd16((uint16)(rsi + 0x18)) + 4));
			return;
		}
		uint16 v = rd16(bx);
		wr16((uint16)(rsi + 0x0A), v);
		if (v == 0)
			wr16((uint16)(rsi + 0x0C), v);
		wr16((uint16)(rsi + 0x18),
		     (uint16)(rd16((uint16)(rsi + 0x18)) + 4));
	}
	warning("MusicPlayer: envelope walk safety limit hit at voice 0x%02X", rsi);
}

// Pattern byte-stream decoder. The pattern language has two kinds of bytes:
//
//  * 0x00 .. 0xF9  -- NOTE command. The high 3 bits select a destination
//                    voice (durQuant << 5), the low 5 bits are the note
//                    index into a pitch table; a 2nd byte (head) encodes
//                    pitch offset (low 7 bits) and a chain flag (bit 7 →
//                    next byte is another note, processed in-line).
//  * 0xFA .. 0xFF  -- ESCAPE command. Byte-offset 0x21 holds a dispatch
//                    table: if discriminant == 0x1DD, this is `m0` which
//                    REMAPS the current voice context to a target voice
//                    (clearing most of its working state). Otherwise it's
//                    `m1` which writes a 16-bit immediate to [rsi+idx].
//                    `m1` with idx == 0 finalises the pattern decode.
//
// Critical subtlety (MASM behaviour): `m0` mutates the `si` register,
// and subsequent `m1` writes target the NEW voice. The original rsi is
// saved in `mark_0006h` (offset 0x06) so the final pattern-pointer commit
// goes back to the voice whose pattern we're actually executing.
void MusicPlayer::advancePattern(uint16 rsiOriginal) {
	uint16 di = rd16((uint16)(rsiOriginal + 0x2));
	if (di == 0) return;

	wr16(0x06, rsiOriginal);
	uint16 rsi = rsiOriginal;

	auto commit = [&]() {
		uint16 rsiCommit = rd16(0x06);
		uint16 diFinal   = di;
		if (rd16(rsiCommit) == 0)
			diFinal = 0;
		wr16((uint16)(rsiCommit + 2), diFinal);
	};

	for (int steps = 0; steps < 4096; steps++) {
		uint8 bl = _seg[di];
		di = (uint16)(di + 1);

		// ---------- Note command (can chain via bit 7 of the head byte) ----------
		bool moreChain = true;
		while (moreChain && bl < 0xFA) {
			moreChain = false;

			uint8 durQuant = (uint8)(bl >> 5);
			uint16 di_note = (uint16)((uint16)durQuant * (uint16)_seg[0x03] +
			                          rd16(0x08));
			uint16 bx = (uint16)(bl & 0x1F);

			uint16 axv = rd16((uint16)(rsi + 0x0E));
			if ((axv & 0xFF) == 0) axv |= 1;

			uint8 mulOperand = _seg[(uint16)(bx + 0x0E)];
			axv = (uint16)((axv & 0xFF) * mulOperand);
			wr16(rsi, axv);

			uint8 head = (uint8)(_seg[di] & 0x7F);
			if (head != 0x7F) {
				uint16 cx = rd16(rsi);
				wr16(di_note, cx);
				cx = (uint16)(cx - rd16((uint16)(di_note + 0x10)));
				wr16((uint16)(di_note + 0x14), cx);

				axv = (uint16)((head + rd16((uint16)(di_note + 0x12))) & 0xFFFF);
				wr16((uint16)(di_note + 0x18), 0);
				wr16((uint16)(di_note + 0x1A), 1);

				uint16 cxcount = 0;
				while (axv >= 0x0C) { cxcount++; axv = (uint16)(axv - 0x0C); }
				uint16 freqBx = (uint16)((axv * 2) + rd16(0x0A));
				uint16 freqV  = (uint16)(rd16(freqBx) >> cxcount);
				wr16((uint16)(di_note + 0x4), freqV);
				wr16((uint16)(di_note + 0x8), freqV);
			}

			uint8 nb = _seg[di];
			di = (uint16)(di + 1);
			if (nb & 0x80) {
				// Chain bit set: the next pattern byte is the new bl and
				// we re-enter the note-processing block with it.
				bl = _seg[di];
				di = (uint16)(di + 1);
				moreChain = true;
			}
		}
		if (bl < 0xFA) {
			commit();
			return;
		}

		// ---------- Escape command (0xFA .. 0xFF) ----------
		uint16 rbx = (uint16)((bl - 0xFA) * 2);
		uint16 disc = rd16((uint16)(rbx + 0x21));

		if (disc == 0x1DD) {
			// m0: remap rsi to another voice base, clearing most of its
			// working state. The NEW rsi is used for subsequent m1 writes
			// and for any note commands that follow.
			uint16 newRsi = (uint16)(rd16(di) + rd16(0x08));
			di = (uint16)(di + 2);
			wr16((uint16)(newRsi + 0x04), 0);
			wr16((uint16)(newRsi + 0x06), 0);
			wr16((uint16)(newRsi + 0x08), 0);
			wr16((uint16)(newRsi + 0x0A), 0);
			wr16((uint16)(newRsi + 0x0C), 0);
			wr16((uint16)(newRsi + 0x10), 0);
			wr16((uint16)(newRsi + 0x12), 0);
			wr16((uint16)(newRsi + 0x16), 0);
			wr16((uint16)(newRsi + 0x18), 0);
			wr16((uint16)(newRsi + 0x1A), 0);
			wr16((uint16)(newRsi + 0x1C), 0);
			wr16((uint16)(newRsi + 0x1E), 0);
			wr16((uint16)(newRsi + 0x20), 0);
			wr16((uint16)(newRsi + 0x22), 0);
			wr16((uint16)(newRsi + 0x24), 0);
			rsi = newRsi;          // <-- MASM `si` assignment; preserved here.
			continue;
		}

		// m1: write a 16-bit immediate to [rsi + idx] using the CURRENT
		// (possibly m0-remapped) rsi. If idx == 0, finalise; the final
		// pattern-pointer commit still uses the ORIGINAL rsi saved at
		// segment offset 0x06.
		uint8  idxByte = _seg[di];
		di = (uint16)(di + 1);
		uint16 val = rd16(di);
		di = (uint16)(di + 2);

		wr16((uint16)(rsi + idxByte), val);

		if (idxByte != 0)
			continue;
		commit();
		return;
	}

	commit();
}

} // End of namespace Neuromancer
