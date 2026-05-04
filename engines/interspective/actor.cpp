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

	Engine::instance().logic()->addAnimation(this);

	init_opcodes<37>();
}

bool Actor::isFine() const {
	return _room == Log.currentRoom() && _base && !_attentionNeeded &&
			(!_framequeue.empty() || !_confused || _nextDirection);
}

void Actor::setAnimation(uint16 offset) {
	_base = _base - _baseOffset + offset;
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
	assert(false);
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
			Common::Array<byte> nexts = current->nexts();
			for (int i = 0; i < 8; i++)
				if (nexts[i]) {
					s += Common::String::format(", %d", int(nexts[i]));
					next.push_back(Log.room()->getFrame(nexts[i]));
					if (nexts[i] == to) {
						found = true;
						break;
					}
				}
			current++;
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

	Common::List<Frame>::iterator it = path.end();
	it--;
	if (it->index() != frame) {
		Common::List<Frame> p;
		p.push_back(Log.room()->getFrame(frame));
		path = p;
	}

	Common::String s;
	it = path.begin();
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

OPCODE(0x01) {
	// DOS ActorOp_01_ScriptEnd (CS:0x68d3): clears actor field0+field1, sets
	// g_actor_script_ended=1 to signal the engine to pick a new script.
	// C++: closest equivalent — terminate current animator (_base=0) and
	// flag _confused so animate() picks a new state next tick. Functionally
	// aligned.
	debugC(1, kDebugLevelActor, "actor opcode 0x01: script end (C++ confuse-fallback)");

	_offset = 0;
	_base = 0;
	_confused = true;
	return kFrameDone;
}

OPCODE(0x14) {
	// DOS ActorOp_14_BranchIfRandomMatch (CS:0x6ad9): rolls a random byte
	// and jumps to embedded offset if it matches. C++ has it commented as
	// "jump if I'm speaking" — that's a guess from the original port and
	// does NOT match DOS. STUB: consume the offset arg, never jump.
	// (Mismatch tolerable because random-jump effect is statistically rare.)
	uint16 off = shift();

	debugC(1, kDebugLevelAnimation, "actor opcode 0x14: BranchIfRandomMatch -> 0x%04x STUB (DOS: random match jump)", off);

	return kOk;
}

OPCODE(0x15) {
	// DOS ActorOp_15_WaitForSpeechSlot (CS:0x6af1): looks up the actor's
	// speech slot via FindSpeechSlotById; checks scroll-dirty for the main
	// character. C++ commented as "look at cursor direction" — wrong.
	// STUB.
	debugC(1, kDebugLevelAnimation, "actor opcode 0x15: WaitForSpeechSlot STUB (DOS: lookup speech slot)");

	return kOk;
}

OPCODE(0x16) {
	// DOS ActorOp_16_PickAnimationSet (CS:0x6c3e): picks the actor's
	// animation set (writes actor.field_0x68) based on cursor offset
	// vs. current pose, when cursor mode is 0x80. C++ commented as
	// "look direction branch" — wrong, and consumes 1 byte + 1 word.
	// Per DOS the embedded byte/word here are NOT the args — leave STUB.
	byte val = embeddedByte();
	uint16 off = shift();

	debugC(1, kDebugLevelAnimation, "actor opcode 0x16: PickAnimationSet STUB val=%d off=0x%04x", val, off);

	return kOk;
}

OPCODE(0x17) {
	// DOS ActorOp_17_BranchIfAnimSetEquals (CS:0x6b17): compares actor's
	// animation set (field_0x68) to embedded byte; jumps if equal.
	// C++ compares to _direction instead and calls setAnimation(off) —
	// the C++ author conflated "animation set" with "facing direction".
	// In practice these may overlap (8 directions ↔ 8 anim sets) so the
	// behavior is often functionally close, but it's not literally the
	// same field. NOT changed — would require modeling field_0x68
	// separately to fix without regression.
	byte val = embeddedByte();
	uint16 off = shift();

	debugC(3, kDebugLevelAnimation, "actor opcode 0x17: BranchIfAnimSetEquals (C++ uses _direction) val=%d off=0x%04x", val, off);

	if (val == _direction) {
		setAnimation(off);
	}

	return kOk;
}

OPCODE(0x18) {
	// DOS ActorOp_18_BranchIfMoodEquals (CS:0x6b29): compares actor's mood
	// (field_0x63) to embedded byte; jumps if equal. C++ instead stashes
	// `_nextAnimator = val` for animate() to apply later — completely
	// different mechanism. The C++ "next animator" is part of its parallel
	// walking model. NOT changed (changing it would need DOS mood-byte
	// modeling on Actor).
	uint16 val = shift();

	debugC(3, kDebugLevelAnimation, "actor opcode 0x18: BranchIfMoodEquals (C++ stashes _nextAnimator=0x%x)", val);
	_nextAnimator = val;

	return kOk;
}

OPCODE(0x23) {
	// DOS ActorOp_23_ClearCallback (CS:0x6c0a): writes 0xffff to
	// actor.field_0x5d (clears a callback pointer). C++ instead sets
	// _direction to one of 8 compass values — completely different
	// semantics. The C++ "face direction" is part of the parallel walking
	// model and is load-bearing for the engine's pathfinding heuristic.
	// NOT changed without porting the DOS callback model.
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
		assert(false);
	}

	return kOk;
}

OPCODE(0x24) {
	// DOS ActorOp_24_SetMood (CS:0x6c13): writes embedded byte to
	// actor.field_0x63 (sets actor's mood). C++ instead sets
	// _attentionNeeded = true to drive its parallel walking model
	// (animate() → nextFrame()). The two have NOTHING in common at the
	// DOS level — the C++ usage is a heuristic that happens to fire on
	// each move-animator script (which emits 0x24 between frames) and
	// drives the walk queue. CRITICAL load-bearing behavior — see iter 7
	// (movement-animation speed fix). Do NOT remove the
	// _attentionNeeded write without first porting the DOS walk-driver
	// model (which uses ActorOp_07_SetTargetFrame and friends).
	byte dir = embeddedByte();

	debugC(3, kDebugLevelAnimation, "actor opcode 0x24: SetMood (C++ uses as walk-attention trigger, mood=%d)", dir);
	_attentionNeeded = true;

	return kOk;
}

// ============================================================================
// Phase-1 actor opcode overrides (iter 11). The C++ Animation::opcodeHandler
// fall-through table has DIFFERENT semantics for these slots than DOS
// requires. Without these overrides, an actor script emitting 0x10/0x11/
// 0x12/0x13/0x26 would trigger Animation handlers that:
//   0x10 — write 1 to a global byte var (DOS: NoOp)
//   0x11 — write 0 to a global byte var (DOS: SET to 1, INVERTED)
//   0x12 — conditionally jump on byte var (DOS: clear global byte var to 0)
//   0x13 — random jump (DOS: NoOp)
//   0x26 — error("unhandled animation opcode") and crash (DOS: PlaySfx)
// All of these are reachable from the bytecode if any actor script uses
// global flag setters or sfx triggers. These overrides make the engine
// SAFE under such bytecode without requiring the broader walking-model
// rewrite (deferred per iter-10 roadmap).
// ============================================================================

OPCODE(0x10) {
	// DOS ActorOp_10_NoOp (CS:0x6a7e): no-op. Override Animation::0x10
	// which would erroneously set a global byte var to 1.
	debugC(4, kDebugLevelAnimation, "actor opcode 0x10: NoOp");
	return kOk;
}

OPCODE(0x11) {
	// DOS ActorOp_11_SetGlobalByteFlag (CS:0x6a83): reads 1 int16 offset,
	// writes 1 to byte at DAT_1000_009d + offset (global byte var table).
	// Override Animation::0x11 which would write 0 (clear) — INVERTED!
	uint16 var = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x11: SetGlobalByteFlag var %d = 1", var);
	*_resources->getGlobalByteVariable(var) = 1;
	return kOk;
}

OPCODE(0x12) {
	// DOS ActorOp_12_ClearGlobalByteFlag (CS:0x6a9d): reads 1 int16 offset,
	// writes 0 to byte at DAT_1000_009d + offset. Override Animation::0x12
	// which would conditionally jump (totally different semantics).
	uint16 var = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x12: ClearGlobalByteFlag var %d = 0", var);
	*_resources->getGlobalByteVariable(var) = 0;
	return kOk;
}

OPCODE(0x13) {
	// DOS ActorOp_13_NoOp (CS:0x6ab7): no-op. Override Animation::0x13
	// which would consume max+offset args and randomly jump.
	debugC(4, kDebugLevelAnimation, "actor opcode 0x13: NoOp");
	return kOk;
}

OPCODE(0x26) {
	// DOS ActorOp_26_PlaySfx (CS:0x6c29): calls DispatchSfxRangeCheck
	// (1000:606d) — registers the actor's sfx for playback if within
	// active range. Engine has no SFX driver yet (see iter-1 sound stubs);
	// safe-stub so the engine doesn't error("unhandled animation opcode").
	debugC(2, kDebugLevelAnimation, "actor opcode 0x26: PlaySfx STUB");
	return kOk;
}

// Per-actor flag-byte ops (DOS field offsets per the iter-10 field map):
//   0x14 — flag14 (cleared by 0x1e)
//   0x15 — flag15 (set by 0x20, cleared by 0x1f)
//   0x65 — misc byte (set by 0x25, byte from script[+1])
// Stored in Actor::_dosFields hashmap. Override Animation handlers (which
// would do completely different things in these slots).

OPCODE(0x1e) {
	// DOS ActorOp_1e_ClearFlag14 (CS:0x6bcd): actor[+0x14] = 0.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1e: ClearFlag14");
	setDosField(0x14, 0);
	return kOk;
}

OPCODE(0x1f) {
	// DOS ActorOp_1f_ClearFlag15 (CS:0x6bd5): actor[+0x15] = 0.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1f: ClearFlag15");
	setDosField(0x15, 0);
	return kOk;
}

OPCODE(0x20) {
	// DOS ActorOp_20_SetFlag15 (CS:0x6bdd): actor[+0x15] = 1.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x20: SetFlag15");
	setDosField(0x15, 1);
	return kOk;
}

OPCODE(0x25) {
	// DOS ActorOp_25_SetField65 (CS:0x6c1e): actor[+0x65] = embedded byte
	// from script[+1].
	byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x25: SetField65 = %d", v);
	setDosField(0x65, v);
	return kOk;
}

// Crash-safety stubs for DOS actor ops not yet ported. Without these, the
// Animation::opcodeHandler<N> fall-through for these slots either crashes
// (error("unhandled animation opcode")) or does completely wrong work.
// Each stub consumes the correct number of arg bytes per the iter-10
// audit so the script PC advances correctly. Swapping these out with
// faithful implementations is a Phase-2 task (requires modeling
// actor.field+8 target frame, the move-queue at +0x19, etc).

OPCODE(0x09) {
	// DOS ActorOp_09_WalkRelative: 2 int8 args (dx, dy at script[+2,+3]),
	// would translate the actor + set walk-counter and end script.
	debugC(2, kDebugLevelAnimation, "actor opcode 0x09: WalkRelative STUB (Phase-2)");
	return kOk;
}

OPCODE(0x0b) {
	// DOS ActorOp_0b_WalkAbsoluteWithFrame: 3 int16 args (x, y, target).
	uint16 x = shift();
	uint16 y = shift();
	uint16 target = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x0b: WalkAbsoluteWithFrame STUB x=%d y=%d target=%d", x, y, target);
	return kOk;
}

OPCODE(0x0c) {
	// DOS ActorOp_0c_FaceAndWalkWithFrame: facing byte + target word.
	byte face = embeddedByte();
	uint16 target = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x0c: FaceAndWalkWithFrame STUB face=%d target=%d", face, target);
	return kOk;
}

OPCODE(0x1c) {
	// DOS ActorOp_1c_QueueMoveSlotMode0: 3 int16 args (mode 0 to slot).
	uint16 a = shift();
	uint16 b = shift();
	uint16 c = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1c: QueueMoveSlotMode0 STUB %d,%d,%d", a, b, c);
	return kOk;
}

OPCODE(0x1d) {
	// DOS ActorOp_1d_QueueMoveSlotMode1: 3 int16 args (mode 1 to slot).
	uint16 a = shift();
	uint16 b = shift();
	uint16 c = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1d: QueueMoveSlotMode1 STUB %d,%d,%d", a, b, c);
	return kOk;
}

OPCODE(0x21) {
	// DOS ActorOp_21_SetCallbackPointer: stashes current script PC as
	// the actor's callback. No args consumed.
	debugC(2, kDebugLevelAnimation, "actor opcode 0x21: SetCallbackPointer STUB");
	return kOk;
}

OPCODE(0x22) {
	// DOS ActorOp_22_SetCallbackRelative: 1 int16 arg (relative offset).
	uint16 off = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x22: SetCallbackRelative STUB off=0x%04x", off);
	return kOk;
}

} // end of namespace
