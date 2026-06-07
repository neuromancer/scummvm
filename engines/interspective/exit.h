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

#ifndef INTERSPECTIVE_EXIT_H
#define INTERSPECTIVE_EXIT_H

#include "common/ptr.h"
#include "common/rect.h"

#include "interspective/debug.h"
#include "interspective/eventmanager.h"
#include "interspective/value.h"

namespace Interspective {
//
class Graphics;
class Sprite;

class Exit : public StaticInspectable, public Clickable {
	DEBUG_INFO
public:
	enum { Size = 0xe };

	uint16 id() const { return _id; }
	uint16 room() const { return _room; }
	void setRoom(uint16 r) { _room = r; }
	const Common::Point &position() const { return _position; }
	void setPosition(Common::Point p) {
		_rect.translate(p.x - _position.x, p.y - _position.y);
		_position = p;
	}
	void setZIndex(byte z) { _zIndex = z; }

	void paint(Graphics *g);

	virtual bool clicked();

	virtual Common::Rect area() const;
	virtual byte zIndex() const;
	virtual bool isClickable() const;

	bool isEnabled() const { return _enabled; }
	void setEnabled(bool en) { _enabled = en; }
	bool hasSprite() const { return !_noSprite; }
	bool noSpriteLikeDos() const { return _noSprite; }
	void setNoSpriteLikeDos(bool noSprite);
	uint16 spriteField() const { return _spriteField; }
	void setSpriteField(uint16 sprite);

	friend class Program;

private:
	Exit(const CodePointer &code, uint16 id);
	virtual ~Exit() {}

	Sprite *sprite() const { return _sprite.get(); }

	byte _zIndex;
	Common::SharedPtr<Sprite> _sprite;
	uint16 _spriteField;
	Common::Point _position;
	Common::Rect _rect;
	uint16 _id;
	uint16 _room;
	CodePointer _clickHandler;
	bool _enabled;
	bool _noSprite;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_EXIT_H
