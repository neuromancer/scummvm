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

class DrillerCPCSfxStream : public Audio::AudioStream {
public:
	DrillerCPCSfxStream(int index, int rate = 44100) : _ay(rate, 1000000), _index(index), _rate(rate) {
		// CPC uses 1MHz clock for AY-3-8912
		initAY();
		_counter = 0;
		_finished = false;
		_phase = 0;

		// Initialize channel state
		for (int i = 0; i < 3; i++) {
			_channelPeriod[i] = 0;
			_channelVolume[i] = 0;
			_channelDelta[i] = 0;
			_channelDuration[i] = 0;
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

	// Channel state for multi-channel processing (sub_2e96h)
	uint16 _channelPeriod[3];
	int16 _channelDelta[3];
	uint8 _channelVolume[3];
	uint16 _channelDuration[3];

	// Sound-specific state
	uint16 _tonePeriod;
	int16 _toneDelta;
	uint8 _loopCount;
	uint8 _maxLoops;

	void writeReg(int reg, uint8 val) {
		if (reg >= 0 && reg < 16) {
			_regs[reg] = val;
			_ay.setReg(reg, val);
		}
	}

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

	// Sound 1: Shoot - Based on sub_2112h analysis
	// Laser-like descending pitch sweep from high to low
	// Original uses 8 channels with complex envelope, simplified here
	void setupShoot() {
		// Period 100 (~625Hz) sweeping to 800 (~78Hz) over ~1.2 seconds
		_tonePeriod = 100;      // Starting period (higher pitch ~625Hz)
		_toneDelta = 12;        // Increase period each tick (descend pitch)
		_channelVolume[0] = 15;
		_maxLoops = 60;         // 60 ticks at 50Hz = 1.2 seconds
		_loopCount = 0;

		// Enable tone on channel A
		writeReg(7, 0x3E);      // Tone A only (bit 0 = 0)
	}

	// Sound 2: Collide - Based on sub_26e2h analysis
	// Bump/thud sound with noise + low tone
	void setupCollide() {
		_tonePeriod = 600;      // Low frequency tone (~104Hz)
		_channelVolume[0] = 15;
		_maxLoops = 15;         // ~300ms
		_loopCount = 0;

		// Enable noise + tone on channel A for thud effect
		writeReg(6, 0x18);      // Noise period (lower = rougher)
		writeReg(7, 0x36);      // Tone A + Noise A
	}

	// Sound 3: Step Up - Based on sub_2607h analysis
	// Short ascending blip for footstep going up
	void setupStepUp() {
		// Period 400 (~156Hz) ascending to 150 (~417Hz)
		_tonePeriod = 400;      // Start lower pitch
		_toneDelta = -10;       // Decrease period (ascend pitch)
		_channelVolume[0] = 12;
		_maxLoops = 20;         // ~400ms
		_loopCount = 0;

		writeReg(7, 0x3E);      // Tone A only
	}

	// Sounds 4-9: Generic handler based on sub_2207h
	// Each sound has specific characteristics
	void setupGeneric(int index) {
		_loopCount = 0;

		switch (index) {
		case 4: // Step Down - descending blip for footstep going down
			_tonePeriod = 200;      // Start higher pitch (~312Hz)
			_toneDelta = 15;        // Increase period (descend pitch)
			_channelVolume[0] = 12;
			_maxLoops = 20;         // ~400ms
			writeReg(7, 0x3E);      // Tone A only
			break;

		case 5: // Area transition sound
			_tonePeriod = 300;      // Medium pitch (~208Hz)
			_toneDelta = 0;
			_channelVolume[0] = 15;
			_maxLoops = 25;         // ~500ms
			writeReg(7, 0x3E);
			break;

		case 6: // Menu click - short high blip
			_tonePeriod = 150;      // Higher pitch (~417Hz)
			_toneDelta = 0;
			_channelVolume[0] = 15;
			_maxLoops = 8;          // ~160ms - short click
			writeReg(7, 0x3E);
			break;

		case 7: // Hit - impact sound with noise
			_tonePeriod = 400;      // Low-mid pitch
			_toneDelta = 20;        // Descending
			_channelVolume[0] = 15;
			_maxLoops = 20;
			writeReg(6, 0x10);      // Add some noise
			writeReg(7, 0x36);      // Tone + Noise
			break;

		case 8: // Generic sound
			_tonePeriod = 250;
			_toneDelta = 5;
			_channelVolume[0] = 12;
			_maxLoops = 30;
			writeReg(7, 0x3E);
			break;

		case 9: // Fallen - long descending sweep
			_tonePeriod = 150;      // Start high (~417Hz)
			_toneDelta = 8;         // Slow descent
			_channelVolume[0] = 15;
			_maxLoops = 80;         // ~1.6 seconds - long fall
			writeReg(7, 0x3E);
			break;

		default:
			_finished = true;
			return;
		}
	}

	// Sounds >= 10: High index handler (l1d8fh)
	// Uses hardware envelope for sustained sounds
	void setupHighIndex(int index) {
		_loopCount = 0;

		// Setup based on specific high index sounds
		switch (index) {
		case 10: // Area Change - Based on flags byte 0x87 at 0x38e9
			// Bit 2=1 (NOISE enabled), Bit 3=0 (TONE disabled)
			_tonePeriod = 0;
			_toneDelta = 0;
			_maxLoops = 100;        // ~2 seconds with envelope decay
			// Set noise period from data byte 9 (0x7f & 0x1f = 0x1f)
			writeReg(6, 0x1F);      // Noise period
			// Mixer: disable tone, enable noise on channel A
			// 0x37 = 0b00110111: bits 0-2=1 (tones off), bit 3=0 (noise A on)
			writeReg(7, 0x37);
			// Use hardware envelope for natural decay
			writeReg(11, 0x00);     // Envelope period low
			writeReg(12, 0x20);     // Envelope period high (slow decay)
			writeReg(13, 0x00);     // Envelope shape: single decay (\)
			writeReg(8, 0x10);      // Volume A = envelope
			break;

		case 11: // Explosion/rumble
			_tonePeriod = 800;      // Low rumble
			_maxLoops = 60;
			writeReg(0, _tonePeriod & 0xFF);
			writeReg(1, (_tonePeriod >> 8) & 0x0F);
			writeReg(6, 0x1F);      // Noise period (rough)
			writeReg(7, 0x36);      // Tone A + Noise A
			writeReg(11, 0x00);
			writeReg(12, 0x30);     // Medium-slow decay
			writeReg(13, 0x00);     // Decay shape
			writeReg(8, 0x10);
			break;

		case 12: // Warning tone
			_tonePeriod = 180;      // ~347Hz
			_maxLoops = 80;
			writeReg(0, _tonePeriod & 0xFF);
			writeReg(1, (_tonePeriod >> 8) & 0x0F);
			writeReg(7, 0x3E);
			writeReg(11, 0x00);
			writeReg(12, 0x08);     // Faster cycle
			writeReg(13, 0x0E);     // Continue, alternate (warble)
			writeReg(8, 0x10);
			break;

		case 13: // Mission Complete - triumphant jingle
			_tonePeriod = 150;      // Start high (~417Hz)
			_toneDelta = -1;        // Slowly rise in pitch
			_maxLoops = 120;        // ~2.4 seconds
			writeReg(0, _tonePeriod & 0xFF);
			writeReg(1, (_tonePeriod >> 8) & 0x0F);
			writeReg(7, 0x3E);
			writeReg(11, 0x00);
			writeReg(12, 0x40);     // Very slow envelope
			writeReg(13, 0x0E);     // Attack/decay cycle
			writeReg(8, 0x10);
			break;

		default:
			// Generic high index sound
			_tonePeriod = 250;
			_maxLoops = 50;
			writeReg(0, _tonePeriod & 0xFF);
			writeReg(1, (_tonePeriod >> 8) & 0x0F);
			writeReg(7, 0x3E);
			writeReg(8, 0x10);
			writeReg(11, 0x00);
			writeReg(12, 0x10);
			writeReg(13, 0x00);
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

	void updateShoot() {
		// Descending pitch sweep (period increases = lower pitch)
		_tonePeriod += _toneDelta;
		if (_tonePeriod > 1000) _tonePeriod = 1000;  // Cap at ~62Hz

		writeReg(0, _tonePeriod & 0xFF);
		writeReg(1, (_tonePeriod >> 8) & 0x0F);

		// Volume decay over duration
		int vol = 15 - (_loopCount / 4);
		if (vol < 0) vol = 0;
		writeReg(8, vol);
	}

	void updateCollide() {
		// Bump sound - quick decay with slight pitch drop
		_tonePeriod += 10;  // Slight pitch drop
		if (_tonePeriod > 900) _tonePeriod = 900;

		writeReg(0, _tonePeriod & 0xFF);
		writeReg(1, (_tonePeriod >> 8) & 0x0F);

		// Quick decay
		int vol = 15 - _loopCount;
		if (vol < 0) vol = 0;
		writeReg(8, vol);
	}

	void updateStepUp() {
		// Ascending pitch sweep (period decreases = higher pitch)
		int period = (int)_tonePeriod + _toneDelta;
		if (period < 100) period = 100;  // Cap at ~625Hz
		_tonePeriod = period;

		writeReg(0, _tonePeriod & 0xFF);
		writeReg(1, (_tonePeriod >> 8) & 0x0F);

		// Volume decay
		int vol = _channelVolume[0] - (_loopCount / 3);
		if (vol < 0) vol = 0;
		writeReg(8, vol);
	}

	void updateGeneric() {
		// Apply tone delta
		int period = (int)_tonePeriod + _toneDelta;
		if (period < 50) period = 50;
		if (period > 2000) period = 2000;
		_tonePeriod = period;

		writeReg(0, _tonePeriod & 0xFF);
		writeReg(1, (_tonePeriod >> 8) & 0x0F);

		// Volume decay based on sound type
		int decayRate = (_index == 6) ? 2 : 5;  // Menu click decays faster
		int vol = _channelVolume[0] - (_loopCount / decayRate);
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
