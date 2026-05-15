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

#include "interspective/room.h"

#include "interspective/logic.h"
#include "interspective/program.h"

namespace Interspective {
//

Room::Room(Logic *l) : _logic(l) {
	_exits = l->blockProgram()->exitsForRoom(l->roomNumber());
	snprintf(_debugInfo, 50, "room %d in %s", l->roomNumber(), +*l->blockProgram());
}

void Room::addActorFrame(Common::Point pos, Common::Array<byte> nexts) {
	_logic->actorFramesAdd(pos, nexts);
}

void Room::clearActorFrames() {
	_logic->actorFramesClearCount();
}

Actor::Frame Room::getFrame(uint16 index) const {
	return _logic->actorFrame(index);
}

void Room::invalidateFrame(uint16 index) {
	_logic->actorFrameInvalidate(index);
}

void Room::setFramePosition(uint16 index, int16 x, int16 y) {
	_logic->actorFrameSetPosition(index, x, y);
}

uint16 Room::frameCount() const {
	return _logic->actorFrameCount();
}

uint16 Room::nearestFrameTo(int16 x, int16 y) const {
	uint16 best = 0;
	int32 bestDistSq = 0x7fffffff;
	for (uint16 i = 0; i < _logic->actorFrameCount(); ++i) {
		Common::Point p = _logic->actorFrame(uint16(i + 1)).position();
		if (p.x == 999)
			continue;  // sentinel for invalidated
		int32 dx = int32(p.x) - int32(x);
		int32 dy = int32(p.y) - int32(y);
		int32 d2 = dx * dx + dy * dy;
		if (d2 < bestDistSq) {
			bestDistSq = d2;
			best = uint16(i + 1);
		}
	}
	return best;
}

}
