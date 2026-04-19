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

#ifndef NEUROMANCER_ROM_H
#define NEUROMANCER_ROM_H

#include "common/array.h"
#include "common/events.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Neuromancer {

class NeuromancerEngine;
class RealWorldScene;

// ROM-construct panel. Mirrors the DOS "rom_panel_open" +
// "rom_main_loop" pair found in neuro.exe (labelled via Ghidra pass in
// RE session 2026-04-19). DOS strings: "X. Exit Rom Construct / 1.
// Software Debug / 2. Software Analysis / 3. Monitor Mode" at the
// binary address 2000:6518.
//
// In the original game this panel is both a plain UI (the three menu
// entries) and the entry point into cyberspace: "Monitor Mode" jacks
// the player in and "Software Debug / Analysis" are cyberdeck tools
// used while jacked. We port the shell here so the UI works, but
// Debug and Monitor Mode currently bounce with a "Cyberspace only"
// message until the cyberspace engine lands.
//
// Analysis dispatches into the existing Skills sub-module; that's
// the direct DOS behaviour (rom_software_analysis is a thin jump into
// FUN_1000_7e62 which is the shared skill-picker).
class Rom {
public:
	explicit Rom(NeuromancerEngine *engine, RealWorldScene *scene);
	~Rom() = default;

	bool isActive() const { return _active; }
	void open();
	void close();
	void update();
	bool handleEvent(const Common::Event &event);

private:
	enum State {
		kStateMainMenu = 0, // "X. Exit / 1. Software Debug / 2. Software Analysis / 3. Monitor Mode"
		kStateMessage  = 1  // generic "press any key" message panel
	};

	void drawWindowFrame();
	void drawMainMenu();
	void drawMessage(const char *text);
	void pushSprite();

	bool dispatchMainMenu(char key);

	NeuromancerEngine *_engine;
	RealWorldScene   *_scene;
	bool  _active;
	State _state;

	// Packed 4bpp sprite for the ROM window. 216x64 at (48, 128) --
	// matches the DOS final-frame window rect from rom_panel_open's
	// neuro_menu_create(mode=6, cellL=7, cellT=17, cellW=24, cellH=6)
	// and keeps the on-screen position consistent with the Skills panel.
	Common::Array<byte> _sprite;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_ROM_H
