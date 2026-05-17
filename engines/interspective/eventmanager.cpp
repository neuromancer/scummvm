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
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/logic.h"
#include "interspective/main_dat.h"
#include "interspective/program.h"
#include "interspective/resources.h"
#include "interspective/sprite.h"
#include "interspective/util.h"
#include "interspective/value.h"

using namespace std;

namespace Common {
	DECLARE_SINGLETON(Interspective::EventManager);
}

namespace Interspective {
//

namespace {

struct HitTarget {
	HitTarget() : type(0), id(0), z(0x7fff), exitPtr(0) {}
	uint16 type; // DOS g_game_state: 0 none, 1 exit, 2 object, 3 actor
	uint16 id;
	int16 z;
	Exit *exitPtr;
};

static uint16 recordWord(const Logic &logic, uint8 selector, uint16 id, uint8 off) {
	return logic.dosRecordField(selector, id, off, 2);
}

static bool spriteContainsWorldPoint(Resources *resources, uint16 spriteId,
		Common::Point pos, Common::Point world) {
	if (!resources || spriteId == 0xffff)
		return false;

	SpriteInfo info = resources->getSpriteInfo(spriteId);
	if (info.width == 0 || info.height == 0)
		return false;

	Common::Rect rect(info.width, info.height);
	rect.moveTo(pos);
	rect.translate(0, -int16(info.height));
	rect.translate(-int16(info.hotLeft), int16(info.hotTop));
	if (!rect.contains(world))
		return false;

	Common::ScopedPtr<Sprite> sprite(resources->loadSprite(spriteId));
	const int16 sx = int16(world.x - rect.left);
	const int16 sy = int16(world.y - rect.top);
	if (sx < 0 || sy < 0 || sx >= sprite->w || sy >= sprite->h)
		return false;

	const byte *pixel = reinterpret_cast<const byte *>(sprite->getBasePtr(sx, sy));
	return pixel && *pixel != 0;
}

static void considerTarget(HitTarget &best, uint16 type, uint16 id, int16 z, Exit *exit = 0) {
	if (best.type == 0 || z < best.z) {
		best.type = type;
		best.id = id;
		best.z = z;
		best.exitPtr = exit;
		debugC(2, kDebugLevelEvents, "click hit candidate type=%u id=%u z=%d", type, id, z);
	}
}

static HitTarget findBestHitTargetLikeDos(Logic &logic, Common::Point world) {
	HitTarget best;
	Resources *resources = logic.resources();

	const Common::Array<Logic::DrawCommand> &commands = logic.drawCommands();
	for (uint i = 0; i < commands.size(); ++i) {
		const Logic::DrawCommand &cmd = commands[i];
		if (cmd.type == 1) {
			Exit *exit = logic.blockProgram() ? logic.blockProgram()->getExit(cmd.id) : 0;
			if (!exit || exit->room() != logic.currentRoom() || !logic.cellBit(cmd.id, 0))
				continue;
			if (exit->area().contains(world))
				considerTarget(best, 1, cmd.id, cmd.layer, exit);
		} else if (cmd.type == 2) {
			if (logic.getObjectRoom(cmd.id) != logic.currentRoom() || !logic.cellBit(cmd.id, 0))
				continue;
			const uint16 spriteId = recordWord(logic, 2, cmd.id, 6);
			const Common::Point pos(logic.getObjectPosX(cmd.id), logic.getObjectPosY(cmd.id));
			if (spriteContainsWorldPoint(resources, spriteId, pos, world))
				considerTarget(best, 2, cmd.id, cmd.layer);
		}
	}

	if (logic.blockProgram()) {
		Common::List<Exit *> exits = logic.blockProgram()->exitsForRoom(logic.currentRoom());
		foreach (Exit *, exits) {
			Exit *exit = *it;
			if (!exit || exit->hasSprite() || !logic.cellBit(exit->id(), 0))
				continue;
			if (exit->area().contains(world))
				considerTarget(best, 1, exit->id(), int16(exit->zIndex()), exit);
		}
	}

	uint16 actorCount = logic.resources()->mainDat()->actorsCount();
	if (logic.blockProgram())
		actorCount += logic.blockProgram()->actorsCount();
	for (uint16 id = 1; id <= actorCount; ++id) {
		Actor *actor = logic.getActor(id);
		if (!actor || actor->room() != logic.currentRoom())
			continue;
		const uint16 spriteId = actor->mainSpriteId();
		if (spriteId == 0xffff)
			continue;
		if (spriteContainsWorldPoint(resources, spriteId, actor->position(), world))
			considerTarget(best, 3, id, int16(actor->zIndex()));
	}

	return best;
}

static bool runEntityScriptLikeDos(Logic &logic, const HitTarget &target, OpcodeMode mode) {
	if (target.type == 0)
		return true;

	logic.setGameState(target.type);
	logic.setCurrentEntityId(target.id);

	if (target.type == 1) {
		if (!target.exitPtr)
			return false;
		debugC(1, kDebugLevelEvents | kDebugLevelScript,
				"entity target type 1 id %u runs exit click handler in mode 0x%02x",
				target.id, uint(mode));
		logic.resetRoomScriptSlotLikeDos(mode);
		return target.exitPtr->clicked();
	}

	Interpreter *interpreter = 0;
	uint16 scriptOffset = 0;
	if (target.type == 2) {
		if (target.id == 0 || target.id > logic.resources()->mainDat()->personsCount()) {
			logic.setPendingError(0x1b);
			return false;
		}
		interpreter = logic.mainInterpreter();
		scriptOffset = recordWord(logic, 2, target.id, 0x0a);
	} else if (target.type == 3) {
		uint16 actorCount = logic.resources()->mainDat()->actorsCount();
		if (logic.blockProgram())
			actorCount += logic.blockProgram()->actorsCount();
		if (target.id == 0 || target.id > actorCount) {
			logic.setPendingError(0x1b);
			return false;
		}
		interpreter = target.id <= logic.resources()->mainDat()->actorsCount()
			? logic.mainInterpreter()
			: logic.blockInterpreter();
		scriptOffset = recordWord(logic, 3, target.id, 0x5b);
	}

	if (!interpreter)
		return false;

	debugC(1, kDebugLevelEvents | kDebugLevelScript,
			"entity type %u id %u runs script 0x%04x in mode 0x%02x [DOS RunEntityScript]",
			target.type, target.id, scriptOffset, uint(mode));
	logic.resetRoomScriptSlotLikeDos(mode);
	CodePointer(scriptOffset, interpreter).run(mode);
	return true;
}

} // End of anonymous namespace

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

	Common::Point world(pos.x + logic.cameraX(), pos.y + logic.cameraY());
	HitTarget target = findBestHitTargetLikeDos(logic, world);
	const uint16 dispatchCursorMode = logic.cursorMode();
	debugC(1, kDebugLevelEvents,
			"click pos=(%d,%d) world=(%d,%d) room=%u camera=(%d,%d) cursor=0x%02x step=%d noStep=%d hit=%u draw=%u -> target type=%u id=%u z=%d",
			pos.x, pos.y, world.x, world.y, logic.currentRoom(),
			logic.cameraX(), logic.cameraY(), dispatchCursorMode,
			logic.stepPending() ? 1 : 0, logic.noStep() ? 1 : 0,
			logic.hitTarget(), logic.drawCommandCount(),
			target.type, target.id, target.z);
	if (dispatchCursorMode == 0)
		return;

	const uint16 currentRoom = logic.currentRoom();
	logic.setStepPending(true);
	if (dispatchCursorMode == 0x10)
		logic.clearPostMoveCallback();

	if (target.type == 0) {
		logic.setGameState(0);
		logic.setCurrentEntityId(0);
	} else if (!runEntityScriptLikeDos(logic, target, kCodeItem)) {
		return;
	}

	if (dispatchCursorMode != 0x10)
		return;

	Actor *protag = logic.protagonist();
	const bool brokeInner = logic.roomChangePending()
		|| logic.currentRoom() != currentRoom
		|| logic.breakInner();

	if (!brokeInner) {
		if (logic.gameState() == 1)
			logic.setGameState(0);
		const Logic::PostMoveCallback savedCallback = logic.postMoveCallback();
		logic.sendActorToCurrentEntity(protag);
		logic.setPostMoveCallback(savedCallback);
		logic.resetRoomScriptSlotLikeDos(kCodeItem);
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
