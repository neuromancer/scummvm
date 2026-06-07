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

#include "interspective/exit.h"

#include "interspective/debugger.h"
#include "interspective/graphics.h"
#include "interspective/logic.h"
#include "interspective/resources.h"

namespace Interspective {
//

enum Offsets {
	kOffsetRoom = 0,
	kOffsetPosition = 2,
	kOffsetSprite = 6,
	kOffsetWidth = 6,
	kOffsetHeight = 7,
	kOffsetClickHandler = 8,
	kOffsetNoSprite = 0xa,
	kOffsetZIndex = 0xb
};

Exit::Exit(const CodePointer &c, uint16 id)
	: _sprite(), _spriteField(0xffff), _id(id), _enabled(false), _noSprite(false) {
	debugC(4, kDebugLevelFiles, "loading exit from %s", +c);

	c.field(_noSprite, kOffsetNoSprite);
	if (!_noSprite) {
		c.field(_spriteField, kOffsetSprite);
		c.field(_sprite, kOffsetSprite);
		_rect = Common::Rect(_sprite->w, _sprite->h);
	} else {
		byte w, h;
		c.field(w, kOffsetWidth);
		c.field(h, kOffsetHeight);
		_spriteField = uint16(w) | (uint16(h) << 8);
		_rect = Common::Rect(w, h);
		debugC(5, kDebugLevelFiles, "exit has no sprite");
	}

	c.field(_position, kOffsetPosition);
	_rect.moveTo(_position.x, _position.y);

	if (!_noSprite) // these have bottom for some reason
		_rect.translate(0, -(_rect.height() - 1));

	c.field(_room, kOffsetRoom);
	c.field(_zIndex, kOffsetZIndex);

	uint16 offset;
	c.field(offset, kOffsetClickHandler);
	_clickHandler = CodePointer(offset, c.interpreter());

	snprintf(_debugInfo, 100, "exit %u %s%s r%d z%d %s", _id, _noSprite ? "n" : "s", +_rect, _room, _zIndex, +c);
}

void Exit::setSpriteField(uint16 sprite) {
	_spriteField = sprite;
	if (_noSprite) {
		_rect = Common::Rect(uint8(sprite), uint8(sprite >> 8));
		_rect.moveTo(_position.x, _position.y);
	}
}

void Exit::setNoSpriteLikeDos(bool noSprite) {
	_noSprite = noSprite;
	if (_noSprite)
		setSpriteField(_spriteField);
}

void Exit::paint(Graphics *g) {
	if (sprite())
		g->paint(sprite(), _position, Graphics::kPaintCameraRelative);
}

byte Exit::zIndex() const {
	return _zIndex;
}

bool Exit::isClickable() const {
	return _room == Logic::instance().currentRoom();
}

Common::Rect Exit::area() const {
	return _rect;
}

bool Exit::clicked() {
	debugC(3, kDebugLevelEvents, "%s got clicked!", +*this);
	Logic &logic = Logic::instance();
	logic.setGameState(1);
	logic.setCurrentEntityId(_id);

	if (!logic.cellBit(_id, 0)) {
		debugC(3, kDebugLevelEvents, "%s click ignored: current-room cell bit 0 clear", +*this);
		return false;
	}

	Debug.clickHandler();
	_clickHandler.run(kCodeItem);
	return true;
}

} // End of namespace Interspective
