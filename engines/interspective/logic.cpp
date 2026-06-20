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

#include "common/endian.h"
#include "common/memstream.h"
#include "common/serializer.h"
#include "common/util.h"

#include "interspective/animation.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/logic.h"
#include "interspective/musicparser.h"
#include "interspective/program.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/sound.h"
#include "interspective/util.h"

namespace Common {
DECLARE_SINGLETON(Interspective::Logic);
}

namespace Interspective {

static SpriteInfo objectSpriteInfo(Resources *resources, Program *blockProgram, uint16 sprite) {
	if (!resources || !resources->mainDat())
		return SpriteInfo();
	if (sprite >= resources->mainDat()->spriteCount() && !blockProgram)
		return SpriteInfo();
	return resources->getSpriteInfo(sprite);
}

// Default bubble formatter line-height. DOS stores the live value in
// DAT_1000_885e; Op_fd can rewrite it before FormatBubbleText_Inner
// computes text height.
static const uint16 kBubbleLineHeight = 12; // matches Graphics::kLineHeight

static int16 stepCameraToward(int16 current, int16 target, int16 speed) {
	const int16 delta = target - current;
	if (delta == 0)
		return current;
	if (delta < 0) {
		const int16 step = MIN<int16>(-delta, speed);
		return current - step;
	}
	const int16 step = MIN<int16>(delta, speed);
	return current + step;
}

static inline uint8 recordWordByte(uint16 value, uint8 baseOff, uint8 off) {
	return uint8((value >> ((off - baseOff) * 8)) & 0xff);
}

static inline uint16 recordWordWithByte(uint16 oldValue, uint8 baseOff, uint8 off, uint8 value) {
	const uint shift = uint(off - baseOff) * 8;
	return uint16((oldValue & ~(0xffu << shift)) | (uint16(value) << shift));
}

static uint16 motionTextStreamLength(const byte *text) {
	if (!text)
		return 0;

	const uint16 kMaxMotionTextBytes = 4096;
	const byte *p = text;
	uint16 length = 0;

	while (length < kMaxMotionTextBytes) {
		const byte ch = *p++;
		++length;
		if (ch == 0)
			return length;

		uint16 extra = 0;
		switch (ch) {
		case 14:
		case kStringMove:
			extra = 4;
			break;
		case kStringSetColour:
		case kStringAdvance:
			extra = 1;
			break;
		case kStringGlobalWord:
		case kStringCountSpacesIf0:
		case kStringCountSpacesIf1:
			extra = 2;
			break;
		case kStringMenuOption:
			while (length < kMaxMotionTextBytes) {
				const byte optionCh = *p++;
				++length;
				if (optionCh == 0)
					break;
			}
			extra = 2;
			break;
		default:
			break;
		}

		while (extra != 0 && length < kMaxMotionTextBytes) {
			++p;
			++length;
			--extra;
		}
	}

	return length;
}

static int16 cameraMaxOrigin(uint16 backdropSize, uint16 viewportSize) {
	return backdropSize > viewportSize ? int16(backdropSize - viewportSize) : 0;
}

static void setReadyCallbackOffsetDirect(Actor *actor, uint16 callback) {
	if (actor)
		actor->setReadyCallbackOffset(callback);
}

static void moveActorToTargetFrame(Logic *logic, Actor *actor, uint16 frame) {
	if (!logic || !actor)
		return;
	setReadyCallbackOffsetDirect(actor, 0);
	if (actor == logic->protagonist()) {
		logic->setBreakInner(true);
		logic->clearPostMoveCallback();
		actor->stopSpeaking();
		logic->setPostMoveTargetFrameMirror(uint8(frame));
		if (actor->room() == logic->currentRoom() && actor->frameId() != 0)
			actor->setRawTargetFrame(uint8(frame));
		actor->moveTo(frame);
		if (actor->movementWaitActive())
			logic->setPostMoveTargetFrameMirror(uint8(actor->frameId()));
		return;
	}
	if (actor->room() != logic->currentRoom()) {
		actor->setFrame(frame);
	} else {
		if (actor->frameId() != 0)
			actor->setRawTargetFrame(uint8(frame));
		actor->moveTo(frame);
	}
}

static void syncHashMapUint16Uint16(Common::Serializer &s, Common::HashMap<uint16, uint16> &map) {
	uint16 count = map.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		map.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint16 key = 0;
			uint16 value = 0;
			s.syncAsUint16LE(key);
			s.syncAsUint16LE(value);
			map[key] = value;
		}
	} else {
		for (Common::HashMap<uint16, uint16>::const_iterator it = map.begin(); it != map.end(); ++it) {
			uint16 key = it->_key;
			uint16 value = it->_value;
			s.syncAsUint16LE(key);
			s.syncAsUint16LE(value);
		}
	}
}

static void syncHashMapUint16Int16(Common::Serializer &s, Common::HashMap<uint16, int16> &map) {
	uint16 count = map.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		map.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint16 key = 0;
			int16 value = 0;
			s.syncAsUint16LE(key);
			s.syncAsSint16LE(value);
			map[key] = value;
		}
	} else {
		for (Common::HashMap<uint16, int16>::const_iterator it = map.begin(); it != map.end(); ++it) {
			uint16 key = it->_key;
			int16 value = it->_value;
			s.syncAsUint16LE(key);
			s.syncAsSint16LE(value);
		}
	}
}

static void syncHashMapUint32Uint8(Common::Serializer &s, Common::HashMap<uint32, uint8> &map) {
	uint16 count = map.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		map.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint32 key = 0;
			uint8 value = 0;
			s.syncAsUint32LE(key);
			s.syncAsByte(value);
			map[key] = value;
		}
	} else {
		for (Common::HashMap<uint32, uint8>::const_iterator it = map.begin(); it != map.end(); ++it) {
			uint32 key = it->_key;
			uint8 value = it->_value;
			s.syncAsUint32LE(key);
			s.syncAsByte(value);
		}
	}
}

static void syncHashMapUint16Uint8(Common::Serializer &s, Common::HashMap<uint16, uint8> &map) {
	uint16 count = map.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		map.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint16 key = 0;
			uint8 value = 0;
			s.syncAsUint16LE(key);
			s.syncAsByte(value);
			if (value != 0)
				map[key] = value;
		}
	} else {
		for (Common::HashMap<uint16, uint8>::const_iterator it = map.begin(); it != map.end(); ++it) {
			uint16 key = it->_key;
			uint8 value = it->_value;
			s.syncAsUint16LE(key);
			s.syncAsByte(value);
		}
	}
}

static void syncHashMapUint16Bool(Common::Serializer &s, Common::HashMap<uint16, bool> &map) {
	uint16 count = map.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		map.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint16 key = 0;
			uint8 value = 0;
			s.syncAsUint16LE(key);
			s.syncAsByte(value);
			map[key] = value != 0;
		}
	} else {
		for (Common::HashMap<uint16, bool>::const_iterator it = map.begin(); it != map.end(); ++it) {
			uint16 key = it->_key;
			uint8 value = it->_value ? 1 : 0;
			s.syncAsUint16LE(key);
			s.syncAsByte(value);
		}
	}
}

static void syncUint16Array(Common::Serializer &s, Common::Array<uint16> &array) {
	uint16 count = array.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		array.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint16 value = 0;
			s.syncAsUint16LE(value);
			array.push_back(value);
		}
	} else {
		for (uint16 i = 0; i < count; ++i)
			s.syncAsUint16LE(array[i]);
	}
}

static void captureActorState(Actor *actor, Common::Array<byte> &state) {
	state.clear();
	if (!actor)
		return;

	Common::MemoryWriteStreamDynamic stream(DisposeAfterUse::NO);
	Common::Serializer serializer(nullptr, &stream);
	actor->synchronize(serializer);
	const uint32 size = uint32(stream.size());
	if (size == 0)
		return;

	state.resize(size);
	memcpy(&state[0], stream.getData(), size);
}

static void restoreActorState(Actor *actor, const Common::Array<byte> &state) {
	if (!actor || state.empty())
		return;

	Common::MemoryReadStream stream(&state[0], state.size(), DisposeAfterUse::NO);
	Common::Serializer serializer(&stream, nullptr);
	actor->synchronize(serializer);
}

static Actor::Frame makeActorFrame(uint16 index, Common::Point pos, const Common::Array<byte> &nexts) {
	Common::Array<byte> storedNexts = nexts;
	storedNexts.resize(8);
	return Actor::Frame(pos, storedNexts, index);
}

static Actor::Frame makeEmptyActorFrame(uint16 index) {
	Common::Array<byte> nexts;
	nexts.resize(8);
	for (uint i = 0; i < nexts.size(); ++i)
		nexts[i] = 0;
	return Actor::Frame(Common::Point(999, 999), nexts, index);
}

static void syncActorFrameRecord(Common::Serializer &s, Actor::Frame &frame, uint16 index) {
	Common::Point pos = frame.position();
	int16 x = pos.x;
	int16 y = pos.y;
	Common::Array<byte> nexts = frame.nexts();
	nexts.resize(8);
	s.syncAsSint16LE(x);
	s.syncAsSint16LE(y);
	for (uint j = 0; j < nexts.size(); ++j)
		s.syncAsByte(nexts[j]);
	if (s.isLoading())
		frame = makeActorFrame(index, Common::Point(x, y), nexts);
}

static void syncActorFrameArray(Common::Serializer &s, Common::Array<Actor::Frame> &frames) {
	uint16 count = frames.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		frames.clear();
		for (uint16 i = 0; i < count; ++i) {
			Actor::Frame frame;
			syncActorFrameRecord(s, frame, uint16(i + 1));
			frames.push_back(frame);
		}
	} else {
		for (uint16 i = 0; i < count; ++i)
			syncActorFrameRecord(s, frames[i], uint16(i + 1));
	}
}

Logic::~Logic() {
	// Animations are owned by the Interpreter that registered them (via rememberAnimation);
	// they are deleted in Interpreter::~Interpreter, which runs as the SharedPtr<Interpreter>
	// members destruct after this body returns. Just clear the index to avoid stale pointers.
	_animations.clear();
}

void Logic::setEngine(Engine *e) {
	_engine = e;
	_resources = e->resources();
	_protagonist = nullptr;
	_protagonistId = 0;
	_currentRoom = 0xffff;
	_currentBlock = 0xffff;
	_nextRoom = 0;
	_forceRoomRestart = false;
	_fullscreenGateActive = false;
	_enteringStatusScreen = false;
	_paused = false;
	_currentPlace = 0;
	_defaultCursorMode = 0x10;
	_cursorStepIndex = 0;
	_rightClickCycleCooldown = 4;
	setCursorMode(_defaultCursorMode);
	_cursorLockedPos = Common::Point(160, 100);
	_buttonsLocked = 0;
	_actorFrameTable.clear();
	_actorFrameCount = 0;
	_walkSpeedFlag = 0;
	_postMoveTargetFrameMirror = 0;
	_speechSkipInput = false;
	_loadedBackdropId = 0;
	_loadBlockOverrideId = 0xffff;
	_loadBlockOverrideData.clear();
	resetActiveActorTable();
	_roomBackup = RoomBackup();
	_statusSaveShadow = RoomBackup();
	_statusSaveOverrideActive = false;
	for (uint i = 0; i < ARRAYSIZE(_dirtyObjectPlacements); ++i)
		_dirtyObjectPlacements[i] = DirtyObjectPlacement();
}

bool Logic::speechWouldConsumeRightClick() const {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		const SpeechSlot &slot = _speechSlots[i];
		if (slot.framesLeft == 0 || slot.active == 0)
			continue;
		if (slot.framesTotal >= 2 && slot.framesLeft <= uint8(slot.framesTotal - 2))
			return true;
	}
	return false;
}

bool Logic::setVerbModeFromHitRegion(uint16 hitRegion) {
	if (_fullscreenGateActive || _cursorMode == 0x20)
		return false;

	uint16 cursorMode = 0;
	switch (hitRegion) {
	case 3:
		cursorMode = 0x01;
		break;
	case 4:
		cursorMode = 0x02;
		break;
	case 5:
		cursorMode = 0x10;
		break;
	case 6:
		cursorMode = 0x04;
		break;
	case 7:
		cursorMode = 0x80;
		break;
	case 8:
		cursorMode = 0x08;
		break;
	default:
		return false;
	}

	debugC(1, kDebugLevelEvents,
		   "verb hit region=%u -> cursor mode 0x%02x [DOS SetVerbModeFromHotkey]",
		   hitRegion, cursorMode);
	_hitTarget = hitRegion;
	setCursorMode(cursorMode);
	return true;
}

void Logic::activateStatusButtonHotkey() {
	// CheckVerbHotkey @ 1000:b9bc maps Space to hit-region 2, and
	// DispatchVerbAction @ 1000:b9a0 sends region 2 to RunStatusScreenLoop.
	// In the status loop, the same region exits through RestoreRoomFromBackup.
	_hitTarget = 2;
	if (_inStatusMode) {
		_stepPending = true;
		restoreRoomFromBackup();
	} else {
		enterStatusScreenLoop();
	}
}

void Logic::cycleCursorModeByRightClick() {
	// CheckDoubleClickReset @ 1000:b92c: when not no-step, not dragging,
	// and the locked button byte is 2, cycle through the verb cursor modes
	// and clear step-pending via SetCursorMode. RunStatusScreenLoop calls
	// the same helper, so status mode keeps the lower verb UI active.
	if (_noStep || _cursorMode == 0x20 || !_roomActive)
		return;
	if (_rightClickCycleCooldown != 0)
		return;
	if (speechWouldConsumeRightClick())
		return;

	uint16 nextMode = _defaultCursorMode;
	switch (_cursorMode) {
	case 0x10:
		nextMode = 0x80;
		break;
	case 0x80:
		nextMode = 0x04;
		break;
	case 0x04:
		nextMode = 0x01;
		break;
	case 0x01:
		nextMode = 0x02;
		break;
	case 0x02:
		nextMode = 0x08;
		break;
	case 0x08:
		nextMode = 0x10;
		break;
	default:
		nextMode = _defaultCursorMode;
		break;
	}
	debugC(2, kDebugLevelEvents, "right-click verb cycle: cursor mode 0x%02x -> 0x%02x",
		   _cursorMode, nextMode);
	setCursorMode(nextMode);
	_rightClickCycleCooldown = 4;
}

uint16 Logic::updateAutoCloseTimerSprite() {
	// UpdateAutoCloseTimer @ 1000:7a2b:
	//   if room_active && g_auto_close_timer != 0, choose CS:[0xc9] for
	//   positive values and the first negative tick, choose CS:[0xcb] for
	//   the persistent -2 sentinel, then draw at (0x40,0xbe).
	if (!_roomActive || _autoCloseTimer == 0 || !_resources || !_resources->mainDat())
		return 0xffff;

	bool statusModeSprite = false;
	if (_autoCloseTimer < 0) {
		statusModeSprite = true;
		if (_autoCloseTimer != -2) {
			--_autoCloseTimer;
			statusModeSprite = false;
		}
	} else {
		--_autoCloseTimer;
	}

	return _resources->mainDat()->getStatusButtonSpriteId(statusModeSprite);
}

void Logic::init() {
	_toplevelInterpreter = Common::SharedPtr<Interpreter>(
		new Interpreter(this, _resources->mainBase(), _resources->mainDat()->dataSize(), "main code"));
}

void Logic::initCode() {
	debugC(2, kDebugLevelScript | kDebugLevelFlow, ">>>running initial code");
	_toplevelInterpreter->run(_resources->mainEntryPoint(), kCodeInitial);
	debugC(2, kDebugLevelScript | kDebugLevelFlow, "<<<finished initial code");
}

void Logic::tick() {
	++_frameCounter;
	_servicedRunModesThisTick.clear();
	tickRightClickCycleCooldown();

	if (handleEscDuringScript())
		return;
	if (_nextRoom)
		doChangeRoom();
	if (handleEscDuringScript())
		return;

	runQueued();
	if (handleEscDuringScript())
		return;
}

void Logic::runRoomLoop() {
	if (_roomLoop.get()) {
		if (serviceRoomScriptSlot(kCodeRoomLoop)) {
			debugC(4, kDebugLevelScript | kDebugLevelFlow,
				   "room loop skipped while mode 0x%02x room-script slot is active",
				   kCodeRoomLoop);
			return;
		}
		//		gDebugLevel--; // room loops aren't that interesting
		debugC(3, kDebugLevelScript | kDebugLevelFlow, ">>>running room loop code");
		_roomLoop->run(kCodeRoomLoop);
		debugC(3, kDebugLevelScript | kDebugLevelFlow, "<<<finished room loop code");
		if (handleEscDuringScript())
			return;
		//		gDebugLevel++;
	}
}

void Logic::runGlobalRoomLoop() {
	if (!_resources || !_toplevelInterpreter)
		return;
	const uint16 loop = _resources->mainRoomLoopEntryPoint();
	if (loop == 0)
		return;
	if (serviceRoomScriptSlot(kCodeGlobalRoomLoop)) {
		debugC(4, kDebugLevelScript | kDebugLevelFlow,
			   "global room loop skipped while mode 0x%02x room-script slot is active",
			   kCodeGlobalRoomLoop);
		return;
	}
	debugC(3, kDebugLevelScript | kDebugLevelFlow, ">>>running global room loop code");
	_toplevelInterpreter->run(loop, kCodeGlobalRoomLoop);
	debugC(3, kDebugLevelScript | kDebugLevelFlow, "<<<finished global room loop code");
}

void Logic::runPostAnimationScripts() {
	// DOS MainGameLoop runs the post-move callback after actor movement
	// updates, then RunRoomLoopScript(mode 3) before RunStatusScript(mode 2).
	// RunStatusScreenLoop is a separate modal loop: it services status-mode room
	// scripts, but does not run the normal game post-move/global/room loops.
	if (_inStatusMode) {
		runStatusScreenScripts();
		if (handleEscDuringScript())
			return;
		tickMotionText();
		return;
	}
	runPostMoveCallbackIfReady();
	runGlobalRoomLoop();
	if (handleEscDuringScript())
		return;
	runRoomLoop();
	if (handleEscDuringScript())
		return;
	runItemRoomScriptSlot();
	if (handleEscDuringScript())
		return;
	tickMotionText();
	updateScrollPosition();
}

void Logic::callAnimations() {
	if (!_animations.empty())
		debugC(4, kDebugLevelFlow | kDebugLevelAnimation, "running animations");
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end();) {
		if ((*it)->isActor()) {
			Actor *const actor = static_cast<Actor *>(*it);
			if (_inStatusMode && actor->room() != _currentRoom) {
				++it;
				continue;
			}
			if (!activeActor(actorGlobalId(actor))) {
				// DOS room-script slots are checked before actor animation
				// updates and are independent of the active actor render table.
				// An ActorOp_02 unregister therefore must still be able to
				// release a pending Op_9a/0x99 wait on the next frame.
				actor->processWaitCallbacks();
				++it;
				continue;
			}
		}
		Animation::Status ret = (*it)->tick();
		if (ret == Animation::kRemove) {
			// it will be deleted by its owner block
			it = _animations.erase(it);
		} else {
			++it;
		}
	}
}

void Logic::clearRoomTransientAnimations() {
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end();) {
		if (!(*it)->isActor())
			it = _animations.erase(it);
		else
			++it;
	}
}

void Logic::resetActiveActorTable() {
	_activeActorIds.clear();
	_activeActorIds.resize(kActiveActorTableSlots);
	for (uint i = 0; i < _activeActorIds.size(); ++i)
		_activeActorIds[i] = 0;
}

bool Logic::registerActiveActor(uint16 id) {
	if (id == 0)
		return false;
	if (_activeActorIds.size() != kActiveActorTableSlots)
		resetActiveActorTable();
	for (uint i = 0; i < _activeActorIds.size(); ++i)
		if (_activeActorIds[i] == id)
			return true;
	for (uint i = 0; i < _activeActorIds.size(); ++i) {
		if (_activeActorIds[i] == 0) {
			_activeActorIds[i] = id;
			return true;
		}
	}
	setPendingError(0x2b);
	return false;
}

void Logic::unregisterActiveActor(uint16 id) {
	for (uint i = 0; i < _activeActorIds.size(); ++i)
		if (_activeActorIds[i] == id)
			_activeActorIds[i] = 0;
}

bool Logic::activeActor(uint16 id) const {
	for (uint i = 0; i < _activeActorIds.size(); ++i)
		if (_activeActorIds[i] == id)
			return true;
	return false;
}

void Logic::registerCurrentRoomActors() {
	if (!_resources || !_resources->mainDat())
		return;

	const uint16 mainActors = _resources->mainDat()->actorsCount();
	for (uint16 i = 0; i < mainActors; ++i) {
		const uint16 id = i + 1;
		Actor *const actor = _resources->mainDat()->actor(i);
		if (!actor || actor->room() != _currentRoom || !actor->hasScriptEntryPoint())
			continue;
		registerActiveActor(id);
		actor->prepareRoomEntryActiveActor();
	}

	if (!_blockProgram)
		return;

	const uint16 blockActors = _blockProgram->actorsCount();
	for (uint16 i = 0; i < blockActors; ++i) {
		const uint16 id = uint16(mainActors + i + 1);
		Actor *const actor = _blockProgram->actor(i);
		if (!actor || actor->room() != _currentRoom || !actor->hasScriptEntryPoint())
			continue;
		registerActiveActor(id);
		actor->prepareRoomEntryActiveActor();
	}
}

void Logic::refreshCurrentRoomActorFrames() {
	// DOS DrawActors @ 1000:6cca walks the active actor table every pass:
	// if actor.field+0x61 is nonzero it calls SetActorPosition, then starts
	// MoveActorToTargetExit when field+0x62 differs. C++ keeps the actor path
	// queue outside the saved scalar fields, so this must run again after
	// deserializing actors, not only during the initial room restart.
	for (uint i = 0; i < _activeActorIds.size(); ++i) {
		Actor *const ac = getActor(_activeActorIds[i]);
		if (ac && ac->room() == _currentRoom && ac->frameId() != 0) {
			const uint16 frame = ac->frameId();
			const uint16 target = ac->targetFrameId();
			ac->setFrame(frame);
			if (target != frame)
				ac->moveTo(target);
		}
	}
}

uint16 Logic::recordField(uint8 selector, uint16 id, uint8 off, uint8 size) const {
	uint8 lo = 0;
	uint8 hi = 0;

	switch (selector) {
	case 1: {
		Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0;
		if (!exit) {
			uint16 raw = 0;
			return (_blockProgram && _blockProgram->getExitRecordField(id, off, size, raw)) ? raw : 0;
		}
		if (off == 0 || off == 1)
			lo = recordWordByte(exit->room(), 0, off);
		else if (off == 2 || off == 3)
			lo = recordWordByte(uint16(exit->position().x), 2, off);
		else if (off == 4 || off == 5)
			lo = recordWordByte(uint16(exit->position().y), 4, off);
		else if (off == 6 || off == 7)
			lo = recordWordByte(exit->spriteField(), 6, off);
		else if (off == 0x0a)
			lo = exit->noSprite() ? 1 : 0;
		else if (off == 0x0b)
			lo = exit->zIndex();
		else
			lo = exitField(id, off);
		if (size == 1)
			return lo;
		hi = recordField(selector, id, uint8(off + 1), 1);
		return uint16(lo) | (uint16(hi) << 8);
	}
	case 2:
		if (off == 0 || off == 1)
			lo = recordWordByte(getObjectRoom(id), 0, off);
		else if (off == 2 || off == 3)
			lo = recordWordByte(uint16(getObjectPosX(id)), 2, off);
		else if (off == 4 || off == 5)
			lo = recordWordByte(uint16(getObjectPosY(id)), 4, off);
		else
			lo = objectField(id, off);
		if (size == 1)
			return lo;
		hi = recordField(selector, id, uint8(off + 1), 1);
		return uint16(lo) | (uint16(hi) << 8);
	case 3: {
		Actor *actor = getActor(id);
		if (!actor)
			return 0;
		if (off == Actor::kOffsetLeft || off == Actor::kOffsetLeft + 1)
			lo = recordWordByte(uint16(actor->position().x), Actor::kOffsetLeft, off);
		else if (off == Actor::kOffsetTop || off == Actor::kOffsetTop + 1)
			lo = recordWordByte(uint16(actor->position().y), Actor::kOffsetTop, off);
		else if (off == Actor::kOffsetMainSprite || off == Actor::kOffsetMainSprite + 1)
			lo = recordWordByte(actor->mainSpriteId(), Actor::kOffsetMainSprite, off);
		else if (off == Actor::kOffsetTicksLeft || off == Actor::kOffsetTicksLeft + 1)
			lo = recordWordByte(uint16(actor->ticksLeft()), Actor::kOffsetTicksLeft, off);
		else if (off == Actor::kOffsetInterval)
			lo = actor->interval();
		else if (off == Actor::kOffsetActorCallbackSegment || off == Actor::kOffsetActorCallbackSegment + 1)
			lo = recordWordByte(actor->actorCallbackSeg(), Actor::kOffsetActorCallbackSegment, off);
		else if (off == Actor::kOffsetActorCallbackOffset || off == Actor::kOffsetActorCallbackOffset + 1)
			lo = recordWordByte(actor->actorCallbackOff(), Actor::kOffsetActorCallbackOffset, off);
		else if (off == Actor::kOffsetRoom || off == Actor::kOffsetRoom + 1)
			lo = recordWordByte(actor->room(), Actor::kOffsetRoom, off);
		else if (off == Actor::kOffsetFrame)
			lo = uint8(actor->frameId());
		else if (off == Actor::kOffsetTargetFrame)
			lo = uint8(actor->targetFrameId());
		else if (off == Actor::kOffsetAttentionNeeded && actor->isMoving())
			lo = 1;
		else
			lo = actor->field(off);
		if (size == 1)
			return lo;
		hi = recordField(selector, id, uint8(off + 1), 1);
		return uint16(lo) | (uint16(hi) << 8);
	}
	default:
		const_cast<Logic *>(this)->setPendingError(0x03);
		return 0;
	}
}

void Logic::setRecordField(uint8 selector, uint16 id, uint8 off, uint8 size, uint16 value) {
	const uint8 count = size == 1 ? 1 : 2;
	for (uint8 i = 0; i < count; ++i) {
		const uint8 byteOff = uint8(off + i);
		const uint8 byteValue = uint8((value >> (i * 8)) & 0xff);
		switch (selector) {
		case 1: {
			Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0;
			if (!exit)
				return;
			if (byteOff == 0 || byteOff == 1)
				exit->setRoom(recordWordWithByte(exit->room(), 0, byteOff, byteValue));
			else if (byteOff == 2 || byteOff == 3) {
				Common::Point p = exit->position();
				p.x = int16(recordWordWithByte(uint16(p.x), 2, byteOff, byteValue));
				exit->setPosition(p);
			} else if (byteOff == 4 || byteOff == 5) {
				Common::Point p = exit->position();
				p.y = int16(recordWordWithByte(uint16(p.y), 4, byteOff, byteValue));
				exit->setPosition(p);
			} else if (byteOff == 6 || byteOff == 7)
				exit->setSpriteField(recordWordWithByte(exit->spriteField(), 6, byteOff, byteValue));
			else if (byteOff == 0x0a)
				exit->setNoSprite(byteValue != 0);
			else if (byteOff == 0x0b)
				exit->setZIndex(byteValue);
			else
				setExitField(id, byteOff, byteValue);
			break;
		}
		case 2:
			if (byteOff == 0 || byteOff == 1)
				setObjectRoom(id, recordWordWithByte(getObjectRoom(id), 0, byteOff, byteValue));
			else if (byteOff == 2 || byteOff == 3) {
				const int16 x = int16(recordWordWithByte(uint16(getObjectPosX(id)), 2, byteOff, byteValue));
				setObjectPosition(id, x, getObjectPosY(id));
			} else if (byteOff == 4 || byteOff == 5) {
				const int16 y = int16(recordWordWithByte(uint16(getObjectPosY(id)), 4, byteOff, byteValue));
				setObjectPosition(id, getObjectPosX(id), y);
			} else
				setObjectField(id, byteOff, byteValue);
			break;
		case 3: {
			Actor *actor = getActor(id);
			if (!actor)
				return;
			actor->setField(byteOff, byteValue);
			if (byteOff == Actor::kOffsetLeft || byteOff == Actor::kOffsetLeft + 1) {
				Common::Point p = actor->position();
				p.x = int16(recordWordWithByte(uint16(p.x), Actor::kOffsetLeft, byteOff, byteValue));
				actor->setRawPosition(p);
			} else if (byteOff == Actor::kOffsetTop || byteOff == Actor::kOffsetTop + 1) {
				Common::Point p = actor->position();
				p.y = int16(recordWordWithByte(uint16(p.y), Actor::kOffsetTop, byteOff, byteValue));
				actor->setRawPosition(p);
			} else if (byteOff == Actor::kOffsetMainSprite || byteOff == Actor::kOffsetMainSprite + 1)
				actor->setRawMainSprite(recordWordWithByte(actor->mainSpriteId(), Actor::kOffsetMainSprite, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetTicksLeft || byteOff == Actor::kOffsetTicksLeft + 1)
				actor->setRawTicksLeft(recordWordWithByte(uint16(actor->ticksLeft()), Actor::kOffsetTicksLeft, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetInterval)
				actor->setRawInterval(byteValue);
			else if (byteOff == Actor::kOffsetActorCallbackSegment || byteOff == Actor::kOffsetActorCallbackSegment + 1)
				actor->setActorCallback(recordWordWithByte(actor->actorCallbackSeg(), Actor::kOffsetActorCallbackSegment, byteOff, byteValue), actor->actorCallbackOff());
			else if (byteOff == Actor::kOffsetActorCallbackOffset || byteOff == Actor::kOffsetActorCallbackOffset + 1)
				actor->setActorCallback(actor->actorCallbackSeg(), recordWordWithByte(actor->actorCallbackOff(), Actor::kOffsetActorCallbackOffset, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetRoom || byteOff == Actor::kOffsetRoom + 1)
				actor->forceRoom(recordWordWithByte(actor->room(), Actor::kOffsetRoom, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetFrame)
				actor->setRawFrame(byteValue);
			else if (byteOff == Actor::kOffsetTargetFrame)
				actor->setRawTargetFrame(byteValue);
			break;
		}
		default:
			setPendingError(0x03);
			return;
		}
	}
}

void Logic::setProtagonist(uint16 actor) {
	_protagonistId = actor;
	_protagonist = getActor(actor);
}

Actor *Logic::protagonist() const {
	return _protagonist;
}

void Logic::actorFramesClearCount() {
	// DOS Op_e2 / room-restart only clear g_walkbox_count (DS:0x6617).
	// The 0xfd-entry backing table remains in memory and SetActorPosition
	// can still read records beyond the live count.
	_actorFrameCount = 0;
}

void Logic::actorFramesAdd(Common::Point p, const Common::Array<byte> &nexts) {
	if (_actorFrameCount >= 0xfd) {
		setPendingError(0x30);
		return;
	}

	const uint16 frameIndex = uint16(_actorFrameCount + 1);
	const Actor::Frame frame = makeActorFrame(frameIndex, p, nexts);
	if (_actorFrameCount < _actorFrameTable.size())
		_actorFrameTable[_actorFrameCount] = frame;
	else
		_actorFrameTable.push_back(frame);
	++_actorFrameCount;
}

Actor::Frame Logic::actorFrame(uint16 index) const {
	if (index == 0)
		return _actorFrameZero;
	if (index > _actorFrameTable.size())
		return Actor::Frame();
	return _actorFrameTable[index - 1];
}

void Logic::actorFrameInvalidate(uint16 index) {
	if (index >= 0xfd) {
		setPendingError(0x30);
		return;
	}
	if (index == 0) {
		_actorFrameZero.invalidate();
		return;
	}

	const uint16 tableIndex = uint16(index - 1);
	while (tableIndex >= _actorFrameTable.size())
		_actorFrameTable.push_back(makeEmptyActorFrame(uint16(_actorFrameTable.size() + 1)));
	_actorFrameTable[tableIndex].invalidate();
}

void Logic::actorFrameSetPosition(uint16 index, int16 x, int16 y) {
	if (index >= 0xfd) {
		setPendingError(0x30);
		return;
	}
	if (index == 0) {
		_actorFrameZero.setPosition(Common::Point(x, y));
		return;
	}

	const uint16 tableIndex = uint16(index - 1);
	while (tableIndex >= _actorFrameTable.size())
		_actorFrameTable.push_back(makeEmptyActorFrame(uint16(_actorFrameTable.size() + 1)));
	_actorFrameTable[tableIndex].setPosition(Common::Point(x, y));
}

void Logic::updateScrollPosition() {
	const int16 oldX = _cameraX;
	const int16 oldY = _cameraY;
	const int16 speedX = _slowCpu ? 4 : 8;
	const int16 speedY = _slowCpu ? 1 : 2;

	if (!_inputEnabled) {
		if (_cameraTargetX != 0xffff) {
			const int16 targetX = int16(_cameraTargetX);
			if (targetX == _cameraX)
				_cameraTargetX = 0xffff;
			else
				_cameraX = stepCameraToward(_cameraX, targetX, speedX);
		}

		if (_cameraTargetY != 0xffff) {
			const int16 targetY = int16(_cameraTargetY);
			if (targetY == _cameraY)
				_cameraTargetY = 0xffff;
			else
				_cameraY = stepCameraToward(_cameraY, targetY, speedY);
		}
	} else {
		// DOS UpdateScrollPosition @ 1000:74a5 follows the protagonist while
		// g_input_enabled is set. The scroll deltas at DS:0x662b/0x662d are
		// persistent and are clamped against the loaded backdrop dimensions.
		Actor *protag = _protagonist;
		Graphics *graphics = _engine ? _engine->graphics() : 0;
		if (protag && graphics) {
			int16 actorScreenX = int16(protag->position().x - _cameraX);
			int16 actorScreenY = int16(protag->position().y - _cameraY);
			const uint16 screenHeight = graphics->screenHeight();
			const int16 screenHalf = int16(screenHeight >> 1);
			const int16 screenMaxY = int16(screenHeight - 1);

			if (_scrollDx != 0) {
				actorScreenX = int16(actorScreenX - _scrollDx);
				if (_scrollDx >= 0) {
					if (actorScreenX <= 0xa0)
						_scrollDx = 0;
				} else if (actorScreenX >= 0xa0) {
					_scrollDx = 0;
				}
			}

			if (_scrollDy != 0) {
				actorScreenY = int16(actorScreenY - _scrollDy);
				if (_scrollDy >= 0) {
					if (actorScreenY <= screenHalf)
						_scrollDy = 0;
				} else if (actorScreenY >= screenHalf) {
					// Assembly clears DS:0x662b here, not DS:0x662d.
					_scrollDx = 0;
				}
			}

			int16 dxCandidate = int16(speedX * 2);
			const uint8 actorWidth = protag->visibleSpriteWidth();
			if (actorScreenX <= int16(actorWidth)) {
				_scrollDx = int16(-dxCandidate);
			} else if (actorScreenX >= int16(0x13f - actorWidth)) {
				_scrollDx = dxCandidate;
			} else {
				dxCandidate = speedX;
				if (actorScreenX <= 0x3c)
					_scrollDx = int16(-dxCandidate);
				else if (actorScreenX >= 0x103)
					_scrollDx = dxCandidate;
			}

			int16 dyCandidate = int16(speedY * 2);
			const uint8 actorHeight = protag->visibleSpriteHeight();
			if (actorScreenY <= int16(actorHeight)) {
				_scrollDy = int16(-dyCandidate);
			} else if (actorScreenY >= screenMaxY) {
				_scrollDy = dyCandidate;
			} else {
				dyCandidate = speedY;
				if (actorScreenY <= 0x0a)
					_scrollDy = int16(-dyCandidate);
				else if (actorScreenY >= 0x96)
					_scrollDy = dyCandidate;
			}

			const int16 maxX = cameraMaxOrigin(graphics->backdropWidth(), 320);
			int16 newX = int16(_cameraX + _scrollDx);
			if (newX < 0) {
				newX = 0;
				_scrollDx = 0;
			} else if (newX + 320 >= int16(graphics->backdropWidth())) {
				newX = maxX;
				_scrollDx = 0;
			}
			_cameraX = newX;

			const int16 maxY = cameraMaxOrigin(graphics->backdropHeight(), screenHeight);
			int16 newY = int16(_cameraY + _scrollDy);
			if (newY < 0) {
				newY = 0;
				_scrollDy = 0;
			} else if (newY + int16(screenHeight) >= int16(graphics->backdropHeight())) {
				newY = maxY;
				_scrollDy = 0;
			}
			_cameraY = newY;
		}
	}

	_scrollChanged = oldX != _cameraX || oldY != _cameraY;
}

void Logic::centerCameraOnProtagonist() {
	// CenterCameraOnActor @ 1000:742c runs after the room script and before
	// DrawActors. It only runs while input is enabled, and centers from the
	// protagonist's current frame table entry, not the actor's raw x/y.
	if (!_inputEnabled || !_protagonist || !_engine || !_engine->graphics())
		return;

	Graphics *graphics = _engine->graphics();
	const Actor::Frame frame = actorFrame(uint8(_protagonist->frameId()));
	const Common::Point pos = frame.position();
	const int16 oldX = _cameraX;
	const int16 oldY = _cameraY;

	const int16 maxX = cameraMaxOrigin(graphics->backdropWidth(), 320);
	int16 x = int16(pos.x - 0xa0);
	if (x < 0)
		x = 0;
	else if (pos.x + 0xa0 >= int16(graphics->backdropWidth()))
		x = maxX;
	_cameraX = x;

	const uint16 viewHeight = graphics->screenHeight();
	const int16 maxY = cameraMaxOrigin(graphics->backdropHeight(), viewHeight);
	int16 y = int16(pos.y - int16(viewHeight >> 1));
	if (y < 0)
		y = 0;
	else if (y + int16(viewHeight) >= int16(graphics->backdropHeight()))
		y = maxY;
	_cameraY = y;

	_scrollChanged = oldX != _cameraX || oldY != _cameraY;
}

void Logic::changeRoom(uint16 newRoom) {
	// ApplyChangeRoomTransition first stores g_current_location = AX and
	// then flushes the five pending PlaceObjectInRoom slots into that room.
	flushDirtyObjectPlacements(newRoom);

	// DOS ApplyChangeRoomTransition restores g_cursor_mode from DS:0x667a
	// after the Op_cc fullscreen gate, before the restart-room pass clears
	// the fullscreen-gate flag.
	if (_fullscreenGateActive)
		setCursorMode(_defaultCursorMode);

	// ApplyChangeRoomTransition @ 1000:4396 performs a direct video wipe for
	// any non-initial script while not already faded out, then leaves the
	// screen black for the restart-room pass.
	Graphics *graphics = _engine ? _engine->graphics() : 0;
	if (_currentRoom != 0xffff && _opcodeMode != 0 && graphics && !graphics->inFade())
		graphics->applyRoomChangeWipe();

	// just schedule it, we'll execute on next tick
	_nextRoom = newRoom;
	_forceRoomRestart = true;
	setLogicDirty();
	setPaused();

	if (_currentRoom == 0xffff)
		doChangeRoom(); // except if it's the first one
}

void Logic::restartRoom() {
	if (_currentRoom == 0xffff)
		return;
	_nextRoom = _currentRoom;
	_forceRoomRestart = true;
}

void Logic::doChangeRoom() {
	assert(_nextRoom);

	debugC(1, kDebugLevelFlow, "Interspective: changeRoom %u → %u", (uint)_currentRoom, (uint)_nextRoom);
	const bool forceRestart = _forceRoomRestart;
	const bool enteringStatusScreen = _enteringStatusScreen;
	_forceRoomRestart = false;
	_enteringStatusScreen = false;
	if (_nextRoom == _currentRoom && !forceRestart) {
		_nextRoom = 0;
		return;
	}
	_currentRoom = _nextRoom;
	_nextRoom = 0;
	_roomLoop.reset();

	// DOS ApplyChangeRoomTransition sets g_flag_restart_room; MainGameLoop's
	// restart-room path then resets the cast table, actor render table, zone
	// counts, overlay count, anim-list count, no-step/step flags, ESC
	// breakpoint flag, and the post-move callback before running the new
	// room script. Mirror the modeled pieces here for every room change,
	// not only block changes.
	clearRoomTransientAnimations();
	resetActiveActorTable();
	castTableClearAll();
	_overlayQueue.clear();
	clearDrawCommands();
	_postMoveCallback = PostMoveCallback();
	_zones.clear();
	_collisionZones.clear();
	_zonesB.clear();
	_walkboxes.clear();
	actorFramesClearCount();
	_animList.clear();
	_cameraX = 0;
	_cameraY = 0;
	_cameraTargetX = 0xffff;
	_cameraTargetY = 0xffff;
	_scrollDx = 0;
	_scrollDy = 0;
	_scrollChanged = false;
	_dialogCursor0 = _dialogCursor1 = _dialogClickGate = 0;
	_noStep = false;
	_stepPending = false;
	_rightClickCycleCooldown = 4;
	_roomActive = true;
	_hitTarget = 0;
	_inStatusMode = enteringStatusScreen;
	_fullscreenGateActive = false;
	_inputEnabled = true;
	clearEscBreakPoint();
	if (_engine && _engine->graphics())
		_engine->graphics()->setFullscreen(false);
	_motionText.clear();
	_motionTextTicks = 0;
	// DOS restart-room calls RecycleStaleSpeechSlots @ 1000:996c, which
	// clears slots whose owner was marked 0xffff by cutscene backup.
	recycleStaleSpeechSlots();
	if (_engine && _engine->graphics())
		_engine->graphics()->clearSpeech();

	uint16 newBlock = _resources->blockOfRoom(_currentRoom);
	const bool changedBlock = newBlock != _currentBlock;

	if (changedBlock) {
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
			cancelSpeechSlotCallbacksForInterpreter(oldBlock);
		}

		// Block change: any animation (including main-code actors like
		// the protagonist whose _base was rebased into block code via
		// Op_be/Op_b9/etc.) holds a raw pointer into _blockProgram->_code.
		// Reassigning _blockProgram below frees that buffer; the next
		// tick would dereference freed memory and ASan-trip in
		// Animation::tick at `_base + _offset`. Find any such animation
		// and drop its _base now — the actor becomes inert until the
		// script re-attaches it (Op_bd/Op_be).
		// EXCEPTION: when a saved scene frame is holding a SharedPtr to
		// the outgoing _blockProgram, its _code buffer survives the
		// reassignment — Op_01's pop will restore the program. Skip the
		// drop in that case so the saved actors' _base pointers remain
		// valid for the popped scene to resume.
		Program *oldProgram = _blockProgram.get();
		const bool oldProgramPreserved =
			(_savedScene && _savedScene->blockProgram == _blockProgram) || (_roomBackup.valid && _roomBackup.blockProgram == _blockProgram);
		if (oldProgram && !oldProgramPreserved) {
			const byte *lo = oldProgram->codeBegin();
			const byte *hi = oldProgram->codeEnd();
			foreach (Animation *, _animations)
				(*it)->dropBaseIfIn(lo, hi);
		}

		_currentBlock = newBlock;
		_blockProgram = Common::SharedPtr<Program>(_resources->loadCodeBlock(newBlock));
		if (_loadBlockOverrideId == newBlock && !_loadBlockOverrideData.empty()) {
			if (_blockProgram->codeSize() == _loadBlockOverrideData.size()) {
				memcpy(_blockProgram->base(), &_loadBlockOverrideData[0], _loadBlockOverrideData.size());
			} else {
				setPendingError(0x07);
				warning("Interspective: saved block %u image size %u does not match loaded block size %u",
						(uint)newBlock, (uint)_loadBlockOverrideData.size(), (uint)_blockProgram->codeSize());
			}
			_loadBlockOverrideData.clear();
			_loadBlockOverrideId = 0xffff;
		}
		// DOS keeps the AddExitToList dynamic-object list in global
		// state; inventory objects registered before a block change must
		// survive into the playable room.

		char buf[100];
		snprintf(buf, 100, "block %d code", newBlock);

		_blockInterpreter = Common::SharedPtr<Interpreter>(
			new Interpreter(this, _blockProgram->base(), _blockProgram->codeSize(), buf));
		_blockProgram->loadActors(_blockInterpreter.get());
		_blockProgram->loadExits(_blockInterpreter.get());

		debugC(2, kDebugLevelScript, ">>>running block entry code for block %d", newBlock);
		_blockInterpreter->run(_blockProgram->begin(), kCodeNewBlock);
		debugC(2, kDebugLevelScript, "<<<finished block entry code for block %d", newBlock);
	}

	cancelDeferredScriptsForInterpreter(_blockInterpreter.get());
	registerCurrentRoomActors();

	_room = Common::SharedPtr<Room>(new Room(this));
	const uint16 roomHandler = _blockProgram->roomHandler(_currentRoom);
	if (roomHandler == 0) {
		// DOS EnsureRoomLoaded scans the room table as 4-byte
		// (room, handler) pairs and raises error 0x07 when the current
		// location is not found in any block; it never interprets offset 0.
		setPendingError(0x07);
		warning("Interspective: room %u has no handler in block %u",
				(uint)_currentRoom, (uint)_currentBlock);
		return;
	}
	debugC(2, kDebugLevelScript, ">>>running room entry code for room %d", _currentRoom);
	_blockInterpreter->run(roomHandler, kCodeNewRoom);
	debugC(2, kDebugLevelScript, "<<<finished room entry code for room %d", _currentRoom);

	if (enteringStatusScreen && _engine && _engine->graphics() && _currentPlace != 0) {
		// RunStatusScreenLoop reloads the current-place backdrop immediately
		// after EnsureRoomLoaded/room entry via RestoreBackdrop.
		MainDat *main = _resources ? _resources->mainDat() : 0;
		if (!main || _currentPlace > main->imagesCount())
			setPendingError(0x0a);
		else {
			_loadedBackdropId = _currentPlace;
			_engine->graphics()->setBackdrop(_currentPlace);
		}
	}

	centerCameraOnProtagonist();

	// Do not force the protagonist into every loaded room. Rooms without
	// protagonist data (e.g. title-card/no-protagonist rooms) must remain
	// actor-free; Op_d6 seeds the protagonist room only for the boot-param
	// shortcut that skips the intro animation.

	refreshCurrentRoomActorFrames();

	if (changedBlock)
		restartBlockAudio();
}

void Logic::queueDirtyObjectPlacement(uint16 objId, int16 x, int16 y) {
	const int16 height = int16(objectField(objId, 0x11));
	const int16 currentX = getObjectPosX(objId);
	const int16 currentYMinusHeight = int16(getObjectPosY(objId) - height);
	const int16 targetYMinusHeight = int16(y - height);
	for (uint i = 0; i < ARRAYSIZE(_dirtyObjectPlacements); ++i) {
		DirtyObjectPlacement &slot = _dirtyObjectPlacements[i];
		if (slot.objId != 0)
			continue;
		slot.objId = objId;
		slot.currentX = currentX;
		slot.currentYMinusHeight = currentYMinusHeight;
		slot.targetX = x;
		slot.targetYMinusHeight = targetYMinusHeight;
		debugC(2, kDebugLevelScript,
			   "queued dirty object placement slot %u: obj=%u current=%d,%d target=%d,%d",
			   i, objId, slot.currentX, slot.currentYMinusHeight, slot.targetX, slot.targetYMinusHeight);
		return;
	}

	// DOS falls back to a direct object-record write if the transient table is full.
	setObjectRoom(objId, uint16(_currentRoom));
	setObjectPosition(objId, x, y);
	debugC(1, kDebugLevelScript,
		   "dirty object placement table full; object %u placed directly", objId);
	setLogicDirty();
}

void Logic::flushDirtyObjectPlacements(uint16 room) {
	for (uint i = 0; i < ARRAYSIZE(_dirtyObjectPlacements); ++i) {
		DirtyObjectPlacement &slot = _dirtyObjectPlacements[i];
		if (slot.objId == 0)
			continue;
		const uint16 objId = slot.objId;
		const int16 y = int16(slot.targetYMinusHeight + int16(objectField(objId, 0x11)));
		setObjectRoom(objId, room);
		setObjectPosition(objId, slot.targetX, y);
		debugC(2, kDebugLevelScript,
			   "flushed dirty object placement slot %u: obj=%u room=%u x=%d y=%d",
			   i, objId, room, slot.targetX, y);
		slot = DirtyObjectPlacement();
		setLogicDirty();
	}
}

void Logic::paintDirtyObjectPlacements(Graphics *graphics, int16 layer) {
	if (!graphics)
		return;

	for (uint i = 0; i < ARRAYSIZE(_dirtyObjectPlacements); ++i) {
		DirtyObjectPlacement &slot = _dirtyObjectPlacements[i];
		if (slot.objId == 0)
			continue;

		if (int8(objectField(slot.objId, 0x0e)) != layer)
			continue;

		const int16 targetX = slot.targetX;
		if (targetX >= 0 && slot.currentX != targetX) {
			if (slot.currentX < targetX)
				slot.currentX = MIN<int16>(targetX, int16(slot.currentX + 2));
			else
				slot.currentX = MAX<int16>(targetX, int16(slot.currentX - 2));
		}

		if (slot.currentYMinusHeight != slot.targetYMinusHeight) {
			if (slot.currentYMinusHeight < slot.targetYMinusHeight)
				slot.currentYMinusHeight = MIN<int16>(slot.targetYMinusHeight, int16(slot.currentYMinusHeight + 8));
			else
				slot.currentYMinusHeight = MAX<int16>(slot.targetYMinusHeight, int16(slot.currentYMinusHeight - 8));
		}

		const uint16 spriteId = uint16(objectField(slot.objId, 6)) | (uint16(objectField(slot.objId, 7)) << 8);
		if (spriteId != 0xffff) {
			Common::ScopedPtr<Sprite> sprite(_resources->loadSprite(spriteId));
			const int16 y = int16(slot.currentYMinusHeight + int16(objectField(slot.objId, 0x11)));
			graphics->paint(sprite.get(), Common::Point(slot.currentX, y), Graphics::kPaintCameraRelative);
		}

		if ((targetX < 0 || slot.currentX == targetX) &&
			slot.currentYMinusHeight == slot.targetYMinusHeight) {
			const uint16 objId = slot.objId;
			const int16 y = int16(slot.targetYMinusHeight + int16(objectField(objId, 0x11)));
			setObjectRoom(objId, uint16(_currentRoom));
			setObjectPosition(objId, slot.currentX, y);
			slot = DirtyObjectPlacement();
			setLogicDirty();
		}
	}
}

void Logic::refreshObjectSpriteAndExitInfo(uint16 objId) {
	const uint16 sprite = uint16(objectField(objId, 6)) | (uint16(objectField(objId, 7)) << 8);
	if (sprite == 0xffff)
		return;

	const SpriteInfo info = objectSpriteInfo(_resources, _blockProgram.get(), sprite);
	setObjectField(objId, 0x10, uint8(info.width));
	setObjectField(objId, 0x11, uint8(info.height));

	int32 zoneX = int32(getObjectPosX(objId)) + (int32(info.width) >> 1);
	if (zoneX < 0)
		zoneX = 0;
	const int16 cx = int16(zoneX);
	const int16 dy = int16(int32(getObjectPosY(objId)) + int32(info.height));

	uint8 low = 0;
	for (uint i = 0; i < _collisionZones.size(); ++i) {
		const CollisionZone &z = _collisionZones[i];
		if (int16(z.a) <= cx && cx <= int16(z.c) &&
			int16(z.b) <= dy && dy <= int16(z.d)) {
			low = uint8(z.slot);
			break;
		}
	}

	uint8 high = 0;
	for (uint i = 0; i < _zonesB.size(); ++i) {
		const ZoneB &z = _zonesB[i];
		if (int16(z.a) <= cx && cx <= int16(z.c) &&
			int16(z.b) <= dy && dy <= int16(z.d)) {
			high = uint8(z.var);
			break;
		}
	}

	setObjectField(objId, 0x0e, low);
	setObjectField(objId, 0x0f, high);
}

void Logic::restartBlockAudio() {
	if (!_engine)
		return;

	if (_engine->dosMusicEnabled() != 0)
		Music.restartCurrent();
	if (Sound *snd = _engine->sound())
		snd->playQueued();
}

void Logic::runLater(const CodePointer &p, uint16 delay) {
	debugC(3, kDebugLevelScript, "will call %s after %d ticks", +p, delay);
	_queued.push_back(DelayedRun(p, delay, frameTicks()));
}

void Logic::runLaterWithMode(const CodePointer &p, uint16 mode, uint16 delay) {
	debugC(3, kDebugLevelScript, "will call %s after %d ticks in mode 0x%02x", +p, delay, mode);
	_queued.push_back(DelayedRun(p, delay, frameTicks(), mode, true));
}

void Logic::runLaterWithCurrentMode(const CodePointer &p, uint16 delay) {
	runLaterWithMode(p, _opcodeMode, delay);
}

uint16 Logic::deferredQueuedCount() const {
	uint16 count = 0;
	for (Common::List<DelayedRun>::const_iterator it = _queued.begin(); it != _queued.end(); ++it)
		if (!it->canceled && it->deferredMode != 0)
			++count;
	return count;
}

void Logic::syncCodePointer(Common::Serializer &s, CodePointer &p) const {
	enum SavedCodeSegment {
		kSavedCodeNone = 0,
		kSavedCodeMain = 1,
		kSavedCodeBlock = 2
	};

	uint8 source = kSavedCodeNone;
	uint16 offset = p.offset();
	if (s.isSaving() && !p.isEmpty()) {
		if (p.interpreter() == _toplevelInterpreter.get()) {
			source = kSavedCodeMain;
		} else if (p.interpreter() == _blockInterpreter.get()) {
			source = kSavedCodeBlock;
		} else {
			warning("Interspective: dropping save callback for stale interpreter %p at 0x%04x",
					(void *)p.interpreter(), (uint)offset);
			source = kSavedCodeNone;
			offset = 0;
		}
	}

	s.syncAsByte(source);
	s.syncAsUint16LE(offset);

	if (!s.isLoading())
		return;

	switch (source) {
	case kSavedCodeMain:
		p = CodePointer(offset, _toplevelInterpreter.get());
		break;
	case kSavedCodeBlock:
		p = CodePointer(offset, _blockInterpreter.get());
		break;
	default:
		p.reset();
		break;
	}
}

void Logic::syncQueuedRuns(Common::Serializer &s) {
	uint16 count = uint16(_queued.size());
	s.syncAsUint16LE(count);

	if (s.isLoading()) {
		_queued.clear();
		_runningQueued = 0;
		_runningQueuedMode = 0;
		for (uint16 i = 0; i < count; ++i) {
			CodePointer code;
			syncCodePointer(s, code);
			uint16 delay = 0;
			uint16 queuedTick = 0;
			uint16 runMode = 0;
			uint8 hasRunMode = 0;
			uint16 deferredMode = 0;
			uint8 canceled = 0;
			uint8 waitKind = 0;
			uint16 waitParam = 0;
			s.syncAsUint16LE(delay);
			s.syncAsUint16LE(queuedTick);
			s.syncAsUint16LE(runMode);
			s.syncAsByte(hasRunMode);
			s.syncAsUint16LE(deferredMode);
			s.syncAsByte(canceled);
			s.syncAsByte(waitKind);
			s.syncAsUint16LE(waitParam);
			if (code.isEmpty())
				continue;
			if (waitKind > DelayedRun::kWaitCastEntryInactive)
				waitKind = DelayedRun::kWaitNone;
			DelayedRun run(code, delay, queuedTick, runMode, hasRunMode != 0,
						   deferredMode, static_cast<DelayedRun::WaitKind>(waitKind), waitParam);
			run.canceled = canceled != 0;
			_queued.push_back(run);
		}
		return;
	}

	for (Common::List<DelayedRun>::const_iterator it = _queued.begin(); it != _queued.end(); ++it) {
		CodePointer code = it->code;
		uint16 delay = it->delay;
		uint16 queuedTick = it->queuedTick;
		uint16 runMode = it->runMode;
		uint8 hasRunMode = it->hasRunMode ? 1 : 0;
		uint16 deferredMode = it->deferredMode;
		uint8 canceled = it->canceled ? 1 : 0;
		uint8 waitKind = uint8(it->waitKind);
		uint16 waitParam = it->waitParam;
		syncCodePointer(s, code);
		s.syncAsUint16LE(delay);
		s.syncAsUint16LE(queuedTick);
		s.syncAsUint16LE(runMode);
		s.syncAsByte(hasRunMode);
		s.syncAsUint16LE(deferredMode);
		s.syncAsByte(canceled);
		s.syncAsByte(waitKind);
		s.syncAsUint16LE(waitParam);
	}
}

bool Logic::queueDeferred(const CodePointer &p) {
	static const uint16 kDeferredModeBase = 0x0b;
	static const uint16 kDeferredSlotCount = 8;

	for (uint16 slot = 0; slot < kDeferredSlotCount; ++slot) {
		const uint16 mode = kDeferredModeBase + slot;
		bool used = false;
		for (Common::List<DelayedRun>::const_iterator it = _queued.begin(); it != _queued.end(); ++it) {
			if (!it->canceled && it->deferredMode == mode) {
				used = true;
				break;
			}
		}
		if (!used) {
			debugC(3, kDebugLevelScript, "will call deferred %s in mode 0x%02x", +p, mode);
			// DOS RunDeferredScripts runs after init/new-room/new-block scripts
			// but before the first object/speech paint of the tick.
			const bool preDeferredPhase = _opcodeMode == kCodeInitial || _opcodeMode == kCodeNewRoom || _opcodeMode == kCodeNewBlock;
			const uint16 queuedTick = preDeferredPhase ? uint16(frameTicks() - 1) : frameTicks();
			_queued.push_back(DelayedRun(p, 0, queuedTick, mode, true, mode));
			return true;
		}
	}

	return false;
}

void Logic::startMotionText(uint16 ticks, const byte *text, uint16 length) {
	_motionTextTicks = ticks;
	_motionText.clear();
	if (!text) {
		_motionText.push_back(0);
		return;
	}

	if (length == 0)
		length = motionTextStreamLength(text);

	for (uint i = 0; i < length; ++i)
		_motionText.push_back(text[i]);

	if (_motionText.empty() || _motionText[_motionText.size() - 1] != 0)
		_motionText.push_back(0);
}

void Logic::tickMotionText() {
	if (_motionTextTicks)
		--_motionTextTicks;
}

void Logic::paintMotionText() {
	if (_motionTextTicks && !_motionText.empty())
		Graf.paintMotionText(&_motionText[0], uint16(_motionText.size()));
}

bool Logic::enableObjectFlag1(uint16 id) {
	const uint16 exitCount = _blockProgram ? _blockProgram->exitsCount() : 0;
	if (int16(id) > int16(exitCount)) {
		setPendingError(0x14);
		return false;
	}

	const bool wasSet = cellBit(id, 0);
	if (!wasSet) {
		setCellBit(id, 0);
		setLogicDirty();
	}
	if (Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0)
		if (!exit->isEnabled())
			exit->setEnabled(true);
	return true;
}

bool Logic::disableObjectFlag1(uint16 id) {
	const uint16 exitCount = _blockProgram ? _blockProgram->exitsCount() : 0;
	if (int16(id) > int16(exitCount)) {
		setPendingError(0x14);
		return false;
	}

	const bool wasSet = cellBit(id, 0);
	if (wasSet) {
		clearCellBit(id, 0);
		setLogicDirty();
	}
	if (Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0)
		if (exit->isEnabled())
			exit->setEnabled(false);
	return true;
}

uint16 Logic::disableObjectFlag1ReturnAx(uint16 id) {
	const uint16 exitCount = _blockProgram ? _blockProgram->exitsCount() : 0;
	const uint16 axAfterCellRead = (int16(id) > int16(exitCount))
									   ? id
									   : uint16(((id - 1) & 0xff00) | cellByte(id));
	disableObjectFlag1(id);
	return axAfterCellRead;
}

// DOS Op_38_SwitchToScene @ 1000:3c58 saves the caller's PC plus a
// memcpy of the cast (0x642 bytes) and actor tables. C++ instead
// captures the entire _blockProgram (which owns _actors) plus the
// _blockInterpreter (owns _exits, animation registrations) by
// SharedPtr — semantically equivalent because changing the slot is
// the only mutator. Single slot (not stack) matches DOS exactly:
// `_g_block_pc_offset == 0` is the empty sentinel.
void Logic::saveSceneFrame(const CodePointer &resumePC) {
	SceneFrame *frame = new SceneFrame();
	frame->blockProgram = _blockProgram;
	frame->blockInterpreter = _blockInterpreter;
	frame->currentBlock = _currentBlock;
	frame->currentRoom = _currentRoom;
	frame->room = _room;
	frame->resumePC = resumePC;
	// Snapshot _animations so the sub-scene's loadActors-appended
	// entries can be unwound on pop (DOS RestoreActorTableBackup).
	frame->savedAnimations = _animations;
	frame->savedActiveActorIds = _activeActorIds;
	// Snapshot the post-move callback record (DOS [0x65ab..0x65bb]).
	// The sub-scene starts with a clean slot and any callbacks it
	// arms get cleared on pop.
	frame->savedPostMoveCallback = _postMoveCallback;
	_postMoveCallback = PostMoveCallback();
	// Snapshot the cast table (DOS Op_38 calls SaveCastBackup which
	// memcpys 0x642 bytes from g_cast_table). Sub-scene starts empty.
	frame->savedCastTable = _castTable;
	castTableClearAll();
	_savedScene = Common::SharedPtr<SceneFrame>(frame);
}

CodePointer Logic::switchToScene(uint16 sceneId, const CodePointer &resumePC) {
	// Op_38 calls LoadRoomLevelHeader, not the normal room-restart path.
	// Scene scripts live in the second IUC_PROG.DAT entry table
	// (main footer count0 + scene id) and execute from offset 2 in a
	// temporary code segment. The current room/location is unchanged.
	saveSceneFrame(resumePC);

	char buf[64];
	snprintf(buf, sizeof(buf), "scene %u code", sceneId);
	_sceneProgramKeepAlive = Common::SharedPtr<Program>(_resources->loadSceneCodeBlock(sceneId));
	_sceneInterpreterKeepAlive = Common::SharedPtr<Interpreter>(
		new Interpreter(this, _sceneProgramKeepAlive->base(), _sceneProgramKeepAlive->codeSize(), buf));

	_currentBlock = uint16(_resources->mainDat()->roomProgramCount() + sceneId);
	_blockProgram = _sceneProgramKeepAlive;
	_blockInterpreter = _sceneInterpreterKeepAlive;
	return CodePointer(2, _blockInterpreter.get());
}

// DOS Op_01 @ 1000:59a3 nested-pop path: when `_g_block_pc_offset != 0`,
// restores the saved PC, calls LoadCodeBlock, RestoreCastBackup,
// RestoreActorTableBackup, and returns WITHOUT setting g_break_loop —
// the dispatch loop continues at the restored PC.
//
// C++ restores the caller's _blockProgram/_blockInterpreter/Room state
// and returns the saved PC to the bytecode dispatcher. The dispatcher
// then transfers directly to that interpreter in the same script run,
// preserving DOS's "return without g_break_loop" behaviour.
CodePointer Logic::restoreSceneFrame() {
	if (!_savedScene)
		return CodePointer();
	SceneFrame frame = *_savedScene;
	_savedScene.reset();
	_blockProgram = frame.blockProgram;
	_blockInterpreter = frame.blockInterpreter;
	_currentBlock = frame.currentBlock;
	_currentRoom = frame.currentRoom;
	_room = frame.room;
	// Restore the _animations list to the pre-Op_38 state. The
	// sub-scene's loadActors appended new entries; replacing the
	// list drops them. The saved actors are still alive because the
	// SceneFrame held the old Program SharedPtr keeping their _code
	// buffer valid.
	_animations = frame.savedAnimations;
	_activeActorIds = frame.savedActiveActorIds;
	// Restore the post-move callback slot (any sub-scene callback is
	// dropped — DOS Op_97/Op_98 do this by save/restoring the [0x65ab..]
	// register block).
	_postMoveCallback = frame.savedPostMoveCallback;
	// Restore cast table (DOS RestoreCastBackup memcpys g_cast_table
	// from the saved buffer).
	_castTable = frame.savedCastTable;
	debugC(2, kDebugLevelScript, "Op_01 popped scene; resuming immediately at %s", +frame.resumePC);
	return frame.resumePC;
}

void Logic::captureRoomStateForStatusSave(RoomBackup &dst) const {
	dst.valid = true;
	dst.currentBlock = _currentBlock;
	dst.currentRoom = _currentRoom;
	dst.loadedBackdropId = _loadedBackdropId;
	dst.blockProgram = _blockProgram;
	dst.blockInterpreter = _blockInterpreter;
	dst.room = _room;
	dst.animations = _animations;
	dst.activeActorIds = _activeActorIds;
	dst.actorStates.clear();
	if (_resources && _resources->mainDat()) {
		uint16 actorCount = _resources->mainDat()->actorsCount();
		if (_blockProgram)
			actorCount += _blockProgram->actorsCount();
		for (uint16 id = 1; id <= actorCount; ++id) {
			Actor *actor = getActor(id);
			if (!actor)
				continue;
			RoomBackup::ActorState state;
			state.id = id;
			captureActorState(actor, state.bytes);
			if (!state.bytes.empty())
				dst.actorStates.push_back(state);
		}
	}
	dst.castTable = _castTable;
	dst.queued = _queued;
	dst.speechSlots = _speechSlots;
	dst.uiTextSpeechSlot = _uiTextSpeechSlot;
	dst.cameraX = _cameraX;
	dst.cameraY = _cameraY;
	dst.scrollChanged = _scrollChanged;
	dst.cursorMode = _cursorMode;
	dst.fullscreen = _engine && _engine->graphics()
						 ? _engine->graphics()->screenHeight() == 200
						 : !_roomActive;
	dst.roomActive = _roomActive;
	dst.noStep = _noStep;
	dst.zones = _zones;
	dst.collisionZones = _collisionZones;
	dst.zonesB = _zonesB;
	dst.walkboxes = _walkboxes;
	dst.actorFrameZero = _actorFrameZero;
	dst.actorFrameTable = _actorFrameTable;
	dst.actorFrameCount = _actorFrameCount;
	dst.overlayQueue = _overlayQueue;
	dst.animList = _animList;
	dst.dialogCursor0 = _dialogCursor0;
	dst.dialogCursor1 = _dialogCursor1;
	dst.dialogClickGate = _dialogClickGate;
	dst.drawCommands = _drawCommands;
	dst.visibleNoSpriteExits = _visibleNoSpriteExits;
	dst.drawCommandCount = _drawCommandCount;
	dst.postMoveCallback = _postMoveCallback;
	dst.postMoveTargetFrameMirror = _postMoveTargetFrameMirror;
	dst.skipPoint = _skipPoint;
	dst.escBreakProc = _escBreakProc;
	dst.escBreakSrcPC = _escBreakSrcPC;
	dst.escBreakPending = _escBreakPending;
	dst.nextRoom = _nextRoom;
	dst.forceRoomRestart = _forceRoomRestart;
	dst.inStatusMode = _inStatusMode;
	dst.enteringStatusScreen = _enteringStatusScreen;
	dst.stepPending = _stepPending;
	dst.logicDirty = _logicDirty;
	dst.autoCloseTimer = _autoCloseTimer;
}

void Logic::restoreStatusActorAndSpeechState(const RoomBackup &src) {
	for (uint i = 0; i < src.actorStates.size(); ++i)
		restoreActorState(getActor(src.actorStates[i].id), src.actorStates[i].bytes);
	_speechSlots = src.speechSlots;
	if (_speechSlots.size() != kSpeechSlotCount)
		_speechSlots.resize(kSpeechSlotCount);
	_uiTextSpeechSlot = src.uiTextSpeechSlot;
}

void Logic::applyRoomStateForStatusSave(const RoomBackup &src) {
	_currentBlock = src.currentBlock;
	_currentRoom = src.currentRoom;
	_loadedBackdropId = src.loadedBackdropId;
	_blockProgram = src.blockProgram;
	_blockInterpreter = src.blockInterpreter;
	_room = src.room;
	_animations = src.animations;
	_activeActorIds = src.activeActorIds;
	_castTable = src.castTable;
	_queued = src.queued;
	restoreStatusActorAndSpeechState(src);
	_cameraX = src.cameraX;
	_cameraY = src.cameraY;
	_scrollChanged = src.scrollChanged;
	_cursorMode = src.cursorMode;
	_roomActive = src.roomActive;
	_noStep = src.noStep;
	_zones = src.zones;
	_collisionZones = src.collisionZones;
	_zonesB = src.zonesB;
	_walkboxes = src.walkboxes;
	_actorFrameZero = src.actorFrameZero;
	_actorFrameTable = src.actorFrameTable;
	_actorFrameCount = src.actorFrameCount;
	_overlayQueue = src.overlayQueue;
	_animList = src.animList;
	_dialogCursor0 = src.dialogCursor0;
	_dialogCursor1 = src.dialogCursor1;
	_dialogClickGate = src.dialogClickGate;
	_drawCommands = src.drawCommands;
	_visibleNoSpriteExits = src.visibleNoSpriteExits;
	_drawCommandCount = src.drawCommandCount;
	_postMoveCallback = src.postMoveCallback;
	_postMoveTargetFrameMirror = src.postMoveTargetFrameMirror;
	_skipPoint = src.skipPoint;
	_escBreakProc = src.escBreakProc;
	_escBreakSrcPC = src.escBreakSrcPC;
	_escBreakPending = src.escBreakPending;
	_nextRoom = src.nextRoom;
	_forceRoomRestart = src.forceRoomRestart;
	_inStatusMode = src.inStatusMode;
	_enteringStatusScreen = src.enteringStatusScreen;
	_stepPending = src.stepPending;
	_logicDirty = src.logicDirty;
	_autoCloseTimer = src.autoCloseTimer;
	if (_engine && _engine->graphics())
		_engine->graphics()->setFullscreen(src.fullscreen);
}

void Logic::backupRoomForStatus() {
	// RunStatusScreenLoop @ 1000:7695 saves these fields into DS:0x5ed5..0x5ee8,
	// then snapshots cast, actor, and script state before switching to room 999.
	captureRoomStateForStatusSave(_roomBackup);
}

bool Logic::beginStatusSaveSnapshot() {
	if (_statusSaveOverrideActive || !_inStatusMode || !_roomBackup.valid)
		return false;

	captureRoomStateForStatusSave(_statusSaveShadow);
	applyRoomStateForStatusSave(_roomBackup);
	_nextRoom = 0;
	_forceRoomRestart = false;
	_enteringStatusScreen = false;
	_inStatusMode = false;
	_stepPending = false;
	_autoCloseTimer = 1;
	_logicDirty = true;
	_statusSaveOverrideActive = true;
	debugC(2, kDebugLevelFlow,
		   "saving status screen through backed-up gameplay room %u",
		   (uint)_currentRoom);
	return true;
}

void Logic::endStatusSaveSnapshot() {
	if (!_statusSaveOverrideActive)
		return;
	applyRoomStateForStatusSave(_statusSaveShadow);
	_statusSaveShadow = RoomBackup();
	_statusSaveOverrideActive = false;
}

void Logic::enterStatusScreenLoop() {
	// DispatchVerbAction @ 1000:b9a0 sends hit-region 2 to
	// RunStatusScreenLoop @ 1000:7695. DOS snapshots the current room state,
	// switches to special room 999, then lets that room's scripts drive the
	// visible status/save/load surface until region 2 restores the backup.
	if (_inStatusMode || _enteringStatusScreen)
		return;
	Graphics *graphics = _engine ? _engine->graphics() : 0;
	// DOS RunStatusScreenLoop @ 1000:7695 refuses to enter only while the
	// fullscreen gate is set or a palette fade is pending. The original game
	// DOES allow SPACE to open the status menu during a cutscene (confirmed by
	// play-testing the DOS build), so do NOT gate on canSkipCutscene/_roomActive
	// here — that would diverge from the original.
	if (_fullscreenGateActive || (graphics && graphics->palettePending())) {
		debugC(2, kDebugLevelEvents,
			   "status screen entry ignored [DOS RunStatusScreenLoop gate: fullscreen=%d palette=%d]",
			   _fullscreenGateActive ? 1 : 0,
			   (graphics && graphics->palettePending()) ? 1 : 0);
		return;
	}

	if (_engine)
		_engine->captureStatusSaveThumbnail();
	backupRoomForStatus();
	// The DOS status loop saves the deferred queue/room-script slots, then
	// services only status-room mode 7 until RestoreScriptStateBackup. Keep the
	// saved game-room queue out of the live C++ dispatcher while room 999 is
	// active; restoreRoomFromBackup() reinstates it.
	_queued.clear();
	_runningQueued = 0;
	_runningQueuedMode = 0;
	castTableClearAll();
	_cameraX = 0;
	_cameraY = 0;
	_scrollChanged = false;
	_zones.clear();
	_inStatusMode = true;
	_autoCloseTimer = -1;
	_noStep = false;
	_roomActive = true;
	_logicDirty = true;
	_enteringStatusScreen = true;
	_nextRoom = 999;
	_forceRoomRestart = true;
	if (graphics) {
		graphics->clearStatusScreenText();
		graphics->clearBackdrop();
		graphics->setFullscreen(false);
	}
}

void Logic::restoreRoomFromBackup() {
	// RestoreRoomFromBackup @ 1000:7886:
	//   subtitle_frames_left = 0; restore DS:0x5ed5 backup fields; reload
	//   g_loaded_backdrop_id; RestoreCastBackup; RestoreActorTableBackup;
	//   RestoreScriptStateBackup; ResetRoomScriptSlot(7); ResetRoomScriptSlot(6);
	//   step_pending = 0; auto_close_timer = 1; change_room = logic_dirty = 1;
	//   in_status_mode = 0.
	if (_engine && _engine->graphics()) {
		_engine->graphics()->clearStatusScreenText();
		_engine->graphics()->clearSpeech();
	}

	if (_roomBackup.valid) {
		_currentBlock = _roomBackup.currentBlock;
		_currentRoom = _roomBackup.currentRoom;
		_loadedBackdropId = _roomBackup.loadedBackdropId;
		_nextRoom = 0;
		_blockProgram = _roomBackup.blockProgram;
		_blockInterpreter = _roomBackup.blockInterpreter;
		_room = _roomBackup.room;
		_animations = _roomBackup.animations;
		_activeActorIds = _roomBackup.activeActorIds;
		_castTable = _roomBackup.castTable;
		_queued = _roomBackup.queued;
		restoreStatusActorAndSpeechState(_roomBackup);
		_cameraX = _roomBackup.cameraX;
		_cameraY = _roomBackup.cameraY;
		_scrollChanged = _roomBackup.scrollChanged;
		if (_roomBackup.cursorMode == 0)
			_cursorMode = 0;
		_roomActive = _roomBackup.roomActive;
		_noStep = _roomBackup.noStep;
		_zones = _roomBackup.zones;
		_collisionZones = _roomBackup.collisionZones;
		_zonesB = _roomBackup.zonesB;
		_walkboxes = _roomBackup.walkboxes;
		_actorFrameZero = _roomBackup.actorFrameZero;
		_actorFrameTable = _roomBackup.actorFrameTable;
		_actorFrameCount = _roomBackup.actorFrameCount;
		_overlayQueue = _roomBackup.overlayQueue;
		_animList = _roomBackup.animList;
		_dialogCursor0 = _roomBackup.dialogCursor0;
		_dialogCursor1 = _roomBackup.dialogCursor1;
		_dialogClickGate = _roomBackup.dialogClickGate;
		_drawCommands = _roomBackup.drawCommands;
		_visibleNoSpriteExits = _roomBackup.visibleNoSpriteExits;
		_drawCommandCount = _roomBackup.drawCommandCount;
		_postMoveCallback = _roomBackup.postMoveCallback;
		_postMoveTargetFrameMirror = _roomBackup.postMoveTargetFrameMirror;
		_skipPoint = _roomBackup.skipPoint;
		_escBreakProc = _roomBackup.escBreakProc;
		_escBreakSrcPC = _roomBackup.escBreakSrcPC;
		_escBreakPending = _roomBackup.escBreakPending;
		if (_engine && _engine->graphics())
			_engine->graphics()->setFullscreen(_roomBackup.fullscreen);
		_roomBackup.valid = false;
	}

	if (_engine && _engine->graphics()) {
		_engine->graphics()->clearFramebuffer();
		const uint16 id = _loadedBackdropId;
		if (id != 0) {
			MainDat *main = _resources ? _resources->mainDat() : 0;
			if (!main || id > main->imagesCount())
				setPendingError(0x0a);
			else
				_engine->graphics()->setBackdrop(id);
		}
	}

	resetQueuedRunMode(7);
	resetQueuedRunMode(6);
	_stepPending = false;
	_autoCloseTimer = 1;
	_logicDirty = true;
	_inStatusMode = false;
}

// Mirrors DOS RunPostMoveCallback @ 1000:73a6. The DOS check sequence is:
//   if (protag.field+0x6f != 0)        return;         // blocked
//   if (protag.field+0x65 == 0)         return;        // not moving
//   if (post_callback_ptr == 0)         return;        // none armed
//   if (protag.field+0x61 == [0x6609]) → CALL [BP];   // fire
//   clear post_callback_ptr;                           // one-shot
// DOS clears regardless of whether the frame matched, but only if it
// passed the first three guards.
void Logic::runPostMoveCallbackIfReady() {
	if (_postMoveCallback.kind == PostMoveCallback::kNone)
		return;
	if (!_protagonist)
		return;
	if (_protagonist->movementWaitActive())
		return;
	if (!_protagonist->needsAttention())
		return;

	PostMoveCallback cb = _postMoveCallback;
	_postMoveCallback = PostMoveCallback(); // one-shot clear before dispatch
	if (uint8(_protagonist->frameId()) != _postMoveTargetFrameMirror)
		return;

	debugC(2, kDebugLevelScript,
		   "post-move callback firing: kind=%d cellId=%u arg0=%u arg1=%u",
		   int(cb.kind), cb.cellId, cb.arg0, cb.arg1);

	switch (cb.kind) {
	case PostMoveCallback::kDisableMoveOptionalEnable:
		// DOS trampoline @ 1000:49df: clearCellBit(cellId) + MovePersonToActor(arg0)
		// + (arg1 != 0 → EnableObjectFlag1 = setCellBit(arg1)).
		disableObjectFlag1(cb.cellId);
		movePersonToActor(cb.arg0);
		if (cb.arg1 != 0)
			enableObjectFlag1(cb.arg1);
		break;
	case PostMoveCallback::kDisableEnableUnregister:
		// DOS trampoline @ 1000:4a36: PUSH BX; DisableObjectFlag1(AX); POP BX;
		// EnableObjectFlag1(AX as left by DisableObjectFlag1); Op_8e.
		enableObjectFlag1(disableObjectFlag1ReturnAx(cb.cellId));
		clearDragInteractionLikeOp8e();
		break;
	case PostMoveCallback::kPlaceProtagonistAfterMove:
		// DOS callback @ 1000:4376: place the protagonist in the destination
		// room/frame after the approach walk reaches the current entity,
		// then sets restart-room/logic-dirty/paused flags unconditionally.
		if (!_inStatusMode && _protagonist) {
			const uint16 room = cb.cellId;
			const uint16 frame = uint8(cb.arg0);
			const uint16 nextFrame = uint8(cb.arg1);
			_protagonist->placeIn(room, frame, nextFrame);
			if (room != _currentRoom)
				changeRoom(room);
			else
				restartRoom();
		} else {
			restartRoom();
		}
		setLogicDirty();
		setPaused();
		break;
	case PostMoveCallback::kPlaceObjectAfterHotspotMove:
		// DOS callback @ 1000:c408: place the dragged object after the protagonist
		// reaches the hotspot approach frame. PlaceObjectInRoom fills a
		// five-entry transient table and DrawDirtyRectInRoom commits the
		// object record only after the short movement reaches its target.
		{
			const uint16 oldRoom = getObjectRoom(cb.cellId);
			const int16 oldX = getObjectPosX(cb.cellId);
			const int16 oldY = getObjectPosY(cb.cellId);
			setObjectRoom(cb.cellId, uint16(_currentRoom));
			setObjectPosition(cb.cellId, int16(cb.arg0), int16(cb.arg1));
			refreshObjectSpriteAndExitInfo(cb.cellId);
			setObjectRoom(cb.cellId, oldRoom);
			setObjectPosition(cb.cellId, oldX, oldY);
		}
		queueDirtyObjectPlacement(cb.cellId, int16(cb.arg0), int16(cb.arg1));
		setDragTarget(0);
		setCursorMode(1);
		setLogicDirty();
		break;
	case PostMoveCallback::kActivateProtagonistSpeechAfterMove:
		// DOS callback @ 1000:9be9: find the protagonist speech slot, mark slot+2
		// active again, and recompute the bubble reference point from the
		// actor's current sprite/size fields.
		activateActorSpeechAfterPostMove(_protagonist);
		break;
	case PostMoveCallback::kBeginDragAfterMove:
		// DOS callback @ 1000:3297: BeginDrag_AfterRemoveExit with BX=0 after
		// HandleSecondaryClick walked the protagonist to a room object.
		beginDragAfterRemoveExit(cb.arg0, false);
		break;
	case PostMoveCallback::kNone:
	default:
		break;
	}
}

// DOS MovePersonToActor @ 1000:4706 (also entry of Op_84_handler).
//
// Disassembly trace:
//   if AX == 0   → JMP Op_8e (cursor=1, drag=0);
//   if AX > g_persons_count → pending error 0x16;
//   if g_cursor_mode == 0x20: ResetObjectAtActorPosition(g_drag_target);
//   g_drag_target = AX;
//   GetObjectOffset(AX) → ES:SI;
//   if (obj.room != g_current_location && obj.room != 0xffff):
//     CALL RetEmpty (returns locked cursor x/y in CX/DX);
//     CX += g_camera_x;  DX += g_camera_y;
//     obj.x = CX;  obj.y = DX;
//   BX = (obj.room == 0xffff) ? 1 : 0;
//   AX = 2;  JMP BeginDrag_AfterRemoveExit;
//
// BeginDrag_AfterRemoveExit (mode=2, BX=0/1):
//   PrepareDragInteraction(drag_target):
//     g_cursor_mode = 0x20; g_drag_target = AX;
//     obj.room = 0;        // "carried" sentinel
//     CalcSpriteOffsetInGraphic(); save sprite-rect bytes;
//   compute screen-rel cursor pos from obj.x/y - camera
//     (or default 128,160 if BX==1);
//   SetCursorAndPosition(cursor_x, cursor_y).
//
// C++ port: capture the script-observable state changes. Per-object
// sprite-rect bytes / cursor-sprite-at-position rendering are the
// renderer's concern — _cursorMode + _dragTarget transitions plus
// the obj.room = 0 marker drive every script branch downstream.
void Logic::movePersonToActor(uint16 id) {
	if (id == 0) {
		// DOS tail-jump to Op_8e.
		clearDragInteractionLikeOp8e();
		return;
	}
	if (_resources && _resources->mainDat() && id > _resources->mainDat()->personsCount()) {
		setPendingError(0x16);
		return;
	}
	if (_cursorMode == 0x20)
		resetObjectAtActorPosition(_dragTarget);

	setDragTarget(id);

	// If the object is in another (non-sentinel) room, snap its position
	// to the locked cursor plus camera origin, matching RetEmpty.
	const uint16 objRoom = getObjectRoom(id);
	if (objRoom != _currentRoom && objRoom != 0xffff) {
		const Common::Point cursor = lockedCursorPosition();
		setObjectPosition(id, int16(cursor.x + _cameraX), int16(cursor.y + _cameraY));
	}

	beginDragAfterRemoveExit(id, objRoom == 0xffff);
}

bool Logic::prepareDragInteraction(uint16 id) {
	setCursorMode(0x20);
	setDragTarget(id);

	uint16 recordId = id;
	if (id == 0) {
		setPendingError(0x16);
		recordId = 1;
	}
	setObjectRoom(recordId, 0);
	const uint16 sprite = uint16(objectField(recordId, 6)) | (uint16(objectField(recordId, 7)) << 8);
	const SpriteInfo info = objectSpriteInfo(_resources, _blockProgram.get(), sprite);
	setObjectField(recordId, 0x10, uint8(info.width));
	setObjectField(recordId, 0x11, uint8(info.height));
	return true;
}

void Logic::beginDragAfterRemoveExit(uint16 id, bool removeExit) {
	const int16 objectX = getObjectPosX(id);
	const int16 objectY = getObjectPosY(id);
	if (!prepareDragInteraction(id))
		return;

	int16 cursorX = objectX;
	int16 cursorY = objectY;
	if (removeExit) {
		unregisterObjectExit(id);
		cursorX = int16(cursorX + 0x80);
		cursorY = int16(cursorY + 0xa0);
	} else {
		cursorX = int16(cursorX - _cameraX);
		cursorY = int16(cursorY - _cameraY);
		if (cursorX < 0)
			cursorX = 0;
		if (cursorY < 0)
			cursorY = 0;
	}

	if (_engine && _engine->graphics())
		_engine->graphics()->setCursorPosition(Common::Point(cursorX, cursorY));
	setLogicDirty();
}

bool Logic::placeObjectInInventoryAtDosPoint(uint16 id, Common::Point screen) {
	if (id == 0)
		return false;
	if (!registerObjectExit(id, false))
		return false;

	setObjectPosition(id, int16(screen.x - 0x80), int16(screen.y - 0xa0));
	setObjectRoom(id, 0xffff);
	clampObjectExitToScreen(id);
	setCursorMode(1);
	setDragTarget(0);
	setLogicDirty();
	return true;
}

// DOS ResetObjectAtActorPosition @ 1000:4837.
//
// Disassembly:
//   GetObjectOffset(AX) → ES:SI;
//   if obj.room == 0xffff: CALL RemoveExitFromList;
//   CALL AddExitToList;  if JC: pending error 0x21;
//   < sprite-relative centering math: CX = (0xb6 - sprite_width) / 2
//                                      DX = (0x1f - sprite_height) / 2
//                                      then add sprite hot offsets >
//   obj.room = 0xffff;        // mark as exit-mode
//   obj.x = CX;  obj.y = DX;
//   < more sprite metadata save >
//   set dirty flags.
//
// C++ port: keep the DOS room sentinel, model dynamic-list membership
// explicitly, and use the same sprite-map bytes DOS reads after
// CalcSpriteOffsetInGraphic.
void Logic::resetObjectAtActorPosition(uint16 id) {
	if (id == 0) {
		setPendingError(0x16);
		return;
	}

	if (getObjectRoom(id) == 0xffff)
		unregisterObjectExit(id);
	if (!registerObjectExit(id))
		return;

	const uint16 placementSprite = uint16(objectField(id, 8)) | (uint16(objectField(id, 9)) << 8);
	const SpriteInfo placementInfo = objectSpriteInfo(_resources, _blockProgram.get(), placementSprite);
	uint16 cx = uint16(0x00b6 - uint16(placementInfo.width));
	uint16 dx = uint16(0x001f - uint16(placementInfo.height));
	cx >>= 1;
	dx >>= 1;
	placeObjectExitAtDosPosition(id, int16(cx), int16(dx));
}

void Logic::placeObjectExitAtDosPosition(uint16 id, int16 x, int16 y) {
	setObjectRoom(id, 0xffff);
	const uint16 placementSprite = uint16(objectField(id, 8)) | (uint16(objectField(id, 9)) << 8);
	const SpriteInfo placementInfo = objectSpriteInfo(_resources, _blockProgram.get(), placementSprite);
	const int16 adjustedX = int16(uint16(uint16(x) + uint16(int16(placementInfo.hotLeft))));
	const int16 adjustedY = int16(uint16(uint16(y) + uint16(int16(placementInfo.hotTop))));
	setObjectPosition(id, adjustedX, adjustedY);
	clampObjectExitToScreen(id);

	const uint16 sprite = uint16(objectField(id, 6)) | (uint16(objectField(id, 7)) << 8);
	const SpriteInfo info = objectSpriteInfo(_resources, _blockProgram.get(), sprite);
	setObjectField(id, 0x10, uint8(info.width));
	setObjectField(id, 0x11, uint8(info.height));
	setLogicDirty();
}

void Logic::clampObjectExitToScreen(uint16 id) {
	if (getObjectRoom(id) != 0xffff)
		return;

	const uint16 placementSprite = uint16(objectField(id, 8)) | (uint16(objectField(id, 9)) << 8);
	const SpriteInfo placementInfo = objectSpriteInfo(_resources, _blockProgram.get(), placementSprite);
	const int16 hotLeft = int16(placementInfo.hotLeft);
	const int16 hotTop = int16(placementInfo.hotTop);
	int16 left = int16(getObjectPosX(id) - hotLeft);
	int16 top = int16(getObjectPosY(id) - hotTop);

	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	const int16 xOverflow = int16(left + int16(placementInfo.width) - 0x00b6);
	if (xOverflow >= 0)
		left = int16(left - xOverflow);
	const int16 yOverflow = int16(top + int16(placementInfo.height) - 0x001f);
	if (yOverflow >= 0)
		top = int16(top - yOverflow);

	setObjectPosition(id, int16(left + hotLeft), int16(top + hotTop));
}

// DOS SendActorToTarget @ 1000:7323 dispatches MoveProtagonistToEntity
// @ 1000:7331 which switches on a "type" register (DX = 1 exit / 2
// object / 3 actor). The C++ port doesn't carry a separate type tag
// across opcode dispatch (DOS sets DX inside the opcode body before
// the call — Op_b5 sets DX=1, Op_b6 DX=2, Op_b7 DX=3). Instead, we
// resolve the target by id and try each entity table in order:
//   * Actor by id (1-based DOS actor id) → frame match.
//   * Exit by id (current block's exit list) → screen pos → nearest frame.
//   * Object by id (Logic::_objectRoom + _objectPos*) → nearest frame.
// First match wins. Cross-room targets are silent no-ops (matches DOS:
// MoveProtagonistToEntity returns early if the entity's room field
// doesn't match g_current_location, with no pending error).
bool Logic::sendActorToTarget(Actor *walker, uint16 targetId) {
	if (!walker) {
		walker = _protagonist;
		if (!walker)
			return false;
	}
	if (!_room)
		return false;

	// 1) Actor target — direct frame match.
	if (Actor *target = getActor(targetId)) {
		if (target->room() == _currentRoom) {
			walker->moveTo(target->frameId());
			return true;
		}
		return false;
	}

	// 2) Exit target — DOS uses GetExitOffset(id) → SI, then reads
	// SI[1] (= screen x) / SI[2] (= screen y) / SI[5] (= sprite flag).
	if (_blockProgram) {
		if (Exit *exit = _blockProgram->getExit(targetId)) {
			if (exit->room() == _currentRoom) {
				const uint16 frame = _room->nearestFrameTo(
					int16(exit->position().x),
					int16(exit->position().y));
				if (frame) {
					walker->moveTo(frame);
					return true;
				}
			}
			return false;
		}
	}

	// 3) Object target — same shape via Logic::_objectRoom/Pos.
	if (getObjectRoom(targetId) == _currentRoom) {
		const uint16 frame = _room->nearestFrameTo(
			getObjectPosX(targetId), getObjectPosY(targetId));
		if (frame) {
			walker->moveTo(frame);
			return true;
		}
	}
	return false;
}

bool Logic::sendActorToEntityByType(Actor *walker, uint16 targetId, uint16 entityType) {
	if (!walker) {
		walker = _protagonist;
		if (!walker)
			return false;
	}
	if (!_room)
		return false;

	int16 targetX = 0;
	int16 targetY = 0;
	switch (entityType) {
	case 1: { // exit
		Exit *exit = _blockProgram ? _blockProgram->getExit(targetId) : 0;
		if (!exit) {
			setPendingError(0x14);
			return false;
		}
		const int16 exitX = int16(recordField(1, targetId, 2, 2));
		const int16 exitY = int16(recordField(1, targetId, 4, 2));
		if (recordField(1, targetId, 0x0a, 1) == 0) {
			const uint16 spriteId = recordField(1, targetId, 6, 2);
			const SpriteInfo info = objectSpriteInfo(_resources, _blockProgram.get(), spriteId);
			targetX = int16(exitX + int16(info.width) / 2);
		} else {
			targetX = exitX;
		}
		targetY = exitY;
		break;
	}
	case 2: { // object/person
		if (targetId == 0) {
			setPendingError(0x16);
			return false;
		}
		// MoveProtagonistToEntity @ 1000:737e returns with CLC when the
		// object/person record is unplaced, so callers still arm their
		// post-move callback even though no immediate walk target is chosen.
		if (getObjectRoom(targetId) == 0xffff)
			return true;
		targetX = int16(getObjectPosX(targetId) + int16(objectField(targetId, 0x10)) / 2);
		targetY = int16(getObjectPosY(targetId) - 5);
		break;
	}
	case 3: { // actor
		Actor *target = getActor(targetId);
		if (!target) {
			setPendingError(0x17);
			return false;
		}
		targetX = int16(target->position().x);
		targetY = int16(target->position().y);
		break;
	}
	default: {
		const Common::Point cursor = lockedCursorPosition();
		targetX = int16(cursor.x + _cameraX);
		targetY = int16(cursor.y + _cameraY);
		break;
	}
	}

	const uint16 frame = _room->nearestFrameTo(targetX, targetY);
	if (frame == 0) {
		setPendingError(0x31);
		moveActorToTargetFrame(this, walker, walker->frameId());
		return walker == _protagonist;
	}
	moveActorToTargetFrame(this, walker, frame);
	return walker == _protagonist;
}

bool Logic::sendActorToCurrentEntity(Actor *walker) {
	return sendActorToEntityByType(walker, _currentEntityId, _gameState);
}

// DOS Op_ba @ 1000:4fe5 / Op_bb @ 1000:4fde:
//   g_walk_speed_flag = 0/1;       // 0=fast, 1=slow
//   if (in_map_mode) RET;
//   if (id > g_anim_count_max) pending error 0x17;
//   if (id == g_main_character_id) g_break_inner = 1;
//   CheckActorAnimReady(id);
//   if (NOT ready) RegisterSampleSlot_LoadDefaultsAndMark; RET;
//   GetActorOffset(id) → ES:SI;
//   ES:[SI + 0x4] = arg2;          // screen x
//   ES:[SI + 0x6] = arg3;          // screen y
//   ES:[SI + 0x61] = 0;            // current frame
//   ResolveOpcodeArg1;             // anim selector (mode-dependent)
//   InitActorState();              // jump script to actor's main code
//
// C++ port: positional state goes through Actor::placeIn (DOS-aligned
// non-script-resetting placement). The walk_speed_flag byte is mirrored
// on Logic; animation rate remains driven by the actor script fields.
bool Logic::walkActorAnim(uint16 actorId, int16 destX, int16 destY, bool slowSpeed) {
	setWalkSpeedFlag(slowSpeed ? 1 : 0);
	Actor *ac = getActor(actorId);
	if (!ac) {
		setPendingError(0x17);
		return false;
	}
	if (!_room)
		return false;

	// DOS sets ES:[SI+0x4]/[SI+0x6] = arg2/arg3 directly (raw screen
	// coords). We translate to a frame via nearestFrameTo so the
	// walk script can pathfind.
	const uint16 destFrame = _room->nearestFrameTo(destX, destY);
	if (destFrame)
		ac->moveTo(destFrame);
	return true;
}

bool Logic::actorIdle(const Actor *actor) const {
	if (!actor)
		return true;
	return !actor->isMoving() && !actor->isSpeaking();
}

// DOS Op_c3_RegisterCastEntry @ 1000:514a (full byte-for-byte spec):
//   ResolveOpcodeArg1 → arg1 (x);
//   ResolveOpcodeArg2 → arg2 (y);
//   ResolveOpcodeArg0 → arg0 (id);
//   for slot in g_cast_table[18]:
//     if (wActive == 0):
//       w_unk_02 = arg0;                    // entity id
//       wActive  = g_codeptr_es_save;        // caller code segment
//       wX       = arg1;
//       wY       = arg2;
//       p_data[0] = 0;                       // ┐
//       p_data[1] = 0;                       // │
//       p_data[2] = 0;                       // │ DOS bookkeeping init
//       p_data[3] = 0;                       // │ (renderer state in DOS)
//       p_data[6] = 1;                       // │ — frame counter
//       p_data[7] = 0;                       // │
//       p_data[8] = 0xff;                    // │ — sprite index sentinel
//       bRect_w   = 0xff;                    // │ — sprite-bounds sentinel
//       bRect_h   = 0xff;                    // │
//       p_data[10] = 0;                      // │
//       p_data[12] = 0;                      // ┘
//       return;
//   pending error 0x2a;
//
// C++ stores `active` as uint16 (0 = free, non-zero = active) and keeps
// the caller code segment as an Interpreter pointer so Op_c6 can perform
// the same script-sentinel check as DOS. The 81-byte `raw` array is
// initialized per the DOS spec.
bool Logic::castTableRegister(uint16 id, int16 x, int16 y, Interpreter *interpreter) {
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.active == 0) {
			if (e.animation)
				removeAnimation(e.animation);
			e.active = 1; // DOS stores caller seg; we use 1 (non-zero = active).
			e.id = id;
			e.x = x;
			e.y = y;
			e.interpreter = interpreter;
			e.animation = 0;
			// Re-init the bookkeeping per DOS Op_c3. Ghidra's CastEntry
			// layout is exact: raw[0]=bRect_w, raw[1]=bRect_h, and
			// raw[2 + N]=p_data[N].
			for (uint j = 0; j < 81; ++j)
				e.raw[j] = 0;
			e.raw[0] = 0xff;  // bRect_w
			e.raw[1] = 0xff;  // bRect_h
			e.raw[8] = 1;     // p_data[6] — frame counter
			e.raw[10] = 0xff; // p_data[8] — sprite index
			// p_data[0/1/2/3/7/10/12] remain zero from the clear above.
			if (interpreter) {
				e.animation = new Animation(CodePointer(id, interpreter), Common::Point(x, y));
				e.animation->setCastTableRunner(true);
				addAnimation(e.animation);
			}
			return true;
		}
	}
	// No free slot — DOS sets pending error 0x2a.
	setPendingError(0x2a);
	return false;
}

// DOS Op_c4_SetCastEntryPosition @ 1000:51a8 (BUG-ACCURATE port):
//
// Disassembly:
//   1000:51a8  CALL ResolveOpcodeArg1   ; AX = arg1
//   1000:51ab  MOV  CX, AX               ; CX = arg1  ← saved here…
//   1000:51ad  CALL ResolveOpcodeArg2   ; AX = arg2
//   1000:51b0  MOV  DX, AX               ; DX = arg2
//   1000:51b2  CALL ResolveOpcodeArg0   ; AX = arg0
//   1000:51b5  MOV  CX, 0x12             ; CX = 0x12 (loop count)
//                                          ↑ ARG1 IS CLOBBERED HERE
//   1000:51b8  MOV  SI, 0x1977
//   1000:51bb  CMP  [SI+0x2], AX         ; cmp slot.id, arg0
//   1000:51be  JZ   0x51c6               ; match → write
//   1000:51c0  ADD  SI, 0x59
//   1000:51c3  LOOP 0x51bb               ; LOOP decrements CX
//   1000:51c5  RET                        ; no match
//   1000:51c6  MOV  [SI+0x4], CX         ; wX = CX = remaining_loop_count
//                                          ↑ NOT arg1 — DOS bug
//   1000:51c9  MOV  [SI+0x6], DX         ; wY = DX = arg2  (correct)
//   1000:51cc  RET
//
// Effect: when slot N (0-indexed) matches, CX still holds (0x12 - N)
// after LOOP iterations. So the saved wX = (kCastTableCap - matched_idx).
// arg1 is silently discarded.
//
// To match DOS faithfully we reproduce the bug. If IUC scripts depend
// on the buggy wX values (or just don't observe them), divergent
// behaviour would be bug-compatible only by reproducing.
void Logic::castTableSetPos(uint16 id, int16 x, int16 y) {
	(void)x; // DOS bug: arg1 is clobbered before the write.
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.id == id) {
			// DOS: wX = (0x12 - i_iterations_through_LOOP).
			// LOOP decrements CX before checking; so for match on i=0,
			// CX is still 0x12; for i=1, CX is 0x11; etc.
			// → wX = kCastTableCap - i.
			e.x = int16(kCastTableCap - i);
			e.y = y;
			return;
		}
	}
	// Silent no-op on miss (matches DOS — no pending error).
}

// DOS Op_c5_ClearCastEntry @ 1000:51cd:
//   pbVar1 = ResolveOpcodeArg0;  iVar2 = 0x12;  pCVar3 = g_cast_table;
//   do {
//     if (pCVar3->w_unk_02 == arg0) {
//       pCVar3->w_unk_02 = 0;
//       pCVar3->wActive  = 0;
//       return;
//     }
//     pCVar3 += 1;  iVar2 -= 1;
//   } while (iVar2 != 0);
//
// Note: DOS only zeros the FIRST 4 BYTES of the slot (wActive +
// w_unk_02). wX/wY/p_data/bRect_w/h are LEFT INTACT. C++ matches.
void Logic::castTableClear(uint16 id) {
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.id == id) {
			if (e.animation)
				removeAnimation(e.animation);
			e.active = 0;
			e.id = 0;
			e.interpreter = 0;
			e.animation = 0;
			// Intentionally preserve x, y, raw[] — DOS leaves them.
			return;
		}
	}
}

// DOS ActorOp_01 @ 1000:68d3 / ActorOp_02 @ 1000:68e3 clear the first
// two words of the active record. For cast-table records those are wActive
// and w_unk_02/id; renderer bytes and position are intentionally left
// intact, just as Op_c5 does.
void Logic::castTableDeactivateAnimation(Animation *animation) {
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.animation == animation) {
			e.active = 0;
			e.id = 0;
			e.interpreter = 0;
			e.animation = 0;
			return;
		}
	}
}

bool Logic::castEntryActive(uint16 id) const {
	for (uint i = 0; i < _castTable.size(); ++i) {
		const CastEntry &e = _castTable[i];
		if (e.active == 0 || e.id != id)
			continue;
		if (e.animation)
			return !e.animation->castWaitComplete();
		if (READ_LE_UINT16(e.raw + 2) == 0 && e.interpreter) {
			const uint16 scriptOffset = uint16(READ_LE_UINT16(e.raw + 4) + id);
			byte *script = e.interpreter->rawCodeChecked(scriptOffset);
			if (script && *script == 0xff)
				return false;
		}
		return true;
	}
	return false;
}

void Logic::runLaterWhenCastEntryInactive(uint16 id, const CodePointer &p) {
	debugC(3, kDebugLevelScript, "will call %s when cast entry %u is inactive in mode 0x%02x",
		   +p, id, _opcodeMode);
	_queued.push_back(DelayedRun(p, 0, frameTicks(), _opcodeMode, true, 0,
								 DelayedRun::kWaitCastEntryInactive, id));
}

// DOS ResetCastTable @ 1000:671d clears only wActive + w_unk_02 for
// all 18 slots. Position/raw renderer bytes are left as-is, like Op_c5.
void Logic::castTableClearAll() {
	for (uint i = 0; i < _castTable.size(); ++i) {
		if (_castTable[i].animation)
			removeAnimation(_castTable[i].animation);
		_castTable[i].active = 0;
		_castTable[i].id = 0;
		_castTable[i].interpreter = 0;
		_castTable[i].animation = 0;
	}
}

// DOS FormatBubbleText_FullPath @ 1000:9333.
//
// Iterates the DI-pointed source byte stream, copying to the formatted
// buffer at 0x40b7 while expanding markup characters. Tracks word count
// (DAT_1000_94b5) and per-line pixel width (iVar7). At terminator (0x00):
//   total_height = (word_count * line_height + 2);
//   if (total_height <= line_height + 2) total_height += line_height;
// else if the 0x1f4 input countdown expires before terminator:
//   pending_error 0x11.
//
// Markup byte semantics (per DOS decompile + line-by-line trace):
//   0x00 → terminator. Patch final row width, return total_height.
//   0x20 (' ') → emit + word_count++.
//   0x2d ('-') → emit + word_count++.
//   0x0d → emit forced newline, patch row width, start next centered row.
//   0x05 → inline literal until next 0x00; each char advances width;
//          spaces inside increment word_count. Then 2 trailing bytes
//          (DOS bookkeeping word) are copied into the buffer.
//   0x09 → marker + 1-byte X-offset copied; width advances by that offset.
//   0x07 → marker + 1-byte color parameter copied.
//   0x06 → 2-byte global-word offset decimal formatter (FormatDecimalNumber).
//   0x0a → conditional skip-block (2-byte global-byte offset). If the
//          game-state byte at the indexed offset is FALSE,
//          skip forward to STX (Start-of-TeXt) marker.
//   0x0b → inverse of 0x0a (skip if TRUE).
//   0x02 → STX marker: consumed, not copied to the formatted buffer.
//   else → emit via LookupCharSprite (= advance width by char's sprite width).
//
// EmitTextRowTerminator @ 1000:94b7 writes a 0x0c center marker at the
// start of each rendered row and later patches the following byte with the
// row width held in BL. RenderSpeechBubbleText consumes that pair to center
// each bubble row.
//
static void appendFormattedTextByte(Common::String &text, byte value) {
	const char ch = char(value);
	text.append(&ch, &ch + 1);
}

// C++ port: produces the DOS formatted text buffer, preserving the rendering
// markup bytes and synthetic row-centering records, plus the dimensions DOS
// computes. Per-glyph widths come from Graphics::getGlyphWidth (the C++
// analog of DOS LookupCharSprite).
Logic::FormattedBubble Logic::formatBubbleText(const byte *src) const {
	FormattedBubble out;
	out.lineCount = 1; // DOS DAT_1000_94b5 init = 1
	out.rowCount = 1;  // DOS DX init = 1
	out.totalHeight = 0;
	out.maxLineWidth = 0;
	out.truncated = false;
	const uint16 lineHeight = bubbleLineHeight();
	if (!src) {
		out.totalHeight = lineHeight * 2 + 2; // DOS minimum
		return out;
	}

	Graphics *g = (_engine ? _engine->graphics() : 0);
	const byte *p = src;
	int currentWidth = 0;
	uint rowWidthPatch = 0;
	bool rowFinished = false;
	uint16 remaining = 0x1f4; // DOS AX countdown in FormatBubbleText_Inner

	auto startTextRow = [&]() {
		appendFormattedTextByte(out.text, kStringCenter);
		appendFormattedTextByte(out.text, 0);
		rowWidthPatch = out.text.size() - 1;
		rowFinished = false;
	};

	auto finishTextRow = [&]() {
		out.text.setChar(char(uint8(currentWidth)), rowWidthPatch);
		if (currentWidth > out.maxLineWidth)
			out.maxLineWidth = currentWidth;
		currentWidth = 0;
		rowFinished = true;
	};

	// Returns DOS LookupCharSprite-equivalent width. Falls back to a
	// fixed 6 px width if Graphics isn't available (early-init path) —
	// in normal gameplay g is always set.
	auto charPixelWidth = [g](byte ch) -> uint16 {
		if (g)
			return g->getGlyphWidth(ch);
		return 6;
	};

	auto readLE16 = [&p]() -> uint16 {
		const uint16 v = READ_LE_UINT16(p);
		p += 2;
		return v;
	};

	auto globalByte = [this](uint16 offset) -> byte {
		if (!_resources)
			return 0;
		return *_resources->getGlobalByteVariable(offset);
	};

	auto globalWord = [this](uint16 offset) -> uint16 {
		if (!_resources)
			return 0;
		return READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	};

	auto skipMarkupBlockToStx = [&]() {
		while (true) {
			const byte ch = *p++;
			if (ch == 0x00)
				return;
			if (ch == 0x20)
				out.lineCount++;
			if (ch == 0x02)
				return;
		}
	};

	auto tickInputCountdown = [&]() -> bool {
		--remaining;
		if (remaining == 0) {
			out.truncated = true;
			return false;
		}
		return true;
	};

	startTextRow();
	while (true) {
		const byte b = *p++;
		if (b == 0x00) {
			// Terminator. Patch final row width → return.
			finishTextRow();
			break;
		}
		if (b == 0x20 || b == 0x2d) {
			// space / dash — word break.
			appendFormattedTextByte(out.text, b);
			out.lineCount++;
			currentWidth += charPixelWidth(b);
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x0d) {
			// Forced newline.
			appendFormattedTextByte(out.text, b);
			finishTextRow();
			++out.rowCount;
			startTextRow();
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x05) {
			// Menu/literal block: preserve marker, NUL, and the two-byte
			// option value in the formatted buffer.
			appendFormattedTextByte(out.text, b);
			while (true) {
				const byte lit = *p++;
				appendFormattedTextByte(out.text, lit);
				if (lit == 0x00)
					break;
				if (lit == 0x20)
					out.lineCount++;
				currentWidth += charPixelWidth(lit);
			}
			const uint16 optionValue = readLE16();
			appendFormattedTextByte(out.text, optionValue & 0xff);
			appendFormattedTextByte(out.text, optionValue >> 8);
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x09) {
			// 1-byte param = X-offset spacing.
			const byte amount = *p++;
			appendFormattedTextByte(out.text, b);
			appendFormattedTextByte(out.text, amount);
			currentWidth += amount;
			out.lineCount++;
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x07) {
			// Color-change marker and parameter are copied verbatim.
			const byte color = *p++;
			appendFormattedTextByte(out.text, b);
			appendFormattedTextByte(out.text, color);
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x06) {
			// Decimal-number formatter: two-byte offset into the global
			// word table (DOS DAT_1000_0099), not an immediate value.
			const uint16 num = globalWord(readLE16());
			Common::String numStr = Common::String::format("%u", num);
			out.text += numStr;
			for (uint i = 0; i < numStr.size(); ++i)
				currentWidth += charPixelWidth(byte(numStr[i]));
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x0a || b == 0x0b) {
			// Conditional skip: two-byte offset into the global byte table
			// (DOS DAT_1000_009d). The marker and offset are not copied to
			// the formatted buffer.
			const byte state = globalByte(readLE16());
			const bool skip = (b == 0x0a) ? (state == 0) : (state != 0);
			if (skip)
				skipMarkupBlockToStx();
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x02) {
			// STX marker terminates a conditional block; DOS backs SI up so
			// the marker is not present in the formatted buffer.
			if (!tickInputCountdown())
				break;
			continue;
		}
		// Default: emit char + advance width via per-glyph lookup.
		appendFormattedTextByte(out.text, b);
		currentWidth += charPixelWidth(b);
		if (!tickInputCountdown())
			break;
	}

	if (!rowFinished)
		finishTextRow();

	// DOS height formula: word_count * line_height + 2; minimum 2*line_height + 2.
	out.totalHeight = uint16(out.lineCount) * lineHeight + 2;
	if (out.totalHeight <= lineHeight + 2)
		out.totalHeight += lineHeight;
	// FormatBubbleText returns CX after subtracting the bubble frame bias.
	out.maxLineWidth = out.maxLineWidth > 0x27 ? uint16(out.maxLineWidth - 0x27) : 0;
	return out;
}

Logic::FormattedBubble Logic::measureVerbBubbleText(const byte *src) const {
	FormattedBubble out;
	out.lineCount = 0;
	out.rowCount = 0;
	out.totalHeight = kBubbleLineHeight + 2;
	out.maxLineWidth = 0;
	out.truncated = false;
	if (!src)
		return out;

	Graphics *g = (_engine ? _engine->graphics() : 0);
	const byte *p = src;
	int32 lineCount = 0;
	int32 maxWidth = 0;
	int32 currentWidth = 0;
	Common::String line;

	auto readLE16 = [&p]() -> uint16 {
		const uint16 v = READ_LE_UINT16(p);
		p += 2;
		return v;
	};

	auto globalByte = [this](uint16 offset) -> byte {
		if (!_resources)
			return 0;
		return *_resources->getGlobalByteVariable(offset);
	};

	auto globalWord = [this](uint16 offset) -> uint16 {
		if (!_resources)
			return 0;
		return READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	};

	auto charPixelWidth = [g](byte ch) -> uint16 {
		if (g)
			return g->getGlyphWidth(ch);
		return 6;
	};

	while (true) {
		const byte b = *p++;
		if (b == 0xff)
			break;
		if (b == 0x04)
			continue;
		if (b == 0x0a) {
			if (globalByte(readLE16()) == 0) {
				--lineCount;
				currentWidth = -1;
			}
			continue;
		}
		if (b == 0x0b) {
			if (globalByte(readLE16()) != 0) {
				--lineCount;
				currentWidth = -1;
			}
			continue;
		}
		if (b == 0x0e) {
			const uint16 offset = readLE16();
			const uint16 expected = readLE16();
			if (globalWord(offset) != expected) {
				--lineCount;
				currentWidth = -1;
			}
			continue;
		}
		if (b == 0x00) {
			++lineCount;
			p += 2;
			if (currentWidth >= maxWidth)
				maxWidth = currentWidth;
			if (currentWidth >= 0 && !line.empty()) {
				if (!out.text.empty())
					out.text += '\r';
				out.text += line;
			}
			line.clear();
			currentWidth = 0;
			continue;
		}
		if (currentWidth == -1)
			continue;
		line += char(b);
		currentWidth += charPixelWidth(b);
	}

	out.lineCount = uint16(lineCount);
	out.maxLineWidth = uint16(maxWidth - 0x0f);
	const uint16 visibleLines = lineCount > 0 ? uint16(lineCount) : 1;
	out.totalHeight = uint16(visibleLines * kBubbleLineHeight + 2);
	return out;
}

Common::String Logic::prepareTextStrippedForRender(const byte *src, bool *truncated) const {
	if (truncated)
		*truncated = false;
	if (!src)
		return Common::String();

	const byte *p = src;
	Common::String out;
	int remaining = 100;

	auto readLE16 = [&p]() -> uint16 {
		const uint16 v = READ_LE_UINT16(p);
		p += 2;
		return v;
	};

	auto globalByte = [this](uint16 offset) -> byte {
		if (!_resources)
			return 0;
		return *_resources->getGlobalByteVariable(offset);
	};

	auto globalWord = [this](uint16 offset) -> uint16 {
		if (!_resources)
			return 0;
		return READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	};

	auto skipMarkupBlockToStx = [&]() {
		while (true) {
			const byte ch = *p++;
			if (ch == 0x00 || ch == 0x02)
				return;
		}
	};

	while (true) {
		const byte b = *p++;
		if (b == 0x00)
			break;
		if (b == 0x06) {
			const uint16 value = globalWord(readLE16());
			out += Common::String::format("%u", value);
			remaining -= 5;
			if (remaining <= 0)
				break;
			continue;
		}
		if (b == 0x07) {
			appendFormattedTextByte(out, b);
			appendFormattedTextByte(out, *p++);
			--remaining;
			if (remaining <= 0) {
				if (truncated)
					*truncated = true;
				break;
			}
			continue;
		}
		if (b == 0x0a) {
			if (globalByte(readLE16()) == 0)
				skipMarkupBlockToStx();
			continue;
		}
		if (b == 0x0b) {
			if (globalByte(readLE16()) != 0)
				skipMarkupBlockToStx();
			continue;
		}
		if (b == 0x02)
			continue;

		appendFormattedTextByte(out, b);
		--remaining;
		if (remaining <= 0) {
			if (truncated)
				*truncated = true;
			break;
		}
	}

	return out;
}

bool Logic::cancelDeferred(const CodePointer &p) {
	Common::List<DelayedRun>::iterator it = _queued.begin();
	while (it != _queued.end()) {
		if (!it->canceled && it->deferredMode != 0 && it->code.offset() == p.offset() && it->code.interpreter() == p.interpreter()) {
			debugC(3, kDebugLevelScript, "cancel deferred %s mode 0x%02x", +p, it->deferredMode);
			const bool selfCancel = _runningQueuedMode != 0 && it->deferredMode == _runningQueuedMode;
			resetQueuedRunMode(it->deferredMode);
			it->canceled = true;
			return selfCancel;
		}
		++it;
	}
	return false;
}

void Logic::runQueued() {
	if (_queued.empty())
		return;

	Interpreter *const liveTopLevel = _toplevelInterpreter.get();
	Interpreter *const liveBlock = _blockInterpreter.get();

	Common::Queue<Common::List<DelayedRun>::iterator> toRemove;
	const uint16 entriesAtStart = uint16(_queued.size());
	debugC(2, kDebugLevelFlow | kDebugLevelScript, ">>>running queued code");
	Common::List<DelayedRun>::iterator it = _queued.begin();
	for (uint16 visited = 0; visited < entriesAtStart && it != _queued.end(); ++visited) {
		Common::List<DelayedRun>::iterator current = it;
		++it;

		if (current->canceled) {
			toRemove.push(current);
		} else if (current->queuedTick == frameTicks()) {
			debugC(3, kDebugLevelScript, "deferred fresh %s until next tick", +current->code);
		} else if (current->delay) {
			debugC(3, kDebugLevelScript, "delayed %s, delay now %d", +current->code,
				   current->delay);
			current->delay--;
		} else if (current->waitKind == DelayedRun::kWaitCastEntryInactive &&
				   castEntryActive(current->waitParam)) {
			debugC(3, kDebugLevelScript, "queued %s waits for cast entry %u",
				   +current->code, current->waitParam);
		} else if (current->deferredMode != 0 &&
				   dispatchReadyActorRoomScriptWaitMode(current->deferredMode)) {
			// DOS RunDeferredScripts always calls RunScriptByMode before it
			// decides whether to interpret the deferred entry. A type-0
			// room-script slot that becomes ready is consumed in that call,
			// and the deferred entry is cleared unless the resumed script arms
			// another slot for the same mode.
			if (current->canceled) {
				toRemove.push(current);
			} else if (!hasQueuedRunMode(current->deferredMode)) {
				debugC(3, kDebugLevelScript,
					   "clearing deferred mode 0x%02x after actor room-script slot completed",
					   current->deferredMode);
				current->canceled = true;
				toRemove.push(current);
			} else {
				debugC(3, kDebugLevelScript, "keeping deferred %s while mode 0x%02x is armed",
					   +current->code, current->deferredMode);
			}
		} else if (current->deferredMode != 0 && hasQueuedRunMode(current->deferredMode)) {
			// DOS RunDeferredScripts first services the per-mode room-script
			// slot via RunScriptByMode. The deferred entry itself does not
			// run while that slot is still armed.
			debugC(3, kDebugLevelScript, "deferred %s waits for mode 0x%02x room-script slot",
				   +current->code, current->deferredMode);
		} else {
			Interpreter *target = current->code.interpreter();
			if (target != liveTopLevel && target != liveBlock) {
				warning("dropping stale queued CodePointer (interpreter %p not live)",
						(void *)target);
				toRemove.push(current);
				continue;
			}
			debugC(2, kDebugLevelFlow | kDebugLevelScript, ">>>running %s", +current->code);
			_runningQueued = &current->code;
			_runningQueuedMode = current->deferredMode;
			const uint16 savedOpcodeMode = _opcodeMode;
			if (current->hasRunMode) {
				if (current->deferredMode == 0)
					markRoomScriptModeServiced(current->runMode);
				current->code.run(static_cast<OpcodeMode>(current->runMode));
			} else {
				current->code.run();
			}
			_opcodeMode = savedOpcodeMode;
			_runningQueued = 0;
			_runningQueuedMode = 0;
			debugC(2, kDebugLevelFlow | kDebugLevelScript, "<<<finished %s", +current->code);
			const bool completedDeferredRoomSlot =
				current->deferredMode == 0 && current->hasRunMode && current->runMode >= 0x0b;
			if (completedDeferredRoomSlot)
				current->canceled = true;
			if (completedDeferredRoomSlot && !hasQueuedRunMode(current->runMode)) {
				for (Common::List<DelayedRun>::iterator deferred = _queued.begin();
					 deferred != _queued.end(); ++deferred) {
					if (!deferred->canceled && deferred->deferredMode == current->runMode) {
						debugC(3, kDebugLevelScript,
							   "clearing deferred mode 0x%02x after room-script slot completed",
							   current->runMode);
						deferred->canceled = true;
					}
				}
			}
			if (current->deferredMode != 0 && hasQueuedRunMode(current->deferredMode)) {
				debugC(3, kDebugLevelScript, "keeping deferred %s while mode 0x%02x is armed",
					   +current->code, current->deferredMode);
			} else {
				toRemove.push(current);
			}
		}
	}
	debugC(2, kDebugLevelFlow | kDebugLevelScript, "<<<finished queued code");

	while (!toRemove.empty())
		_queued.erase(toRemove.pop());

	for (Common::List<DelayedRun>::iterator clean = _queued.begin(); clean != _queued.end();) {
		if (clean->canceled)
			clean = _queued.erase(clean);
		else
			++clean;
	}
}

bool Logic::hasQueuedRunMode(uint16 mode) const {
	// DOS CheckRoomScriptExists tests a single per-mode slot table. The C++
	// mirror can be represented as a queued continuation, an actor wait, or a
	// speech-slot callback depending on which opcode registered the wait.
	for (Common::List<DelayedRun>::const_iterator it = _queued.begin(); it != _queued.end(); ++it)
		if (!it->canceled && it->hasRunMode && it->runMode == mode && it->deferredMode == 0)
			return true;
	if (_resources && _resources->mainDat()) {
		uint16 actorCount = _resources->mainDat()->actorsCount();
		if (_blockProgram)
			actorCount += _blockProgram->actorsCount();
		for (uint16 id = 1; id <= actorCount; ++id)
			if (Actor *actor = getActor(id))
				if (actor->hasRoomScriptWaitMode(mode))
					return true;
	}
	if (speechSlotHasCallbackMode(mode))
		return true;
	return false;
}

bool Logic::roomScriptModeServicedThisTick(uint16 mode) const {
	for (uint i = 0; i < _servicedRunModesThisTick.size(); ++i)
		if (_servicedRunModesThisTick[i] == mode)
			return true;
	return false;
}

void Logic::markRoomScriptModeServiced(uint16 mode) {
	if (!roomScriptModeServicedThisTick(mode))
		_servicedRunModesThisTick.push_back(mode);
}

bool Logic::serviceRoomScriptSlot(uint16 mode) {
	if (roomScriptModeServicedThisTick(mode))
		return true;
	if (dispatchReadyActorRoomScriptWaitMode(mode))
		return true;
	return hasQueuedRunMode(mode);
}

bool Logic::dispatchReadyActorRoomScriptWaitMode(uint16 mode) {
	if (!_resources || !_resources->mainDat())
		return false;

	const uint16 mainActors = _resources->mainDat()->actorsCount();
	const uint16 blockActors = _blockProgram ? _blockProgram->actorsCount() : 0;
	const uint16 actorCount = uint16(mainActors + blockActors);
	for (uint16 id = 1; id <= actorCount; ++id) {
		Actor *actor = getActor(id);
		if (!actor)
			continue;
		const Actor::RoomScriptWaitDispatch status =
			actor->dispatchReadyRoomScriptWaitMode(mode);
		if (status == Actor::kRoomScriptWaitDispatched)
			return true;
		if (status == Actor::kRoomScriptWaitPending)
			return false;
	}
	return false;
}

void Logic::runItemRoomScriptSlot() {
	// DOS HandleInventoryClick @ 1000:2a90 starts with
	// RunScriptByMode(kCodeItem) late in the frame, after actor movement
	// and room-loop scripts. Entity scripts that used Op_9a therefore get
	// one chance to resume after their actor reaches the requested frame,
	// before the next tick's deferred ambient scripts can move that actor
	// again.
	if (!_roomActive || _pendingError != 0)
		return;
	serviceRoomScriptSlot(kCodeItem);
}

void Logic::runStatusScreenScripts() {
	// RunStatusScreenLoop @ 1000:7795..7848:
	//   RunScriptByMode(7);
	//   UpdateRoomAnimation();
	//   ...
	//   RunMissActScript(), whose raw body is RunScriptByMode(6) and,
	//   if no slot is active, EnsureRoomLoaded + InterpretBytecode(mode 6)
	//   at the current room handler. For room 999 this refreshes visible
	//   status labels after save/load/audio/bubble-speed state changes.
	if (!_roomActive || _pendingError != 0 || !_blockInterpreter || !_blockProgram)
		return;

	serviceRoomScriptSlot(kCodeStatusLoop);

	if (serviceRoomScriptSlot(kCodeStatusRefresh))
		return;

	const uint16 handler = _blockProgram->roomHandler(uint16(_currentRoom));
	if (handler == 0)
		return;

	debugC(3, kDebugLevelScript | kDebugLevelFlow,
		   ">>>running status refresh code for room %u", (uint)_currentRoom);
	_blockInterpreter->run(handler, kCodeStatusRefresh);
	debugC(3, kDebugLevelScript | kDebugLevelFlow,
		   "<<<finished status refresh code for room %u", (uint)_currentRoom);
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

void Logic::setLoadBlockImageOverride(uint16 blockId, const Common::Array<byte> &data) {
	_loadBlockOverrideId = blockId;
	_loadBlockOverrideData = data;
}

void Logic::synchronize(Common::Serializer &s) {
	uint16 currentRoom = uint16(_currentRoom);
	uint16 currentPlace = _currentPlace;
	uint16 protagonistId = _protagonistId;
	uint16 currentBlock = _currentBlock;
	uint16 loadedBackdropId = _loadedBackdropId;
	uint8 screenFullscreen = (_engine && _engine->graphics() && _engine->graphics()->screenHeight() == 200) ? 1 : 0;

	s.syncAsUint16LE(currentRoom);
	s.syncAsUint16LE(currentPlace);
	s.syncAsUint16LE(protagonistId);
	s.syncAsUint16LE(currentBlock);
	s.syncAsUint16LE(loadedBackdropId);
	s.syncAsByte(screenFullscreen);

	if (s.isLoading()) {
		_queued.clear();
		_skipPoint.reset();
		_roomLoop.reset();
		_savedScene.reset();
		_roomBackup = RoomBackup();
		_enteringStatusScreen = false;
		_currentRoom = 0xffff;
		_currentBlock = 0xffff;
		_nextRoom = 0;
		_currentPlace = currentPlace;
	}

	uint32 frameCounter = _frameCounter;
	uint16 gameState = _gameState;
	uint8 inStatusMode = _inStatusMode ? 1 : 0;
	uint8 fullscreenGateInitialized = _fullscreenGateInitialized ? 1 : 0;
	uint8 roomActive = _roomActive ? 1 : 0;
	uint8 logicDirty = _logicDirty ? 1 : 0;
	uint8 stepPending = _stepPending ? 1 : 0;
	uint8 noStep = _noStep ? 1 : 0;
	uint16 defaultCursorMode = _defaultCursorMode;
	uint16 cursorMode = _cursorMode;
	uint16 dragTarget = _dragTarget;
	uint16 dragTargetMode40 = _dragTargetMode40;
	uint16 hitTarget = _hitTarget;
	uint16 switchValue = _switchValue;
	uint16 switchTarget = _switchTarget;
	uint16 branchState = _branchState;
	uint8 pendingError = _pendingError;
	uint16 gameScore = _gameScore;
	uint16 currentEntityId = _currentEntityId;
	uint16 drawCommandCount = _drawCommandCount;
	uint16 actorFrameCount = _actorFrameCount;
	uint8 walkSpeedFlag = _walkSpeedFlag;
	int16 cameraX = _cameraX;
	int16 cameraY = _cameraY;
	uint16 cameraTargetX = _cameraTargetX;
	uint16 cameraTargetY = _cameraTargetY;
	int16 scrollDx = _scrollDx;
	int16 scrollDy = _scrollDy;
	uint8 scrollChanged = _scrollChanged ? 1 : 0;
	uint8 inputEnabled = _inputEnabled ? 1 : 0;
	uint16 dialogCursor0 = _dialogCursor0;
	uint16 dialogCursor1 = _dialogCursor1;
	uint16 dialogClickGate = _dialogClickGate;
	uint16 opcodeMode = _opcodeMode;
	uint16 escBreakProc = _escBreakProc;
	uint16 escBreakSrcPC = _escBreakSrcPC;
	uint8 escBreakPending = _escBreakPending ? 1 : 0;
	uint16 bubbleLineHeight = _bubbleLineHeight;
	uint8 parserBufferCapacity = _parserBufferCapacity;
	uint8 callDepth = _callDepth;
	uint16 motionTextTicks = _motionTextTicks;
	uint8 slowCpu = _slowCpu ? 1 : 0;
	uint16 menuStashA = _menuStashA;
	uint16 menuStashB = _menuStashB;
	uint8 menuStashConsumed = _menuStashConsumed ? 1 : 0;
	int16 autoCloseTimer = _autoCloseTimer;

	s.syncAsUint32LE(frameCounter);
	s.syncAsUint16LE(gameState);
	s.syncAsByte(inStatusMode);
	s.syncAsByte(fullscreenGateInitialized);
	s.syncAsByte(roomActive);
	s.syncAsByte(logicDirty);
	s.syncAsByte(stepPending);
	s.syncAsByte(noStep);
	s.syncAsUint16LE(defaultCursorMode);
	s.syncAsUint16LE(cursorMode);
	s.syncAsUint16LE(dragTarget);
	s.syncAsUint16LE(dragTargetMode40);
	s.syncAsUint16LE(hitTarget);
	s.syncAsUint16LE(switchValue);
	s.syncAsUint16LE(switchTarget);
	s.syncAsUint16LE(branchState);
	s.syncAsByte(pendingError);
	s.syncAsUint16LE(gameScore);
	s.syncAsUint16LE(currentEntityId);
	s.syncAsUint16LE(drawCommandCount);
	s.syncAsUint16LE(actorFrameCount);
	s.syncAsByte(walkSpeedFlag);
	s.syncAsSint16LE(cameraX);
	s.syncAsSint16LE(cameraY);
	s.syncAsUint16LE(cameraTargetX);
	s.syncAsUint16LE(cameraTargetY);
	s.syncAsSint16LE(scrollDx);
	s.syncAsSint16LE(scrollDy);
	s.syncAsByte(scrollChanged);
	s.syncAsByte(inputEnabled);
	s.syncAsUint16LE(dialogCursor0);
	s.syncAsUint16LE(dialogCursor1);
	s.syncAsUint16LE(dialogClickGate);
	s.syncAsUint16LE(opcodeMode);
	s.syncAsUint16LE(escBreakProc);
	s.syncAsUint16LE(escBreakSrcPC);
	s.syncAsByte(escBreakPending);
	s.syncAsUint16LE(bubbleLineHeight);
	s.syncString(_parserBuffer);
	s.syncAsByte(parserBufferCapacity);
	s.syncAsByte(callDepth);
	s.syncAsUint16LE(motionTextTicks);
	s.syncAsByte(slowCpu);
	s.syncAsUint16LE(menuStashA);
	s.syncAsUint16LE(menuStashB);
	s.syncAsByte(menuStashConsumed);
	s.syncAsSint16LE(autoCloseTimer);

	const auto applyLoadedScalarState = [&]() {
		_frameCounter = frameCounter;
		_gameState = gameState;
		_inStatusMode = inStatusMode != 0;
		_fullscreenGateActive = false;
		_fullscreenGateInitialized = fullscreenGateInitialized != 0;
		_roomActive = roomActive != 0;
		_logicDirty = logicDirty != 0;
		_stepPending = stepPending != 0;
		_noStep = noStep != 0;
		_defaultCursorMode = defaultCursorMode;
		_cursorMode = cursorMode;
		_dragTarget = dragTarget;
		_dragTargetMode40 = dragTargetMode40;
		_hitTarget = hitTarget;
		_switchValue = switchValue;
		_switchTarget = switchTarget;
		_branchState = branchState;
		_pendingError = pendingError;
		_gameScore = gameScore;
		_currentEntityId = currentEntityId;
		_drawCommandCount = drawCommandCount;
		_actorFrameCount = actorFrameCount;
		_walkSpeedFlag = walkSpeedFlag;
		_cameraX = cameraX;
		_cameraY = cameraY;
		_cameraTargetX = cameraTargetX;
		_cameraTargetY = cameraTargetY;
		_scrollDx = scrollDx;
		_scrollDy = scrollDy;
		_scrollChanged = scrollChanged != 0;
		_inputEnabled = inputEnabled != 0;
		_dialogCursor0 = dialogCursor0;
		_dialogCursor1 = dialogCursor1;
		_dialogClickGate = dialogClickGate;
		_opcodeMode = opcodeMode;
		_escBreakProc = escBreakProc;
		_escBreakSrcPC = escBreakSrcPC;
		_escBreakPending = escBreakPending != 0;
		_bubbleLineHeight = bubbleLineHeight;
		_parserBufferCapacity = parserBufferCapacity;
		_callDepth = callDepth;
		_motionTextTicks = motionTextTicks;
		_slowCpu = slowCpu != 0;
		_menuStashA = menuStashA;
		_menuStashB = menuStashB;
		_menuStashConsumed = menuStashConsumed != 0;
		_autoCloseTimer = autoCloseTimer;
		_runningQueued = nullptr;
		_runningQueuedMode = 0;
	};

	if (s.isLoading()) {
		applyLoadedScalarState();
	}

	syncHashMapUint16Uint16(s, _objectRoom);
	syncHashMapUint16Int16(s, _objectPosX);
	syncHashMapUint16Int16(s, _objectPosY);
	syncHashMapUint32Uint8(s, _objectFields);
	syncHashMapUint32Uint8(s, _exitFields);
	syncUint16Array(s, _objectExitList);
	syncHashMapUint32Uint8(s, _cellBits);
	syncHashMapUint16Uint8(s, _actorFlag70);
	syncHashMapUint16Bool(s, _scoreEventClaimed);
	syncActorFrameRecord(s, _actorFrameZero, 0);
	syncActorFrameArray(s, _actorFrameTable);
	if (s.isLoading() && _actorFrameCount > _actorFrameTable.size())
		_actorFrameCount = uint16(_actorFrameTable.size());

	for (int i = 0; i < 7; ++i)
		s.syncAsUint16LE(_graphicSlots[i]);

	for (uint i = 0; i < ARRAYSIZE(_dirtyObjectPlacements); ++i) {
		s.syncAsUint16LE(_dirtyObjectPlacements[i].objId);
		s.syncAsSint16LE(_dirtyObjectPlacements[i].currentX);
		s.syncAsSint16LE(_dirtyObjectPlacements[i].currentYMinusHeight);
		s.syncAsSint16LE(_dirtyObjectPlacements[i].targetX);
		s.syncAsSint16LE(_dirtyObjectPlacements[i].targetYMinusHeight);
	}

	if (s.isLoading()) {
		const Actor::Frame loadedActorFrameZero = _actorFrameZero;
		const Common::Array<Actor::Frame> loadedActorFrameTable = _actorFrameTable;
		const uint16 loadedActorFrameCount = _actorFrameCount;

		if (currentRoom != 0 && currentRoom != 0xffff)
			changeRoom(currentRoom);

		const uint16 rebuiltDialogCursor0 = _dialogCursor0;
		const uint16 rebuiltDialogCursor1 = _dialogCursor1;
		const uint16 rebuiltDialogClickGate = _dialogClickGate;

		// Room-entry scripts rebuild transient cast/animation/minimap state
		// from the restored persistent maps. Reapply serialized scalar and
		// actor-frame state afterwards because doChangeRoom() resets the same
		// DOS globals while entering the room.
		applyLoadedScalarState();
		if (_dialogClickGate == 0 && rebuiltDialogClickGate != 0) {
			// Saves made via the status screen before RoomBackup carried these
			// fields could store the gameplay room with the status screen's
			// zeroed minimap gate. Keep the room-entry rebuilt gate so loading
			// an existing save matches the state reached after a room restart.
			_dialogCursor0 = rebuiltDialogCursor0;
			_dialogCursor1 = rebuiltDialogCursor1;
			_dialogClickGate = rebuiltDialogClickGate;
			debugC(2, kDebugLevelFlow,
				   "restore kept rebuilt minimap gate=%u at (%u,%u)",
				   _dialogClickGate, _dialogCursor0, _dialogCursor1);
		}
		_actorFrameZero = loadedActorFrameZero;
		_actorFrameTable = loadedActorFrameTable;
		_actorFrameCount = MIN<uint16>(loadedActorFrameCount, uint16(_actorFrameTable.size()));
		setProtagonist(protagonistId);
		_loadedBackdropId = loadedBackdropId;
		if (_engine && _engine->graphics())
			_engine->graphics()->setFullscreen(screenFullscreen != 0);
	}

	uint16 actorCount = _resources->mainDat()->actorsCount();
	if (_blockProgram)
		actorCount += _blockProgram->actorsCount();
	s.syncAsUint16LE(actorCount);
	for (uint16 id = 1; id <= actorCount; ++id) {
		uint16 storedId = id;
		s.syncAsUint16LE(storedId);
		Actor *actor = getActor(storedId);
		if (!actor)
			error("invalid actor %u while synchronizing Interspective save", storedId);
		actor->synchronize(s);
	}

	syncUint16Array(s, _activeActorIds);
	if (s.isLoading() && _activeActorIds.size() != kActiveActorTableSlots)
		_activeActorIds.resize(kActiveActorTableSlots);

	syncQueuedRuns(s);

	if (s.isLoading()) {
		registerCurrentRoomActors();
		refreshCurrentRoomActorFrames();
	}
}

/* counting starts with 1 */
Actor *Logic::getActor(uint16 id) const {
	if (id == 0)
		return nullptr;
	id--;
	if (id < _resources->mainDat()->actorsCount())
		return _resources->mainDat()->actor(id);
	id -= _resources->mainDat()->actorsCount();
	return _blockProgram ? _blockProgram->actor(id) : nullptr;
}

uint16 Logic::actorGlobalId(const Actor *actor) const {
	if (!actor)
		return 0;
	if (!_resources || !_resources->mainDat())
		return actor->id();

	const uint16 mainActorCount = _resources->mainDat()->actorsCount();
	for (uint16 i = 0; i < mainActorCount; ++i) {
		if (_resources->mainDat()->actor(i) == actor)
			return i + 1;
	}

	if (_blockProgram) {
		const uint16 blockActorCount = _blockProgram->actorsCount();
		for (uint16 i = 0; i < blockActorCount; ++i) {
			if (_blockProgram->actor(i) == actor)
				return mainActorCount + i + 1;
		}
	}

	return actor->id();
}

void Logic::setSkipPoint(const CodePointer &p) {
	_skipPoint = p;
	_escBreakPending = false;
}

void Logic::requestSkipCutscene() {
	if (!_skipPoint.isEmpty())
		_escBreakPending = true;
}

bool Logic::handleEscDuringScript() {
	if (!_escBreakPending || _skipPoint.isEmpty())
		return false;

	// DOS HandleEscDuringScript @ 1000:2bd9 is called from the main
	// loop after active script dispatch. Input/fade code only latches
	// ESC, so the target script must run here, not reentrantly inside
	// the fade opcode that latched the keypress.
	skipCutscene();
	return true;
}

static void logicApplyFormattedTextLimit9bcc(uint16 limit, uint16 &height, uint16 &rows) {
	if (int16(rows) > int16(limit)) {
		const uint8 divisor = uint8(limit & 0xff);
		if (divisor != 0) {
			const uint16 pages = uint16(rows / divisor + 1);
			height = uint16(height / pages + 2);
		}
	}
	rows = limit;
}

static uint16 logicSpeechTicksForText(const Common::String &text, uint16 maxLines) {
	Common::String normalized;
	for (uint i = 0; i < text.size(); ++i)
		normalized += char(text[i] == '\n' ? '\r' : text[i]);
	Logic::FormattedBubble fb = Log.formatBubbleText(reinterpret_cast<const byte *>(normalized.c_str()));
	uint16 height = fb.totalHeight;
	uint16 rows = fb.rowCount;
	if (maxLines != 0)
		logicApplyFormattedTextLimit9bcc(maxLines, height, rows);
	return uint8(height & 0xff);
}

static Common::Array<Common::String> logicPaginateSpeechText(const Common::String &text, uint16 maxLines) {
	Common::Array<Common::String> pages;
	if (maxLines == 0) {
		pages.push_back(text);
		return pages;
	}

	Common::String page;
	uint16 completedLines = 0;
	for (uint i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		if (ch == '\n' || ch == '\r') {
			if (completedLines + 1 >= maxLines) {
				pages.push_back(page);
				page.clear();
				completedLines = 0;
			} else {
				page += '\n';
				++completedLines;
			}
			continue;
		}
		page += ch;
	}

	if (!page.empty())
		pages.push_back(page);
	if (pages.empty())
		pages.push_back(text);
	return pages;
}

Logic::SpeechSlot *Logic::findFreeSpeechSlot() {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		if (_speechSlots[i].framesLeft == 0)
			return &_speechSlots[i];
	}
	return nullptr;
}

const Logic::SpeechSlot *Logic::findSpeechSlotForOwner(uint16 owner) const {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		const SpeechSlot &slot = _speechSlots[i];
		if (slot.framesLeft != 0 && slot.owner == owner)
			return &slot;
	}
	return nullptr;
}

Logic::SpeechSlot *Logic::findSpeechSlotForOwner(uint16 owner) {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		if (slot.framesLeft != 0 && slot.owner == owner)
			return &slot;
	}
	return nullptr;
}

void Logic::clearSpeechSlot(SpeechSlot &slot) {
	slot.callbacks.clear();
	slot = SpeechSlot();
}

void Logic::startSpeechSlotPage(SpeechSlot &slot, uint page) {
	if (page >= slot.pages.size()) {
		clearSpeechSlot(slot);
		return;
	}

	slot.pageIndex = page;
	slot.text = slot.pages[page];
	slot.framesLeft = slot.framesTotal;
	slot.active = 1;
}

bool Logic::initSpeechSlot(SpeechSlot &slot, const Common::String &text, uint16 maxLines) {
	slot.maxLines = maxLines;
	slot.framesTotal = uint8(logicSpeechTicksForText(text, maxLines) & 0xff);
	slot.pages = logicPaginateSpeechText(text, maxLines);
	if (slot.pages.empty())
		return false;
	startSpeechSlotPage(slot, 0);
	return slot.framesLeft != 0;
}

bool Logic::allocActorSpeech(Actor *actor, const Common::String &text, uint16 maxLines) {
	if (!actor)
		return false;
	return allocActorSpeechAt(actor, text, actor->getSpeechPosition(), maxLines);
}

bool Logic::allocActorSpeechAt(Actor *actor, const Common::String &text, Common::Point pos, uint16 maxLines) {
	if (!actor)
		return false;

	SpeechSlot *slot = findFreeSpeechSlot();
	if (!slot) {
		setPendingError(0x1d);
		return false;
	}

	clearSpeechSlot(*slot);
	slot->type = 0;
	slot->owner = actorGlobalId(actor);
	slot->refX = uint16(pos.x);
	slot->refY = uint16(pos.y);
	slot->color = actor->speechColor();
	debugC(1, kDebugLevelActor, "alloc speech slot owner=%u at %d:%d maxLines=%u text=\"%s\"",
		   slot->owner, pos.x, pos.y, maxLines, text.c_str());
	if (!initSpeechSlot(*slot, text, maxLines))
		clearSpeechSlot(*slot);
	return slot->framesLeft != 0;
}

bool Logic::allocActorSpeechForPostMove(Actor *actor, const Common::String &text, uint16 maxLines) {
	if (!actor)
		return false;
	const uint16 owner = actorGlobalId(actor);
	const bool allocated = allocActorSpeech(actor, text, maxLines);
	if (SpeechSlot *slot = findSpeechSlotForOwner(owner))
		slot->active = 0;
	return allocated;
}

void Logic::activateActorSpeechAfterPostMove(Actor *actor) {
	if (!actor)
		return;
	SpeechSlot *slot = findSpeechSlotForOwner(actorGlobalId(actor));
	if (!slot)
		return;
	const Common::Point pos = actor->getSpeechPosition();
	slot->active = 1;
	slot->refX = uint16(pos.x);
	slot->refY = uint16(pos.y);
	setLogicDirty();
}

bool Logic::allocNarratorSpeech(const byte *text, uint16 length, uint16 x, uint16 y,
								byte color, uint16 maxLines, uint8 type) {
	if (!text || length == 0)
		return false;

	SpeechSlot *slot = findFreeSpeechSlot();
	if (!slot) {
		setPendingError(0x1d);
		return false;
	}

	clearSpeechSlot(*slot);
	slot->type = type;
	slot->owner = uint16(_currentRoom);
	slot->refX = x;
	slot->refY = y;
	slot->color = color;
	Common::String copied(reinterpret_cast<const char *>(text), length);
	debugC(1, kDebugLevelGraphics, "alloc narrator speech slot type=%u ownerRoom=%u at %u:%u color=%u maxLines=%u text=\"%s\"",
		   type, uint16(_currentRoom), x, y, color, maxLines, copied.c_str());
	_uiTextSpeechSlot = uint16(slot - &_speechSlots[0]);
	if (!initSpeechSlot(*slot, copied, maxLines))
		clearSpeechSlot(*slot);
	return slot->framesLeft != 0;
}

bool Logic::speechSlotActiveForOwner(uint16 owner) const {
	return findSpeechSlotForOwner(owner) != nullptr;
}

bool Logic::anySpeechSlotActive() const {
	for (uint i = 0; i < _speechSlots.size(); ++i)
		if (_speechSlots[i].framesLeft != 0 && _speechSlots[i].owner != 0xffff)
			return true;
	return false;
}

bool Logic::uiTextSpeechSlotActive() const {
	if (_uiTextSpeechSlot >= _speechSlots.size())
		return false;
	return _speechSlots[_uiTextSpeechSlot].framesLeft != 0;
}

void Logic::stashUiTextSpeechSlotForOwner(uint16 owner) {
	if (SpeechSlot *slot = findSpeechSlotForOwner(owner))
		_uiTextSpeechSlot = uint16(slot - &_speechSlots[0]);
}

const Common::String &Logic::speechTextForOwner(uint16 owner) const {
	static const Common::String empty;
	const SpeechSlot *slot = findSpeechSlotForOwner(owner);
	return slot ? slot->text : empty;
}

void Logic::clearSpeechForOwner(uint16 owner) {
	if (SpeechSlot *slot = findSpeechSlotForOwner(owner))
		clearSpeechSlot(*slot);
}

void Logic::queueSpeechSlotCallbackForOwner(uint16 owner, const CodePointer &cp) {
	if (SpeechSlot *slot = findSpeechSlotForOwner(owner)) {
		slot->callbacks.push(SpeechSlotCallback(cp, opcodeMode(), true));
		return;
	}
	runLaterWithMode(cp, opcodeMode());
}

void Logic::queueSpeechSlotCallbackForAnyActive(const CodePointer &cp) {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		if (slot.framesLeft != 0 && slot.owner != 0xffff) {
			slot.callbacks.push(SpeechSlotCallback(cp, opcodeMode(), true));
			return;
		}
	}
	runLaterWithMode(cp, opcodeMode());
}

void Logic::queueUiTextSpeechSlotCallback(const CodePointer &cp) {
	if (_uiTextSpeechSlot < _speechSlots.size()) {
		SpeechSlot &slot = _speechSlots[_uiTextSpeechSlot];
		if (slot.framesLeft != 0) {
			slot.callbacks.push(SpeechSlotCallback(cp, opcodeMode(), true));
			return;
		}
	}
	runLaterWithMode(cp, opcodeMode());
}

bool Logic::backupSpeechSlotForOwner(uint16 owner, Common::String &text) {
	SpeechSlot *slot = findSpeechSlotForOwner(owner);
	if (!slot)
		return false;
	text = slot->text;
	// DOS Op_97 copies the main-character slot to a backup area and marks
	// the live slot owner as 0xffff. RecycleStaleSpeechSlots later reclaims it.
	slot->owner = 0xffff;
	return true;
}

bool Logic::restoreActorSpeechSlot(Actor *actor, const Common::String &text) {
	return allocActorSpeech(actor, text, 0);
}

void Logic::recycleStaleSpeechSlots() {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		if (slot.owner == 0xffff) {
			slot.owner = 0;
			slot.framesLeft = 0;
			slot.active = 0;
			slot.callbacks.clear();
		}
	}
}

bool Logic::speechSlotHasCallbackMode(uint16 mode) const {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		Common::Queue<SpeechSlotCallback> callbacks = _speechSlots[i].callbacks;
		while (!callbacks.empty()) {
			const SpeechSlotCallback cb = callbacks.pop();
			if (cb.hasMode && cb.mode == mode)
				return true;
		}
	}
	return false;
}

void Logic::dropSpeechSlotCallbackMode(uint16 mode) {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		Common::Queue<SpeechSlotCallback> kept;
		while (!slot.callbacks.empty()) {
			const SpeechSlotCallback cb = slot.callbacks.pop();
			if (!cb.hasMode || cb.mode != mode)
				kept.push(cb);
		}
		slot.callbacks = kept;
	}
}

void Logic::finishSpeechSlot(SpeechSlot &slot) {
	if (slot.pageIndex + 1 < slot.pages.size()) {
		startSpeechSlotPage(slot, slot.pageIndex + 1);
		return;
	}

	Common::Queue<SpeechSlotCallback> callbacks = slot.callbacks;
	clearSpeechSlot(slot);
	while (!callbacks.empty()) {
		SpeechSlotCallback cb = callbacks.pop();
		if (cb.hasMode)
			runLaterWithMode(cb.callback, cb.mode);
		else
			runLater(cb.callback);
	}
	setLogicDirty();
}

void Logic::paintSpeechSlots(Graphics *g) {
	// RunStatusScreenLoop @ 1000:7695 services only status subtitles
	// (UpdateSubtitleText). DOS does not tick the six gameplay speech slots
	// until RestoreRoomFromBackup returns to the main loop.
	if (!g || escBreakPending() || _inStatusMode)
		return;

	bool speechSkipAvailable = _speechSkipInput;
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		if (slot.framesLeft == 0 || slot.active == 0)
			continue;

		const uint16 owner = slot.owner;
		const int16 left = int16(slot.refX) - cameraX();
		const int16 top = int16(slot.refY) - cameraY();
		bool shouldDraw = false;
		Graphics::SpeechBubbleMode mode = Graphics::kSpeechBubbleAuto;

		if (slot.type == Graphics::kSpeechBubbleType1 || slot.type == Graphics::kSpeechBubbleType2) {
			if (owner == uint16(_currentRoom)) {
				shouldDraw = true;
				mode = slot.type == Graphics::kSpeechBubbleType2
						   ? Graphics::kSpeechBubbleType2
						   : Graphics::kSpeechBubbleType1;
			}
		} else {
			if (owner == 0xffff)
				continue;
			if (owner == _protagonistId && scrollChanged())
				continue;
			Actor *actor = getActor(owner);
			if (actor && actor->room() == uint16(_currentRoom)) {
				shouldDraw = true;
				mode = Graphics::kSpeechBubbleAuto;
			}
		}

		if (speechSkipAvailable && slot.framesLeft <= uint8(slot.framesTotal - 2)) {
			// UpdateSpeechBubbles @ 1000:9992 consumes right-click after
			// the first two frames and forces the slot to expire.
			speechSkipAvailable = false;
			slot.framesLeft = 1;
		}

		if (shouldDraw) {
			Sprite bubble;
			bubble._hotPoint = Common::Point(0, 0);
			Common::Rect rect = g->paintSpeechInBubble(Common::Point(left, top), slot.color,
													   reinterpret_cast<const byte *>(slot.text.c_str()), &bubble, mode, true, slot.maxLines);
			g->paint(&bubble, Common::Point(rect.left, rect.top),
					 Graphics::kPaintSemiTransparent | Graphics::kPaintPositionIsTop);
		}

		if (slot.framesLeft != 0)
			--slot.framesLeft;
		if (slot.framesLeft == 0)
			finishSpeechSlot(slot);
	}
}

void Logic::resetSpeechSlots() {
	// DOS ResetSpeechSlots @ 1000:9951 only zeros each slot's frames-left
	// byte. C++ also drops callbacks owned by those slots because the ESC
	// target script replaces the old wait path.
	for (uint i = 0; i < _speechSlots.size(); ++i)
		clearSpeechSlot(_speechSlots[i]);
	if (_engine && _engine->graphics())
		_engine->graphics()->clearSpeech();
}

void Logic::resetQueuedRunMode(uint16 mode) {
	for (Common::List<DelayedRun>::iterator it = _queued.begin(); it != _queued.end();) {
		if (!it->canceled && it->hasRunMode && it->runMode == mode && it->deferredMode == 0)
			it = _queued.erase(it);
		else
			++it;
	}

	if (_resources && _resources->mainDat()) {
		uint16 actorCount = _resources->mainDat()->actorsCount();
		if (_blockProgram)
			actorCount += _blockProgram->actorsCount();
		for (uint16 id = 1; id <= actorCount; ++id)
			if (Actor *actor = getActor(id))
				actor->dropRoomScriptWaitMode(mode);
	}

	dropSpeechSlotCallbackMode(mode);
}

void Logic::cancelDeferredScriptsForInterpreter(Interpreter *interpreter) {
	if (!interpreter)
		return;

	// DOS CancelDeferredScriptsForSlot @ 1000:3226 runs on the
	// restart-room path after EnsureRoomLoaded and before RunLocationScript.
	// It scans the 8 deferred-script entries, matches the current block
	// segment (CS:[0x35]) against each entry's segment word, calls
	// ResetRoomScriptSlot(mode), then clears the deferred entry.
	for (Common::List<DelayedRun>::iterator it = _queued.begin(); it != _queued.end();) {
		if (!it->canceled && it->deferredMode != 0 && it->code.interpreter() == interpreter) {
			debugC(3, kDebugLevelScript, "cancel deferred for room restart %s mode 0x%02x",
				   +it->code, it->deferredMode);
			resetQueuedRunMode(it->deferredMode);
			it = _queued.erase(it);
		} else {
			++it;
		}
	}
}

void Logic::cancelSpeechSlotCallbacksForInterpreter(Interpreter *interpreter) {
	if (!interpreter)
		return;

	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		Common::Queue<SpeechSlotCallback> kept;
		while (!slot.callbacks.empty()) {
			SpeechSlotCallback cb = slot.callbacks.pop();
			if (cb.callback.interpreter() != interpreter)
				kept.push(cb);
		}
		slot.callbacks = kept;
	}
}

bool Logic::redirectDeferredMode(uint16 mode, const CodePointer &target) {
	for (Common::List<DelayedRun>::iterator it = _queued.begin(); it != _queued.end(); ++it) {
		if (!it->canceled && it->deferredMode == mode) {
			debugC(2, kDebugLevelScript, "redirect deferred mode 0x%02x to %s", mode, +target);
			it->code = target;
			it->delay = 0;
			it->queuedTick = frameTicks();
			return true;
		}
	}
	return false;
}

void Logic::skipCutscene() {
	if (_skipPoint.isEmpty())
		return;

	const CodePointer target = _skipPoint;
	const uint16 proc = _escBreakProc;
	resetSpeechSlots();
	resetQueuedRunMode(proc);
	if (Actor *protag = protagonist())
		protag->setAttentionNeeded(true);
	else
		setPendingError(0x17);
	clearEscBreakPoint();

	debugC(2, kDebugLevelScript, ">>>running animation skip code");
	if (proc >= 0x0b) {
		if (!redirectDeferredMode(proc, target))
			runLater(target);
	} else {
		target.run(static_cast<OpcodeMode>(proc));
	}
	debugC(2, kDebugLevelScript, "<<<finished animation skip code");
}

Animation *Logic::animation(uint16 offset) const {
	foreach_const(Animation *, _animations) if ((*it)->baseOffset() == offset) return (*it);

	return 0;
}

} // End of namespace Interspective
