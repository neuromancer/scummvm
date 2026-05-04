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

#include "common/util.h"

#include "interspective/logic.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/program.h"
#include "interspective/animation.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

namespace Common {
	DECLARE_SINGLETON(Interspective::Logic);
}

namespace Interspective {

Logic::~Logic() {
	// Animations are owned by the Interpreter that registered them (via rememberAnimation);
	// they are deleted in Interpreter::~Interpreter, which runs as the SharedPtr<Interpreter>
	// members destruct after this body returns. Just clear the index to avoid stale pointers.
	_animations.clear();
}

void Logic::setEngine(Engine *e) {
	_engine = e;
	_resources = e->resources();
	_currentRoom = 0xffff;
	_currentBlock = 0xffff;
	_nextRoom = 0;
}


void Logic::init() {
	_toplevelInterpreter = Common::SharedPtr<Interpreter>(new Interpreter(this, _resources->mainBase(), "main code"));
}

void Logic::initCode() {
	debugC(2, kDebugLevelScript | kDebugLevelFlow, ">>>running initial code");
	_toplevelInterpreter->run(_resources->mainEntryPoint(), kCodeInitial);
	debugC(2, kDebugLevelScript | kDebugLevelFlow, "<<<finished initial code");
}

void Logic::tick() {
	++_frameCounter;

	if (_nextRoom)
		doChangeRoom();

	if (_roomLoop.get()) {
//		gDebugLevel--; // room loops aren't that interesting
		debugC(3, kDebugLevelScript | kDebugLevelFlow, ">>>running room loop code");
		_roomLoop->run(kCodeRoomLoop);
		debugC(3, kDebugLevelScript | kDebugLevelFlow, "<<<finished room loop code");
//		gDebugLevel++;
	}

	runQueued();
}

void Logic::callAnimations() {
	if (!_animations.empty())
		debugC(4, kDebugLevelFlow | kDebugLevelAnimation, "running animations");
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end(); ++it) {
		Animation::Status ret = (*it)->tick();
		if (ret == Animation::kRemove) {
			// it will be deleted by its owner block
			Common::List<Animation *>::iterator _i = it;
			it++;
			_animations.erase(_i);
		}
	}
}

void Logic::setProtagonist(uint16 actor) {
	_protagonist = getActor(actor);
}

Actor *Logic::protagonist() const {
	return _protagonist;
}

void Logic::changeRoom(uint16 newRoom) {
	// just schedule it, we'll execute on next tick
	_nextRoom = newRoom;

	if (_currentRoom == 0xffff)
		doChangeRoom(); // except if it's the first one
}

void Logic::doChangeRoom() {
	assert (_nextRoom);

	// Emit at warning level so the intro's room sequence is visible
	// without enabling debug channels — useful when picking a value for
	// --boot-param=N to skip the intro to a specific scene.
	warning("Interspective: changeRoom %u → %u", (uint)_currentRoom, (uint)_nextRoom);
	if (_nextRoom == _currentRoom)
		return;
	_currentRoom = _nextRoom;
	_nextRoom = 0;
	_roomLoop.reset();
	uint16 newBlock = _resources->blockOfRoom(_currentRoom);

	if (newBlock != _currentBlock) {
		// Drop any deferred code or skip points still pointing into the outgoing block — its
		// Interpreter (and Program::_code) is about to be destroyed and any queued CodePointer
		// to it would be a use-after-free when runQueued() fires it.
		Interpreter *oldBlock = _blockInterpreter.get();
		if (oldBlock) {
			Common::List<DelayedRun>::iterator it = _queued.begin();
			while (it != _queued.end()) {
				if (it->code.interpreter() == oldBlock)
					it = _queued.erase(it);
				else
					++it;
			}
			if (_skipPoint.interpreter() == oldBlock)
				_skipPoint.reset();
		}

		_currentBlock = newBlock;
		_blockProgram = Common::SharedPtr<Program>(_resources->loadCodeBlock(newBlock));

		char buf[100];
		snprintf(buf, 100, "block %d code", newBlock);

		_blockInterpreter = Common::SharedPtr<Interpreter>(new Interpreter(this, _blockProgram->base(), buf));
		_blockProgram->loadActors(_blockInterpreter.get());
		_blockProgram->loadExits(_blockInterpreter.get());

		debugC(2, kDebugLevelScript, ">>>running block entry code for block %d", newBlock);
		_blockInterpreter->run(_blockProgram->begin(), kCodeNewBlock);
		debugC(2, kDebugLevelScript, "<<<finished block entry code for block %d", newBlock);
	}

	_room = Common::SharedPtr<Room>(new Room(this));
	debugC(2, kDebugLevelScript, ">>>running room entry code for room %d", _currentRoom);
	_blockInterpreter->run(_blockProgram->roomHandler(_currentRoom), kCodeNewRoom);
	debugC(2, kDebugLevelScript, "<<<finished room entry code for room %d", _currentRoom);

	// Re-run setFrame on each actor so its position re-syncs to the new
	// room's frame table (if the actor's current frame index is defined
	// there). Same as the original behavior — disrupting the actor's
	// running script via setRoom turned out to be too aggressive (it
	// reset the script PC to the puppeteer's main-code start, which
	// for actors already in the middle of a sequence would land the PC
	// on data bytes mid-instruction). The "invisible character + stale
	// position" issue is best fixed by loading the DOS places table and
	// doing FindPlaceById on transitions; that's a follow-up iteration.
	foreach(Animation *, _animations)
		if ((*it)->isActor()) {
			Actor * const ac = static_cast<Actor *>(*it);
			ac->setFrame(ac->frameId());
		}
}

void Logic::runLater(const CodePointer &p, uint16 delay) {
	debugC(3, kDebugLevelScript, "will call %s after %d ticks", +p, delay);
	_queued.push_back(DelayedRun(p, delay));
}

bool Logic::cancelLater(const CodePointer &p) {
	bool removed = false;
	Common::List<DelayedRun>::iterator it = _queued.begin();
	while (it != _queued.end()) {
		if (it->code.offset() == p.offset() && it->code.interpreter() == p.interpreter()) {
			debugC(3, kDebugLevelScript, "cancel pending %s", +p);
			it = _queued.erase(it);
			removed = true;
		} else {
			++it;
		}
	}
	return removed;
}

void Logic::runQueued() {
	if (_queued.empty()) return;

	Interpreter * const liveTopLevel = _toplevelInterpreter.get();
	Interpreter * const liveBlock = _blockInterpreter.get();

	Common::Queue<Common::List<DelayedRun>::iterator> toRemove;
	debugC(2, kDebugLevelFlow | kDebugLevelScript, ">>>running queued code");
	foreach (DelayedRun, _queued)
		if (it->delay) {
			debugC(3, kDebugLevelScript, "delayed %s, delay now %d", +it->code,
					it->delay);
			it->delay--;
		} else {
			Interpreter *target = it->code.interpreter();
			if (target != liveTopLevel && target != liveBlock) {
				warning("dropping stale queued CodePointer (interpreter %p not live)",
						(void *)target);
				toRemove.push(it);
				continue;
			}
			debugC(2, kDebugLevelFlow | kDebugLevelScript, ">>>running %s", +it->code);
			it->code.run();
			debugC(2, kDebugLevelFlow | kDebugLevelScript, "<<<finished %s", +it->code);
			toRemove.push(it);
		}
	debugC(2, kDebugLevelFlow | kDebugLevelScript, "<<<finished queued code");

	while (!toRemove.empty())
		_queued.erase(toRemove.pop());
}

void Logic::addAnimation(Animation *anim) {
	_animations.push_back(anim);
}

void Logic::removeAnimation(Animation *anim) {
	_animations.remove(anim);
}

void Logic::setRoomLoop(const CodePointer &code) {
	_roomLoop = Common::SharedPtr<CodePointer>(new CodePointer(code));
}

/* counting starts with 1 */
Actor *Logic::getActor(uint16 id) const {
	id--;
	if (id < _resources->mainDat()->actorsCount())
		return _resources->mainDat()->actor(id);
	else {
		id -= _resources->mainDat()->actorsCount();
		return _blockProgram->actor(id);
	}
}

void Logic::setSkipPoint(const CodePointer &p) {
	_skipPoint = p;
}

void Logic::skipCutscene() {
	if (_skipPoint.isEmpty()) return;

	debugC(2, kDebugLevelScript, ">>>running animation skip code");
	_skipPoint.run();
	debugC(2, kDebugLevelScript, "<<<finished animation skip code");
	_skipPoint.reset();
}

Animation *Logic::animation(uint16 offset) const {
	foreach_const (Animation *, _animations)
		if ((*it)->baseOffset() == offset)
			return (*it);

	return 0;
}


} // End of namespace Interspective
