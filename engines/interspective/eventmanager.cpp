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

#include "interspective/eventmanager.h"

#include "interspective/actor.h"
#include "interspective/debug.h"
#include "interspective/graphics.h"
#include "interspective/logic.h"
#include "interspective/util.h"

using namespace std;

namespace Common {
	DECLARE_SINGLETON(Interspective::EventManager);
}

namespace Interspective {
//

Clickable::Clickable() {
	EventManager::instance().push(this);
}

Clickable::~Clickable() {
	EventManager::instance().pop(this);
}

void EventManager::clicked(Common::Point pos) {
	Logic &logic = Logic::instance();
	if (!logic.roomActive() || logic.canSkipCutscene())
		return;

	pos.x = int16(pos.x + logic.cameraX());
	pos.y = int16(pos.y + logic.cameraY());

	Clickable *handler = 0;

	foreach(Clickable *, _handlers)
		if ((*it)->isClickable() && (*it)->area().contains(pos))
			if (!handler || handler->zIndex() > (*it)->zIndex())
				handler = *it;

	if (handler) {
		logic.setStepPending(true);
		const uint16 currentRoom = logic.currentRoom();
		if (!handler->clicked())
			return;
		Actor *protag = logic.protagonist();
		const bool brokeInner = logic.roomChangePending()
			|| logic.currentRoom() != currentRoom
			|| (protag && protag->isMoving());

		if (!brokeInner && logic.cursorMode() != 4) {
			const Logic::PostMoveCallback savedCallback = logic.postMoveCallback();
			logic.sendActorToCurrentEntity(protag);
			logic.setPostMoveCallback(savedCallback);
		}
	}
}

void EventManager::push(Clickable *c) {
	_handlers.push_back(c);
}

void EventManager::pop(Clickable *c) {
	_handlers.remove(c);
}

void EventManager::paint(Graphics *g) const {
	debugC(3, kDebugLevelEvents | kDebugLevelGraphics, "EventManager got paint event");
	if (!_debug)
		return;
	debugC(3, kDebugLevelEvents | kDebugLevelGraphics, "EventManager paints clickable areas");

	foreach_const(Clickable *, _handlers) {
		if (!(*it)->isClickable())
			continue;
		Common::Rect area = (*it)->area();
		area.translate(-Logic::instance().cameraX(), -Logic::instance().cameraY());
		g->paintRect(area);
	}
}

void EventManager::toggleDebug() {
	_debug = !_debug;
	debugC(3, kDebugLevelEvents, "EventManager toggled debug mode to %s", _debug ? "on" : "off");

	if (_debug)
		Graphics::instance().push(this);
	else
		Graphics::instance().pop(this);
}

}
