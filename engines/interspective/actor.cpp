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
		assert(false);
	}

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

// Actor opcodes 0x02..0x0e (other than the ones below) fall through to the
// Animation handler with the same number, which by lucky coincidence happens
// to consume the right number of bytes for each corresponding DOS Op_(N+1)
// (verified by inspection: every slot in 0x02..0x0e matches DOS byte counts
// EXCEPT 0x0f, which Animation implements as "jump"=1 shift but DOS Op_10
// is NoOp=0 args). Override 0x0f to fix the alignment.

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
	// C++ slot 0x20 = DOS Op_21 SetCallbackPointer (CS:0x6be5). 12-BYTE
	// opcode (ADD BP,0xc). The handler stashes (BP+DI+2) as the callback
	// PC, then advances past 10 trailing bytes which presumably form an
	// inline callback body. iter-12 wrongly consumed 0 args. FIXED
	// iter-13: consume 10 bytes (5 shifts) past the baseline 2.
	uint16 a = shift();
	uint16 b = shift();
	uint16 c = shift();
	uint16 d = shift();
	uint16 e = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x20: SetCallbackPointer (skipping 10-byte inline body) STUB [DOS Op_21]");
	(void)a; (void)b; (void)c; (void)d; (void)e;
	return kOk;
}

OPCODE(0x21) {
	// C++ slot 0x21 = DOS Op_22 SetCallbackRelative. Reads 1 int16
	// relative offset (1 shift). Stub: consume bytes, ignore.
	uint16 off = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x21: SetCallbackRelative off=0x%04x STUB [DOS Op_22]", off);
	return kOk;
}

OPCODE(0x22) {
	// C++ slot 0x22 = DOS Op_23 ClearCallback. 0 args.
	debugC(3, kDebugLevelAnimation, "actor opcode 0x22: ClearCallback STUB [DOS Op_23]");
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
// Crash-safety stubs for the DOS walk-driver opcodes (Phase-2 placeholders).
// Each consumes the correct number of bytes per DOS spec to keep the script
// PC aligned; semantics deferred until the walk-driver is properly ported.
// ============================================================================

OPCODE(0x09) {
	// C++ slot 0x09 = DOS Op_0a WalkAbsolute. Reads 2 int16 (x, y) = 2 shifts.
	uint16 x = shift();
	uint16 y = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x09: WalkAbsolute STUB x=%d y=%d [DOS Op_0a]", x, y);
	return kOk;
}

OPCODE(0x0b) {
	// C++ slot 0x0b = DOS Op_0c FaceAndWalkWithFrame. Reads embedded byte
	// at +1 (facing) + word at +2..+3 (target) = byte + 1 shift.
	byte face = embeddedByte();
	uint16 target = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x0b: FaceAndWalkWithFrame face=%d target=%d STUB [DOS Op_0c]", face, target);
	return kOk;
}

OPCODE(0x0c) {
	// C++ slot 0x0c = DOS Op_0d FaceAndWalk. Reads embedded byte at +1
	// only. NO shift — was previously over-consuming a shift.
	byte face = embeddedByte();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x0c: FaceAndWalk face=%d STUB [DOS Op_0d]", face);
	return kOk;
}

OPCODE(0x1b) {
	// C++ slot 0x1b = DOS Op_1c QueueMoveSlotMode0. Reads 3 int16 args
	// (3 shifts = 6 bytes).
	uint16 a = shift();
	uint16 b = shift();
	uint16 c = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1b: QueueMoveSlotMode0 %d,%d,%d STUB [DOS Op_1c]", a, b, c);
	return kOk;
}

OPCODE(0x1c) {
	// C++ slot 0x1c = DOS Op_1d QueueMoveSlotMode1. Reads 3 int16 args.
	uint16 a = shift();
	uint16 b = shift();
	uint16 c = shift();
	debugC(2, kDebugLevelAnimation, "actor opcode 0x1c: QueueMoveSlotMode1 %d,%d,%d STUB [DOS Op_1d]", a, b, c);
	return kOk;
}

} // end of namespace
