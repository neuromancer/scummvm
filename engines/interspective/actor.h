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

#ifndef INTERSPECTIVE_ACTOR_H
#define INTERSPECTIVE_ACTOR_H

#include "common/array.h"
#include "common/endian.h"
#include "common/hashmap.h"
#include "common/queue.h"
#include "common/rect.h"

#include "interspective/animation.h"
#include "interspective/value.h"

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
		kActorId =     		0,
		kMainCode =    		2,
		kMoveAnimators =    4,
		kTurnAnimators = 0x14,
		kSize = 		 0x24
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
		Frame() : _position(999, 999), _nexts(), _nextCount(0xff) {
			_nexts.resize(8);
		}
		Frame(Common::Point pos, Common::Array<byte> n, uint16 i) : _position(pos), _nexts(n), _index(i), _nextCount(0xff) {}

		Common::Point position() const { return _position; }
		const Common::Array<byte> &nexts() const { return _nexts; }
		uint16 index() const { return _index; }

		// Runtime frame mutation — DOS Op_e0 (InvalidateFrame) writes
		// the (999,999) sentinel that findPath skips. DOS Op_e1
		// (SetFramePosition) overwrites the screen position. Both are
		// scene-scoped: the room-load resets _actorFrames so changes
		// don't persist across room transitions.
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
		Speech() : _actor(0) {}
		~Speech();
		Speech(Actor *parent, const Common::String &text);
		bool active() const { return !_text.empty(); }
		void callWhenDone(const CodePointer &cp) { _cb.push(cp); }
		void paint(Graphics *g);
		void tick();

	private:
		Common::String _text;
		Common::Queue<CodePointer> _cb;
		uint16 _ticksLeft;
		Actor *_actor;
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
		kOffsetOffset = 2,
		kOffsetLeft = 4,
		kOffsetTop = 6,
		kOffsetMainSprite = 8,
		kOffsetTicksLeft = 0xa,
		kOffsetCode = 0xc,
		kOffsetInterval = 0x10,
		kOffsetRoom = 0x59
	};

	virtual bool isActor() const { return true; }

	void setFrame(uint16 f);
	uint16 frameId() const { return _frame; }
	uint16 targetFrameId() const { return _nextFrame; }
	Common::Point position() const { return _position; }
	int16 ticksLeft() const { return _ticksLeft; }
	void moveTo(uint16 f);
	static Common::List<Frame> findPath(Frame from, uint16 to);

	uint16 room() const { return _room; }
	void setRoom(uint16, uint16 frame = 0, uint16 nextFrame = 0);

	// Update only the room field, without touching frame/position/script.
	// Used by Logic::doChangeRoom to keep the protagonist's _room in
	// sync with _currentRoom so the gating in Actor::tick() doesn't
	// short-circuit (see PLAN iter-27).
	void forceRoom(uint16 r) { _room = r; }

	// DOS-aligned room/frame placement that does NOT reset the actor's
	// animation script. Mirrors the field assignments in
	// Op_7a_PlaceActorInRoomXY @ 1000:4443: writes _room (field+0x59),
	// _frame (field+0x61), _nextFrame (field+0x62), then runs
	// SetActorPosition (frame X/Y -> _position). InitActorState in DOS
	// preserves the actor's existing code offset, so we mirror that by
	// leaving _base/_offset alone (unlike setRoom which jumps the script
	// to puppeteer.mainCode and clears the sprite — too aggressive and
	// crashes for actors whose puppeteer isn't initialised yet).
	void placeIn(uint16 room, uint16 frame, uint16 nextFrame = 0);

	bool isFine() const;

	void setAnimation(const CodePointer &anim);
	void setAnimation(uint16);

	void hide();
	void callMe(const CodePointer &cp);
	void tellMe(const CodePointer &cp, uint16 timeout);

	bool isSpeaking() const;
	void callMeWhenSilent(const CodePointer &cp);
	void say(const Common::String &text);

	bool isMoving() const;
	void callMeWhenStill(const CodePointer &cp);

	Animation::Status tick();
	void paint(Graphics *g);

	void toggleDebug();

	void setPuppeteer(const Puppeteer &p) { _puppeteer = p; }
private:
	Actor(const CodePointer &code);

	// just in case, we'll explicitly add those if needed
	Actor();
	Actor(const Actor &);
	Actor &operator=(const Actor &);

	void readHeader(const byte *code);

	/**
	 * Get position to put the speech bubble in.
	 * It should be saved and stay still for the entire
	 * duration of a sentence (as it may move because
	 * of the actor animating).
	 * @returns position of the tip of the bubble
	 */
	Common::Point getSpeechPosition() const;

	void animate();
	bool turnTo(Direction);
	bool nextFrame();

	Common::Queue<Frame> _framequeue;
	uint16 _frame;
	uint16 _nextFrame;
	uint16 _room;
	Direction _direction, _nextDirection;
	uint16 _nextAnimator; // to change to whenever possible
	bool _attentionNeeded, _confused;
	Puppeteer _puppeteer;

	// Sparse storage for DOS actor record fields not yet first-class C++
	// members. Keyed by DOS field offset (e.g. 0x14, 0x15, 0x16, 0x65).
	// Used by ActorOp_1e/1f/20/25 and any future DOS-aligned port that
	// touches these per-actor flag bytes. Sparse → absent keys read as 0
	// (matches DOS post-init state).
public:
	uint8 dosField(uint8 off) const {
		Common::HashMap<uint8, uint8>::const_iterator it = _dosFields.find(off);
		return it == _dosFields.end() ? 0 : it->_value;
	}
	void setDosField(uint8 off, uint8 v) {
		if (v == 0)
			_dosFields.erase(off);
		else
			_dosFields[off] = v;
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
		uint16 a, b, c;
		uint8 mode;
	};
	bool queueMoveSlot(const MoveSlot &slot) {
		// Match DOS bound: max 8 entries. Returns false on overflow (DOS
		// sets g_pendingErrorCode = 0xc; we just drop silently).
		if (_moveSlots.size() >= 8)
			return false;
		_moveSlots.push_back(slot);
		return true;
	}
	const Common::Array<MoveSlot> &moveSlots() const { return _moveSlots; }
	void clearMoveSlots() { _moveSlots.clear(); }

	// DOS callback (field+0x5d/0x5f). Set by Op_21/Op_22, cleared by Op_23.
	// 0xffffffff = no callback. Stored as a single u32 since DOS treats
	// the seg+off as a far-pointer pair.
	void setActorCallback(uint16 segment, uint16 offset) {
		_actorCallbackSeg = segment;
		_actorCallbackOff = offset;
	}
	void clearActorCallback() {
		_actorCallbackSeg = 0xffff;
		_actorCallbackOff = 0xffff;
	}
	uint16 actorCallbackSeg() const { return _actorCallbackSeg; }
	uint16 actorCallbackOff() const { return _actorCallbackOff; }
private:
	Common::HashMap<uint8, uint8> _dosFields;
	Common::Array<MoveSlot> _moveSlots;
	uint16 _actorCallbackSeg, _actorCallbackOff;

	Common::Queue<CodePointer> _callBacks;
	void callBacks();

	struct RoomCallback {
		uint16 timeout;
		CodePointer callback;
		RoomCallback(uint16 t, const CodePointer &p) : timeout(t), callback(p) {}
	};
	Common::List<RoomCallback> _roomCallbacks;

	bool _debug;

	template <int opcode>
	Animation::Status opcodeHandler();

	template <int N>
	void init_opcodes();

	virtual Animation::Status op(byte code);

	typedef Animation::Status (Actor::*OpcodeHandler)();
	OpcodeHandler _handlers[38];

	Speech _speech;
};

}

#endif
