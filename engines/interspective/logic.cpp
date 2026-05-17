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

#include "common/endian.h"
#include "common/serializer.h"
#include "common/util.h"

#include "interspective/logic.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/program.h"
#include "interspective/animation.h"
#include "interspective/resources.h"
#include "interspective/room.h"
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
static const uint16 kBubbleLineHeight = 12;  // matches Graphics::kLineHeight

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

static inline uint8 dosWordByte(uint16 value, uint8 baseOff, uint8 off) {
	return uint8((value >> ((off - baseOff) * 8)) & 0xff);
}

static inline uint16 dosWordWithByte(uint16 oldValue, uint8 baseOff, uint8 off, uint8 value) {
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

static void setActorCallbackWordLikeDos(Actor *actor, uint16 callback) {
	if (actor)
		actor->setDosFieldWord(0x69, callback);
}

static void moveActorToTargetFrameLikeDos(Logic *logic, Actor *actor, uint16 frame) {
	if (!logic || !actor)
		return;
	setActorCallbackWordLikeDos(actor, 0);
	if (actor == logic->protagonist()) {
		logic->setBreakInner(true);
		logic->clearPostMoveCallback();
		actor->stopSpeaking();
		logic->setPostMoveTargetFrameMirror(uint8(frame));
		if (actor->room() == logic->currentRoom() && actor->frameId() != 0)
			actor->setRawTargetFrame(uint8(frame));
		actor->moveTo(frame);
		if (actor->dosField(0x6f) != 0)
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
			uint16 key = 0, value = 0;
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
	setCursorMode(_defaultCursorMode);
	_actorFrameTable.clear();
	_actorFrameCount = 0;
	_walkSpeedFlag = 0;
	_postMoveTargetFrameMirror = 0;
	_speechSkipInput = false;
	_loadedBackdropId = 0;
}

bool Logic::speechWouldConsumeRightClickLikeDos() const {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		const SpeechSlot &slot = _speechSlots[i];
		if (slot.framesLeft == 0 || slot.active == 0)
			continue;
		if (slot.framesTotal >= 2 && slot.framesLeft <= uint8(slot.framesTotal - 2))
			return true;
	}
	return false;
}

void Logic::cycleCursorModeByRightClickLikeDos() {
	// CheckDoubleClickReset @ 1000:b92c: when not in status mode, not
	// no-step, not dragging, and the locked button byte is 2, cycle
	// through the verb cursor modes and clear step-pending via SetCursorMode.
	if (_inStatusMode || _noStep || _cursorMode == 0x20 || !_roomActive || canSkipCutscene())
		return;
	if (speechWouldConsumeRightClickLikeDos())
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
	if (handleEscDuringScript())
		return;

	runQueued();
	if (handleEscDuringScript())
		return;
}

void Logic::runRoomLoop() {
	if (_roomLoop.get()) {
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
	if (loop == 0 || hasQueuedRunMode(kCodeGlobalRoomLoop))
		return;
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
	tickMotionText();
	updateScrollPosition();
}

void Logic::callAnimations() {
	if (!_animations.empty())
		debugC(4, kDebugLevelFlow | kDebugLevelAnimation, "running animations");
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end();) {
		if (_inStatusMode && (*it)->isActor()
				&& static_cast<Actor *>(*it)->room() != _currentRoom) {
			++it;
			continue;
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

uint16 Logic::dosRecordField(uint8 selector, uint16 id, uint8 off, uint8 size) const {
	uint8 lo = 0;
	uint8 hi = 0;

	switch (selector) {
	case 1: {
		Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0;
		if (!exit)
			return 0;
		if (off == 0 || off == 1)
			lo = dosWordByte(exit->room(), 0, off);
		else if (off == 2 || off == 3)
			lo = dosWordByte(uint16(exit->position().x), 2, off);
		else if (off == 4 || off == 5)
			lo = dosWordByte(uint16(exit->position().y), 4, off);
		else if (off == 6 || off == 7)
			lo = dosWordByte(exit->spriteField(), 6, off);
		else if (off == 0x0b)
			lo = exit->zIndex();
		else
			lo = exitField(id, off);
		if (size == 1)
			return lo;
		hi = dosRecordField(selector, id, uint8(off + 1), 1);
		return uint16(lo) | (uint16(hi) << 8);
	}
	case 2:
		if (off == 0 || off == 1)
			lo = dosWordByte(getObjectRoom(id), 0, off);
		else if (off == 2 || off == 3)
			lo = dosWordByte(uint16(getObjectPosX(id)), 2, off);
		else if (off == 4 || off == 5)
			lo = dosWordByte(uint16(getObjectPosY(id)), 4, off);
		else
			lo = objectField(id, off);
		if (size == 1)
			return lo;
		hi = dosRecordField(selector, id, uint8(off + 1), 1);
		return uint16(lo) | (uint16(hi) << 8);
	case 3: {
		Actor *actor = getActor(id);
		if (!actor)
			return 0;
		if (off == Actor::kOffsetLeft || off == Actor::kOffsetLeft + 1)
			lo = dosWordByte(uint16(actor->position().x), Actor::kOffsetLeft, off);
		else if (off == Actor::kOffsetTop || off == Actor::kOffsetTop + 1)
			lo = dosWordByte(uint16(actor->position().y), Actor::kOffsetTop, off);
		else if (off == Actor::kOffsetMainSprite || off == Actor::kOffsetMainSprite + 1)
			lo = dosWordByte(actor->mainSpriteId(), Actor::kOffsetMainSprite, off);
		else if (off == Actor::kOffsetTicksLeft || off == Actor::kOffsetTicksLeft + 1)
			lo = dosWordByte(uint16(actor->ticksLeft()), Actor::kOffsetTicksLeft, off);
		else if (off == Actor::kOffsetInterval)
			lo = actor->interval();
		else if (off == 0x5d || off == 0x5e)
			lo = dosWordByte(actor->actorCallbackSeg(), 0x5d, off);
		else if (off == 0x5f || off == 0x60)
			lo = dosWordByte(actor->actorCallbackOff(), 0x5f, off);
		else if (off == Actor::kOffsetRoom || off == Actor::kOffsetRoom + 1)
			lo = dosWordByte(actor->room(), Actor::kOffsetRoom, off);
		else if (off == 0x61)
			lo = uint8(actor->frameId());
		else if (off == 0x62)
			lo = uint8(actor->targetFrameId());
		else if (off == 0x65 && actor->isMoving())
			lo = 1;
		else
			lo = actor->dosField(off);
		if (size == 1)
			return lo;
		hi = dosRecordField(selector, id, uint8(off + 1), 1);
		return uint16(lo) | (uint16(hi) << 8);
	}
	default:
		const_cast<Logic *>(this)->setPendingError(0x03);
		return 0;
	}
}

void Logic::setDosRecordField(uint8 selector, uint16 id, uint8 off, uint8 size, uint16 value) {
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
				exit->setRoom(dosWordWithByte(exit->room(), 0, byteOff, byteValue));
			else if (byteOff == 2 || byteOff == 3) {
				Common::Point p = exit->position();
				p.x = int16(dosWordWithByte(uint16(p.x), 2, byteOff, byteValue));
				exit->setPosition(p);
			} else if (byteOff == 4 || byteOff == 5) {
				Common::Point p = exit->position();
				p.y = int16(dosWordWithByte(uint16(p.y), 4, byteOff, byteValue));
				exit->setPosition(p);
			} else if (byteOff == 6 || byteOff == 7)
				exit->setSpriteField(dosWordWithByte(exit->spriteField(), 6, byteOff, byteValue));
			else if (byteOff == 0x0b)
				exit->setZIndex(byteValue);
			else
				setExitField(id, byteOff, byteValue);
			break;
		}
		case 2:
			if (byteOff == 0 || byteOff == 1)
				setObjectRoom(id, dosWordWithByte(getObjectRoom(id), 0, byteOff, byteValue));
			else if (byteOff == 2 || byteOff == 3) {
				const int16 x = int16(dosWordWithByte(uint16(getObjectPosX(id)), 2, byteOff, byteValue));
				setObjectPosition(id, x, getObjectPosY(id));
			} else if (byteOff == 4 || byteOff == 5) {
				const int16 y = int16(dosWordWithByte(uint16(getObjectPosY(id)), 4, byteOff, byteValue));
				setObjectPosition(id, getObjectPosX(id), y);
			} else
				setObjectField(id, byteOff, byteValue);
			break;
		case 3: {
			Actor *actor = getActor(id);
			if (!actor)
				return;
			actor->setDosField(byteOff, byteValue);
			if (byteOff == Actor::kOffsetLeft || byteOff == Actor::kOffsetLeft + 1) {
				Common::Point p = actor->position();
				p.x = int16(dosWordWithByte(uint16(p.x), Actor::kOffsetLeft, byteOff, byteValue));
				actor->setRawPosition(p);
			} else if (byteOff == Actor::kOffsetTop || byteOff == Actor::kOffsetTop + 1) {
				Common::Point p = actor->position();
				p.y = int16(dosWordWithByte(uint16(p.y), Actor::kOffsetTop, byteOff, byteValue));
				actor->setRawPosition(p);
			} else if (byteOff == Actor::kOffsetMainSprite || byteOff == Actor::kOffsetMainSprite + 1)
				actor->setRawMainSprite(dosWordWithByte(actor->mainSpriteId(), Actor::kOffsetMainSprite, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetTicksLeft || byteOff == Actor::kOffsetTicksLeft + 1)
				actor->setRawTicksLeft(dosWordWithByte(uint16(actor->ticksLeft()), Actor::kOffsetTicksLeft, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetInterval)
				actor->setRawInterval(byteValue);
			else if (byteOff == 0x5d || byteOff == 0x5e)
				actor->setActorCallback(dosWordWithByte(actor->actorCallbackSeg(), 0x5d, byteOff, byteValue), actor->actorCallbackOff());
			else if (byteOff == 0x5f || byteOff == 0x60)
				actor->setActorCallback(actor->actorCallbackSeg(), dosWordWithByte(actor->actorCallbackOff(), 0x5f, byteOff, byteValue));
			else if (byteOff == Actor::kOffsetRoom || byteOff == Actor::kOffsetRoom + 1)
				actor->forceRoom(dosWordWithByte(actor->room(), Actor::kOffsetRoom, byteOff, byteValue));
			else if (byteOff == 0x61)
				actor->setRawFrame(byteValue);
			else if (byteOff == 0x62)
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
			const uint8 actorWidth = protag->dosField(0x17);
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
			const uint8 actorHeight = protag->dosField(0x18);
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

void Logic::changeRoom(uint16 newRoom) {
	// DOS ApplyChangeRoomTransition restores g_cursor_mode from DS:0x667a
	// after the Op_cc fullscreen gate, before the restart-room pass clears
	// the fullscreen-gate flag.
	if (_fullscreenGateActive)
		setCursorMode(_defaultCursorMode);

	// just schedule it, we'll execute on next tick
	_nextRoom = newRoom;
	_forceRoomRestart = false;

	if (_currentRoom == 0xffff)
		doChangeRoom(); // except if it's the first one
}

void Logic::restartRoomLikeDos() {
	if (_currentRoom == 0xffff)
		return;
	_nextRoom = _currentRoom;
	_forceRoomRestart = true;
}

void Logic::doChangeRoom() {
	assert (_nextRoom);

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
	recycleStaleSpeechSlotsLikeDos();
	if (_engine && _engine->graphics())
		_engine->graphics()->clearSpeech();

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
			(_savedScene && _savedScene->blockProgram == _blockProgram)
			|| (_roomBackup.valid && _roomBackup.blockProgram == _blockProgram);
		if (oldProgram && !oldProgramPreserved) {
			const byte *lo = oldProgram->codeBegin();
			const byte *hi = oldProgram->codeEnd();
			foreach(Animation *, _animations)
				(*it)->dropBaseIfIn(lo, hi);
		}

		_currentBlock = newBlock;
		_blockProgram = Common::SharedPtr<Program>(_resources->loadCodeBlock(newBlock));
		// DOS keeps the AddExitToList dynamic-object list in global
		// state; inventory objects registered before a block change must
		// survive into the playable room.

		char buf[100];
		snprintf(buf, 100, "block %d code", newBlock);

		_blockInterpreter = Common::SharedPtr<Interpreter>(new Interpreter(this, _blockProgram->base(), buf));
		_blockProgram->loadActors(_blockInterpreter.get());
		_blockProgram->loadExits(_blockInterpreter.get());

		debugC(2, kDebugLevelScript, ">>>running block entry code for block %d", newBlock);
		_blockInterpreter->run(_blockProgram->begin(), kCodeNewBlock);
		debugC(2, kDebugLevelScript, "<<<finished block entry code for block %d", newBlock);
	}

	cancelDeferredScriptsForInterpreter(_blockInterpreter.get());

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

	// (iter-27's unconditional `_protagonist->forceRoom(_currentRoom)`
	// removed iter-36 — it caused the protagonist sprite to be rendered
	// on top of the title-card logo and any other "no protagonist" room
	// because we were registering them in EVERY room, ignoring the
	// data-file room state. Replaced by Op_d6's boot-param substitution
	// path which seeds the protagonist's room ONLY when the boot-param
	// shortcut fires — matching what the skipped intro animation would
	// have done.)

	// DOS DrawActors @ 1000:64f4 runs after the room script. For each
	// active actor in the current room it calls SetActorPosition only when
	// actor.field+0x61 is nonzero, then starts target movement when
	// field+0x62 differs. Actors with frame 0 keep their raw x/y fields.
	foreach(Animation *, _animations)
		if ((*it)->isActor()) {
			Actor * const ac = static_cast<Actor *>(*it);
			if (ac->room() == _currentRoom && ac->frameId() != 0) {
				const uint16 frame = ac->frameId();
				const uint16 target = ac->targetFrameId();
				ac->setFrame(frame);
				if (target != frame)
					ac->moveTo(target);
			}
		}
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
			const bool preDeferredPhase = _opcodeMode == kCodeInitial
			                           || _opcodeMode == kCodeNewRoom
			                           || _opcodeMode == kCodeNewBlock;
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

CodePointer Logic::switchToSceneLikeDos(uint16 sceneId, const CodePointer &resumePC) {
	// Op_38 calls LoadRoomLevelHeader, not the normal room-restart path.
	// Scene scripts live in the second IUC_PROG.DAT entry table
	// (main footer count0 + scene id) and execute from offset 2 in a
	// temporary code segment. The current room/location is unchanged.
	saveSceneFrame(resumePC);

	char buf[64];
	snprintf(buf, sizeof(buf), "scene %u code", sceneId);
	_sceneProgramKeepAlive = Common::SharedPtr<Program>(_resources->loadSceneCodeBlock(sceneId));
	_sceneInterpreterKeepAlive = Common::SharedPtr<Interpreter>(
		new Interpreter(this, _sceneProgramKeepAlive->base(), buf));

	_currentBlock = uint16(_resources->mainDat()->progEntriesCount0() + sceneId);
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

void Logic::backupRoomForStatusLikeDos() {
	// RunStatusScreenLoop @ 1000:7695 saves these fields into DS:0x5ed5..0x5ee8,
	// then snapshots cast, actor, and script state before switching to room 999.
	_roomBackup.valid = true;
	_roomBackup.currentBlock = _currentBlock;
	_roomBackup.currentRoom = _currentRoom;
	_roomBackup.loadedBackdropId = _loadedBackdropId;
	_roomBackup.blockProgram = _blockProgram;
	_roomBackup.blockInterpreter = _blockInterpreter;
	_roomBackup.room = _room;
	_roomBackup.animations = _animations;
	_roomBackup.castTable = _castTable;
	_roomBackup.queued = _queued;
	_roomBackup.cameraX = _cameraX;
	_roomBackup.cameraY = _cameraY;
	_roomBackup.scrollChanged = _scrollChanged;
	_roomBackup.cursorMode = _cursorMode;
	_roomBackup.fullscreen = _engine && _engine->graphics()
		? _engine->graphics()->screenHeight() == 200
		: !_roomActive;
	_roomBackup.roomActive = _roomActive;
	_roomBackup.noStep = _noStep;
	_roomBackup.zones = _zones;
	_roomBackup.collisionZones = _collisionZones;
	_roomBackup.zonesB = _zonesB;
	_roomBackup.walkboxes = _walkboxes;
	_roomBackup.actorFrameZero = _actorFrameZero;
	_roomBackup.actorFrameTable = _actorFrameTable;
	_roomBackup.actorFrameCount = _actorFrameCount;
	_roomBackup.overlayQueue = _overlayQueue;
	_roomBackup.animList = _animList;
	_roomBackup.drawCommands = _drawCommands;
	_roomBackup.drawCommandCount = _drawCommandCount;
	_roomBackup.postMoveCallback = _postMoveCallback;
	_roomBackup.postMoveTargetFrameMirror = _postMoveTargetFrameMirror;
}

void Logic::enterStatusScreenLoopLikeDos() {
	// DispatchVerbAction @ 1000:b9a0 sends hit-region 2 to
	// RunStatusScreenLoop @ 1000:7695. DOS snapshots the current room state,
	// switches to special room 999, then lets that room's scripts drive the
	// visible status/save/load surface until region 2 restores the backup.
	if (_inStatusMode || _enteringStatusScreen)
		return;

	backupRoomForStatusLikeDos();
	// The DOS status loop saves the deferred queue/room-script slots, then
	// services only status-room mode 7 until RestoreScriptStateBackup. Keep the
	// saved game-room queue out of the live C++ dispatcher while room 999 is
	// active; restoreRoomFromBackupLikeDos() reinstates it.
	_queued.clear();
	_runningQueued = 0;
	_runningQueuedMode = 0;
	castTableClearAll();
	_cameraX = 0;
	_cameraY = 0;
	_scrollChanged = false;
	_zones.clear();
	_inStatusMode = true;
	_noStep = false;
	_roomActive = true;
	_logicDirty = true;
	_enteringStatusScreen = true;
	_nextRoom = 999;
	_forceRoomRestart = true;
	if (_engine && _engine->graphics()) {
		_engine->graphics()->clearStatusScreenTextLikeDos();
		_engine->graphics()->clearBackdropLikeDos();
		_engine->graphics()->setFullscreen(false);
	}
}

void Logic::restoreRoomFromBackupLikeDos() {
	// RestoreRoomFromBackup @ 1000:7886:
	//   subtitle_frames_left = 0; restore DS:0x5ed5 backup fields; reload
	//   g_loaded_backdrop_id; RestoreCastBackup; RestoreActorTableBackup;
	//   RestoreScriptStateBackup; ResetRoomScriptSlot(7); ResetRoomScriptSlot(6);
	//   step_pending = 0; auto_close_timer = 1; change_room = logic_dirty = 1;
	//   in_status_mode = 0.
	if (_engine && _engine->graphics()) {
		_engine->graphics()->clearStatusScreenTextLikeDos();
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
		_castTable = _roomBackup.castTable;
		_queued = _roomBackup.queued;
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
		_drawCommands = _roomBackup.drawCommands;
		_drawCommandCount = _roomBackup.drawCommandCount;
		_postMoveCallback = _roomBackup.postMoveCallback;
		_postMoveTargetFrameMirror = _roomBackup.postMoveTargetFrameMirror;
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
	if (_protagonist->dosField(0x6f) != 0)
		return;
	if (_protagonist->dosField(0x65) == 0)
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
		// DOS @ 0x49df: clearCellBit(cellId) + MovePersonToActor(arg0)
		// + (arg1 != 0 → EnableObjectFlag1 = setCellBit(arg1)).
		disableObjectFlag1(cb.cellId);
		movePersonToActor(cb.arg0);
		if (cb.arg1 != 0)
			enableObjectFlag1(cb.arg1);
		break;
	case PostMoveCallback::kDisableEnableUnregister:
		// DOS @ 0x4a36: PUSH BX; DisableObjectFlag1(AX); POP BX;
		// EnableObjectFlag1(AX as left by DisableObjectFlag1); Op_8e.
		enableObjectFlag1(disableObjectFlag1ReturnAx(cb.cellId));
		clearDragInteractionLikeOp8e();
		break;
	case PostMoveCallback::kPlaceProtagonistAfterMove:
		// DOS @ 0x4376: place the protagonist in the destination
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
				restartRoomLikeDos();
		} else {
			restartRoomLikeDos();
		}
		setLogicDirty();
		setPaused();
		break;
	case PostMoveCallback::kPlaceObjectAfterHotspotMove:
		// DOS @ 0xc408: place the dragged object after the protagonist
		// reaches the hotspot approach frame, clear drag state, and dirty
		// the object pass. The original also fills a small five-entry
		// transient draw table; the persistent object record effects are
		// the room/position/cursor updates below.
		setObjectRoom(cb.cellId, uint16(_currentRoom));
		setObjectPosition(cb.cellId, int16(cb.arg0), int16(cb.arg1));
		setObjectField(cb.cellId, 0x0e, uint8(cb.arg1 & 0xff));
		setObjectField(cb.cellId, 0x0f, uint8(cb.arg1 >> 8));
		setDragTarget(0);
		setCursorMode(1);
		setLogicDirty();
		break;
	case PostMoveCallback::kActivateProtagonistSpeechAfterMove:
		// DOS @ 0x9be9: find the protagonist speech slot, mark slot+2
		// active again, and recompute the bubble reference point from the
		// actor's current sprite/size fields.
		activateActorSpeechAfterPostMoveLikeDos(_protagonist);
		break;
	case PostMoveCallback::kBeginDragAfterMove:
		// DOS @ 0x3297: BeginDrag_AfterRemoveExit with BX=0 after
		// HandleSecondaryClick walked the protagonist to a room object.
		beginDragAfterRemoveExitLikeDos(cb.arg0, false);
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
		const Common::Point cursor = _engine->graphics()->cursorPosition();
		setObjectPosition(id, int16(cursor.x + _cameraX), int16(cursor.y + _cameraY));
	}

	beginDragAfterRemoveExitLikeDos(id, objRoom == 0xffff);
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

void Logic::beginDragAfterRemoveExitLikeDos(uint16 id, bool removeExit) {
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
	clampObjectExitToScreenLikeDos(id);
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
	clampObjectExitToScreenLikeDos(id);

	const uint16 sprite = uint16(objectField(id, 6)) | (uint16(objectField(id, 7)) << 8);
	const SpriteInfo info = objectSpriteInfo(_resources, _blockProgram.get(), sprite);
	setObjectField(id, 0x10, uint8(info.width));
	setObjectField(id, 0x11, uint8(info.height));
	setLogicDirty();
}

void Logic::clampObjectExitToScreenLikeDos(uint16 id) {
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
		targetX = exit->hasSprite()
			? int16(exit->position().x + exit->area().width() / 2)
			: int16(exit->position().x);
		targetY = int16(exit->position().y);
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
		if (!_engine || !_engine->graphics())
			return false;
		const Common::Point cursor = _engine->graphics()->cursorPosition();
		targetX = int16(cursor.x + _cameraX);
		targetY = int16(cursor.y + _cameraY);
		break;
	}
	}

	const uint16 frame = _room->nearestFrameTo(targetX, targetY);
	if (frame == 0) {
		setPendingError(0x31);
		moveActorToTargetFrameLikeDos(this, walker, walker->frameId());
		return walker == _protagonist;
	}
	moveActorToTargetFrameLikeDos(this, walker, frame);
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
			e.active = 1;            // DOS stores caller seg; we use 1 (non-zero = active).
			e.id = id;
			e.x = x;
			e.y = y;
			e.interpreter = interpreter;
			e.animation = 0;
			// Re-init the bookkeeping per DOS Op_c3. Ghidra's CastEntry
			// layout is exact: raw[0]=bRect_w, raw[1]=bRect_h, and
			// raw[2 + N]=p_data[N].
			for (uint j = 0; j < 81; ++j) e.raw[j] = 0;
			e.raw[0] = 0xff;          // bRect_w
			e.raw[1] = 0xff;          // bRect_h
			e.raw[8] = 1;             // p_data[6] — frame counter
			e.raw[10] = 0xff;         // p_data[8] — sprite index
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
	(void)x;  // DOS bug: arg1 is clobbered before the write.
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

// DOS ActorOp_01/02 clear the first two words of the active record
// (1000:68d3 and 1000:68e3). For cast-table records those are wActive
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

bool Logic::castEntryActiveLikeDos(uint16 id) const {
	for (uint i = 0; i < _castTable.size(); ++i) {
		const CastEntry &e = _castTable[i];
		if (e.active == 0 || e.id != id)
			continue;
		if (e.animation)
			return !e.animation->castWaitCompleteLikeDos();
		if (READ_LE_UINT16(e.raw + 2) == 0 && e.interpreter) {
			const uint16 scriptOffset = uint16(READ_LE_UINT16(e.raw + 4) + id);
			byte *script = e.interpreter->rawCode(scriptOffset);
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
// C++ port: produces the DOS formatted text buffer, preserving the rendering
// markup bytes and synthetic row-centering records, plus the dimensions DOS
// computes. Per-glyph widths come from Graphics::getGlyphWidth (the C++
// analog of DOS LookupCharSprite).
Logic::FormattedBubble Logic::formatBubbleText(const byte *src) const {
	FormattedBubble out;
	out.lineCount = 1;          // DOS DAT_1000_94b5 init = 1
	out.rowCount = 1;           // DOS DX init = 1
	out.totalHeight = 0;
	out.maxLineWidth = 0;
	out.truncated = false;
	const uint16 lineHeight = bubbleLineHeight();
	if (!src) {
		out.totalHeight = lineHeight * 2 + 2;  // DOS minimum
		return out;
	}

	Graphics *g = (_engine ? _engine->graphics() : 0);
	const byte *p = src;
	int currentWidth = 0;
	uint rowWidthPatch = 0;
	bool rowFinished = false;
	uint16 remaining = 0x1f4;   // DOS AX countdown in FormatBubbleText_Inner

	auto startTextRow = [&]() {
		out.text += char(kStringCenter);
		out.text += char(0);
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
		if (g) return g->getGlyphWidth(ch);
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
			out.text += char(b);
			out.lineCount++;
			currentWidth += charPixelWidth(b);
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x0d) {
			// Forced newline.
			out.text += char(b);
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
			out.text += char(b);
			while (true) {
				const byte lit = *p++;
				out.text += char(lit);
				if (lit == 0x00)
					break;
				if (lit == 0x20)
					out.lineCount++;
				currentWidth += charPixelWidth(lit);
			}
			const uint16 optionValue = readLE16();
			out.text += char(optionValue & 0xff);
			out.text += char(optionValue >> 8);
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x09) {
			// 1-byte param = X-offset spacing.
			const byte amount = *p++;
			out.text += char(b);
			out.text += char(amount);
			currentWidth += amount;
			out.lineCount++;
			if (!tickInputCountdown())
				break;
			continue;
		}
		if (b == 0x07) {
			// Color-change marker and parameter are copied verbatim.
			const byte color = *p++;
			out.text += char(b);
			out.text += char(color);
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
		out.text += char(b);
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
		if (g) return g->getGlyphWidth(ch);
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
			out += char(b);
			out += char(*p++);
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

		out += char(b);
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
		if (!it->canceled && it->deferredMode != 0
				&& it->code.offset() == p.offset()
				&& it->code.interpreter() == p.interpreter()) {
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
	if (_queued.empty()) return;

	Interpreter * const liveTopLevel = _toplevelInterpreter.get();
	Interpreter * const liveBlock = _blockInterpreter.get();

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
				castEntryActiveLikeDos(current->waitParam)) {
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
			if (current->hasRunMode)
				current->code.run(static_cast<OpcodeMode>(current->runMode));
			else
				current->code.run();
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
	return false;
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

void Logic::addAnimation(Animation *anim) {
	_animations.push_back(anim);
}

void Logic::removeAnimation(Animation *anim) {
	_animations.remove(anim);
}

void Logic::setRoomLoop(const CodePointer &code) {
	_roomLoop = Common::SharedPtr<CodePointer>(new CodePointer(code));
}

void Logic::synchronize(Common::Serializer &s) {
	uint16 currentRoom = uint16(_currentRoom);
	uint16 currentPlace = _currentPlace;
	uint16 protagonistId = _protagonistId;
	uint16 currentBlock = _currentBlock;
	uint16 loadedBackdropId = _loadedBackdropId;
	const bool hasLoadedBackdropId = s.getVersion() >= 2;
	const bool hasScreenMode = s.getVersion() >= 3;
	uint8 screenFullscreen = (_engine && _engine->graphics() && _engine->graphics()->screenHeight() == 200) ? 1 : 0;

	s.syncAsUint16LE(currentRoom);
	s.syncAsUint16LE(currentPlace);
	s.syncAsUint16LE(protagonistId);
	s.syncAsUint16LE(currentBlock);
	if (hasLoadedBackdropId)
		s.syncAsUint16LE(loadedBackdropId);
	if (hasScreenMode)
		s.syncAsByte(screenFullscreen);

	if (s.isLoading()) {
		_queued.clear();
		_skipPoint.reset();
		_roomLoop.reset();
		_savedScene.reset();
		_currentRoom = 0xffff;
		_nextRoom = 0;
		_currentPlace = currentPlace;
		if (currentRoom != 0 && currentRoom != 0xffff)
			changeRoom(currentRoom);
		setProtagonist(protagonistId);
		if (hasLoadedBackdropId)
			_loadedBackdropId = loadedBackdropId;
		if (hasScreenMode && _engine && _engine->graphics())
			_engine->graphics()->setFullscreen(screenFullscreen != 0);
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

	if (s.isLoading()) {
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
		if (!hasScreenMode && _engine && _engine->graphics())
			_engine->graphics()->setFullscreen(!_roomActive);
		_runningQueued = nullptr;
		_runningQueuedMode = 0;
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
	// the fade opcode that observed the keypress.
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
	while (!slot.callbacks.empty())
		slot.callbacks.pop();
	slot = SpeechSlot();
}

void Logic::startSpeechSlotPage(SpeechSlot &slot, uint page) {
	if (page >= slot.pages.size()) {
		clearSpeechSlot(slot);
		return;
	}

	slot.pageIndex = page;
	slot.text = slot.pages[page];
	slot.framesTotal = uint8(logicSpeechTicksForText(slot.text, slot.maxLines) & 0xff);
	slot.framesLeft = slot.framesTotal;
	slot.active = 1;
}

bool Logic::initSpeechSlot(SpeechSlot &slot, const Common::String &text, uint16 maxLines) {
	slot.maxLines = maxLines;
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
	slot->color = actor->dosField(0x70);
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

void Logic::activateActorSpeechAfterPostMoveLikeDos(Actor *actor) {
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

bool Logic::uiTextSpeechSlotActiveLikeDos() const {
	if (_uiTextSpeechSlot >= _speechSlots.size())
		return false;
	return _speechSlots[_uiTextSpeechSlot].framesLeft != 0;
}

void Logic::stashUiTextSpeechSlotForOwnerLikeDos(uint16 owner) {
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

void Logic::queueUiTextSpeechSlotCallbackLikeDos(const CodePointer &cp) {
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

void Logic::recycleStaleSpeechSlotsLikeDos() {
	for (uint i = 0; i < _speechSlots.size(); ++i) {
		SpeechSlot &slot = _speechSlots[i];
		if (slot.owner == 0xffff) {
			slot.owner = 0;
			slot.framesLeft = 0;
			slot.active = 0;
			while (!slot.callbacks.empty())
				slot.callbacks.pop();
		}
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
	if (!g || escBreakPending())
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
					? Graphics::kSpeechBubbleType2 : Graphics::kSpeechBubbleType1;
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
				reinterpret_cast<const byte *>(slot.text.c_str()), &bubble, mode);
			g->paint(&bubble, Common::Point(rect.left, rect.top),
				Graphics::kPaintSemiTransparent | Graphics::kPaintPositionIsTop);
		}

		if (slot.framesLeft != 0)
			--slot.framesLeft;
		if (slot.framesLeft == 0)
			finishSpeechSlot(slot);
	}
}

void Logic::resetSpeechSlotsLikeDos() {
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
	if (_skipPoint.isEmpty()) return;

	const CodePointer target = _skipPoint;
	const uint16 proc = _escBreakProc;
	resetSpeechSlotsLikeDos();
	resetQueuedRunMode(proc);
	if (Actor *protag = protagonist())
		protag->setAttentionNeededLikeDos(true);
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
	foreach_const (Animation *, _animations)
		if ((*it)->baseOffset() == offset)
			return (*it);

	return 0;
}


} // End of namespace Interspective
