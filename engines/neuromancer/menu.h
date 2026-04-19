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

#ifndef NEUROMANCER_MENU_H
#define NEUROMANCER_MENU_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Neuromancer {

// Hit-testable rectangular region inside a NeuroMenu. Matches the
// neuro_button_t layout in Reuromancer/LibNeuroRoutines/neuro_routines.h.
struct MenuButton {
	uint16 left;
	uint16 top;
	uint16 right;
	uint16 bottom;
	uint16 code;   // opaque identifier returned by hitTest()
	char label;    // keyboard accelerator (e.g. 'n', 'l')
};

// Port of scene_main_menu.c's "mode 6" neuro_menu: a bordered white-on-black
// text frame with clickable/keyboard-triggerable items.
//
// Coordinates are expressed in 8-pixel character cells, matching the
// original API:
//   - create(cellL, cellT, cellW, cellH): l and t are 1-based cell origins;
//     w/h are the inner cell dimensions. The frame adds 8px padding on each
//     side. So create(5, 20, 10, 1) -> pixel rect (32, 152, 128, 176).
//   - drawText(text, cellL, cellT): draws text at inner cell offset.
//   - addItem(cellL, cellT, cellW, code, label): registers a hit region.
class NeuroMenu {
public:
	NeuroMenu();

	// (Re)build the menu. Clears any existing items and text.
	void create(int cellL, int cellT, int cellW, int cellH);

	// Draw plain text into the inner region at a cell offset.
	void drawText(const char *text, int cellL, int cellT);

	// Register a hit region keyed to a keyboard label and a `code`
	// returned from hit-test methods.
	void addItem(int cellL, int cellT, int cellW, int code, char label);

	// Keyboard hit-test: returns the button matching `keyAscii` (case-insensitive)
	// or nullptr. The label 'x' -> code 0x0A is the conventional "exit" value.
	const MenuButton *hitTestKey(char keyAscii) const;

	// Mouse hit-test: returns the button covering screen-space (x, y).
	const MenuButton *hitTestPoint(int x, int y) const;

	// Raw IMH buffer (with ImhHeader + packed pixels) suitable for passing to
	// SpriteChain::addSprite.
	const byte *imhBuffer() const { return _imh.data(); }

	// Top-left origin on screen (pixel coordinates).
	int left() const { return _pixL; }
	int top()  const { return _pixT; }

private:
	Common::Array<byte> _imh;
	Common::Array<MenuButton> _items;

	int _pixL, _pixT, _pixW, _pixH;
	int _innerL, _innerT;

	void rebuildFrame();
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_MENU_H
