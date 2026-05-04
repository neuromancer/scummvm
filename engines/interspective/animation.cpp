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

#include "interspective/animation.h"

#include "interspective/debug.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/util.h"

namespace Interspective {
//
ENAME(Animation::Status_, Animation::kOk, "ok");
ENAME(Animation::Status_, Animation::kRemove, "remove");
ENAME(Animation::Status_, Animation::kFrameDone, "frame done");

class Animation::Sprite {
public:
	Sprite(Interspective::Sprite *s) : _sprite(s), _isRelative(true) {}

	void setPosition(Common::Point p) {
		_position = p;
	}

	void setAbsolute() {
		_isRelative = false;
	}

	bool isAbsolute() const {
		return !_isRelative;
	}

	void paint(Graphics *g) const;

	const Interspective::Sprite *sprite() const {
		return _sprite.get();
	}
private:
	Common::SharedPtr<Interspective::Sprite> _sprite;
	Common::Point _position;
	bool _isRelative;
};

template <int opcode>
Animation::Status Animation::opcodeHandler(){
	error("unhandled animation opcode %d [=0x%02x]", opcode, opcode);
}

template<int N>
void Animation::init_opcodes() {
	_handlers[N] = &Interspective::Animation::opcodeHandler<N>;
	init_opcodes<N-1>();
}

template<>
void Animation::init_opcodes<-1>() {}

Animation::Animation(const CodePointer &code, Common::Point position) :
	_position(position),
	_offset(0),
	_interval(1),
	_ticksLeft(0),
	_counter(0),
	_debugInvalid(false),
	_opRingIdx(0) {
	_base = code.code();
	_baseOffset = code.offset();
	_resources = code.interpreter()->resources();
	for (int i = 0; i < 16; i++) {
		_opRing[i].pc = 0xffff;
		_opRing[i].op = 0;
	}
	init_opcodes<37>();
	snprintf(_debugInfo, 50, "animation at %s", +code);
	code.interpreter()->rememberAnimation(this);
}

Animation::~Animation() {
	for (Common::List<Sprite *>::iterator it = _sprites.begin(); it != _sprites.end(); ++it)
		delete *it;
	Log.removeAnimation(this);
}

Animation::Status Animation::tick() {
	debugC(5, kDebugLevelAnimation, "ticking animation %s (ticks left: %d)", _debugInfo, _ticksLeft);

	if (_ticksLeft) {
		_ticksLeft--;
		return kOk;
	}

	clearSprites();

	Status status = kOk;
	while (status == kOk && _base) {
		int8 opcode = -*(_base + _offset);
		if (opcode < 0 || opcode >= 0x27) {
			// Dump 16 bytes around the bad PC so the misalignment can be
			// traced back to whichever upstream handler over/under-consumed.
			// _baseOffset is the script's start in the source file;
			// _offset is the relative PC from there. Bad byte is at the
			// absolute file address _baseOffset + _offset.
			char ctx[80] = {0};
			int written = 0;
			for (int i = -4; i <= 11 && written < 76; i++) {
				const int rel = (int)_offset + i;
				if (rel < 0)
					continue;
				written += snprintf(ctx + written, sizeof(ctx) - written,
					"%s%02x", i == 0 ? "[" : (i == 1 ? "]" : " "),
					*(_base + rel));
			}

			// Dump the ring of last 16 dispatched opcodes — what was
			// executed leading up to this misalignment.
			char ring[256] = {0};
			int rwritten = 0;
			for (int i = 0; i < 16 && rwritten < 250; i++) {
				const uint8 idx = (_opRingIdx + i) & 15;
				if (_opRing[idx].pc == 0xffff)
					continue;
				rwritten += snprintf(ring + rwritten, sizeof(ring) - rwritten,
					"%s@%04x:%02x",
					rwritten ? " " : "",
					(uint)_opRing[idx].pc,
					(uint)_opRing[idx].op);
			}

			error("invalid animation opcode 0x%02x while handling %s "
				"(absolute file offset 0x%04x = base 0x%04x + pc 0x%04x;\n"
				"  context: %s\n"
				"  last 16 dispatched: %s)",
				*(_base + _offset), _debugInfo,
				(uint)(_baseOffset + _offset),
				(uint)_baseOffset, (uint)_offset, ctx, ring);
		}

		// Push to forensic ring before advancing — captures the byte that
		// was actually dispatched, at the offset where it was found.
		_opRing[_opRingIdx].pc = _offset;
		_opRing[_opRingIdx].op = *(_base + _offset);
		_opRingIdx = (_opRingIdx + 1) & 15;

		_offset += 2;

		_debugInvalid = false;
		status = op(opcode - 1);
	}

	if (status == kFrameDone && !_ticksLeft)
		_ticksLeft = _interval;

	if (status == kRemove)
		return status;

	return kOk;
}

void Animation::handleTrigger() {
	unless (_frameTrigger.isEmpty()) {
//		Graf.updateScreen();
		Log.runLater(_frameTrigger);
	}
	_frameTrigger.reset();
}

void Animation::runOnNextFrame(const CodePointer &cp) {
	_frameTrigger = cp;
}


void Animation::setMainSprite(uint16 sprite) {
	// 0xffff is the DOS "no sprite" sentinel (initial value of
	// actor.field+0x8). Loading it would index past the end of the
	// spritemap and ASan-trip in SpriteInfo's spritemap += index *
	// kSpriteMapSize (caught iter-29).
	if (sprite == 0xffff) {
		clearMainSprite();
		return;
	}
	_mainSprite = Common::SharedPtr<Interspective::Sprite>(_resources->loadSprite(sprite));
}

void Animation::clearMainSprite() {
	_mainSprite.reset();
}

void Animation::clearSprites() {
	debugC(5, kDebugLevelAnimation, "clearing sprite list");
	for (Common::List<Sprite *>::iterator it = _sprites.begin(); it != _sprites.end(); ++it)
		delete (*it);
	_sprites.clear();
}

void Animation::paint(Graphics *g) {
	if (!_mainSprite.get())
		return;
	debugC(5, kDebugLevelAnimation | kDebugLevelGraphics, "painting sprites for animation %s", _debugInfo);

	g->paint(_mainSprite.get(), _position);

	for (Common::List<Sprite *>::iterator it = _sprites.begin(); it != _sprites.end(); ++it)
		(*it)->paint(g);
}

void Animation::Sprite::paint(Graphics *g) const {
	assert(isAbsolute());
	g->paint(sprite(), _position);
}

uint16 Animation::shift() {
	uint16 value = READ_LE_UINT16((_base + _offset));
	_offset += 2;
	return value;
}

int8 Animation::shiftByte() {
	byte value = *(_base + _offset);
	_offset += 1;
	return value;
}

int8 Animation::embeddedByte() const {
	return reinterpret_cast<int8 *>((_base + _offset))[-1];
}

Animation::Status Animation::op(byte opcode) {
	return (this->*_handlers[opcode])();
}

#define OPCODE(n) template<> Animation::Status Animation::opcodeHandler<n>()

OPCODE(0x00) {
	debugC(3, kDebugLevelAnimation, "anim opcode 0x00: remove animation");
	handleTrigger();

	return kRemove;
}

OPCODE(0x01) {
	debugC(3, kDebugLevelAnimation, "anim opcode 0x01: hide");
	_base = 0;
	_offset = _baseOffset = 0;

	return kFrameDone;
}

OPCODE(0x02) {
	uint16 left = shift();
	uint16 top = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x02: move to %d:%d", left, top);

	_position = Common::Point(left, top);

	return kOk;
}

OPCODE(0x03) {
	uint16 interval = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x03: set interval to %d", interval);

	_interval = interval;

	return kOk;
}

OPCODE(0x04) {
	uint16 offset = shift();
	uint16 interval = READ_LE_UINT16(_resources->getGlobalWordVariable(offset/2));

	debugC(3, kDebugLevelAnimation, "anim opcode 0x04: set interval to %d (from var %d)", interval, offset / 2);

	_interval = interval;

	return kOk;
}

OPCODE(0x05) {
	byte x = shiftByte();
	int8 xoff = *reinterpret_cast<int8 *>(&x);
	byte y = shiftByte();
	int8 yoff = *reinterpret_cast<int8 *>(&y);
	uint16 sprite = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x05: move by %d:%d, set main sprite to %d, frame done", xoff, yoff, sprite);

	_position.x += xoff;
	_position.y += yoff;
	setMainSprite(sprite);

	return kFrameDone;
}

OPCODE(0x06) {
	uint16 sprite = shift();

	setMainSprite(sprite);

	debugC(3, kDebugLevelAnimation, "anim opcode 0x06: set main sprite to %d, frame done", sprite);

	return kFrameDone;
}

OPCODE(0x07) {
	uint16 var = shift();
	uint16 sprite = READ_LE_UINT16(_resources->getGlobalWordVariable(var/2));

	setMainSprite(sprite);

	debugC(3, kDebugLevelAnimation, "anim opcode 0x07: set main sprite to %d (from global word 0x%04x), frame done", sprite, var/2);

	return kFrameDone;
}

OPCODE(0x08) {
	int8 left = shiftByte();
	int8 top = shiftByte();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x08: move by %d:%d", left, top);

	_position += Common::Point(left, top);

	return kOk;
}

OPCODE(0x0a) {
	uint16 left, top, sprite;
	left = shift();
	top = shift();
	sprite = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0a: run sprite %d at %d:%d", sprite, left, top);

	_position = Common::Point(left, top);
	setMainSprite(sprite);

	return kFrameDone;
}

OPCODE(0x0d) {
	_counter = embeddedByte();
	_loopStart = _offset;

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0d: %d times do", _counter);

	return kOk;
}

OPCODE(0x0e) {
	if (_counter)
		_counter--;

	if (_counter)
		_offset = _loopStart;

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0e: done (%d times left)", _counter);

	return kOk;
}

OPCODE(0x0f) {
	uint16 offset = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0f: jump to 0x%04x", offset);

	_offset = offset;

	return kOk;
}

OPCODE(0x10) {
	uint16 var = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x10: set bvar %d", var);
	*_resources->getGlobalByteVariable(var) = 1;

	return kOk;
}

OPCODE(0x11) {
	uint16 var = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x11: reset flag %d", var);
	*_resources->getGlobalByteVariable(var) = 0;

	return kOk;
}

OPCODE(0x12) {
	uint16 var = shift();
	uint16 off = shift();
	byte ok = *_resources->getGlobalByteVariable(var);

	debugC(3, kDebugLevelAnimation, "anim opcode 0x12: jump to 0x%x if byte var %d (%s)", off, var, ok ? "yes" : "not");

	if (ok)
		_offset = off;
	return kOk;
}

OPCODE(0x13) {
	uint16 max = shift();
	uint16 off = shift();
	uint16 res = Eng.getRandom(max);

	debugC(3, kDebugLevelAnimation, "anim opcode 0x13: jump to 0x%x 1 in %d times (%s)", off, max, res == max ? "do now" : "not now");

	if (res == max)
		_offset = off;
	return kOk;
}

OPCODE(0x19) {
	uint16 delay = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x19: hide for %d frames", delay);

	clearMainSprite();
	_ticksLeft = delay;

	return kFrameDone;
}

OPCODE(0x1a) {
	int8 index = embeddedByte();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x1a: set z index to %d", index);

	_zIndex = index;

	return kOk;
}

OPCODE(0x1b) {
	uint16 left = shift();
	uint16 top = shift();
	uint16 sprite = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x1b: add absolute sprite %d: %d:%d", sprite, left, top);

	Sprite *s = new Sprite(_resources->loadSprite(sprite));
	s->setPosition(Common::Point(left, top));
	s->setAbsolute();
	_sprites.push_back(s);

	return kOk;
}

// ============================================================================
// iter-22: safe-stubs for Animation opcode slots that the original C++ port
// left unimplemented. Without these, a non-actor animation (cursor anim
// added via Op_c2, cast entry via Op_c3, etc.) emitting any of these
// opcode bytes triggers `error("unhandled animation opcode")` which
// terminates the engine.
//
// Bytes consumed match the DOS-disassembly-verified table from iter-13
// (off-by-1 mapping: Animation slot N = DOS Op_(N+1)). Semantics simplified
// to no-ops for non-actor contexts since these handlers exist primarily as
// the fall-through target when an actor's bytecode emits one of these
// opcodes WITHOUT an Actor:: override — Actor opcodes 0x09, 0x0b, 0x0c,
// 0x14..0x18, 0x1c..0x25 are all actor-overridden, so this fall-through is
// only relevant for genuine non-actor anims.
// ============================================================================

OPCODE(0x09) {
	// DOS Op_0a WalkAbsolute: 2 shifts (x, y).
	uint16 x = shift(); uint16 y = shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x09: WalkAbsolute (%u,%u) STUB", x, y);
	return kOk;
}

OPCODE(0x0b) {
	// DOS Op_0c FaceAndWalkWithFrame: byte + 1 shift.
	(void)embeddedByte();
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x0b: FaceAndWalkWithFrame STUB");
	return kOk;
}

OPCODE(0x0c) {
	// DOS Op_0d FaceAndWalk: byte only.
	(void)embeddedByte();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x0c: FaceAndWalk STUB");
	return kOk;
}

OPCODE(0x14) {
	// DOS Op_15 WaitForSpeechSlot: 1 shift (jump target).
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x14: WaitForSpeechSlot STUB");
	return kOk;
}

OPCODE(0x15) {
	// DOS Op_16 PickAnimationSet: 0 extras (2-byte opcode).
	debugC(4, kDebugLevelAnimation, "anim opcode 0x15: PickAnimationSet STUB");
	return kOk;
}

OPCODE(0x16) {
	// DOS Op_17 BranchIfAnimSetEquals: byte + 1 shift.
	(void)embeddedByte();
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x16: BranchIfAnimSetEquals STUB");
	return kOk;
}

OPCODE(0x17) {
	// DOS Op_18 BranchIfMoodEquals: byte + 1 shift.
	(void)embeddedByte();
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x17: BranchIfMoodEquals STUB");
	return kOk;
}

OPCODE(0x18) {
	// DOS Op_19 SetField6d: 1 shift.
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x18: SetField6d STUB");
	return kOk;
}

OPCODE(0x1c) {
	// DOS Op_1d QueueMoveSlotMode1: 3 shifts.
	(void)shift(); (void)shift(); (void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x1c: QueueMoveSlotMode1 STUB");
	return kOk;
}

OPCODE(0x1d) {
	// DOS Op_1e ClearFlag14: 0 extras.
	debugC(4, kDebugLevelAnimation, "anim opcode 0x1d: ClearFlag14 STUB");
	return kOk;
}

OPCODE(0x1e) {
	// DOS Op_1f ClearFlag15: 0 extras.
	debugC(4, kDebugLevelAnimation, "anim opcode 0x1e: ClearFlag15 STUB");
	return kOk;
}

OPCODE(0x1f) {
	// DOS Op_20 SetFlag15: 0 extras.
	debugC(4, kDebugLevelAnimation, "anim opcode 0x1f: SetFlag15 STUB");
	return kOk;
}

OPCODE(0x20) {
	// DOS Op_21 SetCallbackPointer: 5 shifts (12-byte opcode with 10-byte
	// inline body). Consume all 5 shifts to skip past the body.
	(void)shift(); (void)shift(); (void)shift(); (void)shift(); (void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x20: SetCallbackPointer STUB (skipped 10-byte body)");
	return kOk;
}

OPCODE(0x21) {
	// DOS Op_22 SetCallbackRelative: 1 shift.
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x21: SetCallbackRelative STUB");
	return kOk;
}

OPCODE(0x22) {
	// DOS Op_23 ClearCallback: 0 extras.
	debugC(4, kDebugLevelAnimation, "anim opcode 0x22: ClearCallback STUB");
	return kOk;
}

OPCODE(0x23) {
	// DOS Op_24 SetMood: byte only.
	(void)embeddedByte();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x23: SetMood STUB");
	return kOk;
}

OPCODE(0x24) {
	// DOS Op_25 SetField65: byte only.
	(void)embeddedByte();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x24: SetField65 STUB");
	return kOk;
}

OPCODE(0x25) {
	// DOS Op_26 PlaySfx: 1 shift (sfx index).
	(void)shift();
	debugC(4, kDebugLevelAnimation, "anim opcode 0x25: PlaySfx STUB");
	return kOk;
}

}
