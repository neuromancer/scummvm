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

#include "neuromancer/rom.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/scene_real_world.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// Same 216x64 panel layout as the Skills window. Matches the DOS
// rom_panel_open's neuro_menu_create origin.
enum {
	kWindowX        = 48,
	kWindowY        = 128,
	kWindowWidthPx  = 216,
	kWindowHeightPx = 64,
	kWindowPackedW  = kWindowWidthPx / 2,
	kWindowBytes    = kWindowPackedW * kWindowHeightPx
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

Rom::Rom(NeuromancerEngine *engine, RealWorldScene *scene)
	: _engine(engine),
	  _scene(scene),
	  _active(false),
	  _state(kStateMainMenu) {
	_sprite.resize(sizeof(ImhHeader) + kWindowBytes);
	writeImhHeader(_sprite.data(), 0, 0, kWindowPackedW, kWindowHeightPx);
}

void Rom::open() {
	_active = true;
	drawMainMenu();
	debugC(1, kDebugGeneral, "Rom: opened");
}

void Rom::close() {
	_active = false;
	_engine->spriteChain()->clearSprite(kLayerPaxWindow);
	debugC(1, kDebugGeneral, "Rom: closed");
}

void Rom::update() {}

bool Rom::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_KEYDOWN) {
		// Escape always backs out to the main menu; from the main
		// menu it closes the panel entirely. Matches the DOS pattern
		// where "X" is always the accelerator for "exit current".
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			if (_state == kStateMainMenu)
				close();
			else
				drawMainMenu();
			return true;
		}

		// Message screen: any key returns to the menu.
		if (_state == kStateMessage) {
			drawMainMenu();
			return true;
		}

		char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));
		if (_state == kStateMainMenu)
			return dispatchMainMenu(key);
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		if (_state == kStateMessage) {
			drawMainMenu();
			return true;
		}
		// Menu rows: 4 entries starting at y = kWindowY + 8, 8px each.
		// DOS menu layout places the items tightly in the first column
		// of the panel; we mimic that with full-width hitboxes.
		if (_state == kStateMainMenu) {
			int x = event.mouse.x;
			int y = event.mouse.y;
			if (x >= kWindowX && x <= kWindowX + kWindowWidthPx - 1) {
				for (int i = 0; i < 4; i++) {
					int top = kWindowY + 8 + i * 8;
					if (y >= top && y <= top + 7) {
						static const char kHotkeys[4] = {'x', '1', '2', '3'};
						return dispatchMainMenu(kHotkeys[i]);
					}
				}
			}
		}
	}
	return false;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void Rom::drawWindowFrame() {
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	const int packedW = kWindowPackedW;
	for (int row = 0; row < (int)kWindowHeightPx; row++) {
		byte *line = pixels + row * packedW;
		if (row == 0 || row == (int)kWindowHeightPx - 1) {
			memset(line, 0x00, packedW);
		} else {
			memset(line, 0xFF, packedW);
			line[0]           = 0x0F;
			line[packedW - 1] = 0xF0;
		}
	}
}

void Rom::drawMainMenu() {
	_state = kStateMainMenu;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Exact DOS menu text from binary address 2000:6518, split into
	// its four lines. The '\r' separators in the binary become real
	// line breaks for our renderer.
	static const char kMenu[] =
		"X. Exit Rom Construct\n"
		"1. Software Debug\n"
		"2. Software Analysis\n"
		"3. Monitor Mode";
	drawString(kMenu, kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

void Rom::drawMessage(const char *text) {
	_state = kStateMessage;
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	Common::String body = Common::String::format(
		"%s\n\n\n"
		"Press any key.", text ? text : "");
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 16, pixels);
	pushSprite();
}

void Rom::pushSprite() {
	_engine->spriteChain()->addSprite(kLayerPaxWindow, kWindowX, kWindowY,
	                                  _sprite.data(), /*opaque=*/true);
}

// -------------------------------------------------------------------------
// Dispatch
// -------------------------------------------------------------------------

bool Rom::dispatchMainMenu(char key) {
	switch (key) {
	case 'x':
		close();
		return true;
	case '1':
		// "Software Debug": DOS rom_software_debug (FUN_1000_d989) opens
		// a software picker and runs the Debug skill on the selected
		// slot. From this panel (outside cyberspace proper) we jack in
		// so the player reaches the cyberspace scene where the debug
		// loop actually runs.
		close();
		_scene->enterCyberspace();
		return true;
	case '2':
		// "Software Analysis": DOS rom_software_analysis is a thin
		// wrapper that jumps straight into the shared skills menu
		// (FUN_1000_7e62). We do the same -- close the ROM panel and
		// ask the scene to open its Skills sub-module.
		close();
		_scene->openSkillsMenu();
		return true;
	case '3':
		// "Monitor Mode": jacks the player into cyberspace. DOS
		// rom_main_loop case 3 calls FUN_1000_85AA which opens this
		// panel when not yet in cyberspace; here the panel is already
		// open and the user picked Monitor, so switch scenes.
		close();
		_scene->enterCyberspace();
		return true;
	default:
		return false;
	}
}

} // End of namespace Neuromancer
