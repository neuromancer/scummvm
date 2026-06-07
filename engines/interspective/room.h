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

#include "interspective/actor.h"
#include "interspective/debug.h"

namespace Interspective {
//

class Exit;
class Logic;

class Room : public StaticInspectable {
	//
public:
	const Common::List<Exit *> &exits() const { return _exits; }

	void addActorFrame(Common::Point p, Common::Array<byte> nexts);
	void clearActorFrames();
	Actor::Frame getFrame(uint16 index) const;

	// Runtime frame-table mutation (DOS Op_e0/Op_e1). These opcodes index
	// the same frame ids used by SetActorPosition; arg0 == 0 writes DOS's
	// unused frame-0 backing slot.
	void invalidateFrame(uint16 index);
	void setFramePosition(uint16 index, int16 x, int16 y);

	uint16 frameCount() const;

	// Return the 1-based frame index whose position is closest to (x, y),
	// or 0 if no valid frame exists. Used by Op_29/0x2a/0x56 to compute
	// a walk goal for object/exit targets — DOS does this via
	// FindNearestExitToPoint @ 1000:72a2.
	uint16 nearestFrameTo(int16 x, int16 y) const;

	friend class Logic;

private:
	Room(Logic *l);

	// just in case, we'll explicitly add those if needed
	Room();
	Room(const Room &);
	Room &operator=(const Room &);

	Common::List<Exit *> _exits;
	Logic *_logic;

	DEBUG_INFO
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_ROOM_H
