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

	loadMessagesFixedSize(&file, 0x214c, 14, 20);
	loadFonts(&file, 0x5b69);
	loadGlobalObjects(&file, 0x1d07, 8);
	load8bitBinary(&file, 0x5ccb, 16);
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
 * Based on reverse engineering of DRILL.BIN (loads at 0x1c62)
 *
 * Sound Dispatch (0x6305):
 *   Index 1  -> sub_2112h (Shoot)
 *   Index 2  -> sub_26e2h (Collide)
 *   Index 3  -> sub_2607h (Step Up)
 *   Index 4-9 -> sub_2207h (Generic handler)
 *   Index >= 10 -> l1d8fh (High index handler with HW envelope)
 *
 * Data Tables (from DRILL.BIN):
 *   Tone Table at 0x4034: 4 bytes per entry (count, period_lo, period_hi, delta)
 *   Envelope Table at 0x4078: 4 bytes per entry (step, count, delta_lo, delta_hi)
 *   Sound Definition at 0x40e0: 7 bytes per entry
 *
 * AY Register Write at 0x4872:
 *   Port 0xF4 = register select, Port 0xF6 = data
 */

// Original data tables extracted from DRILL.BIN for reference
// These are preserved for documentation - the actual implementation uses
// simplified parameters that approximate the original sound behavior.
// TODO: Use these tables for more accurate sound reproduction

// Tone table entries from DRILL.BIN at 0x4034 (file offset 0x23D2)
// Format: { iterations, period_low, period_high, delta }
// Period values are 12-bit (0-4095), frequency = 1MHz / (16 * period)
#if 0
static const uint8 kDrillerCPCToneTable[][4] = {
	{0x01, 0x01, 0x00, 0x01},  // Entry 0: period=0x0001, delta=0x01
	{0x02, 0x0f, 0x01, 0x03},  // Entry 1: period=0x010F (271), delta=0x03
	{0x01, 0xf1, 0x01, 0x00},  // Entry 2: period=0x01F1 (497), delta=0x00
	{0x01, 0x0f, 0xff, 0x18},  // Entry 3
	{0x01, 0x06, 0xfe, 0x3f},  // Entry 4
	{0x01, 0x0f, 0xff, 0x18},  // Entry 5
	{0x02, 0x01, 0x00, 0x06},  // Entry 6: period=0x0001, delta=0x06
	{0x0f, 0xff, 0x0f, 0x00},  // Entry 7
	{0x04, 0x05, 0xff, 0x0f},  // Entry 8
	{0x01, 0x05, 0x01, 0x01},  // Entry 9: period=0x0105 (261), delta=0x01
	{0x00, 0x7b, 0x0f, 0xff},  // Entry 10
};

// Envelope table entries from DRILL.BIN at 0x4078 (file offset 0x2416)
// Format: { step, count, delta_low, delta_high }
static const uint8 kDrillerCPCEnvelopeTable[][4] = {
	{0x01, 0x02, 0x00, 0xff},  // Entry 0: fast decay
	{0x01, 0x10, 0x01, 0x01},  // Entry 1
	{0x01, 0x02, 0x30, 0x10},  // Entry 2
	{0x01, 0x02, 0xd0, 0x10},  // Entry 3
	{0x03, 0x01, 0xe0, 0x06},  // Entry 4
	{0x01, 0x20, 0x06, 0x01},  // Entry 5
	{0xe0, 0x06, 0x00, 0x00},  // Entry 6
	{0x01, 0x02, 0xfb, 0x03},  // Entry 7
	{0x01, 0x02, 0xfd, 0x0c},  // Entry 8
	{0x02, 0x01, 0x04, 0x03},  // Entry 9
	{0x08, 0xf5, 0x03, 0x00},  // Entry 10
	{0x01, 0x10, 0x02, 0x06},  // Entry 11
	{0x01, 0x80, 0x01, 0x03},  // Entry 12
	{0x01, 0x64, 0x01, 0x01},  // Entry 13
};

// Sound definition table from DRILL.BIN at 0x40e0 (file offset 0x247E)
// Format: { flags, tone_idx, env_idx, period_lo, period_hi, volume, duration }
static const uint8 kDrillerCPCSoundDefs[][7] = {
	{0x02, 0x00, 0x0d, 0x00, 0x00, 0x0f, 0x01},  // Sound def 0
	{0x03, 0x00, 0x01, 0x00, 0x00, 0x0f, 0x01},  // Sound def 1
	{0x09, 0x00, 0x02, 0x20, 0x01, 0x0f, 0x01},  // Sound def 2
	{0x09, 0x00, 0x03, 0x20, 0x01, 0x0f, 0x01},  // Sound def 3
	{0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01},  // Sound def 4
	{0x09, 0x03, 0x00, 0x27, 0x00, 0x0f, 0x01},  // Sound def 5
	{0x05, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01},  // Sound def 6
	{0x09, 0x00, 0x04, 0x20, 0x01, 0x0f, 0x08},  // Sound def 7
	{0x09, 0x00, 0x07, 0x00, 0x01, 0x0f, 0x18},  // Sound def 8
	{0x01, 0x00, 0x08, 0x00, 0x01, 0x0f, 0x02},  // Sound def 9
};
#endif

/**
 * Sound entries extracted from DRILL.BIN at their respective addresses.
 *
 * Sound entry structure (from assembly at l38e9h):
 *   Byte 0: Flags
 *     - Bits 0-1: Channel number (1-3 → A/B/C)
 *     - Bit 2: If set, DISABLE tone (assembly at 4825: bit 2,(ix+000h))
 *     - Bit 3: If set, DISABLE noise (assembly at 482c: bit 3,(ix+000h))
 *     - Bit 6: Disable flag (checked at 1cdc: bit 6,(ix+000h))
 *     - Bit 7: Additional flag
 *   Bytes 1-3: Parameters for calibration processing (IX[1], IX[2], IX[3])
 *   Bytes 4-6: Secondary parameters (IX[4], IX[5], IX[6])
 *   Byte 7: Delta sign/additional param (stored at 03a5ah)
 *   Byte 8: Entry size
 *   Bytes 9+: Additional parameters (volume nibbles, etc.)
 */
// Sound entry raw bytes - preserved for reference and future use
// These entries will be used for full calibration-based implementation
#if 0  // Currently using tuned approximations instead
static const uint8 kDrillerCPCSoundEntry1[] = {
	// Sound 1 (Shoot) at l3904h (file offset 0x1CA2)
	// Flags 0x81: channel A, bits 2,3=0 → tone+noise enabled
	0x81, 0x00, 0x00, 0x00, 0x0c, 0x01, 0x0c, 0xfe, 0x0e, 0x66, 0x22, 0x66, 0x1c, 0x0a
};

static const uint8 kDrillerCPCSoundEntry6[] = {
	// Sound 6 at l3912h (file offset 0x1CB0)
	// Flags 0x86: channel B/C, needs verification
	0x86, 0x00, 0x00, 0x00, 0x06, 0x08, 0x06, 0xfd, 0x10, 0x7f, 0x7f, 0x00, 0x03, 0x03
};
#endif

/**
 * Initial calibration values from assembly at 0x4e78-0x4e87.
 *
 * Original code:
 *   ld a,07dh           ; 4e76 - Default A = 125
 *   ld (l39f9h),a       ; 4e78 - Store
 *   ld h,a              ; 4e7b - H = 125
 *   ld l,000h           ; 4e7c - HL = 125 * 256 = 32000
 *   srl h ; rr l        ; 4e7e-4e84 - HL = 32000 / 4 = 8000
 *   add hl,de           ; 4e86 - HL += DE (DE = 0x20 = 32)
 *   ld (l38a6h),hl      ; 4e87 - Store calibration = 8032
 *
 * Note: The calibration value 0x1F60 (8032) causes processed params to be
 * large negative numbers, which wrap to values > 4095 (AY max period).
 * Setting to 0 for now - needs investigation of actual in-game values.
 * The calibration may be initialized differently at game start or may
 * represent position-based attenuation that doesn't apply to shoot sound.
 */
#if 0  // Calibration system not yet fully implemented
static const uint16 kInitialCalibration[3] = {0x0000, 0x0000, 0x0000};
#endif

class DrillerCPCSfxStream : public Audio::AudioStream {
public:
	DrillerCPCSfxStream(int index, int rate = 44100) : _ay(rate, 1000000), _index(index), _rate(rate) {
		// CPC uses 1MHz clock for AY-3-8912
		initAY();
		_counter = 0;
		_finished = false;
		_phase = 0;
		_channelCount = 0;
		_outerLoops = 0;
		_loopCount = 0;
		_maxLoops = 0;
		_tonePeriod = 0;
		_toneDelta = 0;
		_processedFlags = 0;

		// Initialize channel state for 8 channels (sub_2e96h uses up to 8)
		for (int i = 0; i < 8; i++) {
			_channelPeriod[i] = 0;
			_channelVolume[i] = 0;
			_channelDelta[i] = 0;
			_channelDone[i] = false;
		}

		// Initialize processed parameters
		for (int i = 0; i < 6; i++) {
			_processedParams[i] = 0;
		}

		// Setup based on sound index using dispatch table logic from 0x6305
		setupSound(index);
	}

	void initAY() {
		// Silence all channels (AY register 7 = mixer, FF = all disabled)
		writeReg(7, 0xFF);
		writeReg(8, 0);   // Volume A = 0
		writeReg(9, 0);   // Volume B = 0
		writeReg(10, 0);  // Volume C = 0
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		if (_finished)
			return 0;

		// Process at 50Hz (CPC interrupt rate)
		int samplesPerTick = _rate / 50;
		int samplesGenerated = 0;

		while (samplesGenerated < numSamples && !_finished) {
			int samplesTodo = MIN(numSamples - samplesGenerated, samplesPerTick);

			updateSound();

			_ay.readBuffer(buffer + samplesGenerated, samplesTodo);
			samplesGenerated += samplesTodo;

			if (_finished) break;
		}

		return samplesGenerated;
	}

	bool isStereo() const override { return true; }
	bool endOfData() const override { return _finished; }
	bool endOfStream() const override { return _finished; }
	int getRate() const override { return _rate; }

private:
	Audio::AY8912Stream _ay;
	int _index;
	int _rate;
	int _counter;
	int _phase;
	bool _finished;
	uint8 _regs[16];

	/**
	 * Channel state for multi-channel processing (sub_2e96h at 0x2e96)
	 * Sound 1 uses 8 channels, distributed from processed parameters.
	 * Channel data base at 0x0223, 6 bytes per channel in original.
	 */
	uint16 _channelPeriod[8];    // Tone period per channel
	int16 _channelDelta[8];      // Delta per channel (can be negative)
	uint8 _channelVolume[8];     // Volume per channel
	bool _channelDone[8];        // Completion status per channel

	// Sound-specific state
	uint16 _tonePeriod;          // Current tone period (for simple sounds)
	int16 _toneDelta;            // Tone delta (for simple sounds)
	uint8 _channelCount;         // Number of active channels (l3bc3h)
	uint8 _outerLoops;           // Outer loop count (l3bc4h = 5)
	uint16 _loopCount;           // Current iteration
	uint16 _maxLoops;            // Total iteration limit

	/**
	 * Processed parameters from calibration (output of l1cffh)
	 * 6 x 16-bit values stored at working buffer 0x02D1
	 */
	int16 _processedParams[6];
	uint8 _processedFlags;       // Accumulated flag bits (stored at 0x01e6)

	void writeReg(int reg, uint8 val) {
		if (reg >= 0 && reg < 16) {
			_regs[reg] = val;
			_ay.setReg(reg, val);
		}
	}

	/**
	 * Processes sound entry parameters through calibration system.
	 * NOTE: Currently disabled - using tuned approximations instead.
	 * This function implements the calibration at l1cffh but requires
	 * correct runtime calibration values to produce valid results.
	 */
#if 0  // Calibration-based processing - needs emulator capture for accuracy
	/**
	 * Original assembly at l1cffh (0x1cff-0x1d54):
	 *   ld de,(l3d16h)      ; 1cff - DE = working buffer (0x02D1)
	 *   ld ix,(l3d1ah)      ; 1d04 - IX = sound entry pointer
	 *   ld hl,l38a6h        ; 1d08 - HL = calibration table
	 *   ld b,003h           ; 1d0b - Loop 3 times
	 *
	 * Algorithm per iteration:
	 *   1. BC = calibration[i] from l38a6h
	 *   2. HL = entry[1+i] * 64  (via srl h ; rr l twice)
	 *   3. result1 = HL - BC
	 *   4. Store result1 (2 bytes)
	 *   5. BC = entry[4+i] * 64
	 *   6. result2 = result1 + BC
	 *   7. Store result2 (2 bytes)
	 *   8. Advance entry pointer (inc ix)
	 *
	 * @param soundEntry   Raw sound entry data (14 bytes from l3904h etc.)
	 */
	void processParameters(const uint8 *soundEntry) {
		_processedFlags = 0;  // Assembly: xor a ; 1d03

		for (int i = 0; i < 3; ++i) {
			// Assembly at l1d0dh: ld c,(hl) ; inc hl ; ld b,(hl) ; inc hl
			// BC = calibration[i] from l38a6h
			const int16 calibration = static_cast<int16>(kInitialCalibration[i]);

			// Assembly at 1d13-1d1e:
			//   ld l,000h           ; 1d13
			//   ld h,(ix+001h)      ; 1d15 - H = entry[1+i] (IX advances each iteration)
			//   srl h ; rr l        ; 1d18-1d1e - HL = entry[1+i] * 64
			int16 hl = static_cast<int16>(soundEntry[1 + i]) * 64;

			// Assembly at 1d20-1d21: or a ; sbc hl,bc
			hl -= calibration;

			// Assembly at 1d23-1d28: Track positive result
			//   jr z,l1d2ah ; jp m,l1d2ah ; set 6,a
			if (hl > 0) {
				_processedFlags |= (1 << (5 - i * 2));
			}

			// Assembly at 1d2a-1d31: srl a ; store result
			_processedParams[i * 2] = hl;

			// Assembly at 1d32-1d3d:
			//   ld c,000h           ; 1d32
			//   ld b,(ix+004h)      ; 1d34 - B = entry[4+i]
			//   srl b ; rr c        ; 1d37-1d3d - BC = entry[4+i] * 64
			const int16 bc = static_cast<int16>(soundEntry[4 + i]) * 64;

			// Assembly at 1d3f-1d40: or a ; adc hl,bc
			hl += bc;

			// Assembly at 1d42-1d45: Track overflow
			//   jp p,l1d47h ; set 6,a
			if (hl < 0) {
				_processedFlags |= (1 << (4 - i * 2));
			}

			// Assembly at 1d47-1d4e: srl a ; store result
			_processedParams[i * 2 + 1] = hl;

			// Assembly at 1d4f: inc ix (pointer advances for next iteration)
		}
		// Assembly at 1d55: ld (001e6h),a - store flags
	}
#endif

	void setupSound(int index) {
		// Dispatch based on index (mirrors 0x6305 logic)
		switch (index) {
		case 1: // Shoot (sub_2112h)
			setupShoot();
			break;
		case 2: // Collide (sub_26e2h)
			setupCollide();
			break;
		case 3: // Step Up (sub_2607h)
			setupStepUp();
			break;
		case 4: // Step Down (sub_2207h, index 4)
		case 5: // (sub_2207h, index 5)
		case 6: // Menu (sub_2207h, index 6)
		case 7: // Hit (sub_2207h, index 7)
		case 8: // (sub_2207h, index 8)
		case 9: // Fallen (sub_2207h, index 9)
			setupGeneric(index);
			break;
		default:
			if (index >= 10) {
				// High index handler (l1d8fh) - uses HW envelope
				setupHighIndex(index);
			} else {
				_finished = true;
			}
			break;
		}
	}

	/**
	 * Sound 1: Shoot - implements sub_2112h at 0x2112-0x2206
	 *
	 * Original assembly flow:
	 *   sub_2112h:
	 *     ld iy,(l3d16h)      ; 2118 - IY = processed params buffer (0x02D1)
	 *     ; Lines 211c-2185: Distribute 6 params to 8 channel working areas
	 *     ld a,008h           ; 2188 - Channel count = 8
	 *     ld (l3bc3h),a       ; 218a - Store channel count
	 *     call sub_2e96h      ; 218d - Call sound update loop
	 *
	 * Sound entry at l3904h: 81 00 00 00 0c 01 0c fe 0e 66 22 66 1c 0a
	 *   Byte 0: 0x81 = flags (channel A, tone+noise enabled)
	 *   Bytes 1-3: 0x00, 0x00, 0x00 (base params)
	 *   Bytes 4-6: 0x0c, 0x01, 0x0c (periods: 12*64=768, 1*64=64, 12*64=768)
	 *   Byte 7: 0xfe = -2 signed (delta sign/multiplier)
	 *   Byte 8: 0x0e = 14 (entry size)
	 *   Bytes 9-13: 0x66, 0x22, 0x66, 0x1c, 0x0a (volume nibbles per channel pair)
	 *
	 * Calibration at l1cffh:
	 *   For each i (0-2): result1 = entry[1+i]*64 - calibration
	 *                     result2 = result1 + entry[4+i]*64
	 *
	 * With calibration=0 (initial game state):
	 *   processed[0] = 0*64 - 0 = 0 (invalid period)
	 *   processed[1] = 0 + 0x0c*64 = 768 (~81 Hz bass)
	 *   processed[2] = 0*64 - 0 = 0 (invalid)
	 *   processed[3] = 0 + 0x01*64 = 64 (~977 Hz high)
	 *   processed[4] = 0*64 - 0 = 0 (invalid)
	 *   processed[5] = 0 + 0x0c*64 = 768 (~81 Hz)
	 *
	 * sub_2112h distribution (lines 631-666):
	 *   IY[0-1] → ch 0,3,4,7 at offset 0-1 (period base)
	 *   IY[2-3] → ch 1,2,5,6 at offset 0-1 (period base)
	 *   IY[4-5] → ch 0,1,4,5 at offset 2-3
	 *   IY[6-7] → ch 2,3,6,7 at offset 2-3
	 *   IY[8-9] → ch 0,1,2,3 at offset 4-5 (delta)
	 *   IY[10-11] → ch 4,5,6,7 at offset 4-5 (delta)
	 *
	 * sub_2e96h runs outer loop 5 times (l3bc4h=5), each with different op_type
	 * sub_2d87h processes each channel with the current op_type
	 *
	 * CPC AY 1MHz: freq = 1000000 / (16 * period)
	 */
	void setupShoot() {
		// Sound entry at l3904h
		const uint8 soundEntry[] = {
			0x81, 0x00, 0x00, 0x00, 0x0c, 0x01, 0x0c, 0xfe,
			0x0e, 0x66, 0x22, 0x66, 0x1c, 0x0a
		};

		// Calibration processing at l1cffh
		// The calibration value adjusts the base period. With the assembly's
		// initial value (~8000), periods wrap in 16-bit arithmetic.
		// Using calibration=700 to get periods in the laser frequency range.
		// This gives processed[1] = 0 - 700 + 768 = 68 → 919 Hz
		const int16 calibration = 700;

		int16 processed[6];
		for (int i = 0; i < 3; i++) {
			int16 val1 = static_cast<int16>(soundEntry[1 + i]) * 64;
			int16 val2 = static_cast<int16>(soundEntry[4 + i]) * 64;
			processed[i * 2] = val1 - calibration;
			processed[i * 2 + 1] = val1 - calibration + val2;
		}
		// With calib=700: processed = [-700, 68, -700, -636, -700, 68]
		// Only processed[1] and processed[5] are valid (68 → 919 Hz)

		_channelCount = 8;
		_outerLoops = 5;   // l3bc4h = 5
		_maxLoops = 40;    // 5 outer × 8 channels

		// Channel setup following sub_2112h distribution
		// Each channel has: [base_period, param2, delta]
		for (int ch = 0; ch < 8; ch++) {
			int16 basePeriod;
			int16 param2;
			int16 delta;

			// offset 0-1: IY[0-1] → ch 0,3,4,7; IY[2-3] → ch 1,2,5,6
			if (ch == 0 || ch == 3 || ch == 4 || ch == 7) {
				basePeriod = processed[0];  // 0
			} else {
				basePeriod = processed[1];  // 768
			}

			// offset 2-3: IY[4-5] → ch 0,1,4,5; IY[6-7] → ch 2,3,6,7
			if (ch == 0 || ch == 1 || ch == 4 || ch == 5) {
				param2 = processed[2];  // 0
			} else {
				param2 = processed[3];  // 64
			}

			// offset 4-5: IY[8-9] → ch 0-3; IY[10-11] → ch 4-7
			if (ch < 4) {
				delta = processed[4];  // 0
			} else {
				delta = processed[5];  // 768
			}

			// sub_2d87h op_type=1: HL = basePeriod + delta
			// For channels to be active, we need valid period
			int16 effectivePeriod = basePeriod + delta;

			if (effectivePeriod <= 0 || effectivePeriod > 4095) {
				// Invalid period - check if base or param2 gives valid period
				if (basePeriod > 0 && basePeriod <= 4095) {
					effectivePeriod = basePeriod;
				} else if (param2 > 0 && param2 <= 4095) {
					effectivePeriod = param2;
				} else if (delta > 0 && delta <= 4095) {
					effectivePeriod = delta;
				} else {
					_channelPeriod[ch] = 0;
					_channelDelta[ch] = 0;
					_channelDone[ch] = true;
					continue;
				}
			}

			_channelPeriod[ch] = static_cast<uint16>(effectivePeriod);
			// Delta sign from byte 7: 0xfe = -2 (signed)
			// This indicates descending period (rising frequency)
			// Using small delta for smooth sweep
			_channelDelta[ch] = -2;
			_channelDone[ch] = false;

			debug("setupShoot: ch%d base=%d param2=%d delta=%d -> period=%d",
				  ch, basePeriod, param2, delta, _channelPeriod[ch]);
		}

		// Volume from sound entry bytes 9-13 (nibbles)
		// 0x66 → 6,6; 0x22 → 2,2; 0x66 → 6,6; 0x1c → 12,1; 0x0a → 10,0
		_channelVolume[0] = soundEntry[9] & 0x0F;         // 6
		_channelVolume[1] = (soundEntry[9] >> 4) & 0x0F;  // 6
		_channelVolume[2] = soundEntry[10] & 0x0F;        // 2
		_channelVolume[3] = (soundEntry[10] >> 4) & 0x0F; // 2
		_channelVolume[4] = soundEntry[11] & 0x0F;        // 6
		_channelVolume[5] = (soundEntry[11] >> 4) & 0x0F; // 6
		_channelVolume[6] = soundEntry[12] & 0x0F;        // 12
		_channelVolume[7] = (soundEntry[12] >> 4) & 0x0F; // 1

		// Mixer: flags 0x81 = channel A (bits 0-1=01), tone+noise enabled (bits 2,3=0)
		// Mixer register 7: bit 0 = tone A off, bit 3 = noise A off
		// So 0x36 = 0b00110110 = tone A on, noise A on
		writeReg(7, 0x36);

		// Noise period from assembly - mid-range grit
		writeReg(6, 0x10);

		// Find first active channel for initial output
		uint16 initPeriod = 768;  // Default to entry value
		for (int ch = 0; ch < 8; ch++) {
			if (!_channelDone[ch] && _channelPeriod[ch] > 0) {
				initPeriod = _channelPeriod[ch];
				break;
			}
		}

		writeReg(0, initPeriod & 0xFF);
		writeReg(1, (initPeriod >> 8) & 0x0F);
		writeReg(8, 15);  // Start at max volume

		_loopCount = 0;

		debug("setupShoot: calib=%d initPeriod=%d", calibration, initPeriod);
	}

	/**
	 * Sound 2: Collide - implements sub_26e2h at 0x26e2
	 *
	 * From assembly: Sound 2 skips range check but uses same calibration.
	 * Uses 2 channels (copies to 2 pairs of working areas).
	 * Creates a "bump/thud" sound when hitting walls.
	 *
	 * Characteristics: Low frequency impact with noise, quick decay
	 */
	void setupCollide() {
		_channelCount = 2;      // Assembly: 2 channel pairs
		_maxLoops = 12;         // ~240ms - quick impact
		_loopCount = 0;

		// Low frequency for "thump" character
		// Period 500 → ~125 Hz (bass thud)
		_channelPeriod[0] = 500;
		_channelPeriod[1] = 600;
		_channelDelta[0] = 30;   // Descending pitch
		_channelDelta[1] = 40;
		_channelVolume[0] = 15;
		_channelVolume[1] = 12;
		_channelDone[0] = false;
		_channelDone[1] = false;

		// Set initial tone
		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);

		// Enable noise + tone on channel A for impact texture
		// Noise adds the "crunch" to the collision
		writeReg(6, 0x10);      // Noise period (mid-range)
		writeReg(7, 0x36);      // Tone A + Noise A
		writeReg(8, 15);        // Max volume for impact
	}

	/**
	 * Sound 3: Step Up - implements sub_2607h at 0x2607
	 *
	 * Short ascending blip for footstep going up terrain.
	 * Quick pitch rise then decay - characteristic "bip" sound.
	 */
	void setupStepUp() {
		_channelCount = 1;
		_maxLoops = 8;          // ~160ms - quick step
		_loopCount = 0;

		// Start at medium pitch, ascend (period decreases)
		// Period 300 (~208Hz) → Period 150 (~417Hz)
		_channelPeriod[0] = 300;
		_channelDelta[0] = -20;  // Ascending pitch
		_channelVolume[0] = 10;
		_channelDone[0] = false;

		// Set initial tone
		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);

		// Tone only - clean step sound
		writeReg(7, 0x3E);      // Tone A only, no noise
		writeReg(8, 10);        // Medium volume
	}

	/**
	 * Sounds 4-9: Generic handler based on sub_2207h at 0x2207
	 *
	 * These sounds share a common handler in the original but have
	 * different parameter entries. Each has distinct characteristics.
	 */
	void setupGeneric(int index) {
		_channelCount = 1;
		_loopCount = 0;
		_channelDone[0] = false;

		switch (index) {
		case 4: // Step Down - descending blip for footstep going down
			// Opposite of step up: starts higher, descends
			_channelPeriod[0] = 180;   // Start higher (~347Hz)
			_channelDelta[0] = 15;     // Descending pitch
			_channelVolume[0] = 10;
			_maxLoops = 8;             // ~160ms - quick step
			writeReg(7, 0x3E);         // Tone A only
			break;

		case 5: // Reserved/unused in original
			_channelPeriod[0] = 250;
			_channelDelta[0] = 0;
			_channelVolume[0] = 8;
			_maxLoops = 10;
			writeReg(7, 0x3E);
			break;

		case 6: // Menu click - short high blip
			// Quick "bip" for menu selection
			_channelPeriod[0] = 120;   // High pitch (~521Hz)
			_channelDelta[0] = 0;      // Steady pitch
			_channelVolume[0] = 12;
			_maxLoops = 4;             // ~80ms - very short click
			writeReg(7, 0x3E);         // Tone A only
			break;

		case 7: // Hit - impact when player takes damage
			// Sharp impact with noise texture
			_channelPeriod[0] = 350;   // Low-mid pitch (~179Hz)
			_channelDelta[0] = 25;     // Quick descend
			_channelVolume[0] = 15;
			_maxLoops = 15;            // ~300ms
			writeReg(6, 0x0c);         // Noise adds "crunch"
			writeReg(7, 0x36);         // Tone + Noise
			break;

		case 8: // Reserved/generic
			_channelPeriod[0] = 200;
			_channelDelta[0] = 8;
			_channelVolume[0] = 10;
			_maxLoops = 15;
			writeReg(7, 0x3E);
			break;

		case 9: // Fallen - long descending sweep when falling
			// Dramatic fall sound - starts high, slowly descends
			_channelPeriod[0] = 100;   // Start high (~625Hz)
			_channelDelta[0] = 6;      // Slow descent
			_channelVolume[0] = 15;
			_maxLoops = 60;            // ~1.2 seconds - long fall
			writeReg(7, 0x3E);         // Tone only - "wheee" character
			break;

		default:
			_finished = true;
			return;
		}

		// Set initial tone period
		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
		writeReg(8, _channelVolume[0]);
	}

	/**
	 * Sounds >= 10: High index handler (l1d8fh / sub_257bh)
	 *
	 * Uses hardware envelope for sustained atmospheric sounds.
	 * From assembly: calculates channel count as (index - 8),
	 * reads parameters from IX+9 onwards.
	 *
	 * Sound 7 entry at l38e9h has flags 0x87 (noise only, no tone).
	 */
	void setupHighIndex(int index) {
		_channelCount = 1;
		_loopCount = 0;
		_channelDone[0] = false;

		switch (index) {
		case 10: // Area Change - atmospheric transition
			// From sound 7 entry flags 0x87: bit 2=1 (disable tone), bit 3=0 (noise on)
			// Creates a "whoosh" effect for area transitions
			_channelPeriod[0] = 0;
			_channelDelta[0] = 0;
			_channelVolume[0] = 15;
			_maxLoops = 50;         // ~1 second

			writeReg(6, 0x18);      // Noise period - medium texture
			writeReg(7, 0x37);      // 0b00110111: noise A only, no tone
			// Hardware envelope for smooth decay
			writeReg(11, 0x00);     // Envelope period low
			writeReg(12, 0x18);     // Envelope period high
			writeReg(13, 0x00);     // Shape: single decay (\)
			writeReg(8, 0x10);      // Volume = envelope mode
			break;

		case 11: // Explosion/rumble effect
			_channelPeriod[0] = 700;
			_channelDelta[0] = 20;   // Descending rumble
			_channelVolume[0] = 15;
			_maxLoops = 40;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(6, 0x1C);      // Rough noise
			writeReg(7, 0x36);      // Tone + Noise
			writeReg(11, 0x00);
			writeReg(12, 0x20);
			writeReg(13, 0x00);     // Decay
			writeReg(8, 0x10);
			break;

		case 12: // Warning/alert tone
			_channelPeriod[0] = 150;
			_channelDelta[0] = 0;
			_channelVolume[0] = 15;
			_maxLoops = 60;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(7, 0x3E);      // Tone only
			writeReg(11, 0x00);
			writeReg(12, 0x06);     // Fast cycle for alarm effect
			writeReg(13, 0x0E);     // Continue, alternate (warble)
			writeReg(8, 0x10);
			break;

		case 13: // Mission Complete - triumphant fanfare
			// Rising pitch with envelope swell
			_channelPeriod[0] = 200;
			_channelDelta[0] = -2;   // Ascending pitch
			_channelVolume[0] = 15;
			_maxLoops = 80;         // ~1.6 seconds

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(7, 0x3E);      // Tone only - clean victory sound
			writeReg(11, 0x00);
			writeReg(12, 0x30);     // Slow envelope
			writeReg(13, 0x0A);     // Attack/decay (/ then hold)
			writeReg(8, 0x10);
			break;

		default:
			// Generic high index - simple tone with decay
			_channelPeriod[0] = 200;
			_channelDelta[0] = 0;
			_channelVolume[0] = 12;
			_maxLoops = 30;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(7, 0x3E);
			writeReg(11, 0x00);
			writeReg(12, 0x10);
			writeReg(13, 0x00);
			writeReg(8, 0x10);
			break;
		}
	}

	void updateSound() {
		_counter++;
		_loopCount++;

		if (_loopCount >= _maxLoops) {
			_finished = true;
			initAY();
			return;
		}

		// Update based on sound type
		switch (_index) {
		case 1: // Shoot
			updateShoot();
			break;
		case 2: // Collide
			updateCollide();
			break;
		case 3: // Step Up
			updateStepUp();
			break;
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			updateGeneric();
			break;
		default:
			if (_index >= 10) {
				updateHighIndex();
			}
			break;
		}
	}

	/**
	 * Sound update loop for Shoot - implements sub_2e96h at 0x2e96-0x2edb
	 *
	 * Original assembly structure:
	 *   sub_2e96h:
	 *     call sub_2a65h        ; 2e9c - Initialize
	 *     ld de,00006h          ; 2e9f - 6 bytes per channel
	 *     ld a,(l3bc4h)         ; 2ea6 - A = outer loop count (5)
	 *     ld l,a                ; 2ea9 - L = 5
	 *   l2eaah:
	 *     ld (0025fh),a         ; 2eaa - Store op_type = outer counter
	 *     ld a,(l3bc3h)         ; 2eaf - A = channel count (8)
	 *     ld b,a                ; 2eb2 - B = 8
	 *     ld ix,00223h          ; 2eb3 - IX = channel data base
	 *   l2eb7h:
	 *     call sub_2d87h        ; 2eb7 - Process one channel step
	 *     add ix,de             ; 2ec5 - Next channel (+6 bytes)
	 *     djnz l2eb7h           ; 2ec7 - Loop 8 channels
	 *     dec l                 ; 2ecd - Decrement outer counter
	 *     jr nz,l2eaah          ; 2ecf - Continue if L > 0
	 *
	 * The 8 channels are mixed by cycling through them rapidly.
	 * Each tick we output one channel's period to the AY.
	 */
	void updateShoot() {
		bool anyActive = false;
		int activeChannel = -1;
		uint16 outputPeriod = 0;

		// Cycle through channels - each tick advances to next active channel
		int startCh = _loopCount % _channelCount;
		for (int i = 0; i < _channelCount; i++) {
			int ch = (startCh + i) % _channelCount;
			if (_channelDone[ch]) continue;

			// Apply delta to period (sub_2d87h simplified)
			int32 newPeriod = static_cast<int32>(_channelPeriod[ch]) + _channelDelta[ch];

			// Check bounds (AY period is 12-bit: 0-4095)
			// Use lower bound of 20 to avoid very high frequencies that sound harsh
			if (newPeriod <= 20 || newPeriod > 4095) {
				_channelDone[ch] = true;
				continue;
			}

			_channelPeriod[ch] = static_cast<uint16>(newPeriod);
			anyActive = true;

			// Use this channel for output
			if (activeChannel < 0) {
				activeChannel = ch;
				outputPeriod = _channelPeriod[ch];
			}
		}

		// Output to AY
		if (activeChannel >= 0 && outputPeriod > 0) {
			writeReg(0, outputPeriod & 0xFF);
			writeReg(1, (outputPeriod >> 8) & 0x0F);

			// Volume decay: starts at max, decays over ~40 ticks
			// Use slower decay to maintain presence throughout the sound
			int vol = 15 - (_loopCount / 4);
			if (vol < 2) vol = 2;
			writeReg(8, static_cast<uint8>(vol));

			debug("updateShoot: loop=%d ch=%d period=%d freq=%dHz vol=%d",
				  _loopCount, activeChannel, outputPeriod,
				  1000000 / (16 * outputPeriod), vol);
		}

		// Check completion
		if (!anyActive || _loopCount >= _maxLoops) {
			debug("updateShoot: FINISHED at loop=%d", _loopCount);
			_finished = true;
			initAY();
		}
	}

	void updateCollide() {
		// Process both channels
		for (int ch = 0; ch < _channelCount; ++ch) {
			if (!_channelDone[ch]) {
				int32 newPeriod = static_cast<int32>(_channelPeriod[ch]) + _channelDelta[ch];
				if (newPeriod > 1200) {
					_channelDone[ch] = true;
				} else {
					_channelPeriod[ch] = static_cast<uint16>(newPeriod);
				}
			}
		}

		// Output primary channel
		if (!_channelDone[0]) {
			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
		}

		// Quick decay - collision is sharp impact
		int vol = 15 - (_loopCount * 2);
		if (vol < 0) vol = 0;
		writeReg(8, vol);

		// Fade noise out quickly too
		int noise = 0x10 - _loopCount;
		if (noise < 0x04) noise = 0x04;
		writeReg(6, noise);
	}

	void updateStepUp() {
		if (!_channelDone[0]) {
			// Ascending pitch sweep (period decreases = higher pitch)
			int32 newPeriod = static_cast<int32>(_channelPeriod[0]) + _channelDelta[0];
			if (newPeriod < 80) {  // Cap at ~781Hz
				_channelDone[0] = true;
			} else {
				_channelPeriod[0] = static_cast<uint16>(newPeriod);
			}
		}

		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);

		// Quick volume decay for short "bip"
		int vol = _channelVolume[0] - _loopCount;
		if (vol < 0) vol = 0;
		writeReg(8, vol);
	}

	void updateGeneric() {
		if (!_channelDone[0]) {
			// Apply tone delta
			int32 newPeriod = static_cast<int32>(_channelPeriod[0]) + _channelDelta[0];
			if (newPeriod < 50 || newPeriod > 2000) {
				_channelDone[0] = true;
			} else {
				_channelPeriod[0] = static_cast<uint16>(newPeriod);
			}
		}

		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);

		// Volume decay varies by sound type
		int vol;
		switch (_index) {
		case 6: // Menu click - very fast decay
			vol = _channelVolume[0] - (_loopCount * 3);
			break;
		case 9: // Fallen - slow decay
			vol = _channelVolume[0] - (_loopCount / 8);
			break;
		case 7: // Hit - medium-fast decay
			vol = _channelVolume[0] - _loopCount;
			break;
		default:
			vol = _channelVolume[0] - (_loopCount / 2);
			break;
		}
		if (vol < 0) vol = 0;
		writeReg(8, vol);
	}

	void updateHighIndex() {
		// High index sounds mostly use hardware envelope
		// Update tone if delta is set (for pitch sweeps)
		if (_toneDelta != 0) {
			int period = (int)_tonePeriod + _toneDelta;
			if (period < 50) period = 50;
			if (period > 1000) period = 1000;
			_tonePeriod = period;

			writeReg(0, _tonePeriod & 0xFF);
			writeReg(1, (_tonePeriod >> 8) & 0x0F);
		}
		// Hardware envelope handles volume automatically
	}
};

void FreescapeEngine::playSoundDrillerCPC(int index, Audio::SoundHandle &handle) {
	// DO NOT CHANGE: This debug line is used to track sound usage in Driller CPC
	debug("Playing Driller CPC sound %d", index);
	// Create a new stream for the sound
	DrillerCPCSfxStream *stream = new DrillerCPCSfxStream(index);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream, -1, kFreescapeDefaultVolume, 0, DisposeAfterUse::YES);
}

} // End of namespace Freescape
