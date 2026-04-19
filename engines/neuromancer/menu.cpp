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

#include "neuromancer/menu.h"

#include "neuromancer/decompress.h"
#include "neuromancer/font.h"

#include "common/endian.h"

namespace Neuromancer {

namespace {

// Inner padding in pixels on each side of the bordered frame.
enum {
	kFramePadding = 8,
	kCellPx       = 8
};

} // anonymous namespace

NeuroMenu::NeuroMenu() : _pixL(0), _pixT(0), _pixW(0), _pixH(0), _innerL(0), _innerT(0) {}

void NeuroMenu::create(int cellL, int cellT, int cellW, int cellH) {
	_pixL = (cellL - 1) * kCellPx;
	_pixT = (cellT - 1) * kCellPx;
	_pixW = cellW * kCellPx + 2 * kFramePadding;
	_pixH = cellH * kCellPx + 2 * kFramePadding;
	_innerL = kFramePadding;
	_innerT = kFramePadding;

	_items.clear();
	rebuildFrame();
}

// Construct the bordered frame as a 4bpp-packed IMH sprite. The original
// build_text_frame fills the interior with white (0xFF) and emits the
// vertical borders by punching a 0x0F / 0xF0 pattern into the first/last
// packed bytes of each middle row; the top and bottom rows are fully black.
void NeuroMenu::rebuildFrame() {
	int packedW = _pixW / 2;
	int h = _pixH;
	_imh.resize(sizeof(ImhHeader) + (uint32)packedW * h);
	memset(_imh.data(), 0, _imh.size());

	WRITE_LE_UINT16(_imh.data() + 0, 0);                 // dx
	WRITE_LE_UINT16(_imh.data() + 2, 0);                 // dy
	WRITE_LE_UINT16(_imh.data() + 4, (uint16)packedW);   // packed width
	WRITE_LE_UINT16(_imh.data() + 6, (uint16)h);         // height

	byte *pix = _imh.data() + sizeof(ImhHeader);
	for (int row = 0; row < h; row++) {
		byte *line = pix + row * packedW;
		if (row == 0 || row == h - 1) {
			memset(line, 0x00, packedW);
		} else {
			memset(line, 0xFF, packedW);
			line[0] = 0x0F;
			line[packedW - 1] = 0xF0;
		}
	}
}

void NeuroMenu::drawText(const char *text, int cellL, int cellT) {
	int leftPx = _innerL + cellL * kCellPx;
	int topPx  = _innerT + cellT * kCellPx;
	byte *pixels = _imh.data() + sizeof(ImhHeader);

	// drawString (now routed through Graphics::DosFont) paints black ink
	// on a white cell background -- exactly what we want on top of the
	// white frame interior. The previous XOR-with-scratch trick was an
	// inversion workaround for the old custom renderer and now flips
	// polarity the wrong way, making the menu title look blank. Draw
	// straight onto the frame instead.
	drawString(text, _pixW, _pixH, leftPx, topPx, pixels);
}

void NeuroMenu::addItem(int cellL, int cellT, int cellW, int code, char label) {
	MenuButton b;
	int leftPx  = _pixL + _innerL + cellL * kCellPx;
	int topPx   = _pixT + _innerT + cellT * kCellPx;
	int widthPx = cellW * kCellPx;
	b.left   = (uint16)leftPx;
	b.top    = (uint16)topPx;
	b.right  = (uint16)(leftPx + widthPx - 1);
	b.bottom = (uint16)(topPx + kFontCharHeight - 1);
	b.code   = (uint16)code;
	b.label  = label;
	_items.push_back(b);
}

const MenuButton *NeuroMenu::hitTestKey(char keyAscii) const {
	char lower = (char)tolower((byte)keyAscii);
	for (uint i = 0; i < _items.size(); i++) {
		char itemLower = (char)tolower((byte)_items[i].label);
		if (itemLower == lower)
			return &_items[i];
	}
	return nullptr;
}

const MenuButton *NeuroMenu::hitTestPoint(int x, int y) const {
	for (uint i = 0; i < _items.size(); i++) {
		const MenuButton &b = _items[i];
		if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom)
			return &b;
	}
	return nullptr;
}

} // End of namespace Neuromancer
