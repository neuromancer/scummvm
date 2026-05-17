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
	HitTarget() : type(0), id(0), z(0x7fff), y(0), exitPtr(0) {}
	uint16 type; // DOS g_game_state: 0 none, 1 exit, 2 object, 3 actor
	uint16 id;
	int16 z;
	int16 y;
	Exit *exitPtr;
};

static HitTarget findBestHitTargetLikeDos(Logic &logic, Common::Point world);

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

static bool containsDosInclusive(const Common::Rect &rect, const Common::Point &p) {
	return p.x >= rect.left && p.x <= rect.right && p.y >= rect.top && p.y <= rect.bottom;
}

static bool containsDosHalfOpen(int16 left, int16 top, int16 right, int16 bottom, const Common::Point &p) {
	return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
}

static int16 hitRegionAtPointLikeDos(const Common::Point &pos) {
	// iuc_main.dat hit-region table (footer +0x5a):
	//   0 = room, 1 = inventory strip, 2 = map button, 3..8 = verb buttons.
	if (containsDosHalfOpen(0, 0, 320, 152, pos))
		return 0;
	if (containsDosHalfOpen(128, 160, 310, 191, pos))
		return 1;
	if (containsDosHalfOpen(64, 190, 118, 200, pos))
		return 2;
	return -1;
}

static void considerLowerZTarget(HitTarget &best, uint16 type, uint16 id, int16 z, Exit *exit = 0) {
	if (best.type == 0 || z < best.z) {
		best.type = type;
		best.id = id;
		best.z = z;
		best.y = 0;
		best.exitPtr = exit;
		debugC(2, kDebugLevelEvents, "click hit candidate type=%u id=%u z=%d", type, id, z);
	}
}

static HitTarget hitDrawCommandAtPointLikeDos(Logic &logic, Common::Point world) {
	HitTarget best;
	Resources *resources = logic.resources();
	const Common::Array<Logic::DrawCommand> &commands = logic.drawCommands();

	for (uint i = 0; i < commands.size(); ++i) {
		const Logic::DrawCommand &cmd = commands[i];
		if (cmd.type == 1) {
			Exit *exit = logic.blockProgram() ? logic.blockProgram()->getExit(cmd.id) : 0;
			if (!exit || exit->room() != logic.currentRoom() || !logic.cellBit(cmd.id, 0))
				continue;
			if (spriteContainsWorldPoint(resources, exit->spriteField(), exit->position(), world))
				considerLowerZTarget(best, 1, cmd.id, cmd.layer, exit);
		} else if (cmd.type == 2) {
			if (logic.getObjectRoom(cmd.id) != logic.currentRoom() || !logic.cellBit(cmd.id, 0))
				continue;
			const uint16 spriteId = recordWord(logic, 2, cmd.id, 6);
			const Common::Point pos(logic.getObjectPosX(cmd.id), logic.getObjectPosY(cmd.id));
			if (spriteContainsWorldPoint(resources, spriteId, pos, world))
				considerLowerZTarget(best, 2, cmd.id, cmd.layer);
		}
	}

	return best;
}

static HitTarget hitDrawCommandWithFallbackLikeDos(Logic &logic, Common::Point world) {
	HitTarget target = hitDrawCommandAtPointLikeDos(logic, world);
	if (target.type != 0)
		return target;
	target = hitDrawCommandAtPointLikeDos(logic, Common::Point(world.x, world.y + 1));
	if (target.type != 0)
		return target;
	return hitDrawCommandAtPointLikeDos(logic, Common::Point(world.x + 1, world.y + 1));
}

static HitTarget hitNoSpriteExitAtPointLikeDos(Logic &logic, Common::Point world) {
	HitTarget best;
	if (logic.blockProgram()) {
		Common::List<Exit *> exits = logic.blockProgram()->exitsForRoom(logic.currentRoom());
		foreach (Exit *, exits) {
			Exit *exit = *it;
			if (!exit || exit->hasSprite() || !logic.cellBit(exit->id(), 0))
				continue;
			if (containsDosInclusive(exit->area(), world))
				considerLowerZTarget(best, 1, exit->id(), int16(int8(exit->zIndex())), exit);
		}
	}
	return best;
}

static bool spriteContainsTopLeftPoint(Resources *resources, uint16 spriteId,
		Common::Point topLeft, Common::Point screen) {
	if (!resources || spriteId == 0xffff)
		return false;

	SpriteInfo info = resources->getSpriteInfo(spriteId);
	if (info.width == 0 || info.height == 0)
		return false;

	Common::Rect rect(info.width, info.height);
	rect.moveTo(topLeft);
	if (!rect.contains(screen))
		return false;

	Common::ScopedPtr<Sprite> sprite(resources->loadSprite(spriteId));
	const int16 sx = int16(screen.x - rect.left);
	const int16 sy = int16(screen.y - rect.top);
	if (sx < 0 || sy < 0 || sx >= sprite->w || sy >= sprite->h)
		return false;

	const byte *pixel = reinterpret_cast<const byte *>(sprite->getBasePtr(sx, sy));
	return pixel && *pixel != 0;
}

static HitTarget hitInventoryObjectAtPointLikeDos(Logic &logic, Common::Point screen) {
	HitTarget target;
	Resources *resources = logic.resources();
	if (!resources)
		return target;
	const Common::Array<uint16> &objectExits = logic.objectExitList();

	for (int i = int(objectExits.size()) - 1; i >= 0; --i) {
		const uint16 id = objectExits[i];
		if (id == 0 || logic.getObjectRoom(id) != 0xffff)
			continue;

		const uint16 spriteId = recordWord(logic, 2, id, 8);
		if (spriteId == 0xffff)
			continue;
		const SpriteInfo info = resources->getSpriteInfo(spriteId);
		if (info.width == 0 || info.height == 0)
			continue;

		const Common::Point topLeft(
			int16(0x80 + logic.getObjectPosX(id) - int16(info.hotLeft)),
			int16(0xa0 + logic.getObjectPosY(id) - int16(info.hotTop)));
		if (!spriteContainsTopLeftPoint(resources, spriteId, topLeft, screen))
			continue;

		target.type = 2;
		target.id = id;
		target.z = int16(i);
		debugC(2, kDebugLevelEvents, "inventory hit candidate object id=%u slot=%d", id, i);
		return target;
	}

	return target;
}

static HitTarget findHitTargetForRegionLikeDos(Logic &logic, int16 hitRegion, Common::Point screen) {
	if (hitRegion == 1)
		return hitInventoryObjectAtPointLikeDos(logic, screen);
	if (hitRegion == 0) {
		Common::Point world(screen.x + logic.cameraX(), screen.y + logic.cameraY());
		return findBestHitTargetLikeDos(logic, world);
	}
	return HitTarget();
}

static HitTarget hitActorAtPointLikeDos(Logic &logic, Common::Point world) {
	HitTarget best;
	Resources *resources = logic.resources();
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
		if (!spriteContainsWorldPoint(resources, spriteId, actor->position(), world))
			continue;

		const int16 z = int16(actor->zIndex());
		const int16 y = int16(actor->position().y);
		if (best.type == 0 || z < best.z || (z == best.z && y > best.y)) {
			best.type = 3;
			best.id = id;
			best.z = z;
			best.y = y;
			best.exitPtr = 0;
			debugC(2, kDebugLevelEvents, "click hit candidate type=3 id=%u z=%d y=%d", id, z, y);
		}
	}
	return best;
}

static HitTarget findBestHitTargetLikeDos(Logic &logic, Common::Point world) {
	HitTarget best = hitDrawCommandWithFallbackLikeDos(logic, world);
	const HitTarget noSpriteExit = hitNoSpriteExitAtPointLikeDos(logic, world);
	if (best.type != 0) {
		if (noSpriteExit.type != 0 && best.z >= noSpriteExit.z)
			best = noSpriteExit;
	} else if (noSpriteExit.type != 0) {
		best = noSpriteExit;
	}

	const HitTarget actor = hitActorAtPointLikeDos(logic, world);
	if (actor.type != 0) {
		if (best.type == 0 || best.z > actor.z || (best.z == actor.z && best.type != 2))
			best = actor;
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
		debugC(1, kDebugLevelEvents,
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

	debugC(1, kDebugLevelEvents,
			"entity type %u id %u runs script 0x%04x in mode 0x%02x [DOS RunEntityScript]",
			target.type, target.id, scriptOffset, uint(mode));
	logic.resetRoomScriptSlotLikeDos(mode);
	CodePointer(scriptOffset, interpreter).run(mode);
	return true;
}

static void handleSecondaryClickLikeDos(Logic &logic, int16 hitRegion, Common::Point screen) {
	if (logic.cursorMode() != 1)
		return;

	const HitTarget target = findHitTargetForRegionLikeDos(logic, hitRegion, screen);
	if (target.type != 2)
		return;

	if (hitRegion != 0) {
		logic.beginDragAfterRemoveExitLikeDos(target.id, true);
		debugC(1, kDebugLevelEvents,
			"inventory object %u begins drag [DOS HandleSecondaryClick/BeginDrag_AfterRemoveExit]",
			target.id);
		return;
	}

	if (logic.breakInner())
		return;

	Actor *protag = logic.protagonist();
	const bool waitForWalk = logic.sendActorToCurrentEntity(protag) && protag && protag->isMoving();
	if (waitForWalk) {
		logic.setPostMoveCallback(Logic::PostMoveCallback::kBeginDragAfterMove,
			logic.currentEntityId(), target.id, 0);
		debugC(1, kDebugLevelEvents,
			"room object %u pickup armed after walk [DOS HandleSecondaryClick]",
			target.id);
	} else {
		logic.beginDragAfterRemoveExitLikeDos(target.id, false);
		debugC(1, kDebugLevelEvents,
			"room object %u pickup begins immediately [DOS HandleSecondaryClick]",
			target.id);
	}
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

	const int16 hitRegion = hitRegionAtPointLikeDos(pos);
	if (hitRegion < 0 || hitRegion == 2)
		return;

	Common::Point world(pos.x + logic.cameraX(), pos.y + logic.cameraY());
	HitTarget target = findHitTargetForRegionLikeDos(logic, hitRegion, pos);
	const uint16 dispatchCursorMode = logic.cursorMode();
	const uint16 dragTargetBeforeDispatch = logic.dragTarget();
	debugC(1, kDebugLevelEvents,
			"click pos=(%d,%d) world=(%d,%d) region=%d room=%u camera=(%d,%d) cursor=0x%02x step=%d noStep=%d hit=%u draw=%u -> target type=%u id=%u z=%d",
			pos.x, pos.y, world.x, world.y, hitRegion, logic.currentRoom(),
			logic.cameraX(), logic.cameraY(), dispatchCursorMode,
			logic.stepPending() ? 1 : 0, logic.noStep() ? 1 : 0,
			logic.hitTarget(), logic.drawCommandCount(),
			target.type, target.id, target.z);
	if (dispatchCursorMode == 0)
		return;
	if (logic.noStep())
		return;
	if (dispatchCursorMode != 0x80 || hitRegion != 1 || target.type != 2)
		Graphics::instance().clearInventoryCloseUpObjectLikeDos();

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

	if (logic.roomChangePending() || logic.currentRoom() != currentRoom)
		return;

	if (dispatchCursorMode == 0x20) {
		if (hitRegion == 1 && dragTargetBeforeDispatch != 0
				&& logic.dragTarget() == dragTargetBeforeDispatch
				&& logic.placeObjectInInventoryAtDosPoint(dragTargetBeforeDispatch, pos)) {
			logic.setPaused();
			debugC(1, kDebugLevelEvents,
				"drag object %u returned to inventory at (%d,%d) [DOS HandleHotspotInteraction]",
				dragTargetBeforeDispatch, pos.x, pos.y);
		}
		return;
	}

	if (dispatchCursorMode == 0x80 && hitRegion == 1 && target.type == 2) {
		Graphics::instance().setInventoryCloseUpObjectLikeDos(target.id);
		debugC(1, kDebugLevelEvents,
			"inventory object %u close-up armed [DOS HandleInventoryClick]",
			target.id);
		return;
	}

	if (dispatchCursorMode == 1) {
		handleSecondaryClickLikeDos(logic, hitRegion, pos);
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
