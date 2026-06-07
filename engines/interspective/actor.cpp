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

#include "common/rect.h"
#include "common/serializer.h"

#include "interspective/actor.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/sound.h"
#include "interspective/util.h"

namespace Interspective {
//

static uint16 actorCodeSegmentTag(const byte *base) {
	if (base && Log.blockProgram() && Log.blockProgram()->contains(base))
		return uint16(0x4000 + (Log.currentBlock() & 0x3fff));
	return 0x1cb5;
}

Actor::Actor(const CodePointer &code) : Animation(code, Common::Point()), _id(0) {
	_direction = kDirNone;
	_frame = 0;
	_debug = false;
	_attentionNeeded = false;
	_confused = false;
	_nextAnimator = 0;
	_nextDirection = kDirNone;
	_actorCallbackSeg = 0xffff;
	_actorCallbackOff = 0xffff;

	byte *header = code.code();
	_base = header - code.offset();
	readHeader(header);
	snprintf(_debugInfo, 50, "actor at %s", +code);

	Engine::instance().logic()->addAnimation(this);

	init_opcodes<37>();
}

bool Actor::isFine() const {
	return _room == Log.currentRoom() && _base && !_attentionNeeded &&
		   (!_framequeue.empty() || !_confused || _nextDirection);
}

bool Actor::animReady() const {
	// DOS CheckActorAnimReady @ 1000:6415 returns carry set once the
	// actor no longer has current-room animation state that must be waited.
	if (_room != Log.currentRoom() || recordScriptBase() == 0)
		return true;

	const bool stillActive = movementWaitActive() ||
							 (!needsAttention() &&
							  (walkQueueLength() != 0 || readyCallbackOffset() != 0 ||
							   !confusedRecord() || readyMarker() != 0));
	return !stillActive;
}

bool Actor::idleReady() const {
	// DOS CheckActorIdle @ 1000:645e uses the same carry convention:
	// carry clear while an active current-room actor script must be retried.
	const bool stillActive = _room == Log.currentRoom() &&
							 recordScriptBase() != 0 &&
							 walkQueueLength() == 0 && !needsAttention() &&
							 (!confusedRecord() || readyMarker() != 0);
	return !stillActive;
}

void Actor::setAnimation(uint16 offset) {
	// This overload is used by actor-side movement recovery paths that jump
	// back to the actor puppeteer/main animation table. DOS reaches
	// InitActorState @ 1000:6336 with DS already set to the main data segment
	// for those paths, then stores DS:DI into fields +0/+2 and clears BP.
	// Do not preserve a previous block/cast segment here; only explicit
	// CodePointer callers (Op_b9/Op_be/etc.) may select block code.
	byte *base = Log.mainInterpreter() ? Log.mainInterpreter()->rawCode(0) : 0;
	_base = base ? base + offset : 0;
	_baseOffset = _base ? offset : 0;
	_offset = 0;
	resetActorStateFields();
	if (_base) {
		setRecordSegment(actorCodeSegmentTag(_base));
		setRecordScriptBase(_baseOffset);
	} else {
		setRecordSegment(0);
		setRecordScriptBase(0);
	}
	registerActiveIfCurrentRoom();
}

void Actor::setAnimation(const CodePointer &anim) {
	debugC(3, kDebugLevelScript, "setting animation code of %s to %s", _debugInfo, +anim);
	_base = anim.code();
	_baseOffset = anim.offset();
	_offset = 0;
	resetActorStateFields();
	setRecordSegment(actorCodeSegmentTag(_base));
	setRecordScriptBase(_baseOffset);
	registerActiveIfCurrentRoom();
}

void Actor::resetActorStateFields() {
	// InitActorState @ 1000:6336 preserves actor x/y, installs the new
	// script pointer, then resets the per-script animation fields.
	_debugInvalid = false;
	_confused = _attentionNeeded = false;
	clearMainSprite();
	_interval = 1;
	_counter = 0;
	_ticksLeft = 0;
	_explicitFrameDelay = false;
	_nextDirection = kDirNone;
	setRecordMainSprite(0xffff);
	setRecordTicksLeft(0);
	setScriptPc(0);
	setRecordInterval(1);
	setSkipTimerCount(0);
	setRecordFlag14(true);
	setRecordFlag15(true);
	setAutoZoneLayerEnabled(true);
	setConfusedRecord(false);
	setRecordAttentionNeeded(false);
	setMovementWaitActive(false);
}

void Actor::setActorCodeOffset(uint16 offset) {
	if (!_base)
		return;

	byte *segmentBase = _base - _baseOffset;
	_base = segmentBase + offset;
	_baseOffset = offset;
	_offset = 0;
	setRecordScriptBase(_baseOffset);
	setScriptPc(0);
	_debugInvalid = false;
}

void Actor::hide() {
	clearSprites();
	clearMainSprite();
	_base = 0;
	_baseOffset = _offset = 0;
	_ticksLeft = 0;
	_attentionNeeded = false;
	_confused = false;
	_nextAnimator = 0;
	_nextDirection = kDirNone;
	_framequeue.clear();
}

void Actor::clearScriptPc() {
	_base = 0;
	_baseOffset = 0;
	_offset = 0;
	clearRecordScriptPointer();
}

void Actor::unregister() {
	clearScriptPc();
	const uint16 globalId = Log.actorGlobalId(this);
	if (globalId != 0)
		Log.unregisterActiveActor(globalId);
}

void Actor::prepareRoomEntryActiveActor() {
	const uint16 segment = recordSegment();
	const uint16 codeOffset = recordScriptBase();
	const bool blockSegment = segment == 1 || ((segment & 0xc000) == 0x4000);
	const bool mainSegment = segment == 0 ||
							 segment == actorCodeSegmentTag(Log.mainInterpreter() ? Log.mainInterpreter()->rawCode(0) : 0);
	if (blockSegment || mainSegment) {
		Interpreter *const interp = blockSegment ? Log.blockInterpreter() : Log.mainInterpreter();
		byte *const segmentBase = interp ? interp->rawCode(0) : 0;
		if (segmentBase) {
			_base = segmentBase + codeOffset;
			_baseOffset = codeOffset;
			_offset = scriptPc();
			setRecordScriptPointer(actorCodeSegmentTag(segmentBase), codeOffset, _offset);
		}
	}
	setMood(0);
}

void Actor::registerActiveIfCurrentRoom() {
	if (_room != Log.currentRoom())
		return;
	const uint16 globalId = Log.actorGlobalId(this);
	if (globalId != 0)
		Log.registerActiveActor(globalId);
}

void Actor::syncStateToRecordFields() {
	// DOS has one 0x71-byte actor record. The C++ port keeps common fields
	// as first-class members plus a sparse byte map for less common offsets;
	// keep the sparse record synchronized before/after save/load so helpers
	// that read raw DOS fields see the same state as the renderer/ticker.
	if (_base) {
		setRecordScriptPointer(actorCodeSegmentTag(_base), _baseOffset, _offset);
	} else {
		clearRecordScriptPointer();
	}
	setRecordPosition(_position);
	setRecordMainSprite(_mainSpriteId);
	setRecordTicksLeft(_ticksLeft);
	setRecordInterval(uint8(_interval));
	setRecordDrawLayer(uint8(_zIndex));
	setRecordRoom(_room);
	setRecordFrame(_frame);
	setRecordTargetFrame(_nextFrame);
	setConfusedRecord(_confused);
	setRecordAttentionNeeded(_attentionNeeded);
	setRecordWord(kOffsetActorCallbackSegment, _actorCallbackSeg);
	setRecordWord(kOffsetActorCallbackOffset, _actorCallbackOff);
}

static void syncActorRecordFields(Common::Serializer &s, Common::HashMap<uint8, uint8> &fields) {
	uint16 count = fields.size();
	s.syncAsUint16LE(count);
	if (s.isLoading()) {
		fields.clear();
		for (uint16 i = 0; i < count; ++i) {
			uint8 key = 0;
			uint8 value = 0;
			s.syncAsByte(key);
			s.syncAsByte(value);
			if (value != 0)
				fields[key] = value;
		}
	} else {
		for (Common::HashMap<uint8, uint8>::const_iterator it = fields.begin(); it != fields.end(); ++it) {
			uint8 key = it->_key;
			uint8 value = it->_value;
			s.syncAsByte(key);
			s.syncAsByte(value);
		}
	}
}

void Actor::syncWaitCallbacks(Common::Serializer &s) {
	uint16 callbackCount = uint16(_callBacks.size());
	s.syncAsUint16LE(callbackCount);
	if (s.isLoading()) {
		_callBacks.clear();
		for (uint16 i = 0; i < callbackCount; ++i) {
			CodePointer callback;
			uint16 runMode = 0;
			uint8 hasRunMode = 0;
			Log.syncCodePointer(s, callback);
			s.syncAsUint16LE(runMode);
			s.syncAsByte(hasRunMode);
			if (!callback.isEmpty())
				_callBacks.push(ScriptCallback(callback, runMode, hasRunMode != 0));
		}
	} else {
		Common::Queue<ScriptCallback> callbacks = _callBacks;
		while (!callbacks.empty()) {
			ScriptCallback callback = callbacks.pop();
			uint16 runMode = callback.runMode;
			uint8 hasRunMode = callback.hasRunMode ? 1 : 0;
			Log.syncCodePointer(s, callback.callback);
			s.syncAsUint16LE(runMode);
			s.syncAsByte(hasRunMode);
		}
	}

	uint16 roomCallbackCount = uint16(_roomCallbacks.size());
	s.syncAsUint16LE(roomCallbackCount);
	if (s.isLoading()) {
		_roomCallbacks.clear();
		for (uint16 i = 0; i < roomCallbackCount; ++i) {
			uint16 timeout = 0;
			CodePointer callback;
			uint16 runMode = 0;
			uint8 hasRunMode = 0;
			s.syncAsUint16LE(timeout);
			Log.syncCodePointer(s, callback);
			s.syncAsUint16LE(runMode);
			s.syncAsByte(hasRunMode);
			if (!callback.isEmpty())
				_roomCallbacks.push_back(RoomCallback(timeout, callback, runMode, hasRunMode != 0));
		}
	} else {
		for (Common::List<RoomCallback>::const_iterator it = _roomCallbacks.begin(); it != _roomCallbacks.end(); ++it) {
			uint16 timeout = it->timeout;
			CodePointer callback = it->callback;
			uint16 runMode = it->runMode;
			uint8 hasRunMode = it->hasRunMode ? 1 : 0;
			s.syncAsUint16LE(timeout);
			Log.syncCodePointer(s, callback);
			s.syncAsUint16LE(runMode);
			s.syncAsByte(hasRunMode);
		}
	}
}

void Actor::synchronize(Common::Serializer &s) {
	uint8 baseSource = 0;
	if (s.isSaving() && _base) {
		if (Log.blockProgram() && Log.blockProgram()->contains(_base))
			baseSource = 2;
		else
			baseSource = 1;
	}

	uint16 posX = uint16(_position.x);
	uint16 posY = uint16(_position.y);
	uint8 interval = uint8(_interval);
	uint16 ticksLeft = _ticksLeft;
	int8 zIndex = _zIndex;
	uint16 baseOffset = _baseOffset;
	uint16 offset = _offset;
	uint16 mainSprite = _mainSpriteId;
	uint16 frame = _frame;
	uint16 nextFrame = _nextFrame;
	uint16 room = _room;
	uint8 direction = uint8(_direction);
	uint8 nextDirection = uint8(_nextDirection);
	uint16 nextAnimator = _nextAnimator;
	uint8 attentionNeeded = _attentionNeeded ? 1 : 0;
	uint8 confused = _confused ? 1 : 0;
	uint16 callbackSeg = _actorCallbackSeg;
	uint16 callbackOff = _actorCallbackOff;

	if (s.isSaving())
		syncStateToRecordFields();

	s.syncAsByte(baseSource);
	s.syncAsUint16LE(baseOffset);
	s.syncAsUint16LE(offset);
	s.syncAsUint16LE(posX);
	s.syncAsUint16LE(posY);
	s.syncAsByte(interval);
	s.syncAsUint16LE(ticksLeft);
	s.syncAsSByte(zIndex);
	s.syncAsUint16LE(mainSprite);
	s.syncAsUint16LE(frame);
	s.syncAsUint16LE(nextFrame);
	s.syncAsUint16LE(room);
	s.syncAsByte(direction);
	s.syncAsByte(nextDirection);
	s.syncAsUint16LE(nextAnimator);
	s.syncAsByte(attentionNeeded);
	s.syncAsByte(confused);
	s.syncAsUint16LE(callbackSeg);
	s.syncAsUint16LE(callbackOff);
	syncActorRecordFields(s, _recordFields);

	uint16 moveSlotCount = _moveSlots.size();
	s.syncAsUint16LE(moveSlotCount);
	if (s.isLoading()) {
		_moveSlots.clear();
		for (uint16 i = 0; i < moveSlotCount; ++i) {
			MoveSlot slot;
			s.syncAsUint16LE(slot.a);
			s.syncAsUint16LE(slot.b);
			s.syncAsUint16LE(slot.c);
			s.syncAsByte(slot.mode);
			if (_moveSlots.size() < 8)
				_moveSlots.push_back(slot);
		}
	} else {
		for (uint16 i = 0; i < moveSlotCount; ++i) {
			MoveSlot slot = _moveSlots[i];
			s.syncAsUint16LE(slot.a);
			s.syncAsUint16LE(slot.b);
			s.syncAsUint16LE(slot.c);
			s.syncAsByte(slot.mode);
		}
	}

	if (s.isSaving())
		syncWaitCallbacks(s);

	if (!s.isLoading())
		return;

	_position = Common::Point(int16(posX), int16(posY));
	_interval = int8(interval);
	_ticksLeft = ticksLeft;
	_zIndex = zIndex;
	_baseOffset = baseOffset;
	_offset = offset;
	_frame = frame;
	_nextFrame = nextFrame;
	_room = room;
	_direction = Direction(direction);
	_nextDirection = Direction(nextDirection);
	_nextAnimator = nextAnimator;
	_attentionNeeded = attentionNeeded != 0;
	_confused = confused != 0;
	setActorCallback(callbackSeg, callbackOff);
	_debugInvalid = false;
	_framequeue.clear();
	_callBacks.clear();
	_roomCallbacks.clear();
	_speech = Speech();
	syncWaitCallbacks(s);

	switch (baseSource) {
	case 1:
		_base = Log.mainInterpreter() ? Log.mainInterpreter()->rawCode(baseOffset) : 0;
		break;
	case 2:
		_base = Log.blockInterpreter() ? Log.blockInterpreter()->rawCode(baseOffset) : 0;
		break;
	default:
		_base = 0;
		_baseOffset = _offset = 0;
		break;
	}

	if (mainSprite == 0xffff)
		clearMainSprite();
	else
		setMainSprite(mainSprite);

	syncStateToRecordFields();
}

void Actor::copyIntervalToTicks() {
	// DOS common actor tail at 1000:6953 zero-extends actor byte +0x10
	// into word +0x0a. In C++ those fields are _interval and _ticksLeft.
	const uint16 ticks = uint8(_interval);
	_ticksLeft = ticks;
	_explicitFrameDelay = true;
	setRecordTicksLeft(ticks);
}

void Actor::decrementTicksLeft() {
	// DOS UpdateActorAnimation @ 1000:64ae decrements actor field +0x0a
	// on every active actor update after the optional script dispatch.
	_ticksLeft = uint16(_ticksLeft - 1);
	setRecordTicksLeft(_ticksLeft);
	if (_ticksLeft == 0 && _base && int8(*(_base + _offset)) == -2) {
		// DOS 1000:64ed..64f3 marks field +0x64 when the next actor
		// opcode is ActorOp_02_UnregisterAndEnd (0xfe).
		setConfusedRecord(true);
	}
}

static bool zoneContainsPoint(uint16 left, uint16 top, uint16 right, uint16 bottom, const Common::Point &p) {
	return p.x >= int16(left) && p.x <= int16(right) &&
		   p.y >= int16(top) && p.y <= int16(bottom);
}

void Actor::updateZoneAtPoint() {
	if (mainSpriteId() == 0xffff)
		return;

	uint8 bl = 0;
	const Common::Array<Logic::CollisionZone> &collisionZones = Log.collisionZones();
	for (uint i = 0; i < collisionZones.size(); ++i) {
		const Logic::CollisionZone &z = collisionZones[i];
		if (!zoneContainsPoint(z.a, z.b, z.c, z.d, _position))
			continue;
		bl = uint8(uint16(z.slot) & 0xff);
		break;
	}

	uint8 bh = 0;
	const Common::Array<Logic::ZoneB> &zonesB = Log.zonesB();
	for (uint i = 0; i < zonesB.size(); ++i) {
		const Logic::ZoneB &z = zonesB[i];
		if (!zoneContainsPoint(z.a, z.b, z.c, z.d, _position))
			continue;
		bh = uint8(z.var & 0xff);
		break;
	}

	setSecondaryZone(bh);
	if (!autoZoneLayerEnabled())
		bl = recordDrawLayer();
	setRecordDrawLayer(bl);
	_zIndex = int8(bl);
}

void Actor::callMe(const CodePointer &code) {
	debugC(3, kDebugLevelScript, "actor will call %s when needed", +code);
	_callBacks.push(ScriptCallback(code));
}

void Actor::callMeWithMode(const CodePointer &code, uint16 mode) {
	debugC(3, kDebugLevelScript, "actor will call %s when needed in mode 0x%02x", +code, mode);
	_callBacks.push(ScriptCallback(code, mode, true));
}

bool Actor::takeRoomScriptWait(RoomScriptWaitSnapshot &snapshot) {
	snapshot = RoomScriptWaitSnapshot();
	Common::Queue<ScriptCallback> kept;
	uint16 position = 0;
	bool found = false;

	while (!_callBacks.empty()) {
		ScriptCallback callback = _callBacks.pop();
		if (!found && callback.hasRunMode) {
			snapshot.callback = callback.callback;
			snapshot.runMode = callback.runMode;
			snapshot.position = position;
			snapshot.valid = true;
			found = true;
			continue;
		}

		kept.push(callback);
		if (!found)
			++position;
	}

	_callBacks = kept;
	return found;
}

void Actor::restoreRoomScriptWait(const RoomScriptWaitSnapshot &snapshot) {
	if (!snapshot.valid)
		return;

	Common::Queue<ScriptCallback> rebuilt;
	uint16 position = 0;
	bool inserted = false;
	while (!_callBacks.empty()) {
		if (!inserted && position == snapshot.position) {
			rebuilt.push(ScriptCallback(snapshot.callback, snapshot.runMode, true));
			inserted = true;
		}
		rebuilt.push(_callBacks.pop());
		++position;
	}
	if (!inserted)
		rebuilt.push(ScriptCallback(snapshot.callback, snapshot.runMode, true));
	_callBacks = rebuilt;
}

bool Actor::hasRoomScriptWaitMode(uint16 mode) const {
	Common::Queue<ScriptCallback> callbacks = _callBacks;
	while (!callbacks.empty()) {
		const ScriptCallback callback = callbacks.pop();
		if (callback.hasRunMode && callback.runMode == mode)
			return true;
	}
	return false;
}

void Actor::dropRoomScriptWaitMode(uint16 mode) {
	Common::Queue<ScriptCallback> kept;
	while (!_callBacks.empty()) {
		const ScriptCallback callback = _callBacks.pop();
		if (!callback.hasRunMode || callback.runMode != mode)
			kept.push(callback);
	}
	_callBacks = kept;
}

void Actor::processWaitCallbacks() {
	callBacks();
}

Actor::RoomScriptWaitDispatch Actor::dispatchReadyRoomScriptWaitMode(uint16 mode) {
	Common::Queue<ScriptCallback> callbacks = _callBacks;
	Common::Queue<ScriptCallback> kept;
	_callBacks.clear();

	RoomScriptWaitDispatch status = kNoRoomScriptWait;
	CodePointer readyRoomScriptCallback;
	while (!callbacks.empty()) {
		const ScriptCallback callback = callbacks.pop();
		if (status == kNoRoomScriptWait && callback.hasRunMode && callback.runMode == mode) {
			if (animReady()) {
				status = kRoomScriptWaitDispatched;
				readyRoomScriptCallback = callback.callback;
				continue;
			}
			status = kRoomScriptWaitPending;
		}
		kept.push(callback);
	}

	_callBacks = kept;
	if (status == kRoomScriptWaitDispatched) {
		debugC(3, kDebugLevelScript, "room-script mode 0x%02x actor wait ready; running %s",
			   mode, +readyRoomScriptCallback);
		const uint16 savedOpcodeMode = Log.opcodeMode();
		readyRoomScriptCallback.run(static_cast<OpcodeMode>(mode));
		Log.setOpcodeMode(savedOpcodeMode);
	}
	return status;
}

void Actor::tellMe(const CodePointer &code, uint16 timeout) {
	_ticksLeft = timeout;
	setRecordTicksLeft(timeout);
	_roomCallbacks.push_back(RoomCallback(timeout, code));
}

void Actor::tellMeWithMode(const CodePointer &code, uint16 timeout, uint16 mode) {
	_ticksLeft = timeout;
	setRecordTicksLeft(timeout);
	_roomCallbacks.push_back(RoomCallback(timeout, code, mode, true));
}

bool Actor::isSpeaking() const {
	return Log.speechSlotActiveForOwner(Log.actorGlobalId(this));
}

const Common::String &Actor::speechText() const {
	return Log.speechTextForOwner(Log.actorGlobalId(this));
}

void Actor::stopSpeaking() {
	Log.clearSpeechForOwner(Log.actorGlobalId(this));
	_speech = Speech();
}

void Actor::callMeWhenSilent(const CodePointer &cp) {
	Log.queueSpeechSlotCallbackForOwner(Log.actorGlobalId(this), cp);
}

void Actor::say(const Common::String &text, uint16 maxLines) {
	Log.allocActorSpeech(this, text, maxLines);
}

void Actor::sayAtPos(const Common::String &text, Common::Point pos, uint16 maxLines) {
	Log.allocActorSpeechAt(this, text, pos, maxLines);
}

Actor::Speech::~Speech() {
	// Destruction is used for ESC/room cleanup too; DOS ResetSpeechSlots
	// drops pending speech callbacks instead of dispatching them.
}

bool Actor::isMoving() const {
	// DOS CheckActorScripting @ 1000:6499 treats movement/scripting wait
	// as active while field+0x6f or word field+0x6b is non-zero.
	// _framequeue is the C++ mirror of the same queued walk path.
	return movementWaitActive() || walkQueueLength() != 0 || !_framequeue.empty();
}

void Actor::callMeWhenStill(const CodePointer &cp) {
	// If the actor is already still, fire on the next tick (immediate
	// post-walk callback equivalent). While moving, queue into _callBacks
	// — Animation::tick drains it via callBacks() once attention/state
	// settles.
	if (!isMoving())
		Log.runLaterWithMode(cp, Log.opcodeMode());
	else
		_callBacks.push(ScriptCallback(cp, Log.opcodeMode(), true));
}

void Actor::setFrame(uint16 frame) {
	setRawFrame(frame);
	Frame f(Log.room()->getFrame(frame));
	setRawPosition(f.position());
	debugC(5, kDebugLevelActor, "actor set to frame %d, position %d:%d", f.index(), _position.x, _position.y);
}

Common::List<Actor::Frame> Actor::findPath(Actor::Frame from, uint16 to) {
	// BFS over the room's frame-transition graph. DOS keeps a visited table
	// and backtracks through the discovered path. The old C++ code stored
	// per-level lists and reconstructed parents by counting outgoing edges;
	// that can select a sibling as the parent and produce impossible steps
	// like room-1 frame 13 -> 7. Keep an explicit parent map instead.
	Common::List<Frame> path;
	if (from.index() == 0)
		return path;

	Common::HashMap<uint16, uint16> parent;
	Common::Queue<uint16> queue;
	parent[from.index()] = 0;
	queue.push(from.index());

	bool found = from.index() == to;
	while (!found && !queue.empty()) {
		const uint16 currentIndex = queue.pop();
		const Frame current = Log.room()->getFrame(currentIndex);

		// DOS skips nexts-expansion for placeholder frames (position
		// 999,999). Match that — these are gaps in the frame table.
		if (current.position().x == 999 && current.position().y == 999)
			continue;

		const Common::Array<byte> &nexts = current.nexts();
		for (int i = 0; i < 8; i++) {
			const uint16 nextIndex = nexts[i];
			if (!nextIndex || parent.contains(nextIndex))
				continue;

			const Frame next = Log.room()->getFrame(nextIndex);
			if (next.position().x == 999 && next.position().y == 999)
				continue;

			parent[nextIndex] = currentIndex;
			if (nextIndex == to) {
				found = true;
				break;
			}
			queue.push(nextIndex);
		}
	}

	if (!found) {
		debugC(2, kDebugLevelActor,
			   "findPath: frame %u unreachable from %u (%u frames explored)",
			   to, from.index(), (uint)parent.size());
		return path;
	}

	for (uint16 index = to; index != 0; index = parent[index]) {
		path.push_front(Log.room()->getFrame(index));
		if (index == from.index())
			break;
	}
	return path;
}

void Actor::moveTo(uint16 frame) {
	Frame cur = Log.room()->getFrame(_frame);

	// DOS MoveActorToRoom @ 1000:70e1, NOT-FOUND path
	// @ 1000:7103..1000:710b:
	//   MOV ES:[SI+0x61], AL          ; actor.frame = target frame
	//   CALL SetActorPosition         ; reads frame[N].pos → actor X/Y
	//   STC; RET
	// That's it. No animation change, no direction logic. Just position
	// warp. The actor's existing animation script keeps running.
	//
	// Mirror that exactly: setFrame() does both the _frame assignment
	// and the position lookup-and-update via Room::getFrame. Do not
	// synthesize direction or animation changes here; DOS does not do
	// that, and synthetic frames may not have adjacency data.
	if (_frame == 0 || cur.position().x == 999) {
		debugC(3, kDebugLevelActor, "moveTo(%u): warp (no valid current frame)", (uint)frame);
		_framequeue.clear();
		clearWalkState();
		setFrame(frame);
		return;
	}

	// Validate target frame BEFORE running findPath. If target is invalid
	// in current room (e.g., target lives in a different room and C++
	// only has the player's current room loaded), do a soft warp via
	// setFrame() — same as the !valid-current-frame branch above.
	// Pushing a sentinel Frame onto _framequeue causes _frame corruption
	// on the next nextFrame() pop.
	const Frame targetFrame = Log.room()->getFrame(frame);
	// LookupActorAndStartPath stores whether the destination is left of
	// or equal to the current actor x before running FindActorPath.
	setTurnTieBreaker(int16(targetFrame.position().x) <= int16(_position.x) ? 1 : 0);
	if (targetFrame.position().x == 999) {
		debugC(3, kDebugLevelActor, "moveTo(%u): warp (target frame invalid in current room)", (uint)frame);
		_framequeue.clear();
		clearWalkState();
		setFrame(frame);
		return;
	}

	// DOS FindActorPath @ 1000:713e mirrors the requested destination into
	// actor.field+0x62 before building the path.
	_nextFrame = frame;
	_framequeue.clear();
	setWalkQueueLength(0);
	Common::List<Frame> path = findPath(cur, frame);

	// findPath now returns an EMPTY list when the target is unreachable
	// (matches DOS bound-check on visited buffer). Treat that as a
	// single-frame walk to the validated target.
	bool pathIncludesCurrent = true;
	if (path.empty()) {
		path.push_back(targetFrame);
		pathIncludesCurrent = false;
	} else {
		Common::List<Frame>::iterator it = path.end();
		it--;
		if (it->index() != frame) {
			Common::List<Frame> p;
			p.push_back(targetFrame);
			path = p;
			pathIncludesCurrent = false;
		}
	}

	Common::String s;
	Common::List<Frame>::iterator it = path.begin();
	if (pathIncludesCurrent && it != path.end())
		it++;
	while (it != path.end()) {
		_framequeue.push(*it);
		s += Common::String::format(" %d", int(it->index()));
		it++;
	}

	const uint16 pathLen = uint16(MIN<uint>(_framequeue.size(), 0xffff));
	setWalkQueueLength(pathLen);
	setMovementWaitActive(pathLen != 0);
	if (pathLen != 0) {
		setAttentionNeeded(true);
	}

	debugC(3, kDebugLevelActor, "found path: %s", s.c_str());
	if (!_base)
		nextFrame();
}

void Actor::setRoom(uint16 r, uint16 frame, uint16 next_frame) {
	_room = r;
	unless(next_frame)
		next_frame = frame;
	_nextFrame = next_frame;
	clearMoveQueue();
	setFrame(frame);

	setAnimation(CodePointer(_puppeteer.mainCodeOffset(), Log.mainInterpreter()));
}

void Actor::placeIn(uint16 r, uint16 frame, uint16 next_frame) {
	// Mirrors DOS Op_7a_PlaceActorInRoomXY @ 1000:4443 prelude:
	//   actor.field+0x59 = room
	//   actor.field+0x61 = frame   (current)
	//   actor.field+0x62 = nextFrame (target; same as frame for Op_79/protagonist seed)
	//   actor.field+0x6b = 0       (walk speed reset)
	// Then SetActorPosition copies frame's X/Y into actor.field+0x4/+0x6.
	// Crucially, the script PC (segment/offset) is NOT touched — so any
	// in-flight animation continues, and the puppeteer.mainCode jump
	// (which crashes when the puppeteer isn't loaded for the actor)
	// never happens.
	_room = r;
	if (!next_frame)
		next_frame = frame;
	_nextFrame = next_frame;
	clearMoveQueue();
	setFrame(frame);
}

bool Actor::nextFrame() {
	if (_framequeue.empty())
		return false;

	Frame next = _framequeue.front();
	Frame current = Log.room()->getFrame(_frame);

	Direction direction = next - current;

	debugC(4, kDebugLevelActor, "switching frames %d -> %d", current.index(), next.index());
	if (turnTo(direction))
		return true;

	//	setFrame(_framequeue.front().index());
	setAnimation(_puppeteer.moveAnimator(direction));
	_frame = next.index();
	if (Log.protagonist() == this)
		Log.setPostMoveTargetFrameMirror(uint8(_frame));
	_framequeue.pop();
	setWalkQueueLength(uint16(MIN<uint>(_framequeue.size(), 0xffff)));
	if (_framequeue.empty())
		setMovementWaitActive(false);
	return true;
}

bool Actor::turnTo(Direction dir) {
	if (dir == _direction)
		return false;

	Direction d = _direction >> dir;
	if (_direction >= kDirUp && _direction <= kDirUpLeft &&
		dir >= kDirUp && dir <= kDirUpLeft) {
		// DOS PickActorAnimSet @ 1000:6f7e steps one compass slot toward
		// the requested direction. For exact 180-degree turns it uses field
		// +0x66, set by LookupActorAndStartPath from target-vs-current X, to
		// choose the rotation side.
		int8 delta = int8(dir) - int8(_direction);
		uint8 tie = turnTieBreaker();
		if (_direction == kDirUp)
			tie ^= 1;

		bool increment;
		if (delta < 0) {
			if (delta < -4)
				increment = true;
			else if (delta != -4)
				increment = false;
			else
				increment = tie != 0;
		} else {
			if (delta > 4)
				increment = false;
			else if (delta != 4)
				increment = true;
			else
				increment = tie != 0;
		}

		int step = int(_direction) + (increment ? 1 : -1);
		if (step < kDirUp)
			step = kDirUpLeft;
		if (step > kDirUpLeft)
			step = kDirUp;
		d = Direction(step);
	}
	debugC(4, kDebugLevelActor, "turning %d -> %d >> %d", _direction, d, dir);
	setAnimation(_puppeteer.turnAnimator(d));
	return true;
}

static bool pickReadyMarkerTurnStep(uint8 current, uint8 target, uint8 tie, Direction &stepDir) {
	// DOS PickActorAnimSet @ 1000:6f7e returns carry set when no intermediate
	// turn script is needed. It also treats a one-step difference as already
	// reached for the caller that will install the final/default script.
	if (current == 0 || current == target)
		return false;
	if (current < kDirUp || current > kDirUpLeft || target < kDirUp || target > kDirUpLeft)
		return false;

	uint8 side = tie;
	if (current == kDirUp)
		side ^= 1;

	const int8 delta = int8(target) - int8(current);
	bool increment;
	if (delta < 0) {
		if (delta < -4)
			increment = true;
		else if (delta != -4)
			increment = false;
		else
			increment = side != 0;
	} else {
		if (delta > 4)
			increment = false;
		else if (delta != 4)
			increment = true;
		else
			increment = side != 0;
	}

	int step = int(current) + (increment ? 1 : -1);
	if (step < kDirUp)
		step = kDirUpLeft;
	if (step > kDirUpLeft)
		step = kDirUp;
	if (step == target)
		return false;

	stepDir = Direction(step);
	return true;
}

bool Actor::consumeReadyMarkerCallback() {
	// DOS UpdateActors @ 1000:6d98 consumes the ready marker/callback fields
	// written by opcodes 0xa4..0xa7. The opcode handlers only arm these fields;
	// this actor update path is what eventually starts the callback script.
	const uint8 marker = readyMarker();
	const uint16 callback = readyCallbackOffset();
	if (marker == 0 && callback == 0)
		return false;
	if (walkQueueLength() != 0 || !_framequeue.empty())
		return false;

	if (marker != 0) {
		const uint16 pendingAnim = pendingReadyAnimation();
		clearPendingReadyAnimation();
		if (pendingAnim != 0) {
			debugC(3, kDebugLevelActor,
				   "ready marker %u starting pending actor animation 0x%04x before callback 0x%04x [DOS UpdateActors]",
				   marker, pendingAnim, callback);
			setAnimation(pendingAnim);
			return true;
		}

		Direction stepDir = kDirNone;
		const uint8 current = mood();
		if (pickReadyMarkerTurnStep(current, marker, turnTieBreaker(), stepDir)) {
			debugC(4, kDebugLevelActor, "ready marker %u needs turn step %u before callback 0x%04x [DOS UpdateActors]",
				   marker, uint8(stepDir), callback);
			setAnimation(_puppeteer.turnAnimator(stepDir));
			return true;
		}

		setMood(marker);
		if (marker >= kDirUp && marker <= kDirUpLeft)
			_direction = Direction(marker);
		setReadyMarker(0);
	}

	setReadyCallbackOffset(0);
	if (callback != 0) {
		debugC(3, kDebugLevelActor, "ready marker %u starting actor callback 0x%04x [DOS UpdateActors]",
			   marker, callback);
		setAnimation(callback);
		return true;
	}

	if (marker != 0) {
		debugC(4, kDebugLevelActor, "ready marker %u returning to actor base animation [DOS UpdateActors]",
			   marker);
		setAnimation(_puppeteer.offset());
		return true;
	}

	return false;
}

void Actor::animate() {
	unless(_puppeteer.valid()) return;

	const bool walkQueueActive = !_framequeue.empty() && walkQueueLength() != 0;
	bool queuedWalkReady = false;
	if (walkQueueActive && Log.room()) {
		const Frame current = Log.room()->getFrame(_frame);
		queuedWalkReady = current.position() == _position;
	}
	const bool readyMarkerActive = (readyMarker() != 0 || readyCallbackOffset() != 0) &&
								   walkQueueLength() == 0 && _framequeue.empty();
	unless(_attentionNeeded || _confused || queuedWalkReady || readyMarkerActive /* || _timedOut*/) return;

	// DOS UpdateActors @ 1000:6d3a keeps queued walking alive through
	// word field +0x6b. Actor byte +0x65 is an attention latch that
	// InitActorState can clear while the turn-step script is still part
	// of the same walk. While an actor script is moving between two room
	// frames, let its pixel-delta sequence reach the current logical
	// frame coordinate before selecting the next path node; otherwise the
	// queue restarts the move animator after its first delta and consumes
	// a 5-pixel frame edge after walking only 1 pixel.
	if (_ticksLeft)
		return;

	debugC(4, kDebugLevelActor, "attention needed");

	if (walkQueueActive && nextFrame()) {
		return;
	}

	if (_nextAnimator) {
		setAnimation(_nextAnimator);
		_nextAnimator = 0;
		return;
	}

	if (_nextDirection) {
		if (turnTo(_nextDirection))
			return;
		_direction = _nextDirection;
		_nextDirection = kDirNone;
		/*		ax = _nextPuppeteer;
				_nextPuppeteer = 0;
				if (ax)
					goto set_anim;*/
		setAnimation(_puppeteer.offset());
		/*	} else if (_nextPuppeteer) {
				ax = _nextPuppeteer;
				_nextPuppeteer = 0;
				if (ax)
					goto set_anim;*/
	}

	if (consumeReadyMarkerCallback())
		return;

	if (_confused) {
		if (turnTo(kDirDown))
			return;
		_direction = kDirDown;
		setMood(kDirDown);
		setAnimation(_puppeteer.offset());
		return;
	}
	/*	} else {
			if (turnTo(kDirUp))
				return;
			_direction = kDirUp;
			setAnimation(_puppeteer.offset());
		}*/
}

Animation::Status Actor::tick() {
	// DOS MainGameLoop runs RunDeferredScripts/RunScriptByMode before
	// UpdateActors and UpdateActorAnimation. Room-script waits such as
	// Op_9a therefore observe transient actor-ready fields (notably
	// +0x64 set when the next actor opcode is 0xfe) before the actor
	// update state machine consumes them. Keep the C++ callback bridge in
	// that order; otherwise a ready marker can be reset by animate() before
	// the queued room-script slot resumes.
	callBacks();
	animate();

	if (_room == Log.currentRoom()) {
		// DOS RunActorScript writes DAT_1cb5_666c = current actor id
		// before InterpretBytecode dispatches the actor's script.
		// Op_5b reads this to know "which actor am I scripting for".
		// Save/restore so nested dispatches don't trash the value.
		const uint16 savedEntityId = Log.currentEntityId();
		Log.setCurrentEntityId(_id);

		Animation::Status s = kOk;
		if (_base) {
			if (_ticksLeft) {
				debugC(5, kDebugLevelAnimation, "ticking animation %s (ticks left: %u)", _debugInfo, _ticksLeft);
				decrementTicksLeft();
			} else {
				// DOS RunActorScript clears the 8-slot movement queue at entry
				// before dispatching actor opcodes.
				clearMoveSlots();
				if (_debug)
					gDebugLevel += 3;
				s = Animation::tick();
				if (_debug)
					gDebugLevel -= 3;
				if (_base)
					setScriptPc(_offset);
				else
					clearRecordScriptPointer();
				decrementTicksLeft();
			}
			updateZoneAtPoint();
		}

		Log.setCurrentEntityId(savedEntityId);
		return s;
	} else
		return kOk;
}

void Actor::toggleDebug() {
	_debug = !_debug;
}

void Actor::readHeader(const byte *code) {
	const byte *const headerBase = _base;
	_interval = code[kOffsetInterval];
	_ticksLeft = READ_LE_UINT16(code + kOffsetTicksLeft);
	_zIndex = int8(code[kOffsetDrawLayer]);
	_position = Common::Point(READ_LE_UINT16(code + kOffsetLeft), READ_LE_UINT16(code + kOffsetTop));
	const uint16 segment = READ_LE_UINT16(code + kOffsetSegment);
	const uint16 codeOffset = READ_LE_UINT16(code + kOffsetScriptBase);
	_offset = READ_LE_UINT16(code + kOffsetScriptPc);
	if (segment != 0) {
		byte *segmentBase = const_cast<byte *>(headerBase);
		const uint16 mainSegment = actorCodeSegmentTag(Log.mainInterpreter() ? Log.mainInterpreter()->rawCode(0) : 0);
		const uint16 blockSegment = actorCodeSegmentTag(Log.blockInterpreter() ? Log.blockInterpreter()->rawCode(0) : 0);
		if (segment == mainSegment && Log.mainInterpreter())
			segmentBase = Log.mainInterpreter()->rawCode(0);
		else if ((segment == 1 || segment == blockSegment) && Log.blockInterpreter())
			segmentBase = Log.blockInterpreter()->rawCode(0);
		_base = segmentBase + codeOffset;
		_baseOffset = codeOffset;
	} else {
		_base = 0;
		_baseOffset = _offset = 0;
	}
	uint16 sprite = READ_LE_UINT16(code + kOffsetMainSprite);
	_frame = code[kOffsetFrame];
	_nextFrame = code[kOffsetTargetFrame];
	_room = READ_LE_UINT16(code + kOffsetRoom);
	setActorCallback(READ_LE_UINT16(code + kOffsetActorCallbackSegment), READ_LE_UINT16(code + kOffsetActorCallbackOffset));
	static const uint8 sparseFields[] = {
		0x00, 0x01, 0x02, 0x03, 0x0c, 0x0d, 0x0e, 0x0f,
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
		0x17, 0x18, 0x5b, 0x5c, 0x63, 0x64, 0x65,
		0x66, 0x68, 0x6b, 0x6c, 0x6d, 0x6e, 0x70};
	for (uint i = 0; i < ARRAYSIZE(sparseFields); ++i)
		setRecordByte(sparseFields[i], code[sparseFields[i]]);
	_confused = confusedRecord();
	_attentionNeeded = needsAttention();

	debugC(3, kDebugLevelFiles, "loading %s: interv %d ticks %u z%d pos%d:%d segment %d code %d pc %d sprite %d room %d", _debugInfo, _interval, _ticksLeft, _zIndex, _position.x, _position.y, segment, codeOffset, _offset, sprite, _room);

	if (sprite != 0xffff)
		setMainSprite(sprite);
}

void Actor::callBacks() {
	if (!_callBacks.empty()) {
		Common::Queue<ScriptCallback> callbacks = _callBacks;
		Common::Queue<ScriptCallback> pending;
		_callBacks.clear();
		while (!callbacks.empty()) {
			ScriptCallback callback = callbacks.pop();
			// DOS CheckActorScripting @ 1000:6499 is the wait predicate
			// used by speech opcodes before RegisterSampleSlot_Common:
			// carry set only when actor.field+0x6f == 0 and word +0x6b == 0.
			// Do not use isFine() here; script PC can be clear while a
			// queued walk is still in flight.
			const bool ready = callback.hasRunMode ? animReady() : (!movementWaitActive() && walkQueueLength() == 0);
			if (!ready) {
				pending.push(callback);
				continue;
			}
			if (callback.hasRunMode)
				Log.runLaterWithMode(callback.callback, callback.runMode);
			else
				Log.runLater(callback.callback);
		}
		_callBacks = pending;
	}

	foreach (RoomCallback, _roomCallbacks) {
		if (_room == Log.currentRoom() || !it->timeout) {
			_ticksLeft = 0;
			setRecordTicksLeft(0);
			if (it->hasRunMode)
				Log.runLaterWithMode(it->callback, it->runMode);
			else
				Log.runLater(it->callback);
			Common::List<RoomCallback>::iterator done = it;
			it++;
			_roomCallbacks.erase(done);
		} else {
			it->timeout--;
			_ticksLeft = it->timeout;
			setRecordTicksLeft(it->timeout);
		}
	}
}

void Puppeteer::parse(const byte *data) {
	_actorId = READ_LE_UINT16(data + kActorId);
	_offset = READ_LE_UINT16(data + kMainCode);

	assert(kTurnAnimators == kMoveAnimators + 16);
	const byte *d = data + kMoveAnimators;
	for (int i = 0; i < 16; i++) {
		_animators[i] = READ_LE_UINT16(d);
		d += 2;
	}
}

static const Direction mixeddirs[] = {kDirUp, kDirRight, kDirDown, kDirLeft,
									  kDirUpRight, kDirDownRight, kDirDownLeft, kDirUpLeft};

CodePointer Puppeteer::moveAnimator(Direction d) {
	uint16 off = mainCodeOffset();
	for (int i = 0; i < 8; i++) {
		if (mixeddirs[i] == d)
			off = _animators[i];
	}

	return CodePointer(off, Log.mainInterpreter());
}

CodePointer Puppeteer::turnAnimator(Direction d) {
	uint16 off = mainCodeOffset();
	for (int i = 0; i < 8; i++) {
		if (mixeddirs[i] == d)
			off = _animators[i + 8];
	}

	return CodePointer(off, Log.mainInterpreter());
}

static uint16 speechTicksForText(const Common::String &text) {
	// DOS AllocSpeechSlot @ 1000:9b1f stores the low byte of
	// FormatBubbleText's height return as the slot countdown.
	Common::String normalized;
	for (uint i = 0; i < text.size(); ++i)
		normalized += char(text[i] == '\n' ? '\r' : text[i]);
	Logic::FormattedBubble fb = Log.formatBubbleText(reinterpret_cast<const byte *>(normalized.c_str()));
	return uint8(fb.totalHeight & 0xff);
}

static Common::Array<Common::String> paginateSpeechText(const Common::String &text, uint16 maxLines) {
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

Actor::Speech::Speech(Actor *parent, const Common::String &text, uint16 maxLines)
	: _pages(paginateSpeechText(text, maxLines)), _pageIndex(0), _actor(parent),
	  _anchor(parent->getSpeechPosition()), _color(parent->speechColor()),
	  _image(0) {
	debugC(1, kDebugLevelActor, "adding speech \"%s\" (%u ticks) for %s at %d:%d",
		   text.c_str(), (uint)speechTicksForText(_pages[0]), parent->_debugInfo, _anchor.x, _anchor.y);
	startPage(0);
}

Actor::Speech::Speech(Actor *parent, const Common::String &text, Common::Point overridePos, uint16 maxLines)
	: _pages(paginateSpeechText(text, maxLines)), _pageIndex(0), _actor(parent),
	  _anchor(overridePos), _color(parent->speechColor()),
	  _image(0) {
	debugC(1, kDebugLevelActor, "adding speech \"%s\" (%u ticks) for %s at OVERRIDE %d:%d",
		   text.c_str(), (uint)speechTicksForText(_pages[0]), parent->_debugInfo, overridePos.x, overridePos.y);
	startPage(0);
}

void Actor::Speech::callWhenDone(const CodePointer &cp) {
	_cb.push(SpeechCallback(cp, Log.opcodeMode(), true));
}

void Actor::Speech::startPage(uint page) {
	delete _image;
	_pageIndex = page;
	_text = _pages[_pageIndex];
	_ticksLeft = speechTicksForText(_text);
	_image = new Interspective::Sprite;
	_image->_hotPoint = Common::Point(0, 0);
	_rect = Graf.paintSpeechInBubble(_anchor, _color, reinterpret_cast<const byte *>(_text.c_str()), _image);
}

void Actor::Speech::tick() {
	if (_text.empty())
		return;

	if (_ticksLeft) {
		--_ticksLeft;
		return;
	}

	if (_pageIndex + 1 < _pages.size()) {
		startPage(_pageIndex + 1);
		return;
	}

	_text.clear();
	while (!_cb.empty()) {
		SpeechCallback cb = _cb.pop();
		if (cb.hasMode)
			Log.runLaterWithMode(cb.callback, cb.mode);
		else
			Log.runLater(cb.callback);
	}
}

Common::Point Actor::getSpeechPosition() const {
	Common::Point speechPosition(_position);
	if (mainSpriteId() != 0xffff) {
		const SpriteInfo info = _resources->getSpriteInfo(mainSpriteId());
		const Common::Point hotPoint = info.hotPoint();
		speechPosition.x -= hotPoint.x;
		speechPosition.y += hotPoint.y;
	}
	speechPosition.y -= visibleSpriteHeight();
	return speechPosition;
}

static bool actorMainSpriteVisibleDimensions(const Interspective::Sprite *sprite, Common::Point pos, uint16 screenHeight, uint8 &width, uint8 &height) {
	if (!sprite)
		return false;

	const int32 screenX = int32(pos.x) - int32(Log.cameraX());
	const int32 screenY = int32(pos.y) - int32(Log.cameraY());
	const int32 left = screenX - int32(sprite->_hotPoint.x);
	const int32 top = screenY - int32(sprite->h) + int32(sprite->_hotPoint.y);
	const int32 right = left + int32(sprite->w);
	const int32 bottom = top + int32(sprite->h);

	const int32 clippedLeft = left < 0 ? 0 : left;
	const int32 clippedTop = top < 0 ? 0 : top;
	const int32 clippedRight = right > 320 ? 320 : right;
	const int32 clippedBottom = bottom > int32(screenHeight) ? int32(screenHeight) : bottom;

	if (clippedRight <= clippedLeft || clippedBottom <= clippedTop)
		return false;

	const int32 clippedWidth = clippedRight - clippedLeft;
	const int32 clippedHeight = clippedBottom - clippedTop;
	width = uint8(clippedWidth > 0xff ? 0xff : clippedWidth);
	height = uint8(clippedHeight > 0xff ? 0xff : clippedHeight);
	return true;
}

void Actor::paint(Graphics *g) {
	if (_room != Log.currentRoom())
		return;
	if (!_base)
		return;
	if (!hasMainSpriteForDraw())
		return;

	Animation::paint(g);

	for (uint i = 0; i < _moveSlots.size(); ++i) {
		const MoveSlot &slot = _moveSlots[i];
		paintMoveSlot(g, slot.a, slot.b, slot.c, slot.mode, _position);
	}

	uint8 width = 0;
	uint8 height = 0;
	if (actorMainSpriteVisibleDimensions(_mainSprite.get(), _position, g->screenHeight(), width, height)) {
		// DOS DrawActorAnimSlot @ 1000:6633 / 1000:663b copies g_blit_last_w/h
		// into actor fields +0x17/+0x18 after drawing the main sprite.
		setVisibleDimensions(width, height);
	}
}

void Actor::paintSpeech(Graphics *) {
	// Actor speech is rendered from Logic's six DOS speech slots.
}

void Actor::Speech::paint(Graphics *g) {
	if (_text.empty())
		return;

	g->paint(_image, Common::Point(_rect.left, _rect.top), Graphics::kPaintSemiTransparent | Graphics::kPaintPositionIsTop);
}

Direction Actor::Frame::operator-(const Actor::Frame &other) const {
	for (int i = 0; i < 8; i++)
		if (other._nexts[i] == _index)
			return mixeddirs[i];

	return kDirNone;
}

Direction operator>>(Direction _a, Direction _b) {
	assert(sizeof(Direction) == sizeof(int32));
	int32 a(_a), b(_b);

	b -= a;

	if (b < -3)
		b += 8;
	if (b > 4)
		b -= 8;

	if (b > 0)
		a++;
	if (b < 0)
		a--;

	if (a < 1)
		a += 8;
	if (a > 8)
		a -= 8;
	return *reinterpret_cast<Direction *>(&a);
}

template<int opcode>
Animation::Status Actor::opcodeHandler() {
	return Animation::opcodeHandler<opcode>();
}

template<int N>
void Actor::init_opcodes() {
	_handlers[N] = &Interspective::Actor::opcodeHandler<N>;
	init_opcodes<N - 1>();
}

template<>
void Actor::init_opcodes<-1>() {}

Animation::Status Actor::op(byte opcode) {
	return (this->*_handlers[opcode])();
}

#define OPCODE(n) template<> \
Animation::Status Actor::opcodeHandler<n>()

// Actor opcode dispatch. These handlers preserve the DOS ActorOp_* field
// model while still using the C++ walking state around _attentionNeeded
// (see Op_24 below) and _direction (see Op_17/0x23). Do not remove those
// C++ state writes without porting the rest of the DOS walk driver.

OPCODE(0x00) {
	// DOS Op_01 ScriptEnd @ 1000:68d3 — actually a 1-based-naming
	// confusion. C++ slot 0x00 corresponds to DOS Op_01 per the
	// off-by-1 dispatcher mapping. DOS clears field0+field1 + sets
	// g_actor_script_ended = 1 to break out of RunActorScript.
	// Actor script end clears the PC and stops dispatch without removing
	// the actor from Logic::_animations. Do not return kFrameDone: DOS
	// Op_01 does not write field+0x0a, so the shared interval fallback
	// would add a non-DOS countdown after the script PC is cleared.
	debugC(2, kDebugLevelActor, "actor opcode 0x00: ScriptEnd (clear PC, no remove) [DOS Op_01]");
	clearScriptPc();
	return kOk;
}

OPCODE(0x01) {
	// DOS Op_02 UnregisterAndEnd @ 1000:68e3: UnregisterActor + clear
	// field0/1 + sets g_actor_script_ended = 1. C++ slot 0x01 per the
	// off-by-1 mapping. UnregisterActor only clears the script PC and the
	// actor-table id slot; it does not reset sprite/timer/path fields.
	debugC(1, kDebugLevelActor, "actor opcode 0x01: UnregisterAndEnd (clear script PC) [DOS Op_02]");
	unregister();
	return kOk;
}

OPCODE(0x14) {
	// C++ slot 0x14 = DOS Op_15 WaitForSpeechSlot @ 1000:6af1.
	//   AX = g_render_actor_id;
	//   if (AX == g_main_character_id) {
	//       CALL CheckScrollDirty; if (no carry) → ADD BP,4 (advance);
	//   }
	//   CALL FindSpeechSlotById(this_actor);
	//   if (carry) → ADD BP,4 (advance, no slot found = silent);
	//   else → BP = ES:[BP+DI+2] (loop back to jump target = wait).
	// DOS CheckScrollDirty returns carry clear when DS:0x662f is nonzero;
	// in that case the protagonist advances even if a speech slot exists.
	// Otherwise, while this actor has an active speech slot, DOS replaces
	// BP with the embedded jump target and keeps the actor dispatch loop
	// running. It does not set the script-ended flag or field +0x0a.
	uint16 off = shift();
	if (Log.protagonist() == this && Log.scrollChanged()) {
		debugC(3, kDebugLevelAnimation, "actor opcode 0x14: WaitForSpeechSlot bypassed by scroll-dirty [DOS Op_15]");
		return kOk;
	}
	if (isSpeaking()) {
		_offset = off;
		debugC(3, kDebugLevelAnimation, "actor opcode 0x14: WaitForSpeechSlot looping (still speaking) [DOS Op_15]");
		return kOk;
	}
	debugC(3, kDebugLevelAnimation, "actor opcode 0x14: WaitForSpeechSlot done (silent) [DOS Op_15]");
	return kOk;
}

OPCODE(0x15) {
	// C++ slot 0x15 = DOS Op_16 PickAnimationSet @ 1000:6c3e —
	// FULL-FIDELITY port (matches DOS bounding-rect classifier byte-
	// for-byte; word-for-word step-toward state machine).
	//
	// Op_15 outer @ 1000:6c3e..1000:6cc9:
	//   PUSH ES, DI, BP, DS, SI                ; preserve regs
	//   PUSH DS; PUSH ds; POP DS                ; (DS-restore dance)
	//   CALL RetEmpty;                          ; CX=0, DX=0
	//   SUB CX, 0; SUB DX, 0;                   ; (no-op subtractions)
	//   ADD CX, [0x6659]; ADD DX, [0x665b]      ; CX/DX = camera origin
	//   CMP [0x6678], 0x80                      ; cursor_mode == 0x80?
	//   POP DS                                   ; restore DS
	//   JZ verb_mode_path                        ; → cycle pose toward cursor
	//   MOV [SI+0x68], 0                         ; not verb mode → reset pose
	//   JMP exit
	//
	//   verb_mode_path @ 1000:6c6b:
	//     PUSH DS, SI;                            ; protect actor record
	//     MOV BH,0; MOV BL, [SI+0x18];            ; BX = actor.field+0x18
	//                                              ; (rect-height portion)
	//     CALL CalcSpriteOffsetIfPlaced @ 1000:700f ; → BP = target pose
	//                                              ; (1..8 or 0x63 center)
	//     POP SI, DS;                             ; restore actor record
	//     MOV DX, BP                              ; DX = target
	//     MOV CL, [SI+0x68]                       ; CL = current pose
	//     CMP CL, 0x63; JZ snap                   ; current is center → snap
	//     CMP DL, 0x63; JZ snap                   ; target is center → snap
	//     SUB DL, CL                              ; DL = target - current
	//     JZ snap                                  ; equal → keep / snap
	//     JNS pos_delta                            ; signed positive
	//     ; negative delta path:
	//       MOV BP, 0x63                           ; default = snap to center
	//       CMP DL, 0xfa    ; (-6 in signed) JLE inc_with_wrap
	//                                              ; very large negative → wrap
	//       CMP DL, 0xfe    ; (-2 in signed) JL exit
	//                                              ; mid negative deadband
	//       DEC CL                                  ; small neg: dec current
	//       CMP CL, 1; JGE write
	//       MOV CL, 8                               ; wrap 0 → 8
	//       JMP write
	//     pos_delta:
	//       MOV BP, 0x63
	//       CMP DL, 6; JGE inc_with_wrap
	//       CMP DL, 2; JG exit                       ; mid pos deadband
	//       (FALLTHROUGH to inc)
	//     inc_with_wrap:
	//       INC CL; CMP CL, 8; JLE write
	//       MOV CL, 1                                 ; wrap 9 → 1
	//     write:
	//       MOV BP, CX
	//     snap:
	//       MOV AX, BP; MOV [SI+0x68], AL             ; field+0x68 = result
	//   exit:
	//     POP SI, BP, DI, ES, restore regs
	//     ADD BP, 2                                  ; advance script PC
	//
	// CalcSpriteOffsetIfPlaced @ 1000:700f:
	//   PUSH CX, DX                                  ; save camera (CX=cam_x, DX=cam_y)
	//   MOV CX, [SI+0x4]; MOV DX, [SI+0x6]            ; CX/DX = actor.field+0x4/0x6
	//   MOV AH, 0; MOV AL, [SI+0x17]; MOV BP, AX      ; BP = actor.field+0x17 (width)
	//   MOV AX, [SI+0x8]; CMP AX, 0xffff              ; sprite valid?
	//   JZ no_sprite_adjust
	//     PUSH BP, BX, CX, DX
	//     CALL CalcSpriteOffsetInGraphic              ; loads sprite metrics
	//     POP DX, CX, BX, BP
	//     MOV AL, [SI+0x8]; CBW; SUB CX, AX            ; CX -= sprite_offset_x
	//     MOV AL, [SI+0x9]; CBW; ADD DX, AX            ; DX += sprite_offset_y
	//   no_sprite_adjust:
	//     PUSH DX                                       ; save bottom_y
	//     SUB DX, BX                                    ; DX = top_y = bottom_y - height
	//     MOV AX, CX; ADD AX, BP                         ; AX = right_x = left_x + width
	//     POP BX                                         ; BX = bottom_y
	//     POP DI                                         ; DI = saved cursor_y (caller's CX)
	//     POP SI                                         ; SI = saved cursor_x (caller's DX)
	//                                                    ; (note: caller pushed DS, SI, BP, DI, ES,
	//                                                    ;  so this POP order recovers cursor coords)
	//     CMP DI, DX; JL above_path                      ; cursor_y < top_y → above
	//     CMP DI, BX; JG below_path                      ; cursor_y > bottom_y → below
	//     ; middle row:
	//       MOV BP, 7      ; left=W
	//       CMP SI, CX; JL exit
	//       MOV BP, 0x63    ; center
	//       CMP SI, AX; JL exit
	//       MOV BP, 3      ; right=E
	//       JMP exit
	//     above_path:                                       ; (label was "below" in some traces)
	//       MOV BP, 8      ; left=NW
	//       CMP SI, CX; JL exit
	//       MOV BP, 1      ; mid=N
	//       CMP SI, AX; JL exit
	//       MOV BP, 2      ; right=NE
	//       JMP exit
	//     below_path:
	//       MOV BP, 6      ; left=SW
	//       CMP SI, CX; JL exit
	//       MOV BP, 5      ; mid=S
	//       CMP SI, AX; JL exit
	//       MOV BP, 4      ; right=SE
	//   exit: RET
	//
	// = compass mapping (1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW,
	// 0x63=center). Cursor above the actor's bounding rect → row 8/1/2;
	// cursor inside vertical band → row 7/center/3; cursor below → row
	// 6/5/4. Within each row, left/middle/right partition by left_x and
	// right_x.
	//
	// Step-toward semantics @ 1000:6c79..1000:6cba:
	//   delta = target - current (signed byte arithmetic):
	//     delta == 0 OR current==0x63 OR target==0x63 -> snap.
	//     delta <= -6 -> INC current with wrap (short route via 8->1).
	//    -5..-3       -> write center (0x63).
	//    -2..-1       -> DEC current with wrap.
	//     1..2        -> INC current with wrap.
	//     3..5        -> write center (0x63).
	//     delta >= 6  -> DEC current with wrap (short route via 1->8).
	if (Log.cursorMode() != 0x80) {
		setFacingPose(0);
		debugC(3, kDebugLevelAnimation,
			   "actor opcode 0x15: PickAnimationSet — cursor_mode=%u != 0x80 → field+0x68 = 0",
			   Log.cursorMode());
		return kOk;
	}

	// Verb-mode classifier. Reproduces CalcSpriteOffsetIfPlaced:
	// build the actor's bounding rect from field+0x4/+0x6, current sprite
	// hot offsets, and actor width/height bytes (+0x17/+0x18); then test
	// cursor against rect bounds.
	const Common::Point screenCursor = Log.engine()->graphics()->cursorPosition();
	const int16 cursorX = int16(screenCursor.x + Log.cameraX());
	const int16 cursorY = int16(screenCursor.y + Log.cameraY());
	Common::Point spriteHotPoint(0, 0);
	if (mainSpriteId() != 0xffff) {
		const SpriteInfo info = _resources->getSpriteInfo(mainSpriteId());
		spriteHotPoint = info.hotPoint();
	}
	// adjusted_x = field+0x4 - sprite.hotLeft
	// adjusted_y = field+0x6 + sprite.hotTop
	const int16 adjustedX = int16(position().x) - spriteHotPoint.x;
	const int16 adjustedY = int16(position().y) + spriteHotPoint.y;
	const uint8 width = visibleSpriteWidth();   // BP in DOS
	const uint8 height = visibleSpriteHeight(); // BX in DOS
	// Bounding rect (DOS layout):
	//   left = adjustedX
	//   right = adjustedX + width
	//   top = adjustedY - height
	//   bottom = adjustedY
	const int16 leftX = adjustedX;
	const int16 rightX = adjustedX + int16(width);
	const int16 topY = adjustedY - int16(height);
	const int16 botY = adjustedY;

	uint8 target;
	if (cursorY < topY) {
		// Cursor above actor's rect.
		if (cursorX < leftX)
			target = 8; // NW
		else if (cursorX < rightX)
			target = 1; // N
		else
			target = 2; // NE
	} else if (cursorY > botY) {
		// Cursor below actor's rect.
		if (cursorX < leftX)
			target = 6; // SW
		else if (cursorX < rightX)
			target = 5; // S
		else
			target = 4; // SE
	} else {
		// Cursor in vertical band.
		if (cursorX < leftX)
			target = 7; // W
		else if (cursorX < rightX)
			target = 0x63; // center
		else
			target = 3; // E
	}

	const uint8 current = facingPose();
	if (current == 0x63 || target == 0x63 || target == current) {
		setFacingPose(target);
		debugC(3, kDebugLevelAnimation,
			   "actor opcode 0x15: PickAnimationSet snap target=%u current=%u",
			   target, current);
		return kOk;
	}
	// Reproduces DOS 8-bit signed branches @ 1000:6c79..1000:6cba.
	const int8 delta = int8(uint8(target - current));
	uint8 next = 0x63;
	if (delta < 0) {
		if (delta <= -6) {
			const uint8 n = uint8(current + 1);
			next = int8(n) <= 8 ? n : 1;
		} else if (delta >= -2) {
			const uint8 n = uint8(current - 1);
			next = int8(n) >= 1 ? n : 8;
		}
	} else {
		if (delta >= 6) {
			const uint8 n = uint8(current - 1);
			next = int8(n) >= 1 ? n : 8;
		} else if (delta <= 2) {
			const uint8 n = uint8(current + 1);
			next = int8(n) <= 8 ? n : 1;
		}
	}
	setFacingPose(next);
	debugC(3, kDebugLevelAnimation,
		   "actor opcode 0x15: PickAnimationSet rect=(%d..%d, %d..%d) cursor=(%d,%d) target=%u current=%u → %u (delta=%d)",
		   leftX, rightX, topY, botY, cursorX, cursorY, target, current, next, int(delta));
	return kOk;
}

OPCODE(0x16) {
	// C++ slot 0x16 = DOS Op_17 BranchIfAnimSetEquals @ 1000:6b17.
	//   AL = actor.field+0x68;
	//   CMP AL, embedded_byte;
	//   if (AL == embedded_byte): BP = ES:[BP+DI+0x2];   // jump
	//   else: ADD BP, 4;                                  // advance past 4-byte opcode
	//
	// Was using `_direction` as a proxy. Now correctly compares against
	// facingPose(), which is set by Op_15 (ActorOp_16 PickAnimationSet).
	const byte val = embeddedByte();
	const uint16 off = shift();
	const uint8 pose = facingPose();
	if (pose == val) {
		debugC(3, kDebugLevelAnimation,
			   "actor opcode 0x16: BranchIfAnimSetEquals val=%d field+0x68=%u → jump 0x%04x [DOS Op_17]",
			   val, pose, off);
		_offset = off;
	} else {
		debugC(3, kDebugLevelAnimation,
			   "actor opcode 0x16: BranchIfAnimSetEquals val=%d field+0x68=%u (no match) [DOS Op_17]",
			   val, pose);
	}
	return kOk;
}

OPCODE(0x17) {
	// C++ slot 0x17 = DOS Op_18 BranchIfMoodEquals @ 1000:6b29.
	//   if (actor.field+0x63 == embedded_byte):
	//       AX = ES:[BP+DI+2];
	//       DI = AX;
	//       actor.field+0x2 = AX;
	//       BP = 0;
	//   else fall through.
	// Mood is stored sparsely on Actor::_recordFields (set by ActorOp_24).
	byte val = embeddedByte();
	uint16 off = shift();
	const uint8 actorMood = mood();
	if (actorMood == val) {
		debugC(3, kDebugLevelAnimation, "actor opcode 0x17: BranchIfMoodEquals val=%d mood=%d → code offset 0x%04x [DOS Op_18]",
			   val, actorMood, off);
		setActorCodeOffset(off);
	} else {
		debugC(3, kDebugLevelAnimation, "actor opcode 0x17: BranchIfMoodEquals val=%d mood=%d (no match) [DOS Op_18]",
			   val, actorMood);
	}
	return kOk;
}

OPCODE(0x18) {
	// C++ slot 0x18 = DOS Op_19 SetField6d @ 1000:6b43. Reads 1 int16.
	// Stores the full word at actor.field+0x6d.
	uint16 val = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x18: SetField6d = 0x%04x [DOS Op_19]", val);
	setPendingReadyAnimation(val);
	return kOk;
}

OPCODE(0x19) {
	// C++ slot 0x19 = DOS Op_1a SetWalkFlagsAndEnd @ 1000:6a48. Reads
	// 1 int16, writes actor.field+0xa = arg and actor.field+0x8 = 0xffff,
	// then ends script.
	uint16 flags = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x19: SetWalkFlagsAndEnd flags=0x%04x [DOS Op_1a]", flags);
	_ticksLeft = flags;
	_explicitFrameDelay = true;
	setRecordTicksLeft(flags);
	setMainSprite(0xffff);
	return kFrameDone;
}

OPCODE(0x1a) {
	// C++ slot 0x1a = DOS Op_1b SetField12ClearFlag16 @ 1000:6b4e. Reads
	// embedded byte at +1 only. Animation::0x1a ("set z index") happens
	// to use embeddedByte too. DOS stores the byte in actor field+0x12,
	// and DrawAllRoomObjects uses that byte as the actor render layer.
	byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1a: SetField12ClearFlag16 = %d [DOS Op_1b]", v);
	setRecordDrawLayer(v);
	setAutoZoneLayerEnabled(false);
	_zIndex = int8(v);
	return kOk;
}

OPCODE(0x23) {
	// C++ slot 0x23 = DOS Op_24 SetMood @ 1000:6c13. Reads byte at +1.
	// Byte consumption matches. C++ uses this as "set _direction" — the
	// load-bearing C++ walking heuristic. The bytecode emits this
	// opcode with values 1..8 (compass values) which the C++ interprets
	// as direction. In DOS the byte is the actor's mood. The two
	// usages happen to overlap because mood values are also small.
	byte dir = embeddedByte();

	debugC(3, kDebugLevelAnimation, "actor opcode 0x23: SetMood = %d (C++ mirrors 1..8 to _direction) [DOS Op_24]", dir);

	switch (dir) {
	case 1:
		_direction = kDirUp;
		break;
	case 2:
		_direction = kDirUpRight;
		break;
	case 3:
		_direction = kDirRight;
		break;
	case 4:
		_direction = kDirDownRight;
		break;
	case 5:
		_direction = kDirDown;
		break;
	case 6:
		_direction = kDirDownLeft;
		break;
	case 7:
		_direction = kDirLeft;
		break;
	case 8:
		_direction = kDirUpLeft;
		break;
	default:
		// dir is the DOS mood byte (0..0xff). Values outside 1..8 are
		// valid mood values that don't map to a compass direction —
		// just leave _direction unchanged. (Was assert(false), which
		// fires under UB sanitizer in non-release builds.)
		debugC(4, kDebugLevelAnimation, "actor opcode 0x23: mood %d (no direction match)", dir);
		break;
	}

	// Also store the actual mood byte for any future readers (e.g. when
	// Op_18 BranchIfMoodEquals gets ported to the real DOS field check).
	setMood(dir);
	return kOk;
}

OPCODE(0x24) {
	// C++ slot 0x24 = DOS Op_25 SetField65 @ 1000:6c1e. Reads byte at +1.
	// Byte consumption matches. C++ uses this as "set _attentionNeeded"
	// — the load-bearing C++ walking trigger. The bytecode emits this
	// between walk frames so the next frame can advance. Do not remove
	// the _attentionNeeded write without porting the DOS walk driver.
	// Also stash the field value for any future readers.
	byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x24: SetField65 = %d (C++ also sets _attentionNeeded for walking) [DOS Op_25]", v);
	setAttentionNeeded(v != 0);
	return kOk;
}

// ============================================================================
// DOS-faithful overrides for the movement / frame-state opcode family
// (DOS Op_03..Op_0a, C++ slots 0x02..0x09).
//
// Most of these ops end the script (Op_06..0a; DOS sets
// g_actor_script_ended = 1) and copy actor byte +0x10 into word +0x0a
// before returning kFrameDone. C++ maps those fields to _interval and
// _ticksLeft, so the ending handlers call copyIntervalToTicks() before
// they stop the per-tick opcode loop. Ops that don't end the script
// return kOk (continue with the next opcode this tick).
//
// Op_04/Op_05 (slots 0x03/0x04) are NOT SetCurrentFrame /
// SetCurrentFrameFromGlobal. DOS Op_04 @ 1000:6912 / Op_05 @ 1000:691d both write to
// actor [SI+0x10] = kOffsetInterval (Animation::_interval), NOT
// field+0x61 (_frame). The slot-0x03/0x04 overrides are kept explicit
// for symmetry with the rest of the family.
// ============================================================================

OPCODE(0x02) {
	// C++ slot 0x02 = DOS Op_03 SetPosition @ 1000:6900. 2 shifts.
	// Writes actor.x = word, actor.y = word. Doesn't end script.
	// Keep the override explicit so actor position writes remain grouped
	// with the movement opcode family.
	const uint16 x = shift();
	const uint16 y = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x02: SetPosition (%u,%u) [DOS Op_03]", x, y);
	setRawPosition(Common::Point((int16)x, (int16)y));
	return kOk;
}

OPCODE(0x03) {
	// C++ slot 0x03 = DOS Op_04 SetInterval @ 1000:6912. 1 shift.
	//   MOV AX, ES:[BP+DI+2]      ; arg word
	//   MOV byte ptr [SI+0x10], AL ; field+0x10 = AL  (interval byte)
	//   ADD BP, 4 / RET
	// field+0x10 = kOffsetInterval = Animation::_interval (NOT frame —
	// _frame is field+0x61 per Op_7a / moveTo comment). Write _interval,
	// matching DOS [SI+0x10], and do not reset pathfinding frame state.
	const uint16 word = shift();
	const uint8 interval = uint8(word);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x03: SetInterval %u [DOS Op_04]", interval);
	setRawInterval(interval);
	return kOk;
}

OPCODE(0x04) {
	// C++ slot 0x04 = DOS Op_05 SetIntervalFromGlobal @ 1000:691d. 1 shift.
	//   reads global word var at index (arg/2)
	//   writes its low byte to [SI+0x10] = Animation::_interval.
	// Writes _interval per DOS; do not update _frame.
	const uint16 offset = shift();
	const uint16 word = READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	const uint8 interval = uint8(word);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x04: SetIntervalFromGlobal var[%u/2]=0x%04x → %u [DOS Op_05]",
		   offset, word, interval);
	setRawInterval(interval);
	return kOk;
}

OPCODE(0x05) {
	// C++ slot 0x05 = DOS Op_06 WalkRelativeWithFrame @ 1000:6939.
	// Reads 2 signed bytes (dx, dy) + 1 word (target frame).
	//   actor.field+0x4 += dx;             ; position x
	//   actor.field+0x6 += dy;             ; position y
	//   actor.field+0x8  = target_frame;   ; sprite/target ID
	//   actor.field+0xa  = zero-extended actor.field+0x10
	//   g_actor_script_ended = 1;
	//
	// Despite the "WalkRelative" name, DOS does NOT engage walk
	// pathfinding. It only updates the actor's relative position and
	// sprite-target field. Walk pathfinding (FindActorPath) is engaged
	// separately by other opcodes.
	const int8 dx = shiftByte();
	const int8 dy = shiftByte();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x05: WalkRelativeWithFrame d=(%d,%d) target=%u [DOS Op_06]",
		   dx, dy, target);
	setRawPosition(Common::Point(_position.x + dx, _position.y + dy));
	setMainSprite(target); // sprite ID write (DOS field+0x8 analog)
	copyIntervalToTicks(); // DOS field+0xa = zero-extended field+0x10.
	return kFrameDone;
}

OPCODE(0x06) {
	// C++ slot 0x06 = DOS Op_07 SetTargetFrame @ 1000:69a4. 1 shift.
	//   actor.field+0x8  = target;            ; sprite/target ID
	//   actor.field+0xa  = zero-extended actor.field+0x10
	//   g_actor_script_ended = 1;              ; end this script run
	//
	// DOS does NOT engage walk here. It only updates the sprite-target
	// field. Some other code (e.g., the actor's drawing or animation
	// pipeline) reads field+0x8 later.
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x06: SetTargetFrame %u [DOS Op_07]", target);
	// Update sprite/target ID (DOS field+0x8 analog).
	setMainSprite(target);
	// DOS field+0xa byte copy: actor.field+0xa = zero-extended field+0x10.
	copyIntervalToTicks();
	// g_actor_script_ended = 1 → script run ends. C++ kFrameDone.
	return kFrameDone;
}

OPCODE(0x07) {
	// C++ slot 0x07 = DOS Op_08 SetTargetFrameFromGlobal @ 1000:69b0.
	// 1 shift. Reads global word var at index (offset/2), uses as
	// indirect target. Same field-write semantic as 0x06 — NOT a walk.
	const uint16 offset = shift();
	const uint16 target = READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	debugC(3, kDebugLevelAnimation, "actor opcode 0x07: SetTargetFrameFromGlobal var[%u/2]=%u [DOS Op_08]",
		   offset, target);
	setMainSprite(target);
	copyIntervalToTicks();
	return kFrameDone;
}

OPCODE(0x08) {
	// C++ slot 0x08 = DOS Op_09 WalkRelative @ 1000:697c. Reads 2 signed
	// bytes (dx, dy) only. Adds to actor.x/y, copies field+0x10 to +0x0a,
	// and ends script. Animation::0x08 ("move by") happens to do the same
	// _position update with the same byte consumption — the existing
	// fall-through was correct for position but didn't end the script or
	// perform the actor-field copy. Override returns kFrameDone.
	const int8 dx = shiftByte();
	const int8 dy = shiftByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x08: WalkRelative d=(%d,%d) [DOS Op_09]", dx, dy);
	setRawPosition(Common::Point(_position.x + dx, _position.y + dy));
	copyIntervalToTicks();
	return kFrameDone;
}

OPCODE(0x09) {
	// C++ slot 0x09 = DOS Op_0a WalkAbsolute @ 1000:6991. 2 shifts.
	// Writes actor.x/y = (word, word), copies field+0x10 to +0x0a, and
	// ends script.
	const uint16 x = shift();
	const uint16 y = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x09: WalkAbsolute (%u,%u) [DOS Op_0a]", x, y);
	setRawPosition(Common::Point((int16)x, (int16)y));
	copyIntervalToTicks();
	return kFrameDone;
}

OPCODE(0x0a) {
	// C++ slot 0x0a = DOS Op_0b WalkAbsoluteWithFrame @ 1000:6962.
	// 3 shifts: x, y, sprite/target field.
	//   actor.field+0x4  = x;
	//   actor.field+0x6  = y;
	//   actor.field+0x8  = target_frame;     ; sprite/target ID field
	//   actor.field+0xa  = zero-extended actor.field+0x10
	//   g_actor_script_ended = 1;
	//
	// Despite the "WalkAbsolute" name, DOS does NOT engage walk
	// pathfinding. Just position + sprite-target field updates.
	const uint16 x = shift();
	const uint16 y = shift();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0a: WalkAbsoluteWithFrame (%u,%u) target=%u [DOS Op_0b]",
		   x, y, target);
	setRawPosition(Common::Point((int16)x, (int16)y));
	setMainSprite(target);
	copyIntervalToTicks();
	return kFrameDone;
}

OPCODE(0x0d) {
	// C++ slot 0x0d = DOS Op_0e SetTimerAndSkip @ 1000:6a5e. Reads embedded
	// byte at +1 (skip-timer value), writes to actor.field+0x11. Also DOS
	// stashes BP+2 (script PC after the opcode) into actor.field+0xe — a
	// "resume point" used by the timer loop. Stash via the sparse
	// _recordFields map; field+0x11 is the per-actor skip-timer that DOS
	// Op_0f decrements.
	const byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0d: SetTimerAndSkip = %u [DOS Op_0e]", v);
	setSkipTimerCount(v);
	// The dispatcher has already advanced past the 2-byte opcode header,
	// matching DOS's ADD BP,2 before the store.
	const uint16 nextPC = _offset;
	setSkipTimerResumePc(nextPC);
	return kOk;
}

OPCODE(0x0e) {
	// C++ slot 0x0e = DOS Op_0f DecrementTimer @ 1000:6a6c. 0 args.
	// DOS advances BP by 2, decrements actor.field+0x11 when nonzero,
	// and if the post-decrement value is still nonzero jumps back to
	// actor.field+0x0e.
	const uint8 cur = skipTimerCount();
	if (cur != 0) {
		const uint8 next = uint8(cur - 1);
		setSkipTimerCount(next);
		if (next != 0) {
			const uint16 resume = skipTimerResumePc();
			_offset = resume;
			debugC(4, kDebugLevelAnimation, "actor opcode 0x0e: DecrementTimer %u → %u, loop 0x%04x [DOS Op_0f]",
				   cur, next, resume);
		} else {
			debugC(4, kDebugLevelAnimation, "actor opcode 0x0e: DecrementTimer %u → 0, fall through [DOS Op_0f]", cur);
		}
	} else {
		debugC(5, kDebugLevelAnimation, "actor opcode 0x0e: DecrementTimer (already 0) [DOS Op_0f]");
	}
	return kOk;
}

OPCODE(0x0f) {
	// C++ slot 0x0f = DOS Op_10 @ 1000:6a7e. The Ghidra label says "NoOp"
	// but the disassembly shows `MOV BP, ES:[BP+DI+2] / RET` — it's an
	// UNCONDITIONAL JUMP to the word at script[+2..+3]. 4-byte opcode.
	uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0f: Jump to 0x%04x [DOS Op_10]", target);
	_offset = target;
	return kOk;
}

// ============================================================================
// Actor opcode overrides that depend on the off-by-one dispatcher mapping.
//
// C++ OPCODE(N) handles memory byte (-N-1 as int8), which the DOS
// dispatcher labels as Op_(N+1). For example:
//   memory byte 0xff → C++ OPCODE(0x00) → DOS Op_01 (ScriptEnd)
//   memory byte 0xed → C++ OPCODE(0x12) → DOS Op_13 (JumpIfByteVar)
//   memory byte 0xdc → C++ OPCODE(0x23) → DOS Op_24 (SetMood)
//
// Each handler below is named per its TRUE DOS opcode (= C++ slot + 1).
// Byte consumption MUST match DOS exactly — script PC alignment depends on
// it. Every DOS-visible side effect is modeled directly or through the
// sparse actor-field mirrors used by the C++ renderer/scheduler.
// ============================================================================

OPCODE(0x10) {
	// C++ slot 0x10 = DOS Op_11 SetGlobalByteFlag @ 1000:6a83. Reads
	// 1 int16 offset (1 shift = 2 bytes), writes 1 to global byte var.
	uint16 var = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x10: SetGlobalByteFlag var %d = 1 [DOS Op_11]", var);
	*_resources->getGlobalByteVariable(var) = 1;
	return kOk;
}

OPCODE(0x11) {
	// C++ slot 0x11 = DOS Op_12 ClearGlobalByteFlag @ 1000:6a9d. Reads
	// 1 int16 offset (1 shift), writes 0 to global byte var.
	uint16 var = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x11: ClearGlobalByteFlag var %d = 0 [DOS Op_12]", var);
	*_resources->getGlobalByteVariable(var) = 0;
	return kOk;
}

OPCODE(0x12) {
	// C++ slot 0x12 = DOS Op_13 @ 1000:6ab7. Ghidra labels it "NoOp" but
	// the disassembly is JumpIfByteVar: reads var index at script[+2..+3],
	// reads jump target at script[+4..+5], if global byte var is non-zero
	// jump to target, else ADD BP,6 (skip 6-byte opcode). 2 shifts total.
	uint16 var = shift();
	uint16 off = shift();
	byte ok = *_resources->getGlobalByteVariable(var);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x12: JumpIfByteVar var=%u off=0x%04x val=%u [DOS Op_13]", var, off, ok);
	if (ok)
		_offset = off;
	return kOk;
}

OPCODE(0x13) {
	// C++ slot 0x13 = DOS Op_14 BranchIfRandomMatch @ 1000:6ad9. 6-byte
	// opcode: reads value at script[+2..+3], calls GetRandomBitsBelow with
	// that same value, and jumps to script[+4..+5] if returned BX equals
	// the value. DOS random result is 1..value, so this is a 1/value
	// branch for nonzero values.
	uint16 max = shift();
	uint16 off = shift();
	const uint16 roll = max ? uint16(Eng.getRandom(uint16(max - 1)) + 1) : 0;
	debugC(3, kDebugLevelAnimation, "actor opcode 0x13: BranchIfRandomMatch max=%u roll=%u off=0x%04x [DOS Op_14]",
		   max, roll, off);
	if (max != 0 && roll == max)
		_offset = off;
	return kOk;
}

// Per-actor flag-byte ops (DOS field map):
//   field +0x14 — cleared by C++ 0x1d (DOS Op_1e ClearFlag14)
//   field +0x15 — cleared by C++ 0x1e (DOS Op_1f ClearFlag15)
//                 set     by C++ 0x1f (DOS Op_20 SetFlag15)

OPCODE(0x1d) {
	// C++ slot 0x1d = DOS Op_1e ClearFlag14 @ 1000:6bcd. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1d: ClearFlag14 [DOS Op_1e]");
	setRecordFlag14(false);
	return kOk;
}

OPCODE(0x1e) {
	// C++ slot 0x1e = DOS Op_1f ClearFlag15 @ 1000:6bd5. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1e: ClearFlag15 [DOS Op_1f]");
	setRecordFlag15(false);
	return kOk;
}

OPCODE(0x1f) {
	// C++ slot 0x1f = DOS Op_20 SetFlag15 @ 1000:6bdd. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1f: SetFlag15 [DOS Op_20]");
	setRecordFlag15(true);
	return kOk;
}

OPCODE(0x20) {
	// C++ slot 0x20 = DOS Op_21 SetCallbackPointer @ 1000:6be5. 12-BYTE
	// opcode (ADD BP, 0xc). DOS:
	//   actor.field+0x5f = BP+DI+2  (callback offset = next opcode)
	//   actor.field+0x5d = ES       (callback segment)
	//   advance BP by 12 (skip past 10 trailing bytes — inline callback body)
	// The "callback offset" is the address of the inline body that follows
	// the opcode; later code can resume execution there. We capture the
	// PC of the byte right after the 2-byte opcode header (the start of
	// the 10-byte body), then skip the body by 5 shifts.
	const uint16 callbackPC = _baseOffset + _offset;
	setActorCallback(actorCodeSegmentTag(_base), callbackPC);
	// Skip the 10-byte inline body.
	for (int i = 0; i < 5; i++)
		(void)shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x20: SetCallbackPointer cbPC=0x%04x [DOS Op_21]", callbackPC);
	return kOk;
}

OPCODE(0x21) {
	// C++ slot 0x21 = DOS Op_22 SetCallbackRelative @ 1000:6bf4. 1 shift
	// (signed int16 offset). DOS:
	//   if (arg != 0)  callback_off = DI + arg   (segment-relative)
	//   else           callback_off = 0          (clear)
	//   callback_seg = ES
	const int16 off = (int16)shift();
	uint16 cbOff = 0;
	if (off != 0)
		cbOff = uint16(int32(_baseOffset) + int32(off));
	setActorCallback(actorCodeSegmentTag(_base), cbOff);
	debugC(2, kDebugLevelAnimation, "actor opcode 0x21: SetCallbackRelative off=%d → cbOff=0x%04x [DOS Op_22]",
		   off, cbOff);
	return kOk;
}

OPCODE(0x22) {
	// C++ slot 0x22 = DOS Op_23 ClearCallback @ 1000:6c0a. 0 args.
	// Writes 0xffff to actor.field+0x5d (clears the callback segment;
	// effectively cancels any callback set by Op_20/Op_21). DOS does not
	// clear the offset word at +0x5f.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x22: ClearCallback [DOS Op_23]");
	clearActorCallback();
	return kOk;
}

OPCODE(0x25) {
	// C++ slot 0x25 = DOS ActorOp_26 PlaySfx @ 1000:6c29.
	// 4-byte opcode (opcode + pad + 2-byte sfx index).
	//   AX = ES:[BP+DI+0x2];     ; AX = sfx index
	//   CALL DispatchSfxRangeCheck(AX);
	//   ADD BP, 0x4;
	// = "play sfx index (range-check gated by active-slot bounds)".
	// Routes through Logic::engine()->sound()->rangeCheck — same DOS
	// SFX dispatcher as Op_f2.
	const uint16 sfx = shift();
	if (Sound *snd = Log.engine()->sound())
		snd->rangeCheck(sfx);
	debugC(2, kDebugLevelAnimation, "actor opcode 0x25: PlaySfx %u [DOS ActorOp_26]", sfx);
	return kOk;
}

// ============================================================================
// DOS walk-driver opcodes. These use actor fields +0x61/+0x62 as the
// current/path target frames; the word argument in 0x0b writes field+0x8
// (sprite/target ID), not field+0x62.
// ============================================================================

OPCODE(0x0b) {
	// C++ slot 0x0b = DOS Op_0c FaceAndWalkWithFrame @ 1000:69cd. Embedded
	// current-frame byte at +1, sprite/target word at +2..+3. DOS:
	//   actor.currentFrame (field+0x61) = byte
	//   SetActorPosition(); GetActorOffset(field+0x62)
	//   LookupActorAndStartPath()
	//   actor.sprite/target field (field+8) = target
	//   actor.tick countdown (field+0xa) = zero-extended field+0x10
	//   end script
	// In the C++ model, field+0x61 maps to _frame and field+0x62 maps to
	// _nextFrame. The path target is the pre-existing _nextFrame; the
	// word argument is the sprite field+8, not field+0x62.
	const byte face = embeddedByte();
	const uint16 sprite = shift();
	const uint16 walkTarget = _nextFrame;
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0b: FaceAndWalkWithFrame face=%u sprite=%u walkTarget=%u [DOS Op_0c]",
		   face, sprite, walkTarget);
	setFrame(face);
	if (walkTarget != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(walkTarget);
	setMainSprite(sprite);
	copyIntervalToTicks();
	return kFrameDone;
}

OPCODE(0x0c) {
	// C++ slot 0x0c = DOS Op_0d FaceAndWalk @ 1000:6a0e. Embedded byte at
	// +1 only — same as 0x0b but no sprite field+8 write. Sets
	// field+0x61, updates actor position, starts path toward field+0x62,
	// copies field+0x10 to +0x0a, and ends script.
	const byte face = embeddedByte();
	const uint16 walkTarget = _nextFrame;
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0c: FaceAndWalk face=%u [DOS Op_0d]", face);
	setFrame(face);
	if (walkTarget != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(walkTarget);
	copyIntervalToTicks();
	return kFrameDone;
}

OPCODE(0x1b) {
	// C++ slot 0x1b = DOS Op_1c QueueMoveSlotMode0 @ 1000:6b5d. 3 shifts
	// (a, b, c). Finds first free slot in actor.field+0x19 (8 entries),
	// writes (b, c, a, mode=0). DOS layout per disasm:
	//   slot.field2 = arg1 (b)
	//   slot.field4 = arg2 (c)
	//   slot.field0 = arg3 (a)
	//   slot.field6 = mode (0)
	// Overflow sets g_pendingErrorCode = 0xc.
	const uint16 arg1 = shift();
	const uint16 arg2 = shift();
	const uint16 arg3 = shift();
	const bool ok = queueMoveSlot(MoveSlot(arg3, arg1, arg2, 0));
	if (!ok)
		Log.setPendingError(0x0c);
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1b: QueueMoveSlotMode0 (a=%u,b=%u,c=%u) %s [DOS Op_1c]",
		   arg3, arg1, arg2, ok ? "queued" : "DROPPED (queue full)");
	return kOk;
}

OPCODE(0x1c) {
	// C++ slot 0x1c = DOS Op_1d QueueMoveSlotMode1 @ 1000:6b95. Same as
	// 0x1b but mode=1.
	const uint16 arg1 = shift();
	const uint16 arg2 = shift();
	const uint16 arg3 = shift();
	const bool ok = queueMoveSlot(MoveSlot(arg3, arg1, arg2, 1));
	if (!ok)
		Log.setPendingError(0x0c);
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1c: QueueMoveSlotMode1 (a=%u,b=%u,c=%u) %s [DOS Op_1d]",
		   arg3, arg1, arg2, ok ? "queued" : "DROPPED (queue full)");
	return kOk;
}

} // End of namespace Interspective
