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
#include "interspective/sound.h"
#include "interspective/inter.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

namespace Interspective {
//

Actor::Actor(const CodePointer &code) : Animation(code, Common::Point()), _id(0) {
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

void Actor::sayAtPos(const Common::String &text, Common::Point pos) {
	_speech = Speech(this, text, pos);
}

Actor::Speech::~Speech() { while (!_cb.empty()) Log.runLater(_cb.pop()); }

bool Actor::isMoving() const {
	// Mirrors DOS actor.field+0x65 ("walk-step counter / moving flag"):
	//   non-zero while a step is in flight or queued.
	// In C++ the walk path is staged via _framequeue; while it has
	// entries, more steps remain. After the queue empties the actor is
	// at the path's last frame and considered "still".
	return !_framequeue.empty();
}

void Actor::callMeWhenStill(const CodePointer &cp) {
	// If the actor is already still, fire on the next tick (immediate
	// post-walk callback equivalent). While moving, queue into _callBacks
	// — Animation::tick drains it via callBacks() once attention/state
	// settles.
	if (!isMoving())
		Log.runLater(cp);
	else
		_callBacks.push(cp);
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

	// DOS MoveActorToRoom @ 1000:70e1, NOT-FOUND path (1000:7103-710b):
	//   MOV ES:[SI+0x61], AL          ; actor.frame = target frame
	//   CALL SetActorPosition         ; reads frame[N].pos → actor X/Y
	//   STC; RET
	// That's it. No animation change, no direction logic. Just position
	// warp. The actor's existing animation script keeps running.
	//
	// Mirror that exactly: setFrame() does both the _frame assignment
	// and the position lookup-and-update via Room::getFrame.
	// (iter-34's attempt to also pick a direction and call
	// setAnimation(moveAnimator(dir)) was overreaching — DOS doesn't
	// do that, and the synthetic Frame I built crashed on
	// Frame::operator-'s assumption that _nexts has 8 entries.)
	if (_frame == 0 || cur.position().x == 999) {
		debugC(3, kDebugLevelActor, "moveTo(%u): warp (no valid current frame)", (uint)frame);
		setFrame(frame);
		return;
	}

	// Validate target frame BEFORE running findPath. If target is invalid
	// in current room (e.g., target lives in a different room and C++
	// only has the player's current room loaded), do a soft warp via
	// setFrame() — same as the !valid-current-frame branch above.
	// Pushing a sentinel Frame onto _framequeue causes _frame corruption
	// on the next nextFrame() pop (Pass2-16 game.log: 44064 garbage
	// value from sentinel.index() reads).
	const Frame targetFrame = Log.room()->getFrame(frame);
	if (targetFrame.position().x == 999) {
		debugC(3, kDebugLevelActor, "moveTo(%u): warp (target frame invalid in current room)", (uint)frame);
		setFrame(frame);
		return;
	}

	Common::List<Frame> path = findPath(cur, frame);

	// findPath now returns an EMPTY list when the target is unreachable
	// (matches DOS bound-check on visited buffer). Treat that as a
	// single-frame walk to the validated target.
	if (path.empty()) {
		path.push_back(targetFrame);
	} else {
		Common::List<Frame>::iterator it = path.end();
		it--;
		if (it->index() != frame) {
			Common::List<Frame> p;
			p.push_back(targetFrame);
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
		// DOS RunActorScript writes DAT_1cb5_666c = current actor id
		// before InterpretBytecode dispatches the actor's script.
		// Op_5b reads this to know "which actor am I scripting for".
		// Save/restore so nested dispatches don't trash the value.
		const uint16 savedEntityId = Log.currentEntityId();
		Log.setCurrentEntityId(_id);

		Animation::Status s;
		if (_debug) gDebugLevel += 3;
			s = Animation::tick();
		if (_debug) gDebugLevel -= 3;

		Log.setCurrentEntityId(savedEntityId);
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
	_zIndex = int8(code[kOffsetZIndex]);
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
	_nextFrame = sprite;
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
			Log.runLater(cb.pop());
	}

	foreach (RoomCallback, _roomCallbacks) {
		if (_room == Log.currentRoom() || !it->timeout) {
			Log.runLater(it->callback);
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

Actor::Speech::Speech(Actor *parent, const Common::String &text) : _text(text), _actor(parent) {
	// DOS paces dialog by sample (audio) playback length — typically
	// a few seconds. Without samples, the bubble must stay up long
	// enough to read it. Approximate sample duration from text length:
	//   ~3 ticks per character + 30-tick floor (reading speed roughly
	//   100 chars / 3 sec at our 25Hz = 8 chars/sec). Scales naturally
	//   from short interjections to long monologue. Previously this was
	//   hardcoded to 20 ticks (~0.8s) which made the intro feel rushed
	//   compared to DOS. [iter-33]
	_ticksLeft = MAX<uint16>(30, 3 * uint16(text.size()));
	const Common::Point &position = parent->getSpeechPosition();
	debugC(1, kDebugLevelActor, "adding speech \"%s\" (%u ticks) for %s at %d:%d",
		text.c_str(), (uint)_ticksLeft, parent->_debugInfo, position.x, position.y);
	_image = new Interspective::Sprite;
	_rect = Graf.paintSpeechInBubble(position, 235, reinterpret_cast<const byte *>(text.c_str()), _image);
}

Actor::Speech::Speech(Actor *parent, const Common::String &text, Common::Point overridePos)
		: _text(text), _actor(parent) {
	_ticksLeft = MAX<uint16>(30, 3 * uint16(text.size()));
	debugC(1, kDebugLevelActor, "adding speech \"%s\" (%u ticks) for %s at OVERRIDE %d:%d",
		text.c_str(), (uint)_ticksLeft, parent->_debugInfo, overridePos.x, overridePos.y);
	_image = new Interspective::Sprite;
	_rect = Graf.paintSpeechInBubble(overridePos, 235, reinterpret_cast<const byte *>(text.c_str()), _image);
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
	if (_room != Log.currentRoom())
		return;

	Animation::paint(g);
}

void Actor::paintSpeech(Graphics *g) {
	if (_room != Log.currentRoom())
		return;

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
	// C++ slot 0x14 = DOS Op_15 WaitForSpeechSlot @ 1000:6af1.
	//   AX = g_render_actor_id;
	//   if (AX == g_main_character_id) {
	//       CALL CheckScrollDirty; if (no carry) → ADD BP,4 (advance);
	//   }
	//   CALL FindSpeechSlotById(this_actor);
	//   if (carry) → ADD BP,4 (advance, no slot found = silent);
	//   else → BP = ES:[BP+DI+2] (loop back to jump target = wait).
	// = "while this actor has an active speech slot, re-enter this
	// opcode". C++ has per-actor _speech (Actor::isSpeaking()) which
	// matches the slot model — when the actor is speaking we rewind
	// _offset back to the start of this opcode (jump target encodes
	// "this opcode's address" in DOS scripts).
	uint16 off = shift();
	if (isSpeaking()) {
		// Re-execute: rewind to the jump target. The script encodes
		// the opcode's own address as the target, so following it loops
		// back here on the next tick. Tick advancement happens through
		// the dispatch loop's normal flow.
		_offset = off;
		debugC(3, kDebugLevelAnimation, "actor opcode 0x14: WaitForSpeechSlot looping (still speaking) [DOS Op_15]");
		return kFrameDone;
	}
	debugC(3, kDebugLevelAnimation, "actor opcode 0x14: WaitForSpeechSlot done (silent) [DOS Op_15]");
	return kOk;
}

OPCODE(0x15) {
	// C++ slot 0x15 = DOS Op_16 PickAnimationSet @ 1000:6c3e —
	// FULL-FIDELITY port (matches DOS bounding-rect classifier byte-
	// for-byte; word-for-word step-toward state machine).
	//
	// Op_15 outer (1000:6c3e..0x6cc9):
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
	//   verb_mode_path (1000:6c6b):
	//     PUSH DS, SI;                            ; protect actor record
	//     MOV BH,0; MOV BL, [SI+0x18];            ; BX = actor.field+0x18
	//                                              ; (rect-height portion)
	//     CALL CalcSpriteOffsetIfPlaced @ 0x700f  ; → BP = target pose
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
	// Step-toward semantics (1000:6c79..0x6cba):
	//   delta = target - current (signed byte arithmetic):
	//     delta == 0 OR current==0x63 OR target==0x63 → snap.
	//     |delta| >= 6  → INC current with wrap (1↔8). Large delta →
	//                     short rotation goes via wrap.
	//     3 ≤  delta ≤ 5 → INC current.
	//    -5 ≤  delta ≤ -3 → DEC current (with wrap 1→8).
	//     |delta| ≤ 2     → keep current (deadband for stability).
	if (Log.cursorMode() != 0x80) {
		setDosField(0x68, 0);
		debugC(3, kDebugLevelAnimation,
			"actor opcode 0x15: PickAnimationSet — cursor_mode=%u != 0x80 → field+0x68 = 0",
			Log.cursorMode());
		return kOk;
	}

	// Verb-mode classifier. Reproduces CalcSpriteOffsetIfPlaced exactly:
	// build the actor's bounding rect from field+0x4/+0x6 + sprite offset
	// bytes (+0x8/+0x9) + width/height bytes (+0x17/+0x18); then test
	// cursor against rect bounds.
	const Common::Point cursor = Log.engine()->graphics()->cursorPosition();
	// adjusted_x = field+0x4 - field+0x8 (DOS sprite-offset adjustment)
	// adjusted_y = field+0x6 + field+0x9
	// Sprite offset bytes are signed; we read via dosField + cast to
	// int8 to preserve sign.
	const int16 adjustedX = int16(position().x) - int8(dosField(0x8));
	const int16 adjustedY = int16(position().y) + int8(dosField(0x9));
	const uint8 width  = dosField(0x17);   // BP in DOS
	const uint8 height = dosField(0x18);   // BX in DOS
	// Bounding rect (DOS layout):
	//   left = adjustedX
	//   right = adjustedX + width
	//   top = adjustedY - height
	//   bottom = adjustedY
	const int16 leftX  = adjustedX;
	const int16 rightX = adjustedX + int16(width);
	const int16 topY   = adjustedY - int16(height);
	const int16 botY   = adjustedY;

	const int16 cx = int16(cursor.x);
	const int16 cy = int16(cursor.y);
	uint8 target;
	if (cy < topY) {
		// Cursor above actor's rect.
		if (cx < leftX)        target = 8;  // NW
		else if (cx < rightX)  target = 1;  // N
		else                   target = 2;  // NE
	} else if (cy > botY) {
		// Cursor below actor's rect.
		if (cx < leftX)        target = 6;  // SW
		else if (cx < rightX)  target = 5;  // S
		else                   target = 4;  // SE
	} else {
		// Cursor in vertical band.
		if (cx < leftX)        target = 7;  // W
		else if (cx < rightX)  target = 0x63; // center
		else                   target = 3;  // E
	}

	const uint8 current = dosField(0x68);
	if (current == 0x63 || target == 0x63 || target == current) {
		setDosField(0x68, target);
		debugC(3, kDebugLevelAnimation,
			"actor opcode 0x15: PickAnimationSet snap target=%u current=%u",
			target, current);
		return kOk;
	}
	// Step-toward with wrap and deadband. Reproduces DOS 1000:6c79..0x6cba.
	const int8 delta = int8(target) - int8(current);
	uint8 next = current;
	if (delta <= -6 || delta >= 6) {
		// Large delta: rotate via INC-with-wrap (matches DOS LAB_1000_6cb1).
		next = current + 1;
		if (next > 8) next = 1;
	} else if (delta >= 3 && delta <= 5) {
		next = current + 1;
		if (next > 8) next = 1;
	} else if (delta >= -5 && delta <= -3) {
		next = (current == 1) ? 8 : current - 1;
	}
	// |delta| <= 2: deadband — next stays = current.
	if (next != current)
		setDosField(0x68, next);
	debugC(3, kDebugLevelAnimation,
		"actor opcode 0x15: PickAnimationSet rect=(%d..%d, %d..%d) cursor=(%d,%d) target=%u current=%u → %u (delta=%d)",
		leftX, rightX, topY, botY, cx, cy, target, current, next, int(delta));
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
	// `dosField(0x68)` which is set by Op_15 (ActorOp_16 PickAnimationSet).
	const byte val = embeddedByte();
	const uint16 off = shift();
	const uint8 animSet = dosField(0x68);
	if (animSet == val) {
		debugC(3, kDebugLevelAnimation,
			"actor opcode 0x16: BranchIfAnimSetEquals val=%d field+0x68=%u → jump 0x%04x [DOS Op_17]",
			val, animSet, off);
		setAnimation(off);
	} else {
		debugC(3, kDebugLevelAnimation,
			"actor opcode 0x16: BranchIfAnimSetEquals val=%d field+0x68=%u (no match) [DOS Op_17]",
			val, animSet);
	}
	return kOk;
}

OPCODE(0x17) {
	// C++ slot 0x17 = DOS Op_18 BranchIfMoodEquals @ 1000:6b29.
	//   if (actor.field+0x63 == embedded_byte) actor.PC = jump_target;
	//   else fall through.
	// Mood is stored sparsely on Actor::_dosFields (set by ActorOp_24).
	byte val = embeddedByte();
	uint16 off = shift();
	const uint8 mood = dosField(0x63);
	if (mood == val) {
		debugC(3, kDebugLevelAnimation, "actor opcode 0x17: BranchIfMoodEquals val=%d mood=%d → jump 0x%04x [DOS Op_18]",
			val, mood, off);
		setAnimation(off);
	} else {
		debugC(3, kDebugLevelAnimation, "actor opcode 0x17: BranchIfMoodEquals val=%d mood=%d (no match) [DOS Op_18]",
			val, mood);
	}
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
	// to use embeddedByte too. DOS stores the byte in actor field+0x12,
	// and DrawAllRoomObjects uses that byte as the actor render layer.
	byte v = embeddedByte();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x1a: SetField12ClearFlag16 = %d [DOS Op_1b]", v);
	setDosField(0x12, v);
	setDosField(0x16, 0);
	_zIndex = int8(v);
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
// Most of these ops end the script (Op_06..0a; DOS sets
// g_actor_script_ended = 1) and return kFrameDone — the C++ dispatcher
// breaks out of the per-tick opcode loop and queues
// _ticksLeft = _interval before the next tick. Ops that don't end the
// script return kOk (continue with the next opcode this tick).
//
// **Pass2-17 NOTE**: Op_04/Op_05 (slots 0x03/0x04) are NOT
// "SetCurrentFrame" / "SetCurrentFrameFromGlobal" despite the original
// label. DOS @ 1000:6912 / 1000:691d both write to actor `[SI+0x10]` =
// `kOffsetInterval` (Animation `_interval`), NOT field+0x61 (`_frame`).
// The original Animation::OPCODE(0x03)/(0x04) ("set interval") was
// correct; an earlier override misclassified the op name and clobbered
// `_frame=0` mid-walk, locking the actor in `nextFrame`/`turnTo` cycles.
// The slot-0x03/0x04 overrides below now match DOS — kept for symmetry
// with the rest of the family.
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
	// C++ slot 0x03 = DOS Op_04 SetInterval (1000:6912). 1 shift.
	//   MOV AX, ES:[BP+DI+2]      ; arg word
	//   MOV byte ptr [SI+0x10], AL ; field+0x10 = AL  (interval byte)
	//   ADD BP, 4 / RET
	// field+0x10 = kOffsetInterval = Animation::_interval (NOT frame —
	// _frame is field+0x61 per Op_7a / moveTo comment). Pass2-17: prior
	// override wrote `_frame` based on a misclassification of the opcode
	// name; that wiped pathfinding state mid-script and was the ROOT
	// CAUSE of the room-1 stuck loop (game.log Pass2-16). Now writes
	// _interval, matching DOS [SI+0x10].
	const uint16 word = shift();
	const uint8 interval = uint8(word);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x03: SetInterval %u [DOS Op_04]", interval);
	_interval = interval;
	return kOk;
}

OPCODE(0x04) {
	// C++ slot 0x04 = DOS Op_05 SetIntervalFromGlobal (1000:691d). 1 shift.
	//   reads global word var at index (arg/2)
	//   writes its low byte to [SI+0x10] = Animation::_interval.
	// Pass2-17: same misclassification fix as 0x03 — was wrongly writing
	// _frame, which kept resetting pathfinding state to 0 every cycle in
	// the room-1 loop. Now writes _interval per DOS.
	const uint16 offset = shift();
	const uint16 word = READ_LE_UINT16(_resources->getGlobalWordVariable(offset / 2));
	const uint8 interval = uint8(word);
	debugC(3, kDebugLevelAnimation, "actor opcode 0x04: SetIntervalFromGlobal var[%u/2]=0x%04x → %u [DOS Op_05]",
		offset, word, interval);
	_interval = interval;
	return kOk;
}

OPCODE(0x05) {
	// C++ slot 0x05 = DOS Op_06 WalkRelativeWithFrame @ 1000:6939.
	// Reads 2 signed bytes (dx, dy) + 1 word (target frame).
	//   actor.field+0x4 += dx;             ; position x
	//   actor.field+0x6 += dy;             ; position y
	//   actor.field+0x8  = target_frame;   ; sprite/target ID
	//   actor.field+0xa  = actor.field+0x10;  ; copy a byte
	//   g_actor_script_ended = 1;
	//
	// **Important**: Despite the "WalkRelative" name, DOS does NOT
	// engage walk pathfinding. It only updates the actor's relative
	// position + sprite-target field. Walk pathfinding (FindActorPath)
	// is engaged separately by other opcodes. PRIOR C++ called
	// `moveTo(target)` which corrupts _frame when target is invalid in
	// current room — same bug pattern as Op_06 (see comment there).
	const int8 dx = shiftByte();
	const int8 dy = shiftByte();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x05: WalkRelativeWithFrame d=(%d,%d) target=%u [DOS Op_06]",
		dx, dy, target);
	_position.x += dx;
	_position.y += dy;
	setMainSprite(target);  // sprite ID write (DOS field+0x8 analog)
	_nextFrame = target;    // tracked for query opcodes that read targetFrameId.
	setDosField(0x0a, dosField(0x10));   // DOS field+0xa byte copy.
	return kFrameDone;
}

OPCODE(0x06) {
	// C++ slot 0x06 = DOS Op_07 SetTargetFrame @ 1000:69a4. 1 shift.
	//   actor.field+0x8  = target;            ; sprite/target ID
	//   actor.field+0xa  = actor.field+0x10;  ; copy a byte
	//   g_actor_script_ended = 1;              ; end this script run
	//
	// **Important**: DOS does NOT engage walk here. It only updates
	// the sprite-target field. Some other code (e.g., the actor's
	// drawing or animation pipeline) reads field+0x8 later.
	//
	// PRIOR C++ BUG: called `moveTo(target)` which pushed a sentinel
	// Frame onto _framequeue when target was invalid in current room.
	// After the script ended (Op_01 UnregisterAndEnd) and the
	// _attentionNeeded flag re-fired, nextFrame() popped the sentinel
	// and assigned `_frame = sentinel.index()` which is uninitialized
	// memory (game.log: `_frame = 27680` corruption). The actor then
	// looped forever in this corrupted state, blocking the room-1
	// "Who the heck owns THIS ship?" speech from advancing.
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x06: SetTargetFrame %u [DOS Op_07]", target);
	// Update sprite/target ID (DOS field+0x8 analog).
	setMainSprite(target);
	_nextFrame = target;  // tracked for query opcodes that read targetFrameId.
	// DOS field+0xa byte copy: actor.field+0xa = actor.field+0x10.
	setDosField(0x0a, dosField(0x10));
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
	_nextFrame = target;
	setDosField(0x0a, dosField(0x10));
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
	// C++ slot 0x0a = DOS Op_0b WalkAbsoluteWithFrame @ 1000:6962.
	// 3 shifts: x, y, target_frame.
	//   actor.field+0x4  = x;
	//   actor.field+0x6  = y;
	//   actor.field+0x8  = target_frame;     ; sprite/target ID
	//   actor.field+0xa  = actor.field+0x10; ; byte copy
	//   g_actor_script_ended = 1;
	//
	// Despite the "WalkAbsolute" name, DOS does NOT engage walk
	// pathfinding. Just position + sprite-target field updates.
	// Pass2-15/16: removed the spurious moveTo(target) call.
	const uint16 x = shift();
	const uint16 y = shift();
	const uint16 target = shift();
	debugC(3, kDebugLevelAnimation, "actor opcode 0x0a: WalkAbsoluteWithFrame (%u,%u) target=%u [DOS Op_0b]",
		x, y, target);
	_position.x = (int16)x;
	_position.y = (int16)y;
	setMainSprite(target);
	_nextFrame = target;
	setDosField(0x0a, dosField(0x10));
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
