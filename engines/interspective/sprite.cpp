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

#include "interspective/sprite.h"

namespace Interspective {
//

enum SpriteMap {
	kSpriteMapImage = 0,
	kSpriteMapLeft = 2,
	kSpriteMapTop = 4,
	kSpriteMapWidth = 6,
	kSpriteMapHeight,
	kSpriteMapHotLeft,
	kSpriteMapHotTop,
	kSpriteMapSize = SpriteInfo::kSpriteMapRecordSize
};

namespace {

class SpriteMapRecord {
public:
	SpriteMapRecord(Common::Span<const byte> record) : _record(record) {
		assert(_record.size() >= kSpriteMapSize);
	}

	uint16 image() const { return _record.getUint16LEAt(kSpriteMapImage); }
	uint16 left() const { return _record.getUint16LEAt(kSpriteMapLeft); }
	uint16 top() const { return _record.getUint16LEAt(kSpriteMapTop); }
	uint8 width() const { return _record.getUint8At(kSpriteMapWidth); }
	uint8 height() const { return _record.getUint8At(kSpriteMapHeight); }
	int8 hotLeft() const { return _record.getInt8At(kSpriteMapHotLeft); }
	int8 hotTop() const { return _record.getInt8At(kSpriteMapHotTop); }

private:
	Common::Span<const byte> _record;
};

} // namespace

SpriteInfo::SpriteInfo(Common::Span<const byte> record) {
	const SpriteMapRecord spritemap(record);
	top = spritemap.top();
	left = spritemap.left();
	width = spritemap.width();
	height = spritemap.height();
	image = spritemap.image();
	hotLeft = spritemap.hotLeft();
	hotTop = spritemap.hotTop();
}

} // End of namespace Interspective
