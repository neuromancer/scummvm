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

#include "common/rect.h"

#include "interspective/actor.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

namespace Interspective {
//

Actor::Actor(const CodePointer &code) : Animation(code, Common::Point()) {
	byte *header = code.code();
	_base = header - code.offset();
	readHeader(header);
	snprintf(_debugInfo, 50, "actor at %s", +code);
	_direction = kDirNone;
	_frame = 0;
	_debug = false;
	_attentionNeeded = false;
	_confused = false;
	_nextAnimator = 0;
	_nextDirection = kDirNone;
	_actorCallbackSeg = 0xffff;
	_actorCallbackOff = 0xffff;

	Engine::instance().logic()->addAnimation(this);

	init_opcodes<37>();
}

bool Actor::isFine() const {
	return _room == Log.currentRoom() && _base && !_attentionNeeded &&
			(!_framequeue.empty() || !_confused || _nextDirection);
}

void Actor::setAnimation(uint16 offset) {
	// Invariant when _base is non-null: _base + _offset reads the current
	// opcode, _base - _baseOffset points to file offset 0 of the same code
	// segment. So switching to a new offset within that segment is just
	// rebasing the pointer. BUT if _base is null (Op_01 ScriptEnd / hide()
	// cleared it), the subtract underflows the null pointer → UB → crash.
	// Restore from the main interpreter (the only code source actor scripts
	// use, per Puppeteer::moveAnimator/turnAnimator) so the new animator
	// starts cleanly instead of inheriting a poisoned base.
	byte *base = _base;
	uint16 baseOff = _baseOffset;
	if (!base) {
		base = Log.mainInterpreter()->rawCode(0);
		baseOff = 0;
	}
	_base = base - baseOff + offset;
	_baseOffset = offset;
	_offset = 0;
	_debugInvalid = false;
	_confused = _attentionNeeded = false;
	clearMainSprite();
	_interval = 1;
	_counter = _ticksLeft = 0;
	_nextDirection = kDirNone;
}

void Actor::setAnimation(const CodePointer &anim) {
	debugC(3, kDebugLevelScript, "setting animation code of %s to %s", _debugInfo, +anim);
	_base = anim.code();
	_baseOffset = anim.offset();
	_offset = 0;
	_debugInvalid = false;
	_confused = _attentionNeeded = false;
	clearMainSprite();
	_interval = 1;
	_counter = _ticksLeft = 0;
	_nextDirection = kDirNone;
}

void Actor::hide() {
	_base = 0;
	_baseOffset = _offset = 0;
}

void Actor::callMe(const CodePointer &code) {
	debugC(3, kDebugLevelScript, "actor will call %s when needed", +code);
	_callBacks.push(code);
}

void Actor::tellMe(const CodePointer &code, uint16 timeout) {
	_roomCallbacks.push_back(RoomCallback(timeout, code));
}

bool Actor::isSpeaking() const {
	return _speech.active();
}

void Actor::callMeWhenSilent(const CodePointer &cp) {
	_speech.callWhenDone(cp);
}

void Actor::say(const Common::String &text) {
	_speech = Speech(this, text);
}

Actor::Speech::~Speech() { while (!_cb.empty()) Log.runLater(_cb.pop()); }

bool Actor::isMoving() const {
	//TODO stub
	return false;
}

void Actor::callMeWhenStill(const CodePointer &cp) {
	// `isMoving()` is currently a TODO stub that always returns false, so
	// the actor is always "still" — fire the callback on the next tick.
	// The previous assert(false) was UB-sanitizer poison; if any caller
	// reached here we'd crash. Should be replaced once isMoving is wired
	// to the real walk-state field.
	Log.runLater(cp);
}

void Actor::setFrame(uint16 frame) {
	_frame = frame;
	Frame f(Log.room()->getFrame(frame));
	if (f.position().x == 999)
		return;
	_position = f.position();
	debugC(5, kDebugLevelActor, "actor set to frame %d, position %d:%d", f.index(), _position.x, _position.y);
}

Common::List<Actor::Frame> Actor::findPath(Actor::Frame from, uint16 to) {
	// BFS over the room's frame-transition graph. Modeled on DOS
	// FindActorPath at CS:0x713e — three behaviors verified from the
	// disassembly that the original C++ port missed:
	//
	//   1. Visited-set dedup. DOS keeps `g_pathfind_visited` (a flat byte
	//      array of seen frame indices) and linear-scans it before adding
	//      each candidate. Without this, cycles in the frame graph make
	//      the BFS double its queue size each iteration.
	//   2. Invalid-frame skip. DOS gates each frame's nexts-expansion on
	//      `*piVar14 != 999 || ... != 999` — i.e. don't expand placeholder
	//      frames whose position is (999,999). The room data has these as
	//      gaps in the frame index range.
	//   3. Termination on exhausted reachable set. DOS bound-checks the
	//      visited buffer at 0x1918 bytes / 9 bytes per entry ≈ 160 frames
	//      and bails. The C++ equivalent: if a level adds zero new frames,
	//      we've exhausted the reachable set without finding the target —
	//      return empty list (moveTo() falls back to a single-frame warp).
	//
	// Without these guards the BFS hung the engine (SIGSTOP traceback in
	// list insert at actor.cpp:180) when scripts called Op_07 SetTargetFrame
	// with a target that wasn't reachable from the actor's current frame.
	Common::HashMap<uint16, bool> visited;
	visited[from.index()] = true;

	Common::List<Common::List<Frame> > reachable;
	Common::List<Frame> zero;
	zero.push_back(from);
	reachable.push_back(zero);

	bool found = false;
	while (!found) {
		Common::List<Common::List<Frame> >::iterator back = reachable.end();
		back--;
		Common::List<Frame>::iterator current = back->begin();
		Common::List<Frame> next;
		Common::String s;
		while (!found && current != back->end()) {
			// DOS skips nexts-expansion for placeholder frames (position
			// 999,999). Match that — these are gaps in the frame table.
			if (current->position().x == 999 && current->position().y == 999) {
				current++;
				continue;
			}
			Common::Array<byte> nexts = current->nexts();
			for (int i = 0; i < 8; i++) {
				const byte n = nexts[i];
				if (!n)
					continue;
				if (visited.contains(n))
					continue;
				visited[n] = true;
				s += Common::String::format(", %d", int(n));
				next.push_back(Log.room()->getFrame(n));
				if (n == to) {
					found = true;
					break;
				}
			}
			current++;
		}
		if (!found && next.empty()) {
			// Exhausted reachable set without finding the target. Return an
			// empty list; moveTo() will treat this as a single-frame warp.
			debugC(2, kDebugLevelActor,
				"findPath: frame %u unreachable from %u (%u frames explored)",
				to, from.index(), (uint)visited.size());
			return Common::List<Frame>();
		}
		reachable.push_back(next);
		debugC(4, kDebugLevelActor, "reachable on this level:%s", s.c_str());
	}

	Common::List<Frame> path;

	Common::List<Common::List<Frame> >::iterator level = reachable.end();
	level--;
	Common::List<Frame>::iterator current = level->end();
	current--;
	uint16 index = level->size() - 1;

	forever {
		path.push_front(*current);
		if (*current == from)
			break;
		if (level == reachable.begin()) {
			// Backtrack underflow — shouldn't happen given the BFS guaranteed
			// a path exists, but bail rather than UB-decrement past begin().
			debugC(1, kDebugLevelActor, "findPath: backtrack underflow at frame %u", current->index());
			break;
		}
		level--;
		current = level->begin();
		uint16 new_index = 0;
		while (index >= current->nextCount()) {
			index -= current->nextCount();
			new_index++;
			current++;
		}
		index = new_index;
	}

	return path;
}

void Actor::moveTo(uint16 frame) {
	Frame cur = Log.room()->getFrame(_frame);

	Common::List<Frame> path = findPath(cur, frame);

	// findPath now returns an EMPTY list when the target is unreachable
	// (matches DOS bound-check on visited buffer). Treat that as a
	// single-frame warp — same fallback the original code took for the
	// "last frame != target" case.
	if (path.empty()) {
		path.push_back(Log.room()->getFrame(frame));
	} else {
		Common::List<Frame>::iterator it = path.end();
		it--;
		if (it->index() != frame) {
			Common::List<Frame> p;
			p.push_back(Log.room()->getFrame(frame));
			path = p;
		}
	}

	Common::String s;
	Common::List<Frame>::iterator it = path.begin();
	if (it != path.end())
		it++;
	while (it != path.end()) {
		_framequeue.push(*it);
		s += Common::String::format(" %d", int(it->index()));
		it++;
	}

	debugC(3, kDebugLevelActor, "found path: %s", s.c_str());
	if (!_base)
		nextFrame();
}

void Actor::setRoom(uint16 r, uint16 frame, uint16 next_frame) {
	_room = r;
	unless (next_frame)
		next_frame = frame;
	_nextFrame = next_frame;
	setFrame(frame);

	setAnimation(CodePointer(_puppeteer.mainCodeOffset(), Log.mainInterpreter()));
}

void Actor::placeIn(uint16 r, uint16 frame, uint16 next_frame) {
	// Mirrors DOS Op_7a's prelude (CS:0x4443):
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
	_framequeue.pop();
	return true;
}

bool Actor::turnTo(Direction dir) {
	if (dir == _direction)
		return false;

	Direction d = _direction>>dir;
	debugC(4, kDebugLevelActor, "turning %d -> %d >> %d", _direction, d, dir);
	setAnimation(_puppeteer.turnAnimator(d));
	return true;
}

void Actor::animate() {
	unless (_puppeteer.valid())
		return;

	unless (_attentionNeeded || _confused/* || _timedOut*/)
		return;

	// Don't switch to the next walk frame while the current animator is still
	// in its inter-frame wait. The move animator sets _attentionNeeded via
	// actor opcode 0x24 and ends with kFrameDone, queueing _ticksLeft =
	// _interval. setAnimation() in nextFrame()/turnTo()/etc. resets
	// _ticksLeft to 0, so consuming attention before the wait elapses
	// effectively plays each walk frame for 1 tick instead of _interval+1
	// ticks — visibly too fast. Let Animation::tick decrement _ticksLeft to 0
	// first; only then do we advance.
	if (_ticksLeft)
		return;

	debugC(4, kDebugLevelActor, "attention needed");

	if (nextFrame()) {
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

	if (_confused) {
		if (turnTo(kDirDown))
			return;
		_direction = kDirDown;
	}
/*	} else {
		if (turnTo(kDirUp))
			return;
		_direction = kDirUp;
		setAnimation(_puppeteer.offset());
	}*/
}

Animation::Status Actor::tick() {
	_speech.tick();
	animate();
	callBacks();

	if (_room == Log.currentRoom()) {
		Animation::Status s;
		if (_debug) gDebugLevel += 3;
			s = Animation::tick();
		if (_debug) gDebugLevel -= 3;
		return s;
	} else
		return kOk;
}

void Actor::toggleDebug() {
	_debug = !_debug;
}

void Actor::readHeader(const byte *code) {
//	uint16 segment = READ_LE_UINT16(code + kOffsetCode);
/*	if (segment == 0)
		_base = Log.mainInterpreter()->rawCode(0);
	else if (segment == 1)
		_base = Log.blockInterpreter()->rawCode(0);
	else error("segment %x", segment);*/
	_interval = code[kOffsetInterval];
	_ticksLeft = READ_LE_UINT16(code + kOffsetTicksLeft);
	_zIndex = 0;
	_position = Common::Point(READ_LE_UINT16(code + kOffsetLeft), READ_LE_UINT16(code + kOffsetTop));
	uint16 baseOff = READ_LE_UINT16(code + kOffsetCode);
	_offset = READ_LE_UINT16(code + kOffsetOffset);
	if (_offset || baseOff) {
		_base += baseOff;
		_baseOffset = baseOff;
	} else {
		_base = 0;
		_baseOffset = _offset = 0;
	}
	uint16 sprite = READ_LE_UINT16(code + kOffsetMainSprite);
	_room = READ_LE_UINT16(code + kOffsetRoom);

	debugC(3, kDebugLevelFiles, "loading %s: interv %d ticks %d z%d pos%d:%d code %d offset %d sprite %d room %d", _debugInfo, _interval, _ticksLeft, _zIndex, _position.x, _position.y, baseOff, _offset, sprite, _room);

	if (sprite != 0xffff)
		setMainSprite(sprite);
}

void Actor::callBacks() {
	unless (isFine()) {
		Common::Queue<CodePointer> cb = _callBacks;
		_callBacks.clear();
		while (!cb.empty())
			cb.pop().run();
	}

	foreach (RoomCallback, _roomCallbacks) {
		if (_room == Log.currentRoom() || !it->timeout) {
			it->callback.run();
			Common::List<RoomCallback>::iterator done = it;
			it++;
			_roomCallbacks.erase(done);
		} else
			it->timeout--;
	}
}

void Puppeteer::parse(const byte *data) {
	_actorId = READ_LE_UINT16(data + kActorId);
	_offset = READ_LE_UINT16(data + kMainCode);

	assert (kTurnAnimators == kMoveAnimators + 16);
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

Actor::Speech::Speech(Actor *parent, const Common::String &text) : _text(text), _ticksLeft(20), _actor(parent) {
	const Common::Point &position = parent->getSpeechPosition();
	debugC(1, kDebugLevelActor, "adding speech \"%s\" for %s at %d:%d", text.c_str(), parent->_debugInfo, position.x, position.y);
	_image = new Interspective::Sprite;
	_rect = Graf.paintSpeechInBubble(position, 235, reinterpret_cast<const byte *>(text.c_str()), _image);
}

void Actor::Speech::tick() {
	unless (_ticksLeft--) {
		_text.clear();
		while (!_cb.empty())
			Log.runLater(_cb.pop());
	}
}

Common::Point Actor::getSpeechPosition() const {
	Common::Point speechPosition(_position);
	if (_mainSprite.get()) {
		speechPosition.y -= _mainSprite.get()->h - _mainSprite.get()->_hotPoint.y;
		speechPosition.x -= _mainSprite.get()->_hotPoint.x;
	}
	return speechPosition;
}

void Actor::paint(Graphics *g) {
	Animation::paint(g);
	_speech.paint(g);
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

template <int opcode>
Animation::Status Actor::opcodeHandler(){
	return Animation::opcodeHandler<opcode>();
}

template<int N>
void Actor::init_opcodes() {
	_handlers[N] = &Interspective::Actor::opcodeHandler<N>;
	init_opcodes<N-1>();
}

template<>
void Actor::init_opcodes<-1>() {}

Animation::Status Actor::op(byte opcode) {
	return (this->*_handlers[opcode])();
}

#define OPCODE(n) template<> Animation::Status Actor::opcodeHandler<n>()

// Actor opcode dispatch — the original ScummVM port wrote these without
// reference to the DOS binary; many semantics here diverge from Ghidra's
// findings. The C++ has built a parallel "walking model" around
// _attentionNeeded (see Op_24 below) and _direction (see Op_17/0x23) that
// keeps the engine functional. Audit (2026-05-04 iter 8) cross-referenced
// each handler against the DOS ActorOp_* labels; misclassifications are
// noted inline. Do not "correct" the misclassified ones without porting
// the DOS data model first — the C++ heuristics are load-bearing.

OPCODE(0x00) {
	// DOS Op_01 ScriptEnd (CS:0x68d3) — actually a 1-based-naming
	// confusion. C++ slot 0x00 corresponds to DOS Op_01 per the
	// off-by-1 dispatcher mapping. DOS clears field0+field1 + sets
	// g_actor_script_ended = 1 to break out of RunActorScript.
	// Animation::OPCODE(0x00) returns kRemove which would erase the
	// actor from Logic::_animations entirely — wrong for Actor (the
	// actor should remain in the table, just inactive). Override to
	// clear PC + return kFrameDone (matches Op_01's terminate-script
	// semantic without removal).
	debugC(2, kDebugLevelActor, "actor opcode 0x00: ScriptEnd (clear PC, no remove) [DOS Op_01]");
	_offset = 0;
	_baseOffset = 0;
	_base = 0;
	return kFrameDone;
}

OPCODE(0x01) {
	// DOS Op_02 UnregisterAndEnd (CS:0x68e3): UnregisterActor + clear
	// field0/1 + sets g_actor_script_ended = 1. C++ slot 0x01 per the
	// off-by-1 mapping. Closest equivalent: terminate animator + flag
	// _confused so animate() picks a new state next tick.
	debugC(1, kDebugLevelActor, "actor opcode 0x01: UnregisterAndEnd (terminate + confuse) [DOS Op_02]");

	_offset = 0;
	_base = 0;
	_confused = true;
	return kFrameDone;
}

OPCODE(0x14) {
	// C++ slot 0x14 = DOS Op_15 WaitForSpeechSlot (CS:0x6af1). 4-byte
	// opcode: either jumps to script[+2..+3] (loop while still speaking)
	// or ADD BP,4 (skip past). 1 shift. iter-12 wrongly removed the
	// shift. FIXED iter-13: consume jump target (don't actually loop —
	// engine treats speech as instantaneous; the script will exit the
	// wait loop on first iteration).
	uint16 off = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x14: WaitForSpeechSlot off=0x%04x STUB (no loop) [DOS Op_15]", off);
	(void)off;
	return kOk;
}

OPCODE(0x15) {
	// C++ slot 0x15 = DOS Op_16 PickAnimationSet (CS:0x6c3e). 2-byte
	// opcode (ADD BP,2). NO script reads beyond the opcode byte.
	// iter-12 wrongly added byte+shift. FIXED iter-13: 0 extras.
	debugC(2, kDebugLevelAnimation, "actor opcode 0x15: PickAnimationSet STUB [DOS Op_16]");
	return kOk;
}

OPCODE(0x16) {
	// C++ slot 0x16 = DOS Op_17 BranchIfAnimSetEquals (CS:0x6b17). Reads
	// embedded byte (anim-set value) + word (jump target). Byte
	// consumption matches. C++ uses _direction as proxy for field+0x68.
	byte val = embeddedByte();
	uint16 off = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x16: BranchIfAnimSetEquals val=%d off=0x%04x (C++ uses _direction) [DOS Op_17]", val, off);
	if (val == _direction)
		setAnimation(off);
	return kOk;
}

OPCODE(0x17) {
	// C++ slot 0x17 = DOS Op_18 BranchIfMoodEquals (CS:0x6b29). Reads
	// embedded byte + word. Byte consumption matches. C++ stashes
	// _nextAnimator instead of comparing mood — parallel mechanism.
	byte val = embeddedByte();
	uint16 off = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x17: BranchIfMoodEquals val=%d off=0x%04x (C++ stashes _nextAnimator) [DOS Op_18]", val, off);
	_nextAnimator = off;
	return kOk;
}

OPCODE(0x18) {
	// C++ slot 0x18 = DOS Op_19 SetField6d (CS:0x6b43). Reads 1 int16.
	// Byte consumption matches. Engine doesn't model field+0x6d yet.
	uint16 val = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x18: SetField6d = 0x%04x STUB [DOS Op_19]", val);
	setDosField(0x6d, uint8(val));  // store low byte; field is actually word but no readers yet
	return kOk;
}

OPCODE(0x19) {
	// C++ slot 0x19 = DOS Op_1a SetWalkFlagsAndEnd (CS:0x6a48). Reads
	// 1 int16 (walk flags) and ends script (Animation handler 0x19 was
	// "hide for delay" with 1 shift — happens to match consumption).
	uint16 flags = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x19: SetWalkFlagsAndEnd flags=0x%04x STUB [DOS Op_1a]", flags);
	return kOk;
}

OPCODE(0x1a) {
	// C++ slot 0x1a = DOS Op_1b SetField12ClearFlag16 (CS:0x6b4e). Reads
	// embedded byte at +1 only. Animation::0x1a ("set z index") happens
	// to use embeddedByte too — accidentally aligned, but writing
	// _zIndex instead of field+0x12. Override for correctness.
	byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1a: SetField12ClearFlag16 = %d STUB [DOS Op_1b]", v);
	setDosField(0x12, v);
	setDosField(0x16, 0);
	return kOk;
}

OPCODE(0x23) {
	// C++ slot 0x23 = DOS Op_24 SetMood (CS:0x6c13). Reads byte at +1.
	// Byte consumption matches. C++ uses this as "set _direction" — the
	// load-bearing C++ walking heuristic. The bytecode emits this
	// opcode with values 1..8 (compass values) which the C++ interprets
	// as direction. In DOS the byte is the actor's mood. The two
	// usages happen to overlap because mood values are also small.
	byte dir = embeddedByte();

	debugC(3, kDebugLevelAnimation, "actor opcode 0x23: ClearCallback (C++ sets _direction=%d)", dir);

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
	setDosField(0x63, dir);
	return kOk;
}

OPCODE(0x24) {
	// C++ slot 0x24 = DOS Op_25 SetField65 (CS:0x6c1e). Reads byte at +1.
	// Byte consumption matches. C++ uses this as "set _attentionNeeded"
	// — the load-bearing C++ walking trigger. The bytecode emits this
	// between walk frames so the next frame can advance. CRITICAL: the
	// iter-7 movement-speed fix gates animate() on _ticksLeft; do NOT
	// remove the _attentionNeeded write without porting the DOS walk
	// driver. Also stash the field value for any future readers.
	byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x24: SetField65 = %d (C++ also sets _attentionNeeded for walking) [DOS Op_25]", v);
	setDosField(0x65, v);
	_attentionNeeded = true;
	return kOk;
}

// ============================================================================
// DOS-faithful overrides for the movement / frame-state opcode family
// (DOS Op_03..Op_0a, C++ slots 0x02..0x09). Phase-2 of the iter-10 roadmap.
//
// The Animation::opcodeHandler<N> fall-through coincidentally consumes the
// right number of script bytes for each of these slots (so PC alignment
// stays correct), but writes the data to the WRONG actor field. For
// example, Animation::0x03 ("set interval") writes _interval, but DOS Op_04
// SetCurrentFrame writes the actor's current-frame byte. The visible
// effect of the Animation fall-through is wrong animation timing AND wrong
// per-frame state — bad enough to drive the engine off the rails during
// any cutscene that uses these opcodes.
//
// Each override below writes the correct C++ Actor field per the DOS
// semantics. Ops that end the script (Op_06..0a; DOS sets
// g_actor_script_ended = 1) return kFrameDone, which makes the C++
// dispatcher break out of the per-tick opcode loop and queue
// _ticksLeft = _interval before the next tick. Ops that don't end the
// script return kOk (continue with the next opcode this tick).
// ============================================================================

OPCODE(0x02) {
	// C++ slot 0x02 = DOS Op_03 SetPosition (1000:6900). 2 shifts.
	// Writes actor.x = word, actor.y = word. Doesn't end script.
	// Animation::0x02 ("move to position") fall-through happened to do
	// the right thing structurally — but make the override explicit so
	// any Phase-2 walking-state writes can hook in here later.
	const uint16 x = shift();
	const uint16 y = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x02: SetPosition (%u,%u) [DOS Op_03]", x, y);
	_position.x = (int16)x;
	_position.y = (int16)y;
	return kOk;
}

OPCODE(0x03) {
	// C++ slot 0x03 = DOS Op_04 SetCurrentFrame (1000:6912). 1 shift.
	// Writes the LOW BYTE of the word to actor.field+0x10 (current
	// frame). Animation::0x03 ("set interval") wrote _interval — wrong
	// target. Now writes _frame.
	const uint16 word = shift();
	const uint8 frame = uint8(word);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x03: SetCurrentFrame %u [DOS Op_04]", frame);
	_frame = frame;
	return kOk;
}

OPCODE(0x04) {
	// C++ slot 0x04 = DOS Op_05 SetCurrentFrameFromGlobal (1000:691d).
	// 1 shift. Reads the global WORD var at index (offset/2), takes low
	// byte, writes to actor's current frame. Animation::0x04 wrote
	// _interval — wrong target. Now writes _frame.
	const uint16 offset = shift();
	const uint16 word = READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	const uint8 frame = uint8(word);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x04: SetCurrentFrameFromGlobal var[%u/2]=0x%04x → %u [DOS Op_05]",
		offset, word, frame);
	_frame = frame;
	return kOk;
}

OPCODE(0x05) {
	// C++ slot 0x05 = DOS Op_06 WalkRelativeWithFrame (1000:6939). Reads
	// 2 signed bytes (dx, dy) + 1 word (target frame). Adds the deltas to
	// actor.x/y, sets actor's target frame, ends script.
	//
	// IMPORTANT: the DOS engine renders by looking up sprite from current
	// frame + animation set. The C++ engine instead uses _mainSprite as
	// the rendered sprite source. The move-animator scripts emit Op_06
	// expecting BOTH effects — walk target AND visible sprite update.
	// In Innocent's data, sprite IDs and frame indices coincide, so
	// setMainSprite(target) gives the correct render. Removing this
	// (iter-14) made the protagonist invisible — restored iter-18.
	const int8 dx = shiftByte();
	const int8 dy = shiftByte();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x05: WalkRelativeWithFrame d=(%d,%d) target=%u [DOS Op_06]",
		dx, dy, target);
	_position.x += dx;
	_position.y += dy;
	_nextFrame = target;
	setMainSprite(target);  // restore visibility (see comment above)
	if (target != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(target);
	return kFrameDone;
}

OPCODE(0x06) {
	// C++ slot 0x06 = DOS Op_07 SetTargetFrame (1000:69a4). 1 shift.
	// Writes actor.field+8 (target frame) = arg, ends script.
	// Same render-vs-walk duality as 0x05 — we set _mainSprite too so the
	// new frame is actually drawn (iter-18 restored after iter-14 lost it).
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x06: SetTargetFrame %u [DOS Op_07]", target);
	_nextFrame = target;
	setMainSprite(target);
	if (target != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(target);
	return kFrameDone;
}

OPCODE(0x07) {
	// C++ slot 0x07 = DOS Op_08 SetTargetFrameFromGlobal (1000:69b0).
	// 1 shift. Reads global word var at index (offset/2), writes to
	// target frame, ends script. Sister to 0x06 with indirect target.
	const uint16 offset = shift();
	const uint16 target = READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	debugC(3, kDebugLevelAnimation, "actor opcode 0x07: SetTargetFrameFromGlobal var[%u/2]=%u [DOS Op_08]",
		offset, target);
	_nextFrame = target;
	setMainSprite(target);
	if (target != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(target);
	return kFrameDone;
}

OPCODE(0x08) {
	// C++ slot 0x08 = DOS Op_09 WalkRelative (1000:697c). Reads 2 signed
	// bytes (dx, dy) only. Adds to actor.x/y, ends script. Animation::0x08
	// ("move by") happens to do the same _position update with the same
	// byte consumption — the existing fall-through was correct for
	// position but didn't end the script. Override returns kFrameDone.
	const int8 dx = shiftByte();
	const int8 dy = shiftByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x08: WalkRelative d=(%d,%d) [DOS Op_09]", dx, dy);
	_position.x += dx;
	_position.y += dy;
	return kFrameDone;
}

OPCODE(0x09) {
	// C++ slot 0x09 = DOS Op_0a WalkAbsolute (1000:6991). 2 shifts.
	// Writes actor.x/y = (word, word), ends script. iter-11 was a no-op
	// stub; iter-12 added correct byte consumption; iter-13 now writes
	// the position properly and ends the script.
	const uint16 x = shift();
	const uint16 y = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x09: WalkAbsolute (%u,%u) [DOS Op_0a]", x, y);
	_position.x = (int16)x;
	_position.y = (int16)y;
	return kFrameDone;
}

OPCODE(0x0a) {
	// C++ slot 0x0a = DOS Op_0b WalkAbsoluteWithFrame (1000:6962). 3 shifts:
	// x, y, target_frame. Writes actor.x/y, sets target frame, ends script.
	// Animation::0x0a fall-through ("run sprite at") consumed the right
	// 3 shifts but set _mainSprite from the third arg instead of treating
	// it as a target frame. iter-19 override gives DOS-aligned behavior
	// AND keeps the sprite update for visibility (per iter-18 lesson).
	const uint16 x = shift();
	const uint16 y = shift();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0a: WalkAbsoluteWithFrame (%u,%u) target=%u [DOS Op_0b]",
		x, y, target);
	_position.x = (int16)x;
	_position.y = (int16)y;
	_nextFrame = target;
	setMainSprite(target);
	if (target != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(target);
	return kFrameDone;
}

OPCODE(0x0d) {
	// C++ slot 0x0d = DOS Op_0e SetTimerAndSkip (1000:6a5e). Reads embedded
	// byte at +1 (skip-timer value), writes to actor.field+0x11. Also DOS
	// stashes BP+2 (script PC after the opcode) into actor.field+0xe — a
	// "resume point" used by some other op. Animation::0x0d fall-through
	// ("loop start") wrote _counter and _loopStart instead — wrong fields.
	// Stash via the sparse _dosFields map; field+0x11 is the per-actor
	// skip-timer that DOS Op_0f decrements.
	const byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0d: SetTimerAndSkip = %u [DOS Op_0e]", v);
	setDosField(0x11, v);
	// Note: also stash the next-script-PC for the "resume after skip"
	// behavior. We use the C++ _baseOffset+_offset (already advanced past
	// the embedded byte by the dispatcher) — store as two halves at
	// fields 0x0e/0x0f if any future reader needs it.
	const uint16 nextPC = _baseOffset + _offset;
	setDosField(0x0e, uint8(nextPC & 0xff));
	setDosField(0x0f, uint8(nextPC >> 8));
	return kOk;
}

OPCODE(0x0e) {
	// C++ slot 0x0e = DOS Op_0f DecrementTimer (1000:6a6c). 0 args.
	// If actor.field+0x11 != 0, decrement. Animation::0x0e fall-through
	// ("loop end") was an unrelated counter decrement — replaced.
	const uint8 cur = dosField(0x11);
	if (cur != 0) {
		setDosField(0x11, uint8(cur - 1));
		debugC(4, kDebugLevelAnimation, "actor opcode 0x0e: DecrementTimer %u → %u [DOS Op_0f]", cur, cur - 1);
	} else {
		debugC(5, kDebugLevelAnimation, "actor opcode 0x0e: DecrementTimer (already 0) [DOS Op_0f]");
	}
	return kOk;
}

OPCODE(0x0f) {
	// C++ slot 0x0f = DOS Op_10 (CS:0x6a7e). The Ghidra label says "NoOp"
	// but the disassembly shows `MOV BP, ES:[BP+DI+2] / RET` — it's an
	// UNCONDITIONAL JUMP to the word at script[+2..+3]. 4-byte opcode.
	// Original Animation::0x0f handler was correct ("jump"); iter-12
	// erroneously changed it to NoOp. FIXED iter-13: restore jump semantic.
	uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0f: Jump to 0x%04x [DOS Op_10]", target);
	_offset = target;
	return kOk;
}

// ============================================================================
// Phase-1 actor opcode overrides (iter 11; CORRECTED iter 12 after off-by-1
// dispatcher mapping was uncovered).
//
// CRITICAL: C++ OPCODE(N) handles memory byte (-N-1 as int8), which the DOS
// dispatcher labels as Op_(N+1). For example:
//   memory byte 0xff → C++ OPCODE(0x00) → DOS Op_01 (ScriptEnd)
//   memory byte 0xed → C++ OPCODE(0x12) → DOS Op_13 (NoOp)
//   memory byte 0xdc → C++ OPCODE(0x23) → DOS Op_24 (SetMood)
// The original ScummVM port and iter-10 audit notation labeled handlers by
// the C++ slot number while quoting DOS Op_N semantics — so they were
// systemically mismatched by 1. Iter-12 corrected the mapping after the
// trace at file offset 0x0262 showed OPCODE(0x12) over-consuming 2 bytes
// for an opcode that DOS specifies as 0-arg (NoOp).
//
// Each handler below is named per its TRUE DOS opcode (= C++ slot + 1).
// Byte consumption MUST match DOS exactly — script PC alignment depends on
// it. Semantics may simplify to safe stubs where DOS state isn't modeled.
// ============================================================================

OPCODE(0x10) {
	// C++ slot 0x10 = DOS Op_11 SetGlobalByteFlag. Reads 1 int16 offset
	// (1 shift = 2 bytes), writes 1 to global byte var. Original Animation
	// fall-through (set-bvar) happened to do the right thing structurally
	// but always wrote 1; semantically this matches.
	uint16 var = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x10: SetGlobalByteFlag var %d = 1 [DOS Op_11]", var);
	*_resources->getGlobalByteVariable(var) = 1;
	return kOk;
}

OPCODE(0x11) {
	// C++ slot 0x11 = DOS Op_12 ClearGlobalByteFlag. Reads 1 int16 offset
	// (1 shift), writes 0 to global byte var. Animation::0x11 ("reset
	// flag") happens to also write 0 with 1 shift — accidentally aligned.
	uint16 var = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x11: ClearGlobalByteFlag var %d = 0 [DOS Op_12]", var);
	*_resources->getGlobalByteVariable(var) = 0;
	return kOk;
}

OPCODE(0x12) {
	// C++ slot 0x12 = DOS Op_13 (CS:0x6ab7). Ghidra labels it "NoOp" but
	// the disassembly is JumpIfByteVar: reads var index at script[+2..+3],
	// reads jump target at script[+4..+5], if global byte var is non-zero
	// jump to target, else ADD BP,6 (skip 6-byte opcode). 2 shifts total.
	// Original Animation::0x12 handler ("jump if bvar") was correct; iter-12
	// broke it by treating as NoOp. FIXED iter-13.
	uint16 var = shift();
	uint16 off = shift();
	byte ok = *_resources->getGlobalByteVariable(var);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x12: JumpIfByteVar var=%u off=0x%04x val=%u [DOS Op_13]", var, off, ok);
	if (ok)
		_offset = off;
	return kOk;
}

OPCODE(0x13) {
	// C++ slot 0x13 = DOS Op_14 BranchIfRandomMatch (CS:0x6ad9). 6-byte
	// opcode: reads value at script[+2..+3], reads jump target at
	// script[+4..+5], rolls random; if matches, jump. ADD BP,6 if not.
	// 2 shifts. iter-12 had only 1 shift (under by 2). FIXED iter-13.
	uint16 max = shift();
	uint16 off = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x13: BranchIfRandomMatch max=%u off=0x%04x STUB (no jump) [DOS Op_14]", max, off);
	(void)off;
	return kOk;
}

// Per-actor flag-byte ops (DOS field map):
//   field +0x14 — cleared by C++ 0x1d (DOS Op_1e ClearFlag14)
//   field +0x15 — cleared by C++ 0x1e (DOS Op_1f ClearFlag15)
//                 set     by C++ 0x1f (DOS Op_20 SetFlag15)

OPCODE(0x1d) {
	// C++ slot 0x1d = DOS Op_1e ClearFlag14. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1d: ClearFlag14 [DOS Op_1e]");
	setDosField(0x14, 0);
	return kOk;
}

OPCODE(0x1e) {
	// C++ slot 0x1e = DOS Op_1f ClearFlag15. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1e: ClearFlag15 [DOS Op_1f]");
	setDosField(0x15, 0);
	return kOk;
}

OPCODE(0x1f) {
	// C++ slot 0x1f = DOS Op_20 SetFlag15. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1f: SetFlag15 [DOS Op_20]");
	setDosField(0x15, 1);
	return kOk;
}

OPCODE(0x20) {
	// C++ slot 0x20 = DOS Op_21 SetCallbackPointer (1000:6be5). 12-BYTE
	// opcode (ADD BP, 0xc). DOS:
	//   actor.field+0x5f = BP+DI+2  (callback offset = next opcode)
	//   actor.field+0x5d = ES       (callback segment)
	//   advance BP by 12 (skip past 10 trailing bytes — inline callback body)
	// The "callback offset" is the address of the inline body that follows
	// the opcode; later code can resume execution there. We capture the
	// PC of the byte right after the 2-byte opcode header (the start of
	// the 10-byte body), then skip the body by 5 shifts.
	const uint16 callbackPC = _baseOffset + _offset;
	setActorCallback(0xffff /* segment placeholder */, callbackPC);
	// Skip the 10-byte inline body.
	for (int i = 0; i < 5; i++)
		(void)shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x20: SetCallbackPointer cbPC=0x%04x [DOS Op_21]", callbackPC);
	return kOk;
}

OPCODE(0x21) {
	// C++ slot 0x21 = DOS Op_22 SetCallbackRelative (1000:6bf4). 1 shift
	// (signed int16 offset). DOS:
	//   if (arg != 0)  callback_off = DI + arg   (script-relative)
	//   else           callback_off = 0          (clear)
	//   callback_seg = ES
	const int16 off = (int16)shift();
	uint16 cbOff = 0;
	if (off != 0)
		cbOff = uint16(int32(_baseOffset) + int32(_offset) + int32(off));
	setActorCallback(0xffff, cbOff);
	debugC(2, kDebugLevelAnimation, "actor opcode 0x21: SetCallbackRelative off=%d → cbOff=0x%04x [DOS Op_22]",
		off, cbOff);
	return kOk;
}

OPCODE(0x22) {
	// C++ slot 0x22 = DOS Op_23 ClearCallback (1000:6c0a). 0 args.
	// Writes 0xffff to actor.field+0x5d (clears the callback segment;
	// effectively cancels any callback set by Op_20/Op_21).
	debugC(3, kDebugLevelAnimation, "actor opcode 0x22: ClearCallback [DOS Op_23]");
	clearActorCallback();
	return kOk;
}

OPCODE(0x25) {
	// C++ slot 0x25 = DOS Op_26 PlaySfx (CS:0x6c29). 4-BYTE opcode
	// (ADD BP,4): reads sfx index at script[+2..+3], calls
	// DispatchSfxRangeCheck, advances 4. iter-12 wrongly consumed 0 args.
	// FIXED iter-13: 1 shift.
	uint16 sfx = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x25: PlaySfx %u STUB [DOS Op_26]", sfx);
	return kOk;
}

// ============================================================================
// Crash-safety stubs for the DOS walk-driver opcodes that aren't yet fully
// implemented. (0x09 was promoted to a full implementation above; the
// iter-12 stub is removed to avoid a redefinition.)
// ============================================================================

OPCODE(0x0b) {
	// C++ slot 0x0b = DOS Op_0c FaceAndWalkWithFrame (1000:69cd). Embedded
	// facing byte at +1, target frame word at +2..+3. DOS:
	//   actor.facing (field+0x61) = byte
	//   SetActorPosition(); GetActorOffset(targetFacing)
	//   LookupActorAndStartPath()
	//   actor.targetFrame (field+8) = target
	//   actor.walkFlags (field+10) = currentFrame (field+0x10)
	//   end script
	// In the C++ parallel walking model, "facing" maps to _direction; walk
	// to target uses moveTo() which fills _framequeue.
	const byte face = embeddedByte();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0b: FaceAndWalkWithFrame face=%u target=%u [DOS Op_0c]",
		face, target);
	if (face >= 1 && face <= 8)
		_direction = Direction(face);
	setDosField(0x61, face);
	_nextFrame = target;
	setMainSprite(target);
	if (target != 0xffff && _room == Log.currentRoom() && Log.room())
		moveTo(target);
	return kFrameDone;
}

OPCODE(0x0c) {
	// C++ slot 0x0c = DOS Op_0d FaceAndWalk (1000:6a0e). Embedded byte at
	// +1 only — same as 0x0b but no target frame (the actor walks toward
	// its current targetFacing field). Sets facing, starts path, ends
	// script. Without separate facing-vs-target-facing modeling in C++,
	// just set _direction.
	const byte face = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0c: FaceAndWalk face=%u [DOS Op_0d]", face);
	if (face >= 1 && face <= 8)
		_direction = Direction(face);
	setDosField(0x61, face);
	return kFrameDone;
}

OPCODE(0x1b) {
	// C++ slot 0x1b = DOS Op_1c QueueMoveSlotMode0 (1000:6b5d). 3 shifts
	// (a, b, c). Finds first free slot in actor.field+0x19 (8 entries),
	// writes (b, c, a, mode=0). DOS layout per disasm:
	//   slot.field2 = arg1 (b)
	//   slot.field4 = arg2 (c)
	//   slot.field0 = arg3 (a)
	//   slot.field6 = mode (0)
	// Overflow sets g_pendingErrorCode = 0xc; we drop silently.
	const uint16 arg1 = shift();
	const uint16 arg2 = shift();
	const uint16 arg3 = shift();
	const bool ok = queueMoveSlot(MoveSlot(arg3, arg1, arg2, 0));
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1b: QueueMoveSlotMode0 (a=%u,b=%u,c=%u) %s [DOS Op_1c]",
		arg1, arg2, arg3, ok ? "queued" : "DROPPED (queue full)");
	return kOk;
}

OPCODE(0x1c) {
	// C++ slot 0x1c = DOS Op_1d QueueMoveSlotMode1 (1000:6b95). Same as
	// 0x1b but mode=1.
	const uint16 arg1 = shift();
	const uint16 arg2 = shift();
	const uint16 arg3 = shift();
	const bool ok = queueMoveSlot(MoveSlot(arg3, arg1, arg2, 1));
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1c: QueueMoveSlotMode1 (a=%u,b=%u,c=%u) %s [DOS Op_1d]",
		arg1, arg2, arg3, ok ? "queued" : "DROPPED (queue full)");
	return kOk;
}

} // end of namespace
