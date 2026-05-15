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
#include "interspective/sound.h"
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
	_explicitFrameDelay(false),
	_zIndex(-1),
	_mainSpriteId(0xffff),
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
	debugC(5, kDebugLevelAnimation, "ticking animation %s (ticks left: %u)", _debugInfo, _ticksLeft);

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
		_explicitFrameDelay = false;
		status = op(opcode - 1);
	}

	if (status == kFrameDone && !_ticksLeft && !_explicitFrameDelay)
		_ticksLeft = uint8(_interval);

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
	_mainSpriteId = sprite;
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
	_mainSpriteId = 0xffff;
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
	debugC(3, kDebugLevelAnimation, "anim opcode 0x01: unregister and end");
	clearSprites();
	clearMainSprite();
	_base = 0;
	_offset = _baseOffset = 0;

	return kRemove;
}

OPCODE(0x02) {
	uint16 left = shift();
	uint16 top = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x02: move to %d:%d", left, top);

	_position = Common::Point(left, top);

	return kOk;
}

OPCODE(0x03) {
	uint8 interval = uint8(shift());

	debugC(3, kDebugLevelAnimation, "anim opcode 0x03: set interval to %d", interval);

	_interval = interval;

	return kOk;
}

OPCODE(0x04) {
	uint16 offset = shift();
	uint8 interval = uint8(READ_LE_UINT16(_resources->getGlobalWordVariable(offset/2)));

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

	debugC(3, kDebugLevelAnimation, "anim opcode 0x08: move by %d:%d, frame done", left, top);

	_position += Common::Point(left, top);

	return kFrameDone;
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
	_counter = uint8(embeddedByte());
	_loopStart = _offset;

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0d: %u times do", _counter);

	return kOk;
}

OPCODE(0x0e) {
	if (_counter)
		_counter--;

	if (_counter)
		_offset = _loopStart;

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0e: done (%u times left)", _counter);

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
	uint16 res = max ? uint16(Eng.getRandom(uint16(max - 1)) + 1) : 0;
	const bool doJump = max != 0 && res == max;

	debugC(3, kDebugLevelAnimation, "anim opcode 0x13: jump to 0x%x 1 in %d times (%s)", off, max, doJump ? "do now" : "not now");

	if (doJump)
		_offset = off;
	return kOk;
}

OPCODE(0x19) {
	// DOS Op_1a SetWalkFlagsAndEnd @ 1000:6a48:
	//   actor.field+0xa = word arg; actor.field+0x8 = 0xffff;
	//   g_actor_script_ended = 1.
	// For a non-actor Animation, field+0xa maps to `_ticksLeft` and
	// field+0x8 maps to the main sprite id. Mark the delay as explicit
	// so a zero word stays zero instead of falling back to `_interval`.
	uint16 flags = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x19: SetWalkFlagsAndEnd flags=0x%04x [DOS Op_1a]", flags);

	clearMainSprite();
	_ticksLeft = flags;
	_explicitFrameDelay = true;

	return kFrameDone;
}

OPCODE(0x1a) {
	// DOS Op_1b SetField12ClearFlag16 @ 1000:6b4e:
	//   actor.field+0x12 = embedded byte; actor.field+0x16 = 0.
	// C++ uses `_zIndex` as the render-layer analog for field+0x12.
	byte v = embeddedByte();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x1a: SetField12ClearFlag16 = %d [DOS Op_1b]", v);

	_zIndex = int8(v);

	return kOk;
}

OPCODE(0x1b) {
	// DOS Op_1c QueueMoveSlotMode0 @ 1000:6b5d: 3 shifts (a, b, c).
	//   Locates first free actor move-queue slot and writes (a, b, c, 0);
	//   on overflow sets pending error 0x0c.
	// Non-actor Animations have no walk queue. The old fallback created
	// an absolute sprite from these operands, which has no DOS backing.
	(void)shift(); (void)shift(); (void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1b: QueueMoveSlotMode0 "
		"[actor walk queue has no non-actor analog — NO-OP]");

	return kOk;
}

// ============================================================================
// Non-actor Animation fallback handlers for opcode slots that Actor::
// overrides. Animation::opcodeHandler<N> is the fallback when Actor doesn't
// override slot N. For genuine non-actor Animations (cursor sprites added
// via Op_c2, cast-table renderable entries via Op_c3, room-anim cycles, etc.)
// this fallback IS the dispatch target.
//
// DOS architecture: there's a single per-actor opcode table — both Actor
// and non-Actor entries share `DispatchActorOpcode @ 1000:6865`. Non-actor
// entries point at simpler bytecode that uses only a subset of the opcode
// space. When DOS scripts emit an actor-only opcode (PickAnimationSet,
// SetMood, SetCallbackPointer, etc.) on a non-actor entry, DOS writes to
// `[SI + actor.field+offset]` where SI points at the non-actor record —
// this writes to whatever bytes happen to be at that offset (typically
// renderer-internal state that doesn't drive observable behavior).
//
// C++ port: each fallback either has a meaningful non-actor semantic
// (tied to Animation::_position / _mainSprite / _ticksLeft) OR is a
// documented NO-OP because the DOS write-target has no C++ analog on
// non-actor Animations. Byte consumption matches DOS exactly per the
// disassembly references.
// ============================================================================

OPCODE(0x09) {
	// DOS Op_0a WalkAbsolute @ 1000:6991:
	//   AX = ES:[BP+DI+0x2];   ES:[SI+0x4] = AX;   ; actor.field+0x4 = X
	//   BX = ES:[BP+DI+0x4];   ES:[SI+0x6] = BX;   ; actor.field+0x6 = Y
	//   actor.field+0xa = zero-extended actor.field+0x10; end script.
	//   ADD BP, 0x6;            ; total opcode length 6 bytes (opcode + 1
	//                           ;   pad + 2 X word + 2 Y word + dispatcher
	//                           ;   pre-advance covers the first 2 bytes).
	// Actor-side: writes the actor's screen position. Non-actor side:
	// the equivalent semantic is `Animation::_position = (X, Y)` since
	// Animation::paint draws the main sprite at `_position`; kFrameDone
	// supplies the same zero-extended interval delay.
	uint16 x = shift();
	uint16 y = shift();
	_position = Common::Point(int16(x), int16(y));
	debugC(3, kDebugLevelAnimation, "anim opcode 0x09: WalkAbsolute → _position = (%d, %d)",
		int16(x), int16(y));
	return kFrameDone;
}

OPCODE(0x0b) {
	// DOS Op_0c FaceAndWalkWithFrame @ 1000:69cd:
	//   AL = ES:[BP+DI+0x1];   ES:[SI+0x61] = AL;     ; actor.field+0x61 = current frame
	//   CALL SetActorPosition;                          ; actor.x/y from frame
	//   AL = [SI+0x62];   BX = g_render_actor_id;
	//   CALL LookupActorAndStartPath;                   ; engages walk pathfinder
	//   AX = ES:[BP+DI+0x2];   ES:[SI+0x8] = AX;        ; actor.field+0x8 = sprite ID
	//   actor.field+0xa = zero-extended actor.field+0x10; end script.
	//   ADD BP, 0x4;                                     ; total length 4
	// = "set face direction + sprite ID, engage walk script". For non-
	// actor Animations: actor.field+0x61 (current frame) and
	// LookupActorAndStartPath have no analog. The sprite-ID write
	// (field+0x8) maps to setMainSprite for non-actors.
	(void)embeddedByte();
	uint16 spriteId = shift();
	setMainSprite(spriteId);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x0b: FaceAndWalkWithFrame → setMainSprite(%u) "
		"[walk-pathfinding is actor-only; non-actor Animation has no walk script]",
		spriteId);
	return kFrameDone;
}

OPCODE(0x0c) {
	// DOS Op_0d FaceAndWalk @ 1000:6a0e: byte only.
	//   AL = embedded;   ES:[SI+0x61] = AL;          ; current frame
	//   CALL SetActorPosition + LookupActorAndStartPath;
	//   actor.field+0xa = zero-extended actor.field+0x10; end script.
	//   ADD BP, 0x2.
	// = "set face direction + engage walk". For non-actor Animations
	// no walk path exists; the byte is consumed (NO-OP).
	(void)embeddedByte();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x0c: FaceAndWalk "
		"[actor-only walk pathfinder; NO-OP for non-actor Animation]");
	return kFrameDone;
}

OPCODE(0x14) {
	// DOS Op_15 WaitForSpeechSlot @ 1000:6af1:
	//   if (g_render_actor_id == g_main_character_id) CALL CheckScrollDirty;
	//   if NOT carry: CALL FindSpeechSlotById;
	//   if NOT carry (slot found, still speaking): BP = ES:[BP+DI+0x2] (loop back);
	//   else ADD BP, 4 (advance past).
	// = "loop back to this opcode while a speech slot is active for this
	// entity". Speech is per-Actor in C++ (Actor::_speech); non-actor
	// Animations have no speech. The fallback consumes the jump target
	// and falls through (= "speech is silent → don't loop"), matching
	// DOS behavior when no slot is found.
	(void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x14: WaitForSpeechSlot "
		"[non-actor has no speech → fall through, matching DOS no-slot path]");
	return kOk;
}

OPCODE(0x15) {
	// DOS Op_16 PickAnimationSet @ 1000:6c3e: 2-byte opcode (0 extras).
	//   if (cursor_mode != 0x80): actor.field+0x68 = 0;
	//   else: cycle field+0x68 toward target pose from cursor.
	// Pure actor-state mutation (writes field+0x68). Non-actor
	// Animations have no anim-set field. NO-OP — the DOS write target
	// for non-actor entries would be whatever byte is at offset 0x68
	// of the non-actor record, which is renderer-internal state with
	// no C++ analog.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x15: PickAnimationSet "
		"[actor.field+0x68 has no non-actor analog — NO-OP]");
	return kOk;
}

OPCODE(0x16) {
	// DOS Op_17 BranchIfAnimSetEquals @ 1000:6b17:
	//   AL = ES:[SI+0x68];  CMP AL, embedded;
	//   if equal: BP = ES:[BP+DI+0x2] (jump);
	//   else ADD BP, 4 (advance).
	// For non-actor: field+0x68 doesn't exist (effectively 0). If the
	// embedded byte is 0, the jump fires; otherwise fall through.
	// This matches DOS reading-from-zero behavior for non-actor
	// records (assuming the byte at offset 0x68 of non-actor entries
	// is zero-initialized, which is typical).
	const byte val = embeddedByte();
	const uint16 jumpTarget = shift();
	if (val == 0) {
		_offset = jumpTarget;
		debugC(3, kDebugLevelAnimation, "anim opcode 0x16: BranchIfAnimSetEquals val=0 → jump 0x%04x",
			jumpTarget);
	} else {
		debugC(3, kDebugLevelAnimation,
			"anim opcode 0x16: BranchIfAnimSetEquals val=%d (non-actor field=0 implicit, no jump)", val);
	}
	return kOk;
}

OPCODE(0x17) {
	// DOS Op_18 BranchIfMoodEquals @ 1000:6b29:
	//   if (ES:[SI+0x63] == embedded): BP = ES:[BP+DI+0x2] (jump);
	//   else ADD BP, 4.
	// Same shape as 0x16 with field+0x63 (mood). Non-actor: implicit 0.
	const byte val = embeddedByte();
	const uint16 jumpTarget = shift();
	if (val == 0) {
		_offset = jumpTarget;
		debugC(3, kDebugLevelAnimation, "anim opcode 0x17: BranchIfMoodEquals val=0 → jump 0x%04x",
			jumpTarget);
	} else {
		debugC(3, kDebugLevelAnimation,
			"anim opcode 0x17: BranchIfMoodEquals val=%d (non-actor mood=0 implicit, no jump)", val);
	}
	return kOk;
}

OPCODE(0x18) {
	// DOS Op_19 SetField6d @ 1000:6b43: 1 shift.
	//   AX = ES:[BP+DI+0x2];  ES:[SI+0x6d] = AX (actually written as a word).
	// Pure actor-state write (field+0x6d). NO-OP for non-actor.
	(void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x18: SetField6d "
		"[actor.field+0x6d has no non-actor analog — NO-OP]");
	return kOk;
}

OPCODE(0x1c) {
	// DOS Op_1d QueueMoveSlotMode1 @ 1000:6b95: 3 shifts (a, b, c).
	//   Locates first free slot in actor.field+0x19's 8-entry move
	//   queue (32 bytes total: 8 entries × 4 ints), writes (a, b, c, 1).
	//   On overflow: pending error 0xc.
	// Pure actor walk-queue mutation. Non-actor Animations have no
	// walk queue. NO-OP.
	(void)shift(); (void)shift(); (void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1c: QueueMoveSlotMode1 "
		"[actor walk queue has no non-actor analog — NO-OP]");
	return kOk;
}

OPCODE(0x1d) {
	// DOS Op_1e ClearFlag14 @ 1000:6bcd: ES:[SI+0x14] = 0.
	// Per-actor flag byte. NO-OP for non-actor.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1d: ClearFlag14 "
		"[actor flag byte — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x1e) {
	// DOS Op_1f ClearFlag15 @ 1000:6bd5: ES:[SI+0x15] = 0.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1e: ClearFlag15 "
		"[actor flag byte — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x1f) {
	// DOS Op_20 SetFlag15 @ 1000:6bdd: ES:[SI+0x15] = 1.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1f: SetFlag15 "
		"[actor flag byte — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x20) {
	// DOS Op_21 SetCallbackPointer @ 1000:6be5:
	//   ES:[SI+0x5f] = BP+DI+0x2 (callback offset = next opcode addr);
	//   ES:[SI+0x5d] = ES (callback segment);
	//   ADD BP, 0xc (skip past 12-byte opcode = opcode + 1 pad + 10 inline body).
	// Captures script's PC into actor's callback-PC fields. Non-actor
	// has no callback fields. We skip the inline body via 5 shifts to
	// keep dispatcher PC aligned.
	(void)shift(); (void)shift(); (void)shift(); (void)shift(); (void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x20: SetCallbackPointer "
		"[12-byte opcode body skipped; actor callback fields have no non-actor analog]");
	return kOk;
}

OPCODE(0x21) {
	// DOS Op_22 SetCallbackRelative @ 1000:6bf4: 1 shift (signed offset).
	//   if (offset != 0): callback_off = DI + offset (script-relative);
	//   else callback_off = 0 (clear);
	//   callback_seg = ES.
	// Same actor-callback fields as 0x20. NO-OP for non-actor.
	(void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x21: SetCallbackRelative "
		"[actor callback — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x22) {
	// DOS Op_23 ClearCallback @ 1000:6c0a: ES:[SI+0x5d] = 0xffff.
	// Same actor-callback field. NO-OP for non-actor.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x22: ClearCallback "
		"[actor callback — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x23) {
	// DOS Op_24 SetMood @ 1000:6c13: ES:[SI+0x63] = embedded byte.
	// Pure actor-state. NO-OP for non-actor.
	(void)embeddedByte();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x23: SetMood "
		"[actor mood — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x24) {
	// DOS Op_25 SetField65 @ 1000:6c1e: ES:[SI+0x65] = embedded byte.
	// Pure actor-state. NO-OP for non-actor.
	(void)embeddedByte();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x24: SetField65 "
		"[actor field+0x65 — NO-OP for non-actor]");
	return kOk;
}

OPCODE(0x25) {
	// DOS Op_26 PlaySfx @ 1000:6c29: 1 shift (sfx index).
	//   AX = ES:[BP+DI+0x2];   CALL DispatchSfxRangeCheck(AX).
	// Plays an SFX. NOT actor-specific in DOS — the sfx index is
	// world-relative. Routes through Sound::rangeCheck (same DOS
	// SFX dispatcher used by Op_f2 and actor 0x25).
	const uint16 sfxId = shift();
	if (Sound *snd = Log.engine()->sound())
		snd->rangeCheck(sfxId);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x25: PlaySfx %u", sfxId);
	return kOk;
}

}
