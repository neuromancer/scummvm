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

#ifndef INTERSPECTIVE_SPRITE_H
#define INTERSPECTIVE_SPRITE_H

#include "common/rect.h"

namespace Interspective {

struct SpriteInfo {
	enum {
		kSpriteMapRecordSize = 10
	};

	// Default-construct an empty sprite — width=0/height=0, used as a
	// safe fallback when an out-of-range sprite index is requested.
	SpriteInfo() : left(0), top(0), width(0), height(0), image(0), hotLeft(0), hotTop(0) {}
	SpriteInfo(const byte *, uint16 index);
	bool empty() const { return width == 0 || height == 0; }
	uint16 imageId() const { return image; }
	Common::Rect rect() const { return Common::Rect(width, height); }
	Common::Rect sourceRect() const { return Common::Rect(left, top, left + width, top + height); }
	Common::Point hotPoint() const { return Common::Point(hotLeft, hotTop); }
	Common::Rect topLeftRect(Common::Point topLeft) const {
		Common::Rect r = rect();
		r.moveTo(topLeft);
		return r;
	}
	Common::Rect bottomAnchoredRect(Common::Point pos) const {
		Common::Rect r = rect();
		r.moveTo(pos);
		r.translate(0, -int16(height));
		r.translate(-int16(hotLeft), int16(hotTop));
		return r;
	}

	uint16 left;
	uint16 top;
	uint16 width;
	uint16 height;
	uint16 image;
	int8 hotLeft;
	int8 hotTop;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_SPRITE_H
