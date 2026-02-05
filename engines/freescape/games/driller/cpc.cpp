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
 *   Bytes 1-3: Base params for calibration (multiply by 64, subtract calibration)
 *   Bytes 4-6: Add params (multiply by 64, add to result)
 *   Byte 7: Delta sign/multiplier (signed: 0xfe=-2, 0xfd=-3, 0xff=-1)
 *   Byte 8: Entry size (total bytes in entry)
 *   Bytes 9+: Volume nibbles (low=even ch, high=odd ch)
 *
 * Calibration formula at l1cffh:
 *   For each i (0-2):
 *     result1 = entry[1+i] * 64 - calibration[i]
 *     result2 = result1 + entry[4+i] * 64
 *
 * The calibration value is position-dependent in the original game.
 * We use a fixed calibration that produces reasonable frequencies.
 */

/**
 * Sound entries extracted from DRILL.BIN at their respective addresses.
 *
 * IMPLEMENTATION STATUS:
 *   - Types 1, 6, 7: HAVE ASSEMBLY ENTRY DATA (l3904h, l3912h, l38e9h, l3922h, l3932h, l3942h)
 *   - Types 2, 3:    HANDLERS EXIST but NO ENTRY DATA in table (sub_26e2h, sub_2607h)
 *   - Types 4, 5, 8, 9: HANDLERS EXIST (sub_2207h) but NO ENTRY DATA found
 *   - Types >= 10:   Use sub_257bh with hardware envelope
 *
 * Entry structure: flags(1), base[3], add[3], delta(1), size(1), vol/extra[...]
 * Channel count for sub_2207h: compare bytes 0x0c-0x0f
 *   - All equal → 5 channels
 *   - One pair differs → 6 channels
 *   - Both pairs differ → 8 channels
 */

// Sound 7 (Hit) at l38e9h - 27 bytes (0x1b)
// Type 7 → sub_2207h, bytes 0c-0f = 03 03 05 05 (differ) → 8 channels
// Flags 0x87: bit 2=1 (no tone), bit 3=0 (noise on)
static const uint8 kSoundEntry7[] = {
	0x87, 0x00, 0x00, 0x00, 0x08, 0x0a, 0x08, 0xff,  // bytes 0-7
	0x1b, 0x7f, 0x7f, 0x00, 0x03, 0x03, 0x05, 0x05   // bytes 8-15 (0c-0f for channel count)
};

// Sound 1 (Shoot) at l3904h - 14 bytes (0x0e)
// Type 1 → sub_2112h → 8 channels (hardcoded in handler)
// Flags 0x81: bits 2,3=0 → tone+noise enabled
static const uint8 kSoundEntry1[] = {
	0x81, 0x00, 0x00, 0x00, 0x0c, 0x01, 0x0c, 0xfe,  // bytes 0-7
	0x0e, 0x66, 0x22, 0x66, 0x1c, 0x0a               // bytes 8-13
};

// Sound 6 (Menu) at l3912h - 16 bytes (0x10)
// Type 6 → sub_2207h, bytes 0c-0f = 03 03 03 03 (all equal) → 5 channels
// Flags 0x86: bit 2=1 (no tone), bit 3=0 (noise on)
static const uint8 kSoundEntry6[] = {
	0x86, 0x00, 0x00, 0x00, 0x06, 0x08, 0x06, 0xfd,  // bytes 0-7
	0x10, 0x7f, 0x7f, 0x00, 0x03, 0x03, 0x03, 0x03   // bytes 8-15 (0c-0f for channel count)
};

/**
 * Sound 2 (Collide) - Synthetic entry for sub_26e2h
 *
 * sub_26e2h at 0x26e2 (lines 1281-1380 in assembly):
 *   - Uses 2 channels (line 1349: ld a,002h)
 *   - Reads entry from l3d1ah
 *   - Reads processed params from l3d16h (after calibration at l1cffh)
 *   - Duplicates params to 2 working areas (lines 1303-1314)
 *   - Calls sub_2e96h for the main update loop
 *
 * Entry structure for type 2:
 *   byte 0: flags (0x82 = type 2, tone+noise enabled)
 *   bytes 1-3: base params (multiplied by 64, subtract calibration)
 *   bytes 4-6: add params (multiplied by 64, added)
 *   byte 7: delta sign
 *   byte 8: entry size
 *   byte 9: volume nibbles
 *   bytes 0xa: extra byte (copied to iy+001h at line 1295-1296)
 *   bytes 0xb-0xc: threshold for comparison (line 1344-1345)
 *   byte 0xd: flag mask for AND with 01e6h (line 1300)
 *
 * Collide uses result1 values (IX[0,4,8]) which are entry[1-3]*64 - calibration.
 * With base=0 and calibration=700, this gives period 700 (~89 Hz bass thud).
 * Using base values to shift the frequency range.
 */
// Sound 2 (Collide) entry from DRILL_CODE.bin offset 0x1d00
static const uint8 kSoundEntry2[] = {
	0x82,                   // Flags: type 2, bits 2,3=0 (tone+noise)
	0x40, 0x2e, 0x40,       // Base params: 64, 46, 64
	0x00, 0x00, 0x00,       // Add params: 0, 0, 0
	0xf8,                   // Byte 7 (stored to l3a5ah, NOT a delta sign)
	0x0e,                   // Entry size (14 bytes)
	0x11,                   // Volume nibbles (low=1, high=1)
	0x50, 0x00,             // Threshold (0x0050 = 80)
	0x20,                   // Flag mask (0x20 = 32)
	0x04                    // Extra byte
};

/**
 * Sound 3 (Step) - Synthetic entry for sub_2607h
 *
 * sub_2607h at 0x2607 (lines 1177-1280 in assembly):
 *   - Uses 4 channels (line 1227: ld a,004h)
 *   - Reads processed params from l3d16h (lines 1184-1209)
 *   - Reads entry[4] and entry[5] to determine distribution (lines 1203, 1219)
 *   - Distributes to 4 pairs of working areas
 *   - Calls sub_2e96h for the main update loop
 *
 * Distribution logic (lines 1210-1225):
 *   if entry[4] == 0: B=1, use one pattern
 *   else if entry[5] == 0: B=2, use another pattern
 *   else: B=3, use third pattern
 *
 * Step uses BOTH result1 (IX[0,2]) AND result2 (IX[4,6,8,10]) values.
 * For an ascending chirp, we want:
 *   - result1 values: mid-range starting pitch (~300-400 Hz)
 *   - result2 values: higher pitch offset (~500-800 Hz)
 *   - Negative delta to decrease period (ascending pitch)
 *
 * With calibration=700:
 *   base=0x09: result1 = 9*64 - 700 = -124 → 124 period (~504 Hz)
 *   add=0x0a: result2 = -124 + 10*64 = 516 period - wait that's wrong
 *   Actually: result2 = result1 + add*64 = -124 + 640 = 516 → too low
 *
 * Let's use base=0x0b for result1 = 11*64-700 = 4 → ~15kHz (too high)
 * base=0x0a: result1 = 10*64-700 = -60 → 60 period (~1042 Hz) - good chirp start
 * add=0x02: result2 = -60 + 128 = 68 period (~919 Hz)
 */
static const uint8 kSoundEntry3[] = {
	0x83,                   // Flags: type 3, bits 2,3=0 (tone+noise)
	0x00, 0x00, 0x00,       // Base params (to be determined from assembly)
	0x06, 0x04, 0x06,       // Add params
	0xfe,                   // Delta sign (-2)
	0x10,                   // Entry size (16 bytes)
	0x66,                   // Volume nibbles (low=6, high=6)
	0x01,                   // entry[4] - pattern selector (non-zero)
	0x01,                   // entry[5] - pattern selector (non-zero)
	0x00, 0x00, 0x00, 0x00  // Padding to 16 bytes
};

/**
 * Calibration value for sound processing.
 *
 * The original assembly uses position-dependent calibration (~8000 initially).
 * This creates wrapped 16-bit values. We use a smaller calibration that
 * produces valid AY periods in the audible frequency range.
 *
 * Calibration affects the starting period:
 *   period = entry[4+i] * 64 - calibration
 *
 * With calibration=700 and entry[4]=0x0c (12):
 *   period = 12 * 64 - 700 = 768 - 700 = 68 → 919 Hz
 */
static const int16 kSoundCalibration = 700;

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
	 * Unified sound entry processing - implements l1cffh calibration
	 *
	 * Processes a sound entry through the calibration system and sets up
	 * all channel parameters. This is the core function that makes all
	 * sounds work consistently.
	 *
	 * @param entry      Raw sound entry bytes (14+ bytes from l38e9h, l3904h, etc.)
	 * @param numChannels Number of channels to use (8 for shoot, fewer for others)
	 * @param outerLoops Number of outer loop iterations (l3bc4h, typically 5)
	 */
	void setupFromEntry(const uint8 *entry, int numChannels, int outerLoops) {
		// Extract flags from byte 0
		uint8 flags = entry[0];
		int8 deltaSign = static_cast<int8>(entry[7]);  // Signed delta from byte 7

		// Process parameters through calibration (l1cffh)
		// Formula: result1 = entry[1+i]*64 - calibration
		//          result2 = result1 + entry[4+i]*64
		int16 processed[6];
		for (int i = 0; i < 3; i++) {
			int16 val1 = static_cast<int16>(entry[1 + i]) * 64;
			int16 val2 = static_cast<int16>(entry[4 + i]) * 64;
			processed[i * 2] = val1 - kSoundCalibration;
			processed[i * 2 + 1] = val1 - kSoundCalibration + val2;
		}

		_channelCount = numChannels;
		_outerLoops = outerLoops;
		_maxLoops = outerLoops * numChannels;

		// Channel setup following sub_2112h distribution pattern
		for (int ch = 0; ch < numChannels; ch++) {
			int16 basePeriod, param2, delta;

			// Distribution pattern from assembly (lines 631-666 of sub_2112h)
			// IY[0-1] → ch 0,3,4,7; IY[2-3] → ch 1,2,5,6
			if (ch == 0 || ch == 3 || ch == 4 || ch == 7) {
				basePeriod = processed[0];
			} else {
				basePeriod = processed[1];
			}

			// IY[4-5] → ch 0,1,4,5; IY[6-7] → ch 2,3,6,7
			if (ch == 0 || ch == 1 || ch == 4 || ch == 5) {
				param2 = processed[2];
			} else {
				param2 = processed[3];
			}

			// IY[8-9] → ch 0-3; IY[10-11] → ch 4-7
			if (ch < 4) {
				delta = processed[4];
			} else {
				delta = processed[5];
			}

			// Calculate effective period (sub_2d87h op_type=1: HL = base + delta)
			int16 effectivePeriod = basePeriod + delta;

			// Handle invalid periods - try fallbacks
			if (effectivePeriod <= 0 || effectivePeriod > 4095) {
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
			_channelDelta[ch] = deltaSign;  // Use delta sign from entry byte 7
			_channelDone[ch] = false;

			debug("setupFromEntry: ch%d base=%d param2=%d delta=%d -> period=%d deltaSgn=%d",
				  ch, basePeriod, param2, delta, _channelPeriod[ch], deltaSign);
		}

		// Extract volume nibbles from bytes 9+ (low nibble = even ch, high = odd ch)
		for (int i = 0; i < (numChannels + 1) / 2 && i < 5; i++) {
			int ch0 = i * 2;
			int ch1 = i * 2 + 1;
			if (ch0 < numChannels) _channelVolume[ch0] = entry[9 + i] & 0x0F;
			if (ch1 < numChannels) _channelVolume[ch1] = (entry[9 + i] >> 4) & 0x0F;
		}

		// Configure mixer based on flags
		// Bit 2: disable tone, Bit 3: disable noise
		uint8 mixer = 0x3F;  // Start with all disabled
		int channel = (flags & 0x03);  // Channel A=1, B=2, C=3
		if (channel >= 1 && channel <= 3) {
			int chIdx = channel - 1;
			if (!(flags & 0x04)) mixer &= ~(1 << chIdx);        // Enable tone
			if (!(flags & 0x08)) mixer &= ~(1 << (chIdx + 3));  // Enable noise
		}
		writeReg(7, mixer);

		// Noise period (reasonable default)
		writeReg(6, 0x10);

		// Find first active channel for initial output
		uint16 initPeriod = 100;
		for (int ch = 0; ch < numChannels; ch++) {
			if (!_channelDone[ch] && _channelPeriod[ch] > 0) {
				initPeriod = _channelPeriod[ch];
				break;
			}
		}

		writeReg(0, initPeriod & 0xFF);
		writeReg(1, (initPeriod >> 8) & 0x0F);
		writeReg(8, 15);  // Start at max volume

		_loopCount = 0;

		debug("setupFromEntry: flags=0x%02x mixer=0x%02x initPeriod=%d deltaSign=%d",
			  flags, mixer, initPeriod, deltaSign);
	}

	/**
	 * Unified update function for entry-based sounds
	 * Applies delta per outer loop, cycles through channels
	 */
	void updateFromEntry() {
		int outerLoop = _loopCount / _channelCount;
		int innerIdx = _loopCount % _channelCount;

		// Apply delta at start of each outer loop
		if (innerIdx == 0 && _loopCount > 0) {
			for (int ch = 0; ch < _channelCount; ch++) {
				if (_channelDone[ch]) continue;

				int32 newPeriod = static_cast<int32>(_channelPeriod[ch]) + _channelDelta[ch];
				if (newPeriod <= 20 || newPeriod > 4095) {
					_channelDone[ch] = true;
					continue;
				}
				_channelPeriod[ch] = static_cast<uint16>(newPeriod);
			}
		}

		// Find active channel for output
		bool anyActive = false;
		int activeChannel = -1;
		uint16 outputPeriod = 0;

		for (int i = 0; i < _channelCount; i++) {
			int ch = (innerIdx + i) % _channelCount;
			if (_channelDone[ch]) continue;

			anyActive = true;
			if (activeChannel < 0) {
				activeChannel = ch;
				outputPeriod = _channelPeriod[ch];
			}
		}

		if (activeChannel >= 0 && outputPeriod > 0) {
			writeReg(0, outputPeriod & 0xFF);
			writeReg(1, (outputPeriod >> 8) & 0x0F);

			int vol = 15 - (outerLoop * 2);
			if (vol < 4) vol = 4;
			writeReg(8, static_cast<uint8>(vol));
		}

		if (!anyActive || _loopCount >= _maxLoops) {
			_finished = true;
			initAY();
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
	 * Uses unified setupFromEntry() with assembly data from l3904h.
	 * Assembly: 8 channels (ld a,008h at 2188h), 5 outer loops (l3bc4h).
	 *
	 * Entry at l3904h: 81 00 00 00 0c 01 0c fe 0e 66 22 66 1c 0a
	 */
	void setupShoot() {
		// Assembly: sub_2112h uses 8 channels and calls sub_2e96h with l3bc4h=5
		setupFromEntry(kSoundEntry1, 8, 5);
	}

	/**
	 * Sound 2: Collide - implements sub_26e2h at 0x26e2
	 *
	 * Assembly at sub_26e2h (lines 1281-1380):
	 *   Line 1302: ld ix,(l3d16h)        → Reads processed params
	 *   Lines 1303-1314: Copies params to 2 working areas (DUPLICATES each):
	 *     IX[0-1] → 01e7h AND 01edh (period1)
	 *     IX[4-5] → 01e9h AND 01efh (period2)
	 *     IX[8-9] → 01ebh AND 01f1h (delta)
	 *   Line 1349: ld a,002h             → Sets 2 CHANNELS
	 *   Line 1351: call sub_2e96h        → Main update loop
	 *
	 * Uses result1 values (IX byte offsets 0-1, 4-5, 8-9):
	 *   IX[0-1] = processed[0] = entry[1]*64 - calibration (period1)
	 *   IX[4-5] = processed[2] = entry[2]*64 - calibration (period2)
	 *   IX[8-9] = processed[4] = entry[3]*64 - calibration (delta base)
	 *
	 * NOTE: Calibration and delta accumulation via sub_2a65h needs investigation.
	 */
	void setupCollide() {
		const uint8 *entry = kSoundEntry2;

		// Process parameters through calibration (l1cffh)
		// Formula: result1 = entry[1+i]*64 - calibration
		int16 processed[6];
		for (int i = 0; i < 3; i++) {
			int16 val1 = static_cast<int16>(entry[1 + i]) * 64;
			int16 val2 = static_cast<int16>(entry[4 + i]) * 64;
			processed[i * 2] = val1 - kSoundCalibration;
			processed[i * 2 + 1] = val1 - kSoundCalibration + val2;
		}

		// Assembly line 1349: sub_26e2h uses exactly 2 channels
		_channelCount = 2;
		_outerLoops = 5;
		_maxLoops = _channelCount * _outerLoops;  // 2 * 5 = 10
		_loopCount = 0;

		// sub_26e2h reads result1 values at byte offsets 0-1, 4-5, 8-9
		// With base=0 and calibration=700:
		//   period1 = processed[0] = 0 - 700 = -700
		//   period2 = processed[2] = 0 - 700 = -700
		//   delta   = processed[4] = 0 - 700 = -700

		// Handle negative values (lines 1315-1323): negate if negative
		int16 period1 = processed[0];
		if (period1 < 0) period1 = -period1;
		int16 period2 = processed[2];
		if (period2 < 0) period2 = -period2;

		// Both channels use same period (duplicated in assembly lines 1305-1314)
		_channelPeriod[0] = static_cast<uint16>(period1);
		_channelPeriod[1] = static_cast<uint16>(period1);

		// Delta from entry[7] = 0xff = -1 (signed)
		int8 deltaSign = static_cast<int8>(entry[7]);
		_channelDelta[0] = deltaSign;
		_channelDelta[1] = deltaSign;

		_channelVolume[0] = 15;
		_channelVolume[1] = 15;
		_channelDone[0] = false;
		_channelDone[1] = false;

		// Set initial tone period
		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);

		// Configure mixer based on flags byte 0
		// Flags 0x82: bit 2=0 (tone enabled), bit 3=0 (noise enabled)
		writeReg(6, 0x10);      // Noise period
		writeReg(7, 0x36);      // Enable tone A + noise A
		writeReg(8, 15);        // Start at MAX volume

		debug("setupCollide: period=%d delta=%d (~%dHz)",
			  _channelPeriod[0], _channelDelta[0],
			  _channelPeriod[0] > 0 ? 1000000 / (16 * _channelPeriod[0]) : 0);
	}

	/**
	 * Sound 3: Step Up - implements sub_2607h at 0x2607
	 *
	 * Assembly at sub_2607h (lines 1177-1280):
	 *   Line 1184: ld ix,(l3d16h)        → Reads processed params
	 *   Lines 1185-1209: Distributes params to 4 pairs of working areas:
	 *     IX[0-1] → 01e7h, 01f9h (period1 for ch 0,2)
	 *     IX[2-3] → 01edh, 01f3h (period1 for ch 1,3)
	 *     IX[4-5] → 01e9h, 01efh (period2)
	 *     IX[6-7] → 01f5h, 01fbh (period2)
	 *     IX[8-9], IX[10-11] → delta distribution based on entry[4-5]
	 *   Lines 1210-1225: Pattern selection based on entry[4] and entry[5]
	 *   Line 1227: ld a,004h             → Sets 4 CHANNELS
	 *   Line 1229: call sub_2e96h        → Main update loop
	 *
	 * NOTE: Delta accumulation via sub_2a65h needs investigation.
	 */
	void setupStepUp() {
		const uint8 *entry = kSoundEntry3;
		int8 deltaSign = static_cast<int8>(entry[7]);  // -2 for ascending pitch

		// Process parameters through calibration (l1cffh)
		int16 processed[6];
		for (int i = 0; i < 3; i++) {
			int16 val1 = static_cast<int16>(entry[1 + i]) * 64;
			int16 val2 = static_cast<int16>(entry[4 + i]) * 64;
			processed[i * 2] = val1 - kSoundCalibration;
			processed[i * 2 + 1] = val1 - kSoundCalibration + val2;
		}

		// Assembly line 1227: sub_2607h uses exactly 4 channels
		_channelCount = 4;
		_outerLoops = 5;
		_maxLoops = _channelCount * _outerLoops;  // 4 * 5 = 20
		_loopCount = 0;

		// Distribution to 4 channels following assembly (lines 1185-1209)
		// sub_2607h reads from processed buffer:
		//   IX[0-1] = processed[0] = result1[0] → channels 0,2
		//   IX[2-3] = processed[1] = result2[0] → channels 1,3
		for (int ch = 0; ch < 4; ch++) {
			int16 periodBase;

			// Assembly distributes: channels 0,2 use IX[0-1], channels 1,3 use IX[2-3]
			if (ch == 0 || ch == 2) {
				periodBase = processed[0];  // result1
			} else {
				periodBase = processed[1];  // result2
			}

			// Handle negative values (negate if negative)
			if (periodBase < 0) periodBase = -periodBase;

			_channelPeriod[ch] = static_cast<uint16>(periodBase);
			_channelDelta[ch] = deltaSign;  // -2 from entry[7]
			_channelVolume[ch] = 15;
			_channelDone[ch] = false;
		}

		// Set initial tone period
		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);

		// Configure mixer based on flags
		// Flags 0x83: bit 2=0 (tone enabled), bit 3=0 (noise enabled)
		writeReg(6, 0x10);      // Noise period
		writeReg(7, 0x36);      // Enable tone A + noise A
		writeReg(8, 15);        // Start at MAX volume

		debug("setupStepUp: period[0]=%d period[1]=%d delta=%d (~%dHz)",
			  _channelPeriod[0], _channelPeriod[1], _channelDelta[0],
			  _channelPeriod[0] > 0 ? 1000000 / (16 * _channelPeriod[0]) : 0);
	}

	/**
	 * Sounds 4-9: Generic handler based on sub_2207h at 0x2207
	 *
	 * IMPLEMENTATION STATUS:
	 *   - Sound 6 (Menu): ASSEMBLY DATA from l3912h (type 6, 5 channels)
	 *   - Sound 7 (Hit):  ASSEMBLY DATA from l38e9h (type 7, 8 channels)
	 *   - Sound 4, 5, 8, 9: APPROXIMATION - no entries with these types exist
	 *
	 * Channel count for sub_2207h determined by comparing entry bytes 0x0c-0x0f:
	 *   - All equal → 5 channels
	 *   - One pair differs → 6 channels
	 *   - Both pairs differ → 8 channels
	 */
	void setupGeneric(int index) {
		_loopCount = 0;

		switch (index) {
		case 6: // Menu - ASSEMBLY DATA from l3912h
			// Type 6, bytes 0c-0f = 03 03 03 03 (all equal) → 5 channels
			setupFromEntry(kSoundEntry6, 5, 5);
			return;

		case 7: // Hit - ASSEMBLY DATA from l38e9h
			// Type 7, bytes 0c-0f = 03 03 05 05 (differ) → 8 channels
			setupFromEntry(kSoundEntry7, 8, 5);
			return;

		// APPROXIMATIONS: Sounds 4, 5, 8, 9 have no entries in the assembly table.
		// These would use sub_2207h if entries existed.
		// Implementing reasonable approximations based on handler behavior.

		case 4: // Step Down - APPROXIMATION (no type 4 entry exists)
			// Would use 5 channels (sub_2207h default) if entry existed
			// Descending pitch (opposite of step up)
			_channelCount = 4;  // Match step handler channel count
			for (int ch = 0; ch < 4; ch++) {
				_channelPeriod[ch] = 180 + ch * 20;  // 180, 200, 220, 240
				_channelDelta[ch] = 15;               // Descending pitch
				_channelVolume[ch] = 10 - ch;
				_channelDone[ch] = false;
			}
			_maxLoops = 20;
			writeReg(7, 0x3E);  // Tone only
			break;

		case 5: // Reserved - APPROXIMATION (no type 5 entry exists)
			_channelCount = 5;  // sub_2207h minimum
			for (int ch = 0; ch < 5; ch++) {
				_channelPeriod[ch] = 250;
				_channelDelta[ch] = 0;
				_channelVolume[ch] = 8;
				_channelDone[ch] = false;
			}
			_maxLoops = 25;
			writeReg(7, 0x3E);
			break;

		case 8: // Reserved - APPROXIMATION (no type 8 entry exists)
			_channelCount = 5;  // sub_2207h default
			for (int ch = 0; ch < 5; ch++) {
				_channelPeriod[ch] = 200 + ch * 10;
				_channelDelta[ch] = 8;
				_channelVolume[ch] = 10;
				_channelDone[ch] = false;
			}
			_maxLoops = 25;
			writeReg(7, 0x3E);
			break;

		case 9: // Fallen - APPROXIMATION (no type 9 entry exists)
			// Long descending sound for falling
			_channelCount = 5;
			for (int ch = 0; ch < 5; ch++) {
				_channelPeriod[ch] = 100 + ch * 20;
				_channelDelta[ch] = 6;
				_channelVolume[ch] = 15 - ch;
				_channelDone[ch] = false;
			}
			_maxLoops = 60;  // Long sound
			writeReg(7, 0x3E);
			break;

		default:
			_finished = true;
			return;
		}

		// Set initial tone period for approximation-based sounds
		writeReg(0, _channelPeriod[0] & 0xFF);
		writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
		writeReg(8, _channelVolume[0]);
	}

	/**
	 * Sounds >= 10: High index handler (l1d8fh / sub_257bh)
	 *
	 * From assembly at sub_257bh (0x257b, line 1109 in disassembly):
	 *   ld a,(001e5h)      ; Get type
	 *   sub 008h           ; Channel count = type - 8
	 *   ld (l3bc3h),a      ; Store channel count
	 *
	 * Channel count formula: index - 8
	 *   Type 10 → 2 channels
	 *   Type 11 → 3 channels
	 *   Type 12 → 4 channels
	 *   Type 13 → 5 channels
	 *
	 * APPROXIMATION: No specific entry data found for high index sounds.
	 * These use hardware envelope (AY registers 11-13) based on sub_257bh behavior.
	 * Uses l38a6h calibration table and processes through l1cffh.
	 */
	void setupHighIndex(int index) {
		// Assembly (line 1109): channel count = type - 8
		_channelCount = index - 8;
		if (_channelCount < 1) _channelCount = 1;
		if (_channelCount > 8) _channelCount = 8;
		_loopCount = 0;

		for (int ch = 0; ch < _channelCount; ch++) {
			_channelDone[ch] = false;
		}

		switch (index) {
		case 10: // Area Change - 2 channels (10 - 8 = 2)
			// APPROXIMATION: Uses HW envelope for transition effect
			_channelPeriod[0] = 0;
			_channelPeriod[1] = 0;
			_channelDelta[0] = 0;
			_channelDelta[1] = 0;
			_channelVolume[0] = 15;
			_channelVolume[1] = 15;
			_maxLoops = 100;        // ~2 seconds

			writeReg(6, 0x18);      // Noise period
			writeReg(7, 0x37);      // Noise A only, no tone
			writeReg(11, 0x00);     // Envelope period low
			writeReg(12, 0x40);     // Envelope period high
			writeReg(13, 0x00);     // Shape: single decay
			writeReg(8, 0x10);      // Volume = envelope mode
			break;

		case 11: // Explosion - 3 channels (11 - 8 = 3)
			// APPROXIMATION: Low rumble with envelope
			_channelPeriod[0] = 700;
			_channelPeriod[1] = 750;
			_channelPeriod[2] = 800;
			_channelDelta[0] = 20;
			_channelDelta[1] = 25;
			_channelDelta[2] = 30;
			_channelVolume[0] = 15;
			_channelVolume[1] = 14;
			_channelVolume[2] = 13;
			_maxLoops = 40;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(6, 0x1C);
			writeReg(7, 0x36);      // Tone + Noise
			writeReg(11, 0x00);
			writeReg(12, 0x20);
			writeReg(13, 0x00);     // Single decay
			writeReg(8, 0x10);      // Envelope mode
			break;

		case 12: // Warning - 4 channels (12 - 8 = 4)
			// APPROXIMATION: Alert tone with envelope
			_channelPeriod[0] = 150;
			_channelPeriod[1] = 160;
			_channelPeriod[2] = 170;
			_channelPeriod[3] = 180;
			_channelDelta[0] = 0;
			_channelDelta[1] = 0;
			_channelDelta[2] = 0;
			_channelDelta[3] = 0;
			_channelVolume[0] = 15;
			_channelVolume[1] = 14;
			_channelVolume[2] = 13;
			_channelVolume[3] = 12;
			_maxLoops = 60;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(7, 0x3E);      // Tone only
			writeReg(11, 0x00);
			writeReg(12, 0x06);
			writeReg(13, 0x0E);     // Continue, attack-decay
			writeReg(8, 0x10);      // Envelope mode
			break;

		case 13: // Mission Complete - 5 channels (13 - 8 = 5)
			// APPROXIMATION: Triumphant ascending tone
			_channelPeriod[0] = 200;
			_channelPeriod[1] = 190;
			_channelPeriod[2] = 180;
			_channelPeriod[3] = 170;
			_channelPeriod[4] = 160;
			_channelDelta[0] = -2;
			_channelDelta[1] = -2;
			_channelDelta[2] = -2;
			_channelDelta[3] = -2;
			_channelDelta[4] = -2;
			_channelVolume[0] = 15;
			_channelVolume[1] = 14;
			_channelVolume[2] = 13;
			_channelVolume[3] = 12;
			_channelVolume[4] = 11;
			_maxLoops = 80;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(7, 0x3E);      // Tone only
			writeReg(11, 0x00);
			writeReg(12, 0x30);
			writeReg(13, 0x0A);     // Continue, hold
			writeReg(8, 0x10);      // Envelope mode
			break;

		default:
			// Generic high index sound
			for (int ch = 0; ch < _channelCount; ch++) {
				_channelPeriod[ch] = 200 + ch * 20;
				_channelDelta[ch] = 0;
				_channelVolume[ch] = 12 - ch;
			}
			_maxLoops = 30;

			writeReg(0, _channelPeriod[0] & 0xFF);
			writeReg(1, (_channelPeriod[0] >> 8) & 0x0F);
			writeReg(7, 0x3E);
			writeReg(11, 0x00);
			writeReg(12, 0x10);
			writeReg(13, 0x00);
			writeReg(8, 0x10);      // Envelope mode
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
	 * Sound update loop for Shoot - uses unified updateFromEntry()
	 *
	 * Assembly: sub_2e96h runs 5 outer loops × 8 channels.
	 * Delta from entry byte 7 (0xfe = -2) applied per outer loop.
	 */
	void updateShoot() {
		updateFromEntry();
	}

	/**
	 * Update for Sound 2 (Collide) - follows sub_2e96h loop
	 *
	 * Assembly sub_2e96h (lines 2292-2335):
	 *   - Outer loop count from l3bc4h (=5)
	 *   - Inner loop for each channel
	 *   - Calls sub_2d87h per channel (applies delta based on op_type)
	 *
	 * For type 2, both channels have duplicated parameters,
	 * so they evolve similarly but may finish at different times.
	 */
	void updateCollide() {
		int outerLoop = _loopCount / _channelCount;
		int innerIdx = _loopCount % _channelCount;

		// sub_2d87h applies delta based on outer loop iteration (op_type at 0x025f)
		// op_type 1,2 = add delta, op_type 3,4 = subtract delta
		// For simplicity, we apply delta uniformly at start of each outer loop
		if (innerIdx == 0 && _loopCount > 0) {
			for (int ch = 0; ch < _channelCount; ch++) {
				if (_channelDone[ch]) continue;

				// Apply delta (can be negative from entry[7])
				int32 newPeriod = static_cast<int32>(_channelPeriod[ch]) + _channelDelta[ch];

				// Check bounds (AY period range is 1-4095)
				if (newPeriod < 20 || newPeriod > 4095) {
					_channelDone[ch] = true;
				} else {
					_channelPeriod[ch] = static_cast<uint16>(newPeriod);
				}
			}
		}

		// Output current channel's period
		int ch = innerIdx % _channelCount;
		if (!_channelDone[ch] && _channelPeriod[ch] > 0) {
			writeReg(0, _channelPeriod[ch] & 0xFF);
			writeReg(1, (_channelPeriod[ch] >> 8) & 0x0F);
		}

		// Volume decay matching other working sounds (start at 15, min 4)
		int vol = 15 - (outerLoop * 2);
		if (vol < 4) vol = 4;
		writeReg(8, static_cast<uint8>(vol));

		// Check if all channels are done
		bool anyActive = false;
		for (int i = 0; i < _channelCount; i++) {
			if (!_channelDone[i]) {
				anyActive = true;
				break;
			}
		}
		if (!anyActive) {
			_finished = true;
			initAY();
		}
	}

	/**
	 * Update for Sound 3 (Step) - follows sub_2e96h loop
	 *
	 * Assembly sub_2e96h (lines 2292-2335):
	 *   - Outer loop count from l3bc4h (=5)
	 *   - Inner loop for each of 4 channels
	 *   - sub_2d87h processes based on op_type (1-5)
	 *
	 * For type 3, 4 channels with different parameter distributions.
	 */
	void updateStepUp() {
		int outerLoop = _loopCount / _channelCount;
		int innerIdx = _loopCount % _channelCount;

		// Apply delta at start of each outer loop (sub_2d87h behavior)
		if (innerIdx == 0 && _loopCount > 0) {
			for (int ch = 0; ch < _channelCount; ch++) {
				if (_channelDone[ch]) continue;

				int32 newPeriod = static_cast<int32>(_channelPeriod[ch]) + _channelDelta[ch];

				// Check bounds
				if (newPeriod < 20 || newPeriod > 4095) {
					_channelDone[ch] = true;
				} else {
					_channelPeriod[ch] = static_cast<uint16>(newPeriod);
				}
			}
		}

		// Output current channel's period
		int ch = innerIdx % _channelCount;
		if (!_channelDone[ch] && _channelPeriod[ch] > 0) {
			writeReg(0, _channelPeriod[ch] & 0xFF);
			writeReg(1, (_channelPeriod[ch] >> 8) & 0x0F);
		}

		// Volume decay matching other working sounds (start at 15, min 4)
		int vol = 15 - (outerLoop * 2);
		if (vol < 4) vol = 4;
		writeReg(8, static_cast<uint8>(vol));

		// Check if all channels are done
		bool anyActive = false;
		for (int i = 0; i < _channelCount; i++) {
			if (!_channelDone[i]) {
				anyActive = true;
				break;
			}
		}
		if (!anyActive) {
			_finished = true;
			initAY();
		}
	}

	void updateGeneric() {
		// Sounds 6 and 7 use unified entry-based system (ASSEMBLY DATA)
		if (_index == 6 || _index == 7) {
			updateFromEntry();
			return;
		}

		// Sounds 4, 5, 8, 9: APPROXIMATIONS using multi-channel update
		// (mimics sub_2207h behavior with 4-5 channels)
		int outerLoop = _loopCount / _channelCount;
		int innerIdx = _loopCount % _channelCount;

		// Apply delta at start of each outer loop
		if (innerIdx == 0 && _loopCount > 0) {
			for (int ch = 0; ch < _channelCount; ch++) {
				if (_channelDone[ch]) continue;

				int32 newPeriod = static_cast<int32>(_channelPeriod[ch]) + _channelDelta[ch];
				if (newPeriod < 50 || newPeriod > 2000) {
					_channelDone[ch] = true;
				} else {
					_channelPeriod[ch] = static_cast<uint16>(newPeriod);
				}
			}
		}

		// Find active channel for output
		bool anyActive = false;
		for (int i = 0; i < _channelCount; i++) {
			int ch = (innerIdx + i) % _channelCount;
			if (!_channelDone[ch] && _channelPeriod[ch] > 0) {
				writeReg(0, _channelPeriod[ch] & 0xFF);
				writeReg(1, (_channelPeriod[ch] >> 8) & 0x0F);
				anyActive = true;
				break;
			}
		}

		if (!anyActive) {
			_finished = true;
			initAY();
			return;
		}

		// Volume decay varies by sound type
		int vol;
		switch (_index) {
		case 9: // Fallen - slow decay
			vol = 15 - (outerLoop / 2);
			break;
		default:
			vol = 15 - outerLoop * 2;
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
