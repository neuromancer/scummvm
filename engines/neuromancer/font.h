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

#ifndef NEUROMANCER_FONT_H
#define NEUROMANCER_FONT_H

#include "common/scummsys.h"

namespace Neuromancer {

enum {
	kFontCharWidth  = 8,
	kFontCharHeight = 8
};

// Render an ASCII string as IMH-format (4bpp-packed) data in-place into
// `dst`. `dst` must be a sprite buffer large enough to cover (widthPx × h)
// pixels starting at (leftPx, topPx). The buffer layout is:
//
//     [ImhHeader (8 bytes)][packed pixel rows, widthPx/2 bytes each]
//
// `widthPx` and `h` describe the owning sprite's pixel dimensions (NOT
// packed width). `leftPx` / `topPx` are pixel offsets inside the sprite.
// Non-printable characters are skipped except for '\n', which advances to
// the next line at column 0.
void drawString(const char *string, int widthPx, int heightPx,
                int leftPx, int topPx, byte *dst);

} // End of namespace Neuromancer

#endif // NEUROMANCER_FONT_H
