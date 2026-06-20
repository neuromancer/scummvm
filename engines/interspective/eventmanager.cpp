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

#include "interspective/eventmanager.h"

#include "interspective/actor.h"
#include "interspective/debug.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/inter.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/main_dat.h"
#include "interspective/program.h"
#include "interspective/resources.h"
#include "interspective/sprite.h"
#include "interspective/util.h"
#include "interspective/value.h"

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

static HitTarget findBestHitTarget(Logic &logic, Common::Point world);

static uint16 recordWord(const Logic &logic, uint8 selector, uint16 id, uint8 off) {
	return logic.recordField(selector, id, off, 2);
}

static bool spriteContainsWorldPoint(Resources *resources, uint16 spriteId,
									 Common::Point pos, Common::Point world) {
	if (!resources || spriteId == 0xffff)
		return false;

	SpriteInfo info = resources->getSpriteInfo(spriteId);
	if (info.empty())
		return false;

	Common::Rect rect = info.bottomAnchoredRect(pos);
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

static bool containsDosInclusive(int16 left, int16 top, int16 right, int16 bottom, const Common::Point &p) {
	return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
}

static bool containsDosInclusive(const Common::Rect &rect, const Common::Point &p) {
	return containsDosInclusive(rect.left, rect.top, rect.right, rect.bottom, p);
}

// DOS FindActorAtCursor @ 1000:bbc1 reads actor fields +0x5d/+0x5f as
// the hit-rectangle segment/offset. A segment of 0xffff means "derive the
// rectangle from the current main sprite"; zero means "not hittable".
static Interpreter *actorHitRectInterpreter(Logic &logic, uint16 segment) {
	if (segment == 0 || segment == 0xffff)
		return 0;
	if (segment == 0x1cb5)
		return logic.mainInterpreter();

	const uint16 blockSegment = uint16(0x4000 + (logic.currentBlock() & 0x3fff));
	if (segment == 1 || segment == blockSegment)
		return logic.blockInterpreter();

	return 0;
}

static bool actorCustomHitRectContains(Logic &logic, Actor *actor, Common::Point world) {
	Interpreter *interpreter = actorHitRectInterpreter(logic, actor->actorCallbackSeg());
	if (!interpreter)
		return false;

	uint16 off = actor->actorCallbackOff();
	for (uint i = 0; i < 64; ++i, off = uint16(off + 8)) {
		const byte *rect = interpreter->rawCodeChecked(off, 8);
		if (!rect)
			return false;

		const int16 left = int16(READ_LE_UINT16(rect));
		if (left == -1)
			return false;

		const int16 top = int16(READ_LE_UINT16(rect + 2));
		const int16 right = int16(READ_LE_UINT16(rect + 4));
		const int16 bottom = int16(READ_LE_UINT16(rect + 6));
		if (containsDosInclusive(left, top, right, bottom, world))
			return true;
	}

	return false;
}

static bool actorMainSpriteHitRectContains(Resources *resources, Actor *actor, Common::Point world) {
	if (!resources || actor->mainSpriteId() == 0xffff)
		return false;

	const SpriteInfo info = resources->getSpriteInfo(actor->mainSpriteId());
	if (info.empty())
		return false;

	const Common::Point hotPoint = info.hotPoint();
	int16 left = int16(actor->position().x - hotPoint.x);
	if (left < 0)
		left = 0;
	const int16 bottom = int16(actor->position().y + hotPoint.y);
	int16 top = int16(bottom - actor->visibleSpriteHeight());
	if (top < 0)
		top = 0;
	const int16 right = int16(left + actor->visibleSpriteWidth());

	return containsDosInclusive(left, top, right, bottom, world);
}

static Common::Point actorMoveSlotPosition(const Actor::MoveSlot &slot, Common::Point base) {
	if (slot.mode == 0)
		return Common::Point(int16(slot.b), int16(slot.c));
	return Common::Point(int16(base.x + int16(slot.b)), int16(base.y + int16(slot.c)));
}

static bool actorQueuedSlotHitRectContains(Resources *resources, const Graphics *graphics,
										   Actor *actor, Common::Point world) {
	if (!resources || !graphics || !actor)
		return false;
	if (actor->mainSpriteId() == 0xffff)
		return false;

	const SpriteInfo mainInfo = resources->getSpriteInfo(actor->mainSpriteId());
	const bool placeholderMainSprite = mainInfo.empty() ||
									   ((mainInfo.width <= 1 && mainInfo.height <= 1) ||
										(actor->visibleSpriteWidth() <= 1 && actor->visibleSpriteHeight() <= 1));
	if (!placeholderMainSprite)
		return false;

	const uint16 drawMode = actor->drawModeForLayer(actor->zIndex());
	const Common::Array<Actor::MoveSlot> &slots = actor->moveSlots();
	for (uint i = 0; i < slots.size(); ++i) {
		const Actor::MoveSlot &slot = slots[i];
		if (slot.a == 0xffff)
			continue;

		Common::ScopedPtr<Sprite> sprite(resources->loadSprite(slot.a));
		if (!sprite)
			continue;

		const Common::Rect rect = graphics->layerScaledSpriteRect(sprite.get(),
																  actorMoveSlotPosition(slot, actor->position()),
																  drawMode);
		if (containsDosInclusive(rect, world))
			return true;
	}
	return false;
}

static bool actorContainsWorldPoint(Logic &logic, Resources *resources, Actor *actor, Common::Point world) {
	const uint16 segment = actor->actorCallbackSeg();
	if (segment == 0)
		return false;
	if (segment == 0xffff) {
		if (actorMainSpriteHitRectContains(resources, actor, world))
			return true;
		return actorQueuedSlotHitRectContains(resources, logic.engine() ? logic.engine()->graphics() : 0,
											  actor, world);
	}
	return actorCustomHitRectContains(logic, actor, world);
}

static bool containsDosHalfOpen(int16 left, int16 top, int16 right, int16 bottom, const Common::Point &p) {
	return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
}

static int16 hitRegionAtPoint(const Common::Point &pos) {
	// iuc_main.dat hit-region table (footer +0x5a):
	//   0 = room, 1 = inventory strip, 2 = status button, 3..8 = verb buttons.
	if (containsDosHalfOpen(0, 0, 320, 152, pos))
		return 0;
	if (containsDosHalfOpen(65, 153, 79, 169, pos))
		return 3;
	if (containsDosHalfOpen(83, 153, 99, 169, pos))
		return 4;
	if (containsDosHalfOpen(101, 153, 117, 169, pos))
		return 5;
	if (containsDosHalfOpen(65, 172, 81, 188, pos))
		return 6;
	if (containsDosHalfOpen(83, 172, 99, 188, pos))
		return 7;
	if (containsDosHalfOpen(101, 172, 117, 188, pos))
		return 8;
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

static HitTarget hitDrawCommandAtPoint(Logic &logic, Common::Point world) {
	HitTarget best;
	Resources *resources = logic.resources();
	const Common::Array<Logic::DrawCommand> &commands = logic.drawCommands();

	for (uint i = 0; i < commands.size(); ++i) {
		const Logic::DrawCommand &cmd = commands[i];
		if (cmd.type == 1) {
			Exit *exit = logic.blockProgram() ? logic.blockProgram()->getExit(cmd.id) : 0;
			if (!exit || logic.recordField(1, cmd.id, 0, 2) != logic.currentRoom() || !logic.cellBit(cmd.id, 0))
				continue;
			const uint16 spriteId = recordWord(logic, 1, cmd.id, 6);
			const Common::Point pos(
				int16(recordWord(logic, 1, cmd.id, 2)),
				int16(recordWord(logic, 1, cmd.id, 4)));
			if (spriteContainsWorldPoint(resources, spriteId, pos, world))
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

static HitTarget hitDrawCommandWithFallback(Logic &logic, Common::Point world) {
	HitTarget target = hitDrawCommandAtPoint(logic, world);
	if (target.type != 0)
		return target;
	target = hitDrawCommandAtPoint(logic, Common::Point(world.x, world.y + 1));
	if (target.type != 0)
		return target;
	return hitDrawCommandAtPoint(logic, Common::Point(world.x + 1, world.y + 1));
}

static HitTarget hitNoSpriteExitAtPoint(Logic &logic, Common::Point world) {
	HitTarget best;
	best.z = 0x0c;
	Program *program = logic.blockProgram();
	if (!program)
		return best;

	// FindObjectAtCursor @ 1000:bd0f scans the side list built by
	// CollectObjectsForRoom from back to front. Equal z values therefore
	// keep the later visible entry, not the lowest exit id.
	const Common::Array<uint16> &visible = logic.visibleNoSpriteExits();
	for (int i = int(visible.size()) - 1; i >= 0; --i) {
		const uint16 id = visible[i];
		Exit *exit = program->getExit(id);
		if (!exit)
			continue;

		const int16 left = int16(recordWord(logic, 1, id, 2));
		const int16 top = int16(recordWord(logic, 1, id, 4));
		const int16 right = int16(left + int16(logic.recordField(1, id, 6, 1)));
		const int16 bottom = int16(top + int16(logic.recordField(1, id, 7, 1)));
		const Common::Rect area(left, top, right, bottom);
		const int16 z = int16(int8(logic.recordField(1, id, 0x0b, 1)));
		if (containsDosInclusive(area, world) && z < best.z) {
			best.type = 1;
			best.id = id;
			best.z = z;
			best.y = 0;
			best.exitPtr = exit;
			debugC(2, kDebugLevelEvents, "click hit candidate type=1 id=%u z=%d", id, z);
		}
	}
	return best;
}

static bool spriteContainsTopLeftPoint(Resources *resources, uint16 spriteId,
									   Common::Point topLeft, Common::Point screen) {
	if (!resources || spriteId == 0xffff)
		return false;

	SpriteInfo info = resources->getSpriteInfo(spriteId);
	if (info.empty())
		return false;

	Common::Rect rect = info.topLeftRect(topLeft);
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

static HitTarget hitInventoryObjectAtPoint(Logic &logic, Common::Point screen) {
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
		if (info.empty())
			continue;

		const Common::Point hotPoint = info.hotPoint();
		const Common::Point topLeft(
			int16(0x80 + logic.getObjectPosX(id) - hotPoint.x),
			int16(0xa0 + logic.getObjectPosY(id) - hotPoint.y));
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

static HitTarget findHitTargetForRegion(Logic &logic, int16 hitRegion, Common::Point screen) {
	if (hitRegion == 1)
		return hitInventoryObjectAtPoint(logic, screen);
	if (hitRegion == 0) {
		Common::Point world(screen.x + logic.cameraX(), screen.y + logic.cameraY());
		return findBestHitTarget(logic, world);
	}
	return HitTarget();
}

static HitTarget hitActorAtPoint(Logic &logic, Common::Point world) {
	HitTarget best;
	Resources *resources = logic.resources();
	uint16 actorCount = logic.resources()->mainDat()->actorsCount();
	if (logic.blockProgram())
		actorCount += logic.blockProgram()->actorsCount();
	for (uint16 id = 1; id <= actorCount; ++id) {
		if (!logic.activeActor(id))
			continue;
		Actor *actor = logic.getActor(id);
		if (!actor || actor->room() != logic.currentRoom())
			continue;
		if (!actorContainsWorldPoint(logic, resources, actor, world))
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

static HitTarget findBestHitTarget(Logic &logic, Common::Point world) {
	HitTarget best = hitDrawCommandWithFallback(logic, world);
	const HitTarget noSpriteExit = hitNoSpriteExitAtPoint(logic, world);
	if (best.type != 0) {
		if (noSpriteExit.type != 0 && best.z >= noSpriteExit.z)
			best = noSpriteExit;
	} else if (noSpriteExit.type != 0) {
		best = noSpriteExit;
	}

	const HitTarget actor = hitActorAtPoint(logic, world);
	if (actor.type != 0) {
		if (best.type == 0 || best.z > actor.z || (best.z == actor.z && best.type != 2))
			best = actor;
	}

	return best;
}

static bool runEntityScript(Logic &logic, const HitTarget &target, OpcodeMode mode) {
	if (target.type == 0)
		return true;

	logic.setGameState(target.type);
	logic.setCurrentEntityId(target.id);

	if (target.type == 1) {
		if (target.exitPtr) {
			debugC(1, kDebugLevelEvents,
				   "entity target type 1 id %u runs exit click handler in mode 0x%02x",
				   target.id, uint(mode));
			logic.resetRoomScriptSlot(mode);
			return target.exitPtr->clicked();
		}

		uint16 scriptOffset = 0;
		if (!logic.blockProgram() || !logic.blockProgram()->getExitRecordField(target.id, 8, 2, scriptOffset)) {
			logic.setPendingError(0x1b);
			return false;
		}
		if (!logic.cellBit(target.id, 0)) {
			debugC(3, kDebugLevelEvents,
				   "raw exit %u click ignored: current-room cell bit 0 clear",
				   target.id);
			return false;
		}
		debugC(1, kDebugLevelEvents,
			   "entity target type 1 id %u runs raw exit script 0x%04x in mode 0x%02x [DOS DispatchEntityClick]",
			   target.id, scriptOffset, uint(mode));
		logic.resetRoomScriptSlot(mode);
		CodePointer(scriptOffset, logic.blockInterpreter()).run(mode);
		return true;
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
	logic.resetRoomScriptSlot(mode);
	CodePointer(scriptOffset, interpreter).run(mode);
	return true;
}

static bool resolveHitTargetScript(Logic &logic, const HitTarget &target, Interpreter *&interpreter, uint16 &scriptOffset) {
	interpreter = 0;
	scriptOffset = 0;
	if (target.type == 0)
		return false;

	if (target.type == 1) {
		if (!logic.cellBit(target.id, 0))
			return false;
		if (target.exitPtr) {
			const CodePointer &handler = target.exitPtr->clickHandler();
			if (handler.isEmpty())
				return false;
			interpreter = handler.interpreter();
			scriptOffset = handler.offset();
			return interpreter != 0;
		}

		if (!logic.blockProgram() || !logic.blockProgram()->getExitRecordField(target.id, 8, 2, scriptOffset))
			return false;
		interpreter = logic.blockInterpreter();
		return interpreter != 0;
	}

	if (target.type == 2) {
		if (!logic.resources() || !logic.resources()->mainDat() ||
			target.id == 0 || target.id > logic.resources()->mainDat()->personsCount())
			return false;
		interpreter = logic.mainInterpreter();
		scriptOffset = recordWord(logic, 2, target.id, 0x0a);
		return interpreter != 0;
	}

	if (target.type == 3) {
		if (!logic.resources() || !logic.resources()->mainDat())
			return false;
		uint16 actorCount = logic.resources()->mainDat()->actorsCount();
		if (logic.blockProgram())
			actorCount += logic.blockProgram()->actorsCount();
		if (target.id == 0 || target.id > actorCount)
			return false;

		interpreter = target.id <= logic.resources()->mainDat()->actorsCount()
						  ? logic.mainInterpreter()
						  : logic.blockInterpreter();
		scriptOffset = recordWord(logic, 3, target.id, 0x5b);
		return interpreter != 0;
	}

	return false;
}

static bool handleMinimapExitClick(Logic &logic, Common::Point screen) {
	if (logic.cursorMode() == 0x80 || logic.dialogClickGate() == 0 || logic.inStatusMode())
		return false;

	const Common::Array<Logic::AnimListEntry> &entries = logic.animList();
	for (uint i = 0; i < entries.size(); ++i) {
		const Logic::AnimListEntry &entry = entries[i];
		if (!containsDosHalfOpen(int16(entry.x0), int16(entry.y0),
								 int16(entry.x1), int16(entry.y1), screen))
			continue;

		logic.setStepPending(true);
		logic.setGameState(1);
		logic.setCurrentEntityId(entry.arg3);
		debugC(1, kDebugLevelEvents,
			   "minimap exit click pos=(%d,%d) entry=%u exit=%u marker=%u cursor=0x%02x [DOS HandleClick]",
			   screen.x, screen.y, i, entry.arg3, entry.arg2, logic.cursorMode());

		if (!logic.cellBit(entry.arg3, 0))
			return true;

		const uint16 currentRoom = logic.currentRoom();
		HitTarget target;
		target.type = 1;
		target.id = entry.arg3;
		target.z = int16(i);
		target.exitPtr = logic.blockProgram() ? logic.blockProgram()->getExit(entry.arg3) : 0;
		if (!runEntityScript(logic, target, kCodeItem))
			return true;

		if (logic.roomChangePending() || logic.currentRoom() != currentRoom || logic.breakInner())
			return true;

		if (logic.cursorMode() != 0x04) {
			Actor *protag = logic.protagonist();
			const Logic::PostMoveCallback savedCallback = logic.postMoveCallback();
			logic.sendActorToCurrentEntity(protag);
			logic.setPostMoveCallback(savedCallback);
			logic.resetRoomScriptSlot(kCodeItem);
		}
		return true;
	}
	return false;
}

static void handleSecondaryClick(Logic &logic, int16 hitRegion, Common::Point screen) {
	if (logic.cursorMode() != 1)
		return;

	const HitTarget target = findHitTargetForRegion(logic, hitRegion, screen);
	if (target.type != 2)
		return;

	if (hitRegion != 0) {
		logic.beginDragAfterRemoveExit(target.id, true);
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
		logic.beginDragAfterRemoveExit(target.id, false);
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
	// DOS HandleClick is gated by g_room_active, but not by g_flag_no_step or
	// g_esc_during_script. The subway ride arms an ESC break point while normal
	// room interaction remains live.
	if (!logic.roomActive())
		return;

	logic.lockCursorAndButtons(pos, 1);

	if (handleMinimapExitClick(logic, pos))
		return;

	const int16 hitRegion = hitRegionAtPoint(pos);
	if (hitRegion < 0) {
		logic.setHitTarget(0xffff);
		return;
	}
	logic.setHitTarget(uint16(hitRegion));

	Common::Point world(pos.x + logic.cameraX(), pos.y + logic.cameraY());
	HitTarget target = findHitTargetForRegion(logic, hitRegion, pos);
	const uint16 dispatchCursorMode = logic.cursorMode();
	const uint16 dragTargetBeforeDispatch = logic.dragTarget();
	debugC(1, kDebugLevelEvents,
		   "click pos=(%d,%d) world=(%d,%d) region=%d room=%u camera=(%d,%d) cursor=0x%02x step=%d noStep=%d hit=%u draw=%u -> target type=%u id=%u z=%d",
		   pos.x, pos.y, world.x, world.y, hitRegion, logic.currentRoom(),
		   logic.cameraX(), logic.cameraY(), dispatchCursorMode,
		   logic.stepPending() ? 1 : 0, logic.noStep() ? 1 : 0,
		   logic.hitTarget(), logic.drawCommandCount(),
		   target.type, target.id, target.z);
	if (hitRegion >= 3 && hitRegion <= 8) {
		logic.setStepPending(true);
		logic.setVerbModeFromHitRegion(hitRegion);
		return;
	}
	if (hitRegion == 2) {
		logic.setStepPending(true);
		if (logic.inStatusMode()) {
			debugC(1, kDebugLevelEvents,
				   "status button restores previous room [DOS RunStatusScreenLoop]");
			logic.restoreRoomFromBackup();
		} else {
			debugC(1, kDebugLevelEvents,
				   "status button enters room 999 [DOS RunStatusScreenLoop]");
			logic.enterStatusScreenLoop();
		}
		return;
	}
	if (dispatchCursorMode == 0)
		return;
	if (dispatchCursorMode != 0x80 || hitRegion != 1 || target.type != 2)
		Graphics::instance().clearInventoryCloseUpObject();

	const uint16 currentRoom = logic.currentRoom();
	logic.setStepPending(true);
	if (dispatchCursorMode == 0x10)
		logic.clearPostMoveCallback();

	if (target.type == 0) {
		logic.setGameState(0);
		logic.setCurrentEntityId(0);
	} else if (!runEntityScript(logic, target, kCodeItem)) {
		return;
	}

	if (logic.roomChangePending() || logic.currentRoom() != currentRoom)
		return;

	if (logic.inStatusMode() && dispatchCursorMode != 1 && dispatchCursorMode != 0x20)
		return;

	if (dispatchCursorMode == 0x20) {
		if (hitRegion == 1 && dragTargetBeforeDispatch != 0 && logic.dragTarget() == dragTargetBeforeDispatch && logic.placeObjectInInventoryAtDosPoint(dragTargetBeforeDispatch, pos)) {
			logic.setPaused();
			debugC(1, kDebugLevelEvents,
				   "drag object %u returned to inventory at (%d,%d) [DOS HandleHotspotInteraction]",
				   dragTargetBeforeDispatch, pos.x, pos.y);
		}
		return;
	}

	if (dispatchCursorMode == 0x80 && hitRegion == 1 && target.type == 2) {
		Graphics::instance().setInventoryCloseUpObject(target.id);
		debugC(1, kDebugLevelEvents,
			   "inventory object %u close-up armed [DOS HandleInventoryClick]",
			   target.id);
		return;
	}

	if (dispatchCursorMode == 0x80 && hitRegion == 0) {
		Graphics::instance().setRoomCloseUp(pos);
		debugC(1, kDebugLevelEvents,
			   "room eye close-up armed at (%d,%d) [DOS DrawCursorWithBackdrop]",
			   pos.x, pos.y);
		return;
	}

	if (dispatchCursorMode == 1) {
		handleSecondaryClick(logic, hitRegion, pos);
		return;
	}

	if (dispatchCursorMode != 0x10)
		return;

	Actor *protag = logic.protagonist();
	const bool brokeInner = logic.roomChangePending() || logic.currentRoom() != currentRoom || logic.breakInner();

	if (!brokeInner) {
		if (logic.gameState() == 1)
			logic.setGameState(0);
		const Logic::PostMoveCallback savedCallback = logic.postMoveCallback();
		logic.sendActorToCurrentEntity(protag);
		logic.setPostMoveCallback(savedCallback);
		logic.resetRoomScriptSlot(kCodeItem);
	}
}

Common::String EventManager::hoverObjectName(Common::Point pos) const {
	Logic &logic = Logic::instance();
	if (!logic.roomActive() || logic.noStep() || !logic.inputEnabled())
		return Common::String();
	if (logic.cursorMode() == 0 || logic.cursorMode() == 0x80)
		return Common::String();
	if (logic.cursorMode() == 0x10)
		return Common::String();

	const int16 hitRegion = hitRegionAtPoint(pos);
	if (hitRegion != 0 && hitRegion != 1)
		return Common::String();

	const HitTarget target = findHitTargetForRegion(logic, hitRegion, pos);
	if (target.type == 0)
		return Common::String();

	Interpreter *interpreter = 0;
	uint16 scriptOffset = 0;
	if (!resolveHitTargetScript(logic, target, interpreter, scriptOffset))
		return Common::String();

	Common::String text;
	if (!interpreter->extractFirstStatusOverlayLine(scriptOffset, text))
		return Common::String();

	debugC(4, kDebugLevelEvents, "hover label target type=%u id=%u script=0x%04x text='%s'",
		   target.type, target.id, scriptOffset, text.c_str());
	return text;
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

} // End of namespace Interspective
