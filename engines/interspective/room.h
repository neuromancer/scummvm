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

#ifndef INTERSPECTIVE_ROOM_H
#define INTERSPECTIVE_ROOM_H

#include "common/array.h"
#include "common/list.h"
#include "common/rect.h"

#include "interspective/actor.h"
#include "interspective/debug.h"

namespace Interspective {
//

class Exit;
class Logic;

class Room : public StaticInspectable {
//
public:
	class Rect {
	public:
		Rect() : _zindex(999) {}
		Rect(int16 z, Common::Rect r) : _zindex(z), _rect(r) {}

	private:
		int16 _zindex;
		Common::Rect _rect;
	};

	const Common::List<Exit *> &exits() const { return _exits; }

	void addActorFrame(Common::Point p, Common::Array<byte> nexts);
	Actor::Frame getFrame(uint16 index) const {
		if (index >= _actorFrames.size() || index == 0)
			return Actor::Frame();
		else
			return _actorFrames[index-1];
	}

	// Runtime frame-table mutation (DOS Op_e0/Op_e1). Indices are 1-based
	// to match getFrame()'s convention. Out-of-range writes are dropped
	// silently to match DOS bound-check (it sets g_pendingErrorCode = 0x30
	// for arg0 >= 0xfd; we just no-op).
	void invalidateFrame(uint16 index) {
		if (index > 0 && index <= _actorFrames.size())
			_actorFrames[index - 1].invalidate();
	}
	void setFramePosition(uint16 index, int16 x, int16 y) {
		if (index > 0 && index <= _actorFrames.size())
			_actorFrames[index - 1].setPosition(Common::Point(x, y));
	}

	void addRect(const Room::Rect &f) { _rects.push_back(f); }

	friend class Logic;

private:
	Room(Logic *l);

	// just in case, we'll explicitly add those if needed
	Room();
	Room(const Room &);
	Room &operator=(const Room &);

	Common::List<Exit *> _exits;
	Logic *_logic;

	Common::Array<Actor::Frame> _actorFrames;
	Common::List<Rect> _rects;

	DEBUG_INFO
};

}

#endif
