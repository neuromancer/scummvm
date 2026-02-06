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

#include "common/file.h"
#include "common/memstream.h"

#include "freescape/freescape.h"
#include "freescape/games/driller/driller.h"
#include "freescape/language/8bitDetokeniser.h"

#include "audio/audiostream.h"
#include "audio/softsynth/ay8912.h"

namespace Freescape {

void DrillerEngine::initCPC() {
	_viewArea = Common::Rect(36, 16, 284, 117);
	_soundIndexShoot = 1;
	_soundIndexCollide = 2;
	_soundIndexStepUp = 3;
	_soundIndexStepDown = 4;
	_soundIndexMenu = 6;
	_soundIndexAreaChange = 10;
	_soundIndexHit = 7;
	_soundIndexFallen = 9;
	_soundIndexMissionComplete = 13;
}

byte kCPCPaletteTitleData[4][3] = {
	{0x00, 0x00, 0x00},
	{0x00, 0x80, 0xff},
	{0xff, 0x00, 0x00},
	{0xff, 0xff, 0x00},
};

byte kCPCPaletteBorderData[4][3] = {
	{0x00, 0x00, 0x00},
	{0xff, 0x80, 0x00},
	{0x80, 0xff, 0xff},
	{0x00, 0x80, 0x00},
};

byte getCPCPixelMode1(byte cpc_byte, int index) {
	if (index == 0)
		return ((cpc_byte & 0x08) >> 2) | ((cpc_byte & 0x80) >> 7);
	else if (index == 1)
		return ((cpc_byte & 0x04) >> 1) | ((cpc_byte & 0x40) >> 6);
	else if (index == 2)
		return (cpc_byte & 0x02)        | ((cpc_byte & 0x20) >> 5);
	else if (index == 3)
		return ((cpc_byte & 0x01) << 1) | ((cpc_byte & 0x10) >> 4);
	else
		error("Invalid index %d requested", index);
}

byte getCPCPixelMode0(byte cpc_byte, int index) {
    if (index == 0) {
        // Extract Pixel 0 from the byte
        return ((cpc_byte & 0x02) >> 1) |  // Bit 1 -> Bit 3 (MSB)
               ((cpc_byte & 0x20) >> 4) |  // Bit 5 -> Bit 2
               ((cpc_byte & 0x08) >> 1) |  // Bit 3 -> Bit 1
               ((cpc_byte & 0x80) >> 7);   // Bit 7 -> Bit 0 (LSB)
    }
    else if (index == 2) {
        // Extract Pixel 1 from the byte
        return ((cpc_byte & 0x01) << 3) |  // Bit 0 -> Bit 3 (MSB)
               ((cpc_byte & 0x10) >> 2) |  // Bit 4 -> Bit 2
               ((cpc_byte & 0x04) >> 1) |  // Bit 2 -> Bit 1
               ((cpc_byte & 0x40) >> 6);   // Bit 6 -> Bit 0 (LSB)
    }
    else {
        error("Invalid index %d requested", index);
    }
}

byte getCPCPixel(byte cpc_byte, int index, bool mode1) {
	if (mode1)
		return getCPCPixelMode1(cpc_byte, index);
	else
		return getCPCPixelMode0(cpc_byte, index);
}

Graphics::ManagedSurface *readCPCImage(Common::SeekableReadStream *file, bool mode1) {
	Graphics::ManagedSurface *surface = new Graphics::ManagedSurface();
	surface->create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
	surface->fillRect(Common::Rect(0, 0, 320, 200), 0);

	int x, y;
	file->seek(0x80);
	for (int block = 0; block < 8; block++) {
		for (int line = 0; line < 25; line++) {
			for (int offset = 0; offset < 320 / 4; offset++) {
				byte cpc_byte = file->readByte(); // Get CPC byte

				// Process first pixel
				int pixel_0 = getCPCPixel(cpc_byte, 0, mode1); // %Aa
				y = line * 8 + block ; // Coord Y for the pixel
				x = 4 * offset + 0; // Coord X for the pixel
				surface->setPixel(x, y, pixel_0);

				// Process second pixel
				y = line * 8 + block ; // Coord Y for the pixel
				x = 4 * offset + 1; // Coord X for the pixel
				if (mode1) {
					int pixel_1 = getCPCPixel(cpc_byte, 1, mode1); // %Bb
					surface->setPixel(x, y, pixel_1);
				} else
					surface->setPixel(x, y, pixel_0);

				// Process third pixel
				int pixel_2 = getCPCPixel(cpc_byte, 2, mode1); // %Cc
				y = line * 8 + block ; // Coord Y for the pixel
				x = 4 * offset + 2; // Coord X for the pixel
				surface->setPixel(x, y, pixel_2);

				// Process fourth pixel
				y = line * 8 + block ; // Coord Y for the pixel
				x = 4 * offset + 3; // Coord X for the pixel
				if (mode1) {
					int pixel_3 = getCPCPixel(cpc_byte, 3, mode1); // %Dd
					surface->setPixel(x, y, pixel_3);
				} else
					surface->setPixel(x, y, pixel_2);
			}
		}
		// We should skip the next 48 bytes, because they are padding the block to be 2048 bytes
		file->seek(48, SEEK_CUR);
	}
	return surface;
}

void DrillerEngine::loadAssetsCPCFullGame() {
	Common::File file;

	file.open("DSCN1.BIN");
	if (!file.isOpen())
		error("Failed to open DSCN1.BIN");

	_title = readCPCImage(&file, true);
	_title->setPalette((byte*)&kCPCPaletteTitleData, 0, 4);

	file.close();
	file.open("DSCN2.BIN");
	if (!file.isOpen())
		error("Failed to open DSCN2.BIN");

	_border = readCPCImage(&file, true);
	_border->setPalette((byte*)&kCPCPaletteBorderData, 0, 4);

	file.close();
	file.open("DRILL.BIN");

	if (!file.isOpen())
		error("Failed to open DRILL.BIN");

	loadSoundsCPC(&file, 0x23D2 + 0x80, 0x2416 + 0x80, 0x247E + 0x80);
	loadMessagesFixedSize(&file, 0x214c, 14, 20);
	loadFonts(&file, 0x5b69);
	loadGlobalObjects(&file, 0x1d07, 8);
	load8bitBinary(&file, 0x5ccb, 16);
}

void FreescapeEngine::loadSoundsCPC(Common::SeekableReadStream *file, int offsetTone, int offsetEnvelope, int offsetSoundDef) {
	// DRILL.BIN has a 128-byte AMSDOS header, so file offsets = (memory_addr - 0x1C62) + 0x80
	// Tone table at l4034h: volume envelope data (68 bytes = gap to envelope table)
	_soundsCPCToneTable.resize(68);
	file->seek(offsetTone);
	file->read(_soundsCPCToneTable.data(), 68);

	// Envelope table at l4078h: pitch sweep data (104 bytes = gap to sound def table)
	_soundsCPCEnvelopeTable.resize(104);
	file->seek(offsetEnvelope);
	file->read(_soundsCPCEnvelopeTable.data(), 104);

	// Sound definition table at l40e0h: 20 entries × 7 bytes = 140 bytes
	_soundsCPCSoundDefTable.resize(140);
	file->seek(offsetSoundDef);
	file->read(_soundsCPCSoundDefTable.data(), 140);
}

void DrillerEngine::drawCPCUI(Graphics::Surface *surface) {
	uint32 color = _currentArea->_underFireBackgroundColor;
	uint8 r, g, b;

	_gfx->readFromPalette(color, r, g, b);
	uint32 front = _gfx->_texturePixelFormat.ARGBToColor(0xFF, r, g, b);

	color = _currentArea->_usualBackgroundColor;
	if (_gfx->_colorRemaps && _gfx->_colorRemaps->contains(color)) {
		color = (*_gfx->_colorRemaps)[color];
	}

	_gfx->readFromPalette(color, r, g, b);
	uint32 back = _gfx->_texturePixelFormat.ARGBToColor(0xFF, r, g, b);

	int score = _gameStateVars[k8bitVariableScore];
	drawStringInSurface(_currentArea->_name, 200, 185, front, back, surface);
	drawStringInSurface(Common::String::format("%04d", int(2 * _position.x())), 151, 145, front, back, surface);
	drawStringInSurface(Common::String::format("%04d", int(2 * _position.z())), 151, 153, front, back, surface);
	drawStringInSurface(Common::String::format("%04d", int(2 * _position.y())), 151, 161, front, back, surface);
	if (_playerHeightNumber >= 0)
		drawStringInSurface(Common::String::format("%d", _playerHeightNumber), 54, 161, front, back, surface);
	else
		drawStringInSurface(Common::String::format("%s", "J"), 54, 161, front, back, surface);

	drawStringInSurface(Common::String::format("%02d", int(_angleRotations[_angleRotationIndex])), 47, 145, front, back, surface);
	drawStringInSurface(Common::String::format("%3d", _playerSteps[_playerStepIndex]), 44, 153, front, back, surface);
	drawStringInSurface(Common::String::format("%07d", score), 239, 129, front, back, surface);

	int seconds, minutes, hours;
	getTimeFromCountdown(seconds, minutes, hours);
	drawStringInSurface(Common::String::format("%02d", hours), 209, 8, front, back, surface);
	drawStringInSurface(Common::String::format("%02d", minutes), 232, 8, front, back, surface);
	drawStringInSurface(Common::String::format("%02d", seconds), 255, 8, front, back, surface);

	Common::String message;
	int deadline;
	getLatestMessages(message, deadline);
	if (deadline <= _countdown) {
		drawStringInSurface(message, 191, 177, back, front, surface);
		_temporaryMessages.push_back(message);
		_temporaryMessageDeadlines.push_back(deadline);
	} else if (_messagesList.size() > 0) {
		if (_currentArea->_gasPocketRadius == 0)
			message = _messagesList[2];
		else if (_drillStatusByArea[_currentArea->getAreaID()])
			message = _messagesList[0];
		else
			message = _messagesList[1];

		drawStringInSurface(message, 191, 177, front, back, surface);
	}

	int energy = _gameStateVars[k8bitVariableEnergy];
	int shield = _gameStateVars[k8bitVariableShield];

	if (energy >= 0) {
		Common::Rect backBar(25, 184, 89 - energy, 191);
		surface->fillRect(backBar, back);
		Common::Rect energyBar(88 - energy, 184, 88, 191);
		surface->fillRect(energyBar, front);
	}

	if (shield >= 0) {
		Common::Rect backBar(25, 177, 89 - shield, 183);
		surface->fillRect(backBar, back);

		Common::Rect shieldBar(88 - shield, 177, 88, 183);
		surface->fillRect(shieldBar, front);
	}

	drawCompass(surface, 87, 156, _yaw - 30, 10, 75, front);
	drawCompass(surface, 230, 156, _pitch - 30, 10, 60, front);
}

/**
 * Driller CPC Sound Implementation
 *
 * Based on reverse engineering of DRILL.BIN (loads at 0x1c62).
 *
 * All sounds use the sub_4760h system (0x4760-0x4871):
 *   - Sound initialization loads 7-byte entry from l40e0h
 *   - Volume envelope from "Tone" Table at l4034h
 *   - Pitch sweep from "Envelope" Table at l4078h
 *   - 300Hz interrupt-driven update via sub_7571h (0x7571-0x76A9)
 *
 * AY-3-8912 PSG with 1MHz clock, register write at 0x4872:
 *   Port 0xF4 = register select, Port 0xF6 = data
 *
 * ---- Sound Definition Table (l40e0h) ----
 * 20 entries, 7 bytes each. Loaded by sub_4760h with 1-based sound number.
 *   Byte 0: flags
 *     - Bits 0-1: channel number (1=A, 2=B, 3=C)
 *     - Bit 2: tone disable (0 = enable tone, 1 = disable)
 *     - Bit 3: noise disable (0 = enable noise, 1 = disable)
 *   Byte 1: "tone" table index (volume envelope)
 *   Byte 2: "envelope" table index (pitch sweep)
 *   Bytes 3-4: initial AY tone period (little-endian, 12-bit)
 *   Byte 5: initial AY volume (0-15)
 *   Byte 6: duration (repeat count; 0 = single play)
 *
 * ---- "Tone" Table (l4034h) - Volume Envelope ----
 * Despite the name, this table controls VOLUME modulation, not pitch.
 * Indexed by 4-byte stride: base = index * 4.
 *   Byte 0: number of triplets (N)
 *   Then N triplets of 3 bytes each:
 *     Byte 0: counter - how many times to apply the delta
 *     Byte 1: delta (signed) - added to volume each step
 *     Byte 2: limit - ticks between each application
 *
 * Volume update algorithm (sub_7571h, l763ah):
 *   Every tick: decrement limit countdown. When it reaches 0:
 *     1. Reload limit countdown from current triplet's limit
 *     2. volume = (volume + delta) & 0x0F
 *     3. Decrement counter. When it reaches 0:
 *        Advance to next triplet, or set finishedFlag if all done.
 *
 * ---- "Envelope" Table (l4078h) - Pitch Sweep ----
 * Despite the name, this table controls PITCH modulation, not envelope.
 * Indexed by 4-byte stride: base = index * 4.
 *   Byte 0: number of triplets (N)
 *   Then N triplets of 3 bytes each:
 *     Byte 0: counter - how many times to apply the delta
 *     Byte 1: delta (signed) - added to period each step
 *     Byte 2: limit - ticks between each application
 *
 * Pitch update algorithm (sub_7571h, l758bh):
 *   Every tick: decrement limit countdown. When it reaches 0:
 *     1. Reload limit countdown from current triplet's limit
 *     2. period += sign_extend(delta) (natural 16-bit wrapping)
 *     3. Decrement counter. When it reaches 0:
 *        Advance to next triplet, or if all done:
 *          - Decrement duration. If 0 -> shutdown (silence + deactivate).
 *          - If duration > 0 -> restart BOTH volume and pitch from beginning.
 */

class DrillerCPCSfxStream : public Audio::AudioStream {
public:
	DrillerCPCSfxStream(int index, const byte *soundDefTable, const byte *toneTable, const byte *envelopeTable, int rate = 44100)
		: _ay(rate, 1000000), _rate(rate),
		  _soundDefTable(soundDefTable), _toneTable(toneTable), _envelopeTable(envelopeTable) {
		_finished = false;
		_tickSampleCount = 0;

		// Reset all AY registers to match CPC init state
		for (int r = 0; r < 14; r++)
			_ay.setReg(r, 0);
		// Noise period from CPC init table at 0x66A4h (verified in binary)
		_ay.setReg(6, 0x07);

		memset(&_ch, 0, sizeof(_ch));
		setupSound(index);
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		if (_finished)
			return 0;

		int samplesGenerated = 0;
		// AY8912Stream is stereo: readBuffer counts int16 values (2 per frame).
		// CPC interrupts fire at 300Hz (6 per frame). sub_7571h is called
		// unconditionally at every interrupt (0x68DD), NOT inside the 50Hz divider.
		int samplesPerTick = (_rate / 300) * 2;

		while (samplesGenerated < numSamples && !_finished) {
			// Generate samples until next tick
			int remaining = samplesPerTick - _tickSampleCount;
			int toGenerate = MIN(numSamples - samplesGenerated, remaining);

			if (toGenerate > 0) {
				_ay.readBuffer(buffer + samplesGenerated, toGenerate);
				samplesGenerated += toGenerate;
				_tickSampleCount += toGenerate;
			}

			// Run interrupt handler at 300Hz tick boundary
			if (_tickSampleCount >= samplesPerTick) {
				_tickSampleCount -= samplesPerTick;
				tickUpdate();
			}
		}

		return samplesGenerated;
	}

	bool isStereo() const override { return true; }
	bool endOfData() const override { return _finished; }
	bool endOfStream() const override { return _finished; }
	int getRate() const override { return _rate; }

private:
	Audio::AY8912Stream _ay;
	int _rate;
	bool _finished;
	int _tickSampleCount; // Samples generated in current tick

	// Pointers to table data loaded from DRILL.BIN (owned by FreescapeEngine)
	const byte *_soundDefTable;  // 20 entries × 7 bytes at l40e0h
	const byte *_toneTable;      // Volume envelope data at l4034h
	const byte *_envelopeTable;  // Pitch sweep data at l4078h

	/**
	 * Channel state - mirrors the 23-byte per-channel structure at l416dh
	 * as populated by sub_4760h and updated by sub_7571h.
	 *
	 * "vol" fields come from the "tone" table (l4034h) - controls volume envelope
	 * "pitch" fields come from the "envelope" table (l4078h) - controls pitch sweep
	 */
	struct ChannelState {
		// Volume modulation (from "tone" table)
		byte volCounter;        // ix+000h: initial counter value
		int8 volDelta;          // ix+001h: signed delta added to volume
		byte volLimit;          // ix+002h: initial limit value
		byte volCounterCur;     // ix+003h: current counter (decremented)
		byte volLimitCur;       // ix+004h: current limit countdown
		byte volume;            // ix+005h: current AY volume (0-15)
		byte volTripletTotal;   // ix+006h: total number of volume triplets
		byte volCurrentStep;    // ix+007h: current triplet index
		byte duration;          // ix+008h: repeat count
		byte volToneIdx;        // tone table index (to recompute data pointer)

		// Pitch modulation (from "envelope" table)
		byte pitchCounter;      // ix+00Bh: initial counter value
		int8 pitchDelta;        // ix+00Ch: signed delta added to period
		byte pitchLimit;        // ix+00Dh: initial limit value
		byte pitchCounterCur;   // ix+00Eh: current counter (decremented)
		byte pitchLimitCur;     // ix+00Fh: current limit countdown
		uint16 period;          // ix+010h-011h: current 16-bit AY tone period
		byte pitchTripletTotal; // ix+012h: total number of pitch triplets
		byte pitchCurrentStep;  // ix+013h: current triplet index
		byte pitchEnvIdx;       // envelope table index (to recompute data pointer)

		byte finishedFlag;      // ix+016h: set when volume envelope exhausted

		// AY register mapping for this channel
		byte channelNum;        // 1=A, 2=B, 3=C
		byte toneRegLo;         // AY register for tone fine
		byte toneRegHi;         // AY register for tone coarse
		byte volReg;            // AY register for volume
		bool active;             // Channel is producing sound
	} _ch;

	void writeReg(int reg, byte val) {
		_ay.setReg(reg, val);
	}

	/**
	 * Route all sounds through the sub_4760h system.
	 *
	 * In the original game, sub_4760h is called with a 1-based sound number.
	 * It loads a 7-byte entry from l40e0h and configures AY registers.
	 */
	void setupSound(int index) {
		if (index >= 1 && index <= 20) {
			setupSub4760h(index);
		} else {
			_finished = true;
		}
	}

	/**
	 * Implements sub_4760h (0x4760-0x4871) - sound initialization.
	 *
	 * Assembly flow:
	 *   1. entry = l40e0h[(soundNum-1) * 7]
	 *   2. channel = flags & 0x03 (1=A, 2=B, 3=C)
	 *   3. Configure mixer from flags bits 2-3
	 *   4. Set AY tone period from entry[3-4]
	 *   5. Set AY volume from entry[5]
	 *   6. Load "tone" table (l4034h) -> volume envelope fields
	 *   7. Load "envelope" table (l4078h) -> pitch sweep fields
	 *   8. Set duration from entry[6]
	 */
	void setupSub4760h(int soundNum) {
		if (soundNum < 1 || soundNum > 20) {
			_finished = true;
			return;
		}

		const byte *entry = &_soundDefTable[(soundNum - 1) * 7];
		byte flags = entry[0];
		byte toneIdx = entry[1];
		byte envIdx = entry[2];
		uint16 period = entry[3] | (entry[4] << 8);
		byte volume = entry[5];
		byte duration = entry[6];

		// Channel number (1-based): 1=A, 2=B, 3=C
		byte channelNum = flags & 0x03;
		if (channelNum < 1 || channelNum > 3) {
			_finished = true;
			return;
		}

		// AY register mapping
		_ch.channelNum = channelNum;
		_ch.toneRegLo = (channelNum - 1) * 2;       // A=0, B=2, C=4
		_ch.toneRegHi = (channelNum - 1) * 2 + 1;   // A=1, B=3, C=5
		_ch.volReg = channelNum + 7;                 // A=8, B=9, C=10

		// Configure mixer (register 7)
		// Start with all disabled (0xFF), selectively enable per flags
		// Bit 2 set in flags = DISABLE tone, Bit 3 set = DISABLE noise
		byte mixer = 0xFF;
		if (!(flags & 0x04))
			mixer &= ~(1 << (channelNum - 1));        // Enable tone
		if (!(flags & 0x08))
			mixer &= ~(1 << (channelNum - 1 + 3));    // Enable noise
		writeReg(7, mixer);

		// Set AY tone period from entry[3-4]
		_ch.period = period;
		writeReg(_ch.toneRegLo, period & 0xFF);
		writeReg(_ch.toneRegHi, period >> 8);

		// Set AY volume from entry[5]
		_ch.volume = volume;
		writeReg(_ch.volReg, volume);

		// Duration from entry[6]
		_ch.duration = duration;

		// Load volume envelope from "tone" table (l4034h)
		// Assembly: index * 4 stride, byte[0]=triplet_count, then {counter, delta, limit}
		int toneBase = toneIdx * 4;
		_ch.volTripletTotal = _toneTable[toneBase];
		_ch.volCurrentStep = 0;
		_ch.volToneIdx = toneIdx;

		// Load first volume triplet
		int volOff = toneBase + 1;
		_ch.volCounter = _toneTable[volOff];
		_ch.volDelta = static_cast<int8>(_toneTable[volOff + 1]);
		_ch.volLimit = _toneTable[volOff + 2];
		_ch.volCounterCur = _ch.volCounter;
		_ch.volLimitCur = _ch.volLimit;

		// Load pitch sweep from "envelope" table (l4078h)
		// Assembly: index * 4 stride, byte[0]=triplet_count, then {counter, delta, limit}
		int envBase = envIdx * 4;
		_ch.pitchTripletTotal = _envelopeTable[envBase];
		_ch.pitchCurrentStep = 0;
		_ch.pitchEnvIdx = envIdx;

		// Load first pitch triplet
		int pitchOff = envBase + 1;
		_ch.pitchCounter = _envelopeTable[pitchOff];
		_ch.pitchDelta = static_cast<int8>(_envelopeTable[pitchOff + 1]);
		_ch.pitchLimit = _envelopeTable[pitchOff + 2];
		_ch.pitchCounterCur = _ch.pitchCounter;
		_ch.pitchLimitCur = _ch.pitchLimit;

		_ch.finishedFlag = 0;
		_ch.active = true;

		debugC(1, kFreescapeDebugMedia, "sub_4760h: sound %d ch=%d mixer=0x%02x period=%d vol=%d dur=%d tone[%d] env[%d]",
			soundNum, channelNum, mixer, period, volume, duration, toneIdx, envIdx);
		debugC(1, kFreescapeDebugMedia, "  vol envelope: triplets=%d counter=%d delta=%d limit=%d",
			_ch.volTripletTotal, _ch.volCounter, _ch.volDelta, _ch.volLimit);
		debugC(1, kFreescapeDebugMedia, "  pitch sweep:  triplets=%d counter=%d delta=%d limit=%d",
			_ch.pitchTripletTotal, _ch.pitchCounter, _ch.pitchDelta, _ch.pitchLimit);
	}

	/**
	 * Implements sub_7571h (0x7571-0x76A9) - 300Hz interrupt-driven update.
	 *
	 * Called at 300Hz (every CPC interrupt). Updates pitch first, then volume.
	 *
	 * PITCH UPDATE (l758bh):
	 *   1. dec pitchLimitCur; if != 0, skip to volume
	 *   2. reload pitchLimitCur from pitchLimit
	 *   3. period += sign_extend(pitchDelta); write to AY tone regs
	 *   4. dec pitchCounterCur; if != 0, skip to volume
	 *   5. advance pitch triplet; if all done -> dec duration; if 0 -> shutdown
	 *
	 * VOLUME UPDATE (l763ah):
	 *   1. if finishedFlag set, skip entirely
	 *   2. dec volLimitCur; if != 0, skip
	 *   3. reload volLimitCur from volLimit
	 *   4. volume = (volume + volDelta) & 0x0F; write to AY vol reg
	 *   5. dec volCounterCur; if != 0, skip
	 *   6. advance vol triplet; if all done -> set finishedFlag
	 *
	 * SHUTDOWN (l761eh):
	 *   - Write volume 0 to AY
	 *   - Mark channel inactive
	 */
	void tickUpdate() {
		if (!_ch.active) {
			_finished = true;
			return;
		}

		const byte *toneRaw = _toneTable;
		const byte *envRaw = _envelopeTable;

		// === PITCH UPDATE (l758bh) ===
		_ch.pitchLimitCur--;
		if (_ch.pitchLimitCur == 0) {
			// Reload limit countdown
			_ch.pitchLimitCur = _ch.pitchLimit;

			// period += sign_extend(pitchDelta) with natural 16-bit wrapping
			// Assembly: add hl,de where de = sign-extended 8-bit delta
			_ch.period += static_cast<int8>(_ch.pitchDelta);

			// Write period to AY tone registers (AY masks coarse to 4 bits)
			writeReg(_ch.toneRegLo, _ch.period & 0xFF);
			writeReg(_ch.toneRegHi, _ch.period >> 8);

			// Decrement pitch counter
			_ch.pitchCounterCur--;
			if (_ch.pitchCounterCur == 0) {
				// Advance to next pitch triplet
				_ch.pitchCurrentStep++;
				if (_ch.pitchCurrentStep >= _ch.pitchTripletTotal) {
					// All pitch triplets exhausted -> check duration
					_ch.duration--;
					if (_ch.duration == 0) {
						// SHUTDOWN (l761eh): silence and deactivate
						writeReg(_ch.volReg, 0);
						_ch.active = false;
						_finished = true;
						return;
					}
					// Duration > 0: restart BOTH volume and pitch from beginning
					// Assembly at 0x75D0: reloads tone (volume) table first triplet,
					// then resets both position indices and done flag,
					// then loads first envelope (pitch) triplet.

					// Reload first volume triplet (from tone table)
					int volOff = _ch.volToneIdx * 4 + 1;
					_ch.volCounter = toneRaw[volOff];
					_ch.volDelta = static_cast<int8>(toneRaw[volOff + 1]);
					_ch.volLimit = toneRaw[volOff + 2];
					_ch.volCounterCur = _ch.volCounter;
					_ch.volLimitCur = _ch.volLimit;

					// Reset both position indices and done flag
					_ch.volCurrentStep = 0;
					_ch.pitchCurrentStep = 0;
					_ch.finishedFlag = 0;

					// Reload first pitch triplet (from envelope table)
					int off = _ch.pitchEnvIdx * 4 + 1;
					_ch.pitchCounter = envRaw[off];
					_ch.pitchDelta = static_cast<int8>(envRaw[off + 1]);
					_ch.pitchLimit = envRaw[off + 2];
					_ch.pitchCounterCur = _ch.pitchCounter;
					_ch.pitchLimitCur = _ch.pitchLimit;
				} else {
					// Load next pitch triplet
					int off = _ch.pitchEnvIdx * 4 + 1 + _ch.pitchCurrentStep * 3;
					_ch.pitchCounter = envRaw[off];
					_ch.pitchDelta = static_cast<int8>(envRaw[off + 1]);
					_ch.pitchLimit = envRaw[off + 2];
					_ch.pitchCounterCur = _ch.pitchCounter;
					_ch.pitchLimitCur = _ch.pitchLimit;
				}
			}
		}

		// === VOLUME UPDATE (l763ah) ===
		if (!_ch.finishedFlag) {
			_ch.volLimitCur--;
			if (_ch.volLimitCur == 0) {
				// Reload limit countdown
				_ch.volLimitCur = _ch.volLimit;

				// volume = (volume + volDelta) & 0x0F
				_ch.volume = (_ch.volume + _ch.volDelta) & 0x0F;
				writeReg(_ch.volReg, _ch.volume);

				// Decrement volume counter
				_ch.volCounterCur--;
				if (_ch.volCounterCur == 0) {
					// Advance to next volume triplet
					_ch.volCurrentStep++;
					if (_ch.volCurrentStep >= _ch.volTripletTotal) {
						// All volume triplets exhausted -> set finished flag
						// NOTE: Does NOT shutdown channel - pitch continues
						_ch.finishedFlag = 1;
					} else {
						// Load next volume triplet
						int off = _ch.volToneIdx * 4 + 1 + _ch.volCurrentStep * 3;
						_ch.volCounter = toneRaw[off];
						_ch.volDelta = static_cast<int8>(toneRaw[off + 1]);
						_ch.volLimit = toneRaw[off + 2];
						_ch.volCounterCur = _ch.volCounter;
						_ch.volLimitCur = _ch.volLimit;
					}
				}
			}
		}
	}
};

void FreescapeEngine::playSoundDrillerCPC(int index, Audio::SoundHandle &handle) {
	debugC(1, kFreescapeDebugMedia, "Playing Driller CPC sound %d", index);
	// Create a new stream for the sound
	DrillerCPCSfxStream *stream = new DrillerCPCSfxStream(index,
		_soundsCPCSoundDefTable.data(), _soundsCPCToneTable.data(), _soundsCPCEnvelopeTable.data());
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream, -1, kFreescapeDefaultVolume, 0, DisposeAfterUse::YES);
}

} // End of namespace Freescape
