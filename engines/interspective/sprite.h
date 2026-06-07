/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $URL$
 * $Id$
 *
 */

#ifndef INTERSPECTIVE_SPRITE_H
#define INTERSPECTIVE_SPRITE_H

namespace Interspective {

struct SpriteInfo {
	// Default-construct an empty sprite — width=0/height=0, used as a
	// safe fallback when an out-of-range sprite index is requested
	// (Program::getSpriteInfo iter-29).
	SpriteInfo() : left(0), top(0), width(0), height(0), image(0), hotLeft(0), hotTop(0) {}
	SpriteInfo(const byte *, uint16 index);
	uint16 left, top, width, height, image;
	int8 hotLeft, hotTop;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_SPRITE_H
