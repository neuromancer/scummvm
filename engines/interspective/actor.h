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

#ifndef INTERSPECTIVE_ACTOR_H
#define INTERSPECTIVE_ACTOR_H

#include "common/array.h"
#include "common/endian.h"
#include "common/hashmap.h"
#include "common/queue.h"
#include "common/rect.h"

#include "interspective/animation.h"
#include "interspective/value.h"

namespace Common {
class Serializer;
}

namespace Interspective {
//

class MainDat;
class Program;
class Sprite;

enum Direction {
	kDirNone = 0,
	kDirUp,
	kDirUpRight,
	kDirRight,
	kDirDownRight,
	kDirDown,
	kDirDownLeft,
	kDirLeft,
	kDirUpLeft,
	kDirCenter = 99
};

Direction operator>>(Direction a, Direction b);

class Puppeteer {
public:
	enum Offsets {
		kActorId = 0,
		kMainCode = 2,
		kMoveAnimators = 4,
		kTurnAnimators = 0x14,
		kSize = 0x24
	};
	Puppeteer() : _offset(0), _actorId(0) {}
	Puppeteer(const byte *data) { parse(data); }

	uint16 mainCodeOffset() const { return _offset; }
	uint16 offset() const { return _offset; }
	uint16 actorId() const { return _actorId; }
	bool valid() const { return _offset; }
	CodePointer moveAnimator(Direction d);
	CodePointer turnAnimator(Direction d);

private:
	void parse(const byte *data);

	uint16 _actorId;
	uint16 _offset;
	uint16 _animators[16];
};

class Actor : public Animation {
	//
public:
	class Frame {
	public:
		Frame() : _index(0), _position(999, 999), _nexts(), _nextCount(0xff) {
			// _index = 0 sentinel — the (999, 999) position marks the
			// frame as "invalid / uninitialized". Without this explicit
			// zero-init, _index was uninitialized memory and reads could
			// corrupt _frame when nextFrame() popped a sentinel from
			// _framequeue.
			_nexts.resize(8);
		}
		Frame(Common::Point pos, Common::Array<byte> n, uint16 i) : _position(pos), _nexts(n), _index(i), _nextCount(0xff) {}

		Common::Point position() const { return _position; }
		const Common::Array<byte> &nexts() const { return _nexts; }
		uint16 index() const { return _index; }

		// Runtime frame mutation — DOS Op_e0 (InvalidateFrame) writes
		// the (999,999) sentinel that findPath skips. DOS Op_e1
		// (SetFramePosition) overwrites the screen position. Both are
		// applied to DOS's global frame-table backing memory; room loads
		// reset the active count, not the backing records.
		void setPosition(Common::Point p) { _position = p; }
		void invalidate() { _position = Common::Point(999, 999); }

		Direction operator-(const Frame &other) const;
		bool operator==(const Frame &other) const {
			return _index == other._index;
		}
		byte nextCount() const {
			if (_nextCount == 0xff) {
				byte ct = 0;
				for (int i = 0; i < 8; i++)
					if (_nexts[i])
						ct++;
				_nextCount = ct;
			}

			return _nextCount;
		}

	private:
		uint16 _index;
		Common::Point _position;
		Common::Array<byte> _nexts;
		mutable byte _nextCount;
	};

	class Speech {
	public:
		Speech() : _pageIndex(0), _ticksLeft(0), _actor(0), _color(0), _image(0) {}
		~Speech();
		Speech(Actor *parent, const Common::String &text, uint16 maxLines);
		// Variant for Op_40/0x42/0x44: use the target-speech paging mode
		// while anchoring at the DOS-computed speaker position.
		Speech(Actor *parent, const Common::String &text, Common::Point overridePos, uint16 maxLines);
		bool active() const { return !_text.empty(); }
		const Common::String &text() const { return _text; }
		void callWhenDone(const CodePointer &cp);
		void paint(Graphics *g);
		void tick();

	private:
		struct SpeechCallback {
			SpeechCallback() : mode(0), hasMode(false) {}
			SpeechCallback(const CodePointer &p, uint16 m, bool h)
				: callback(p), mode(m), hasMode(h) {}
			CodePointer callback;
			uint16 mode;
			bool hasMode;
		};
		void startPage(uint page);
		Common::Array<Common::String> _pages;
		uint _pageIndex;
		Common::String _text;
		Common::Queue<SpeechCallback> _cb;
		uint16 _ticksLeft;
		Actor *_actor;
		Common::Point _anchor;
		byte _color;
		Common::Rect _rect;
		Interspective::Sprite *_image;
	};

	friend class MainDat;
	friend class Program;
	enum {
		Size = 0x71
	};

	enum ActorOffsets {
		kOffsetSegment = 0,
		// DOS field +2 is DI: the script-base offset within the selected segment.
		kOffsetScriptBase = 2,
		kOffsetLeft = 4,
		kOffsetTop = 6,
		kOffsetMainSprite = 8,
		kOffsetTicksLeft = 0xa,
		// DOS field +0x0c is BP: the current PC relative to kOffsetScriptBase.
		kOffsetScriptPc = 0xc,
		kOffsetSkipTimerResumePc = 0xe,
		kOffsetInterval = 0x10,
		kOffsetSkipTimerCount = 0x11,
		kOffsetDrawLayer = 0x12,
		kOffsetSecondaryZone = 0x13,
		kOffsetFlag14 = 0x14,
		kOffsetFlag15 = 0x15,
		kOffsetAutoZoneLayer = 0x16,
		kOffsetVisibleSpriteWidth = 0x17,
		kOffsetVisibleSpriteHeight = 0x18,
		kOffsetMoveSlots = 0x19,
		kOffsetActorCallbackSegment = 0x5d,
		kOffsetActorCallbackOffset = 0x5f,
		kOffsetRoom = 0x59,
		kOffsetFrame = 0x61,
		kOffsetTargetFrame = 0x62,
		kOffsetMood = 0x63,
		kOffsetConfused = 0x64,
		kOffsetAttentionNeeded = 0x65,
		kOffsetTurnTieBreaker = 0x66,
		kOffsetReadyMarker = 0x67,
		kOffsetFacingPose = 0x68,
		kOffsetReadyCallbackOffset = 0x69,
		kOffsetWalkQueueLength = 0x6b,
		kOffsetPendingReadyAnimation = 0x6d,
		kOffsetMovementWaitActive = 0x6f,
		kOffsetSpeechColor = 0x70
	};

	virtual bool isActor() const { return true; }

	void setFrame(uint16 f);
	uint16 frameId() const { return _frame; }
	uint16 targetFrameId() const { return _nextFrame; }
	Common::Point position() const { return _position; }
	uint16 ticksLeft() const { return _ticksLeft; }
	uint8 interval() const { return uint8(_interval); }
	void setRawPosition(Common::Point p) {
		_position = p;
		setRecordPosition(p);
	}
	void setRawTicksLeft(uint16 ticks) {
		_ticksLeft = ticks;
		setRecordTicksLeft(ticks);
	}
	void setRawInterval(uint8 interval) {
		_interval = int8(interval);
		setRecordInterval(interval);
	}
	void setRawFrame(uint16 frame) {
		_frame = frame;
		setRecordFrame(frame);
	}
	void setRawTargetFrame(uint16 frame) {
		_nextFrame = frame;
		setRecordTargetFrame(frame);
	}
	void clearMoveQueue() {
		_framequeue.clear();
		setWalkQueueLength(0);
	}
	void setRawMainSprite(uint16 sprite) { setMainSprite(sprite); }
	void setRawSpriteTarget(uint16 target) {
		setMainSprite(target);
		setRawTargetFrame(target);
	}
	void moveTo(uint16 f);
	static Common::List<Frame> findPath(Frame from, uint16 to);

	uint16 room() const { return _room; }
	void setRoom(uint16, uint16 frame = 0, uint16 nextFrame = 0);

	// Update only the room field, without touching frame/position/script.
	// Used by Logic::doChangeRoom to keep the protagonist's _room in
	// sync with _currentRoom so the gating in Actor::tick() doesn't
	// short-circuit.
	void forceRoom(uint16 r) {
		_room = r;
		setRecordRoom(r);
	}

	// DOS-aligned room/frame placement that does NOT reset the actor's
	// animation script. Mirrors the field assignments in
	// Op_7a_PlaceActorInRoomXY @ 1000:4443: writes _room (field+0x59),
	// _frame (field+0x61), _nextFrame (field+0x62), clears the walk word
	// at +0x6b, then runs SetActorPosition (frame X/Y -> _position).
	// InitActorState in DOS preserves the actor's existing code offset,
	// so we mirror that by leaving _base/_offset alone (unlike setRoom
	// which jumps the script to puppeteer.mainCode and clears the sprite
	// — too aggressive and crashes for actors whose puppeteer isn't
	// initialised yet).
	void placeIn(uint16 room, uint16 frame, uint16 nextFrame = 0);

	bool isFine() const;
	bool scriptActive() const { return _base != 0; }
	bool animReady() const;
	bool idleReady() const;

	// 1-based DOS actor id (matches DAT_1cb5_666c when this actor's
	// script is dispatching). Set by the loader at construction.
	uint16 id() const { return _id; }
	void setId(uint16 id) { _id = id; }

	void setAnimation(const CodePointer &anim);
	void setAnimation(uint16);
	bool hasScriptEntryPoint() const { return recordScriptBase() != 0; }

	void clearScriptPc();
	void unregister();
	void prepareRoomEntryActiveActor();
	void hide();
	void callMe(const CodePointer &cp);
	void callMeWithMode(const CodePointer &cp, uint16 mode);
	void tellMe(const CodePointer &cp, uint16 timeout);
	void tellMeWithMode(const CodePointer &cp, uint16 timeout, uint16 mode);

	struct RoomScriptWaitSnapshot {
		RoomScriptWaitSnapshot() : runMode(0), position(0), valid(false) {}
		CodePointer callback;
		uint16 runMode;
		uint16 position;
		bool valid;
	};
	bool takeRoomScriptWait(RoomScriptWaitSnapshot &snapshot);
	void restoreRoomScriptWait(const RoomScriptWaitSnapshot &snapshot);
	bool hasRoomScriptWaitMode(uint16 mode) const;
	void dropRoomScriptWaitMode(uint16 mode);
	void processWaitCallbacks();
	enum RoomScriptWaitDispatch {
		kNoRoomScriptWait,
		kRoomScriptWaitPending,
		kRoomScriptWaitDispatched
	};
	RoomScriptWaitDispatch dispatchReadyRoomScriptWaitMode(uint16 mode);

	bool isSpeaking() const;
	const Common::String &speechText() const;
	void stopSpeaking();
	void setAttentionNeeded(bool v) {
		_attentionNeeded = v;
		setRecordAttentionNeeded(v);
	}
	void callMeWhenSilent(const CodePointer &cp);
	void say(const Common::String &text, uint16 maxLines = 0);
	void sayAtPos(const Common::String &text, Common::Point pos, uint16 maxLines = 0);
	/// Position to anchor the speech bubble.
	Common::Point getSpeechPosition() const;

	bool isMoving() const;
	void callMeWhenStill(const CodePointer &cp);

	Animation::Status tick();
	void paint(Graphics *g);
	void paint(Graphics *g, uint16 drawMode);
	void paintSpeech(Graphics *g);
	void synchronize(Common::Serializer &s);
	void syncWaitCallbacks(Common::Serializer &s);

	void toggleDebug();

	void setPuppeteer(const Puppeteer &p) { _puppeteer = p; }
	uint8 visibleSpriteWidth() const { return recordByte(kOffsetVisibleSpriteWidth); }
	uint8 visibleSpriteHeight() const { return recordByte(kOffsetVisibleSpriteHeight); }
	uint16 drawModeForLayer(int16 layer) const;
	uint8 speechColor() const { return recordByte(kOffsetSpeechColor); }
	bool movementWaitActive() const { return recordByte(kOffsetMovementWaitActive) != 0; }
	void setMovementWaitActive(bool active) { setRecordByte(kOffsetMovementWaitActive, active ? 1 : 0); }
	uint16 walkQueueLength() const { return recordWord(kOffsetWalkQueueLength); }
	void setWalkQueueLength(uint16 length) { setRecordWord(kOffsetWalkQueueLength, length); }
	uint8 readyMarker() const { return recordByte(kOffsetReadyMarker); }
	void setReadyMarker(uint8 marker) { setRecordByte(kOffsetReadyMarker, marker); }
	uint16 readyCallbackOffset() const { return recordWord(kOffsetReadyCallbackOffset); }
	void setReadyCallbackOffset(uint16 callback) { setRecordWord(kOffsetReadyCallbackOffset, callback); }
	uint8 facingPose() const { return recordByte(kOffsetFacingPose); }
	void setFacingPose(uint8 pose) { setRecordByte(kOffsetFacingPose, pose); }
	uint8 mood() const { return recordByte(kOffsetMood); }
	void setMood(uint8 mood) { setRecordByte(kOffsetMood, mood); }
	bool needsAttention() const { return recordByte(kOffsetAttentionNeeded) != 0; }
	uint8 turnTieBreaker() const { return recordByte(kOffsetTurnTieBreaker); }
	void setTurnTieBreaker(uint8 tie) { setRecordByte(kOffsetTurnTieBreaker, tie); }
	void setSpeechColor(uint8 color) { setRecordByte(kOffsetSpeechColor, color); }

private:
	Actor(const CodePointer &code);

	// just in case, we'll explicitly add those if needed
	Actor();
	Actor(const Actor &);
	Actor &operator=(const Actor &);

	void readHeader(const byte *code);

	void animate();
	void updateZoneAtPoint();
	void resetActorStateFields();
	void syncStateToRecordFields();
	void registerActiveIfCurrentRoom();
	bool consumeReadyMarkerCallback();
	bool turnTo(Direction);
	bool nextFrame();
	void copyIntervalToTicks();
	void decrementTicksLeft();
	void setActorCodeOffset(uint16 offset);
	uint8 recordByte(uint8 off) const { return field(off); }
	uint16 recordWord(uint8 off) const { return fieldWord(off); }
	void setRecordByte(uint8 off, uint8 v) { setField(off, v); }
	void setRecordWord(uint8 off, uint16 v) { setFieldWord(off, v); }
	uint16 recordSegment() const { return recordWord(kOffsetSegment); }
	uint16 recordScriptBase() const { return recordWord(kOffsetScriptBase); }
	uint16 scriptPc() const { return recordWord(kOffsetScriptPc); }
	void setRecordSegment(uint16 segment) { setRecordWord(kOffsetSegment, segment); }
	void setRecordScriptBase(uint16 offset) { setRecordWord(kOffsetScriptBase, offset); }
	void setScriptPc(uint16 pc) { setRecordWord(kOffsetScriptPc, pc); }
	void setRecordScriptPointer(uint16 segment, uint16 offset, uint16 pc) {
		setRecordSegment(segment);
		setRecordScriptBase(offset);
		setScriptPc(pc);
	}
	void clearRecordScriptPointer() { setRecordScriptPointer(0, 0, 0); }
	void setRecordPosition(Common::Point p) {
		setRecordWord(kOffsetLeft, uint16(p.x));
		setRecordWord(kOffsetTop, uint16(p.y));
	}
	void setRecordMainSprite(uint16 sprite) { setRecordWord(kOffsetMainSprite, sprite); }
	void setRecordTicksLeft(uint16 ticks) { setRecordWord(kOffsetTicksLeft, ticks); }
	void setRecordInterval(uint8 interval) { setRecordByte(kOffsetInterval, interval); }
	void setRecordDrawLayer(uint8 zIndex) { setRecordByte(kOffsetDrawLayer, zIndex); }
	uint8 recordDrawLayer() const { return recordByte(kOffsetDrawLayer); }
	void setRecordRoom(uint16 room) { setRecordWord(kOffsetRoom, room); }
	void setSecondaryZone(uint8 zone) { setRecordByte(kOffsetSecondaryZone, zone); }
	bool autoZoneLayerEnabled() const { return recordByte(kOffsetAutoZoneLayer) != 0; }
	void setAutoZoneLayerEnabled(bool enabled) { setRecordByte(kOffsetAutoZoneLayer, enabled ? 1 : 0); }
	void setRecordFlag14(bool enabled) { setRecordByte(kOffsetFlag14, enabled ? 1 : 0); }
	void setRecordFlag15(bool enabled) { setRecordByte(kOffsetFlag15, enabled ? 1 : 0); }
	void setRecordFrame(uint16 frame) { setRecordByte(kOffsetFrame, uint8(frame)); }
	void setRecordTargetFrame(uint16 frame) { setRecordByte(kOffsetTargetFrame, uint8(frame)); }
	bool confusedRecord() const { return recordByte(kOffsetConfused) != 0; }
	void setConfusedRecord(bool confused) {
		_confused = confused;
		setRecordByte(kOffsetConfused, confused ? 1 : 0);
	}
	void setRecordAttentionNeeded(bool attentionNeeded) { setRecordByte(kOffsetAttentionNeeded, attentionNeeded ? 1 : 0); }
	void setVisibleDimensions(uint8 width, uint8 height) {
		setRecordByte(kOffsetVisibleSpriteWidth, width);
		setRecordByte(kOffsetVisibleSpriteHeight, height);
	}
	void clearWalkState() {
		setWalkQueueLength(0);
		setMovementWaitActive(false);
	}
	uint16 pendingReadyAnimation() const { return recordWord(kOffsetPendingReadyAnimation); }
	void setPendingReadyAnimation(uint16 animation) { setRecordWord(kOffsetPendingReadyAnimation, animation); }
	void clearPendingReadyAnimation() { setPendingReadyAnimation(0); }
	uint8 skipTimerCount() const { return recordByte(kOffsetSkipTimerCount); }
	void setSkipTimerCount(uint8 timer) { setRecordByte(kOffsetSkipTimerCount, timer); }
	uint16 skipTimerResumePc() const { return recordWord(kOffsetSkipTimerResumePc); }
	void setSkipTimerResumePc(uint16 pc) { setRecordWord(kOffsetSkipTimerResumePc, pc); }

	Common::Queue<Frame> _framequeue;
	uint16 _frame;
	uint16 _nextFrame;
	uint16 _room;
	Direction _direction;
	Direction _nextDirection;
	uint16 _nextAnimator; // to change to whenever possible
	bool _attentionNeeded;
	bool _confused;
	Puppeteer _puppeteer;

	// Sparse storage for DOS actor record fields not yet first-class C++
	// members. Keyed by DOS field offset (e.g. 0x14, 0x15, 0x16, 0x65).
	// Used by ActorOp_1e/1f/20/25 and any future DOS-aligned port that
	// touches these per-actor flag bytes. Sparse → absent keys read as 0
	// (matches DOS post-init state).
public:
	uint8 field(uint8 off) const {
		Common::HashMap<uint8, uint8>::const_iterator it = _recordFields.find(off);
		return it == _recordFields.end() ? 0 : it->_value;
	}
	uint16 fieldWord(uint8 off) const {
		return uint16(field(off)) | (uint16(field(uint8(off + 1))) << 8);
	}
	void setField(uint8 off, uint8 v) {
		if (v == 0)
			_recordFields.erase(off);
		else
			_recordFields[off] = v;
	}
	void setFieldWord(uint8 off, uint16 v) {
		setField(off, uint8(v & 0xff));
		setField(uint8(off + 1), uint8(v >> 8));
	}

	// DOS actor's 8-slot move queue at field+0x19 (32 bytes total: 8 entries
	// × 4 ints). Populated by Op_1c/Op_1d (mode 0/1). DOS finds the first
	// "free" slot (field0 == 0xffff) and writes (a, b, c, mode). Used for
	// queued movement commands. Slot model: Common::Array<MoveSlot> with
	// max 8 entries; sentinel 0xffff for an inactive slot, but we use a
	// presence flag instead.
	struct MoveSlot {
		MoveSlot() : a(0), b(0), c(0), mode(0) {}
		MoveSlot(uint16 _a, uint16 _b, uint16 _c, uint8 _m)
			: a(_a), b(_b), c(_c), mode(_m) {}
		uint16 a;
		uint16 b;
		uint16 c;
		uint8 mode;
	};
	bool queueMoveSlot(const MoveSlot &slot) {
		// Match DOS bound: max 8 entries. Returns false on overflow so
		// the opcode handler can set g_pendingErrorCode = 0xc.
		if (_moveSlots.size() >= 8)
			return false;
		const uint slotIndex = _moveSlots.size();
		_moveSlots.push_back(slot);
		const uint8 off = uint8(kOffsetMoveSlots + slotIndex * 8);
		setRecordWord(off, slot.a);
		setRecordWord(uint8(off + 2), slot.b);
		setRecordWord(uint8(off + 4), slot.c);
		setRecordWord(uint8(off + 6), slot.mode);
		return true;
	}
	const Common::Array<MoveSlot> &moveSlots() const { return _moveSlots; }
	void clearMoveSlots() {
		_moveSlots.clear();
		for (uint i = 0; i < 8; ++i)
			setRecordWord(uint8(kOffsetMoveSlots + i * 8), 0xffff);
	}

	// DOS callback (field+0x5d/0x5f). Set by Op_21/Op_22, cleared by Op_23.
	// Segment 0xffff = no callback; DOS leaves the offset word untouched
	// when clearing.
	void setActorCallback(uint16 segment, uint16 offset) {
		_actorCallbackSeg = segment;
		_actorCallbackOff = offset;
		setRecordWord(kOffsetActorCallbackSegment, segment);
		setRecordWord(kOffsetActorCallbackOffset, offset);
	}
	void clearActorCallback() {
		_actorCallbackSeg = 0xffff;
		setRecordWord(kOffsetActorCallbackSegment, 0xffff);
	}
	uint16 actorCallbackSeg() const { return _actorCallbackSeg; }
	uint16 actorCallbackOff() const { return _actorCallbackOff; }

private:
	uint16 _id; // 1-based DOS actor id
	Common::HashMap<uint8, uint8> _recordFields;
	Common::Array<MoveSlot> _moveSlots;
	uint16 _actorCallbackSeg;
	uint16 _actorCallbackOff;

	struct ScriptCallback {
		ScriptCallback() : runMode(0), hasRunMode(false) {}
		ScriptCallback(const CodePointer &p, uint16 mode = 0, bool hasMode = false)
			: callback(p), runMode(mode), hasRunMode(hasMode) {}
		CodePointer callback;
		uint16 runMode;
		bool hasRunMode;
	};
	Common::Queue<ScriptCallback> _callBacks;
	void callBacks();

	struct RoomCallback {
		uint16 timeout;
		CodePointer callback;
		uint16 runMode;
		bool hasRunMode;
		RoomCallback(uint16 t, const CodePointer &p, uint16 mode = 0, bool hasMode = false)
			: timeout(t), callback(p), runMode(mode), hasRunMode(hasMode) {}
	};
	Common::List<RoomCallback> _roomCallbacks;

	bool _debug;

	template<int opcode>
	Animation::Status opcodeHandler();

	template<int N>
	void init_opcodes();

	virtual Animation::Status op(byte code);

	typedef Animation::Status (Actor::*OpcodeHandler)();
	OpcodeHandler _handlers[38];

	Speech _speech;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_ACTOR_H
