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

#include "interspective/animation.h"

#include "interspective/debug.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/sound.h"
#include "interspective/util.h"

namespace Interspective {
//
static uint16 animationCodeSegmentTag(Interpreter *interpreter) {
	if (interpreter && interpreter == Log.blockInterpreter())
		return uint16(0x4000 + (Log.currentBlock() & 0x3fff));
	return 0x1cb5;
}

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

template<int opcode>
Animation::Status Animation::opcodeHandler() {
	error("unhandled animation opcode %d [=0x%02x]", opcode, opcode);
}

template<int N>
void Animation::init_opcodes() {
	_handlers[N] = &Interspective::Animation::opcodeHandler<N>;
	init_opcodes<N - 1>();
}

template<>
void Animation::init_opcodes<-1>() {}

Animation::Animation(const CodePointer &code, Common::Point position) : _position(position),
																		_offset(0),
																		_interval(1),
																		_ticksLeft(0),
																		_explicitFrameDelay(false),
																		_zIndex(-1),
																		_scriptInterpreter(code.interpreter()),
																		_mainSpriteId(0xffff),
																		_counter(0),
																		_castTableRunner(false),
																		_debugInvalid(false),
																		_opRingIdx(0) {
	_base = _scriptInterpreter ? _scriptInterpreter->rawCodeChecked(code.offset(), 0) : 0;
	_baseOffset = _base ? code.offset() : 0;
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
		if (_castTableRunner)
			decrementAnimationTicksLeft();
		else
			_ticksLeft--;
		return kOk;
	}

	clearAnimationMoveSlots();
	clearSprites();

	Status status = kOk;
	bool ranScript = false;
	while (status == kOk && _base) {
		byte opByte = 0;
		currentScriptByte(opByte);
		int8 opcode = -dosSignedByte(opByte);
		if (opcode <= 0 || opcode >= 0x27) {
			// Dump 16 bytes around the bad PC so the misalignment can be
			// traced back to whichever upstream handler over/under-consumed.
			// _baseOffset is the script's start in the source file;
			// _offset is the relative PC from there. Bad byte is at the
			// absolute file address _baseOffset + _offset.
			char ctx[80] = {0};
			int written = 0;
			for (int i = -4; i <= 11 && written < 76; i++) {
				byte contextByte = 0;
				if (!readScriptByte(i, contextByte, false))
					continue;
				written += snprintf(ctx + written, sizeof(ctx) - written,
									"%s%02x", i == 0 ? "[" : (i == 1 ? "]" : " "),
									contextByte);
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
					  opByte, _debugInfo,
					  (uint)(_baseOffset + _offset),
					  (uint)_baseOffset, (uint)_offset, ctx, ring);
		}

		// Push to forensic ring before advancing — captures the byte that
		// was actually dispatched, at the offset where it was found.
		_opRing[_opRingIdx].pc = _offset;
		_opRing[_opRingIdx].op = opByte;
		_opRingIdx = (_opRingIdx + 1) & 15;

		_offset += 2;

		_debugInvalid = false;
		_explicitFrameDelay = false;
		ranScript = true;
		status = op(opcode - 1);
	}

	if (status == kFrameDone && !_ticksLeft && !_explicitFrameDelay) {
		_ticksLeft = uint8(_interval);
		if (_castTableRunner)
			setAnimationRecordTicksLeft(_ticksLeft);
	}

	if (ranScript && _castTableRunner) {
		setAnimationRecordScriptPc(_offset);
		decrementAnimationTicksLeft();
	}

	if (status == kRemove)
		return status;

	return kOk;
}

bool Animation::castWaitComplete() const {
	// DOS FindCastByActorId @ 1000:67f5 resumes Op_c6 when no matching
	// active cast exists, or when cast field +0x0a is zero and the next
	// script byte at field +0x0c is the 0xff sentinel.
	if (!_castTableRunner || !_base)
		return true;
	if (_ticksLeft != 0)
		return false;
	byte op = 0;
	return currentScriptByte(op) && op == 0xff;
}

void Animation::handleTrigger() {
	unless(_frameTrigger.isEmpty()) {
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
	setAnimationRecordMainSprite(sprite);
	// 0xffff is the DOS "no sprite" sentinel (initial value of
	// actor.field+0x8). Loading it would index past the end of the
	// spritemap and ASan-trip in SpriteInfo's spritemap += index *
	// kSpriteMapSize.
	if (sprite == 0xffff) {
		clearMainSprite();
		return;
	}
	_mainSprite = Common::SharedPtr<Interspective::Sprite>(_resources->loadSprite(sprite));
}

void Animation::clearMainSprite() {
	_mainSpriteId = 0xffff;
	setAnimationRecordMainSprite(0xffff);
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

	g->paint(_mainSprite.get(), _position, Graphics::kPaintCameraRelative);

	for (Common::List<Sprite *>::iterator it = _sprites.begin(); it != _sprites.end(); ++it)
		(*it)->paint(g);

	paintAnimationMoveSlots(g);
}

void Animation::Sprite::paint(Graphics *g) const {
	assert(isAbsolute());
	g->paint(sprite(), _position);
}

void Animation::attachScript(Interpreter *interpreter, uint16 baseOffset, uint16 pc) {
	_scriptInterpreter = interpreter;
	_base = interpreter ? interpreter->rawCodeChecked(baseOffset, 0) : 0;
	if (_base) {
		_baseOffset = baseOffset;
		_offset = pc;
	} else {
		_baseOffset = 0;
		_offset = 0;
		_scriptInterpreter = 0;
	}
}

void Animation::clearScript() {
	_base = 0;
	_baseOffset = 0;
	_offset = 0;
	_scriptInterpreter = 0;
}

void Animation::rebaseScript(uint16 baseOffset) {
	if (!_scriptInterpreter) {
		clearScript();
		return;
	}
	attachScript(_scriptInterpreter, baseOffset, 0);
}

bool Animation::readScriptByte(int32 relative, byte &value, bool warn) const {
	value = 0;
	if (!_scriptInterpreter || !_base) {
		if (warn)
			warning("Interspective: script byte read through inactive animation %s", _debugInfo);
		return false;
	}

	const int32 absolute = int32(_baseOffset) + int32(_offset) + relative;
	if (absolute < 0 || absolute > 0xffff) {
		if (warn) {
			warning("Interspective: script byte read at invalid offset %d "
					"(base 0x%04x pc 0x%04x rel %+d) in %s",
					absolute, _baseOffset, _offset, int(relative), _debugInfo);
		}
		return false;
	}

	return _scriptInterpreter->readCodeByte(uint16(absolute), value);
}

bool Animation::readScriptWord(int32 relative, uint16 &value, bool warn) const {
	value = 0;
	if (!_scriptInterpreter || !_base) {
		if (warn)
			warning("Interspective: script word read through inactive animation %s", _debugInfo);
		return false;
	}

	const int32 absolute = int32(_baseOffset) + int32(_offset) + relative;
	if (absolute < 0 || absolute > 0xffff) {
		if (warn) {
			warning("Interspective: script word read at invalid offset %d "
					"(base 0x%04x pc 0x%04x rel %+d) in %s",
					absolute, _baseOffset, _offset, int(relative), _debugInfo);
		}
		return false;
	}

	return _scriptInterpreter->readCodeWord(uint16(absolute), value);
}

bool Animation::currentScriptByte(byte &value) const {
	return readScriptByte(0, value);
}

uint16 Animation::shift() {
	uint16 value = 0;
	if (!readScriptWord(0, value)) {
		clearScript();
		return 0;
	}
	_offset += 2;
	return value;
}

int8 Animation::shiftByte() {
	byte value = 0;
	if (!readScriptByte(0, value)) {
		clearScript();
		return 0;
	}
	_offset += 1;
	return dosSignedByte(value);
}

int8 Animation::embeddedByte() const {
	byte value = 0;
	readScriptByte(-1, value);
	return dosSignedByte(value);
}

uint8 Animation::animationField(uint8 off) const {
	Common::HashMap<uint8, uint8>::const_iterator it = _animationFields.find(off);
	return it == _animationFields.end() ? 0 : it->_value;
}

uint16 Animation::animationFieldWord(uint8 off) const {
	return uint16(animationField(off)) | (uint16(animationField(uint8(off + 1))) << 8);
}

void Animation::setAnimationField(uint8 off, uint8 v) {
	if (v == 0)
		_animationFields.erase(off);
	else
		_animationFields[off] = v;
}

void Animation::setAnimationFieldWord(uint8 off, uint16 v) {
	setAnimationField(off, uint8(v & 0xff));
	setAnimationField(uint8(off + 1), uint8(v >> 8));
}

static bool animationZoneContainsPoint(uint16 left, uint16 top, uint16 right, uint16 bottom, const Common::Point &p) {
	return p.x >= int16(left) && p.x <= int16(right) &&
		   p.y >= int16(top) && p.y <= int16(bottom);
}

void Animation::setPositionFromFrame(uint8 frame) {
	setAnimationRecordFrame(frame);
	if (!Log.room())
		return;

	const Actor::Frame f = Log.room()->getFrame(frame);
	_position = f.position();
	setAnimationRecordPosition(_position);

	uint8 bl = 0;
	const Common::Array<Logic::CollisionZone> &collisionZones = Log.collisionZones();
	for (uint i = 0; i < collisionZones.size(); ++i) {
		const Logic::CollisionZone &z = collisionZones[i];
		if (!animationZoneContainsPoint(z.a, z.b, z.c, z.d, _position))
			continue;
		bl = uint8(uint16(z.slot) & 0xff);
		break;
	}

	uint8 bh = 0;
	const Common::Array<Logic::ZoneB> &zonesB = Log.zonesB();
	for (uint i = 0; i < zonesB.size(); ++i) {
		const Logic::ZoneB &z = zonesB[i];
		if (!animationZoneContainsPoint(z.a, z.b, z.c, z.d, _position))
			continue;
		bh = uint8(z.var & 0xff);
		break;
	}

	setAnimationRecordDrawLayer(bl);
	setAnimationRecordSecondaryZone(bh);
	_zIndex = dosSignedByte(bl);
}

void Animation::copyAnimationIntervalToTicks() {
	const uint16 ticks = uint8(_interval);
	_ticksLeft = ticks;
	_explicitFrameDelay = true;
	setAnimationRecordTicksLeft(ticks);
}

void Animation::clearAnimationMoveSlots() {
	_animationMoveSlots.clear();
	for (uint i = 0; i < 8; ++i) {
		clearAnimationRecordMoveSlot(i);
	}
}

bool Animation::queueAnimationMoveSlot(uint16 arg1, uint16 arg2, uint16 arg3, uint8 mode) {
	if (_animationMoveSlots.size() >= 8)
		return false;

	const uint slot = _animationMoveSlots.size();
	_animationMoveSlots.push_back(AnimationMoveSlot(arg3, arg1, arg2, mode));
	setAnimationRecordMoveSlot(slot, arg3, arg1, arg2, mode);
	return true;
}

void Animation::decrementAnimationTicksLeft() {
	// DOS UpdateActorTimers @ 1000:673e decrements cast field +0x0a
	// after the optional RunActorScript call, even when the script just
	// wrote a fresh delay through the common tail at 1000:6953.
	_ticksLeft = uint16(_ticksLeft - 1);
	setAnimationRecordTicksLeft(_ticksLeft);
}

void Animation::paintMoveSlot(Graphics *g, uint16 spriteId, uint16 x, uint16 y, uint8 mode, const Common::Point &base) const {
	if (spriteId == 0xffff)
		return;

	Common::Point pos;
	if (mode == 0)
		pos = Common::Point(int16(x), int16(y));
	else
		pos = Common::Point(base.x + int16(x), base.y + int16(y));

	Common::SharedPtr<Interspective::Sprite> sprite(_resources->loadSprite(spriteId));
	g->paint(sprite.get(), pos, Graphics::kPaintCameraRelative);
}

void Animation::paintAnimationMoveSlots(Graphics *g) const {
	for (uint i = 0; i < _animationMoveSlots.size(); ++i) {
		const AnimationMoveSlot &slot = _animationMoveSlots[i];
		paintMoveSlot(g, slot.a, slot.b, slot.c, slot.mode, _position);
	}
}

Animation::Status Animation::op(byte opcode) {
	return (this->*_handlers[opcode])();
}

#define OPCODE(n) template<> \
Animation::Status Animation::opcodeHandler<n>()

OPCODE(0x00) {
	// DOS ActorOp_01_ScriptEnd @ 1000:68d3 clears record words +0/+2
	// and sets g_actor_script_ended. For cast entries those words are
	// wActive/wId, so DrawCastEntries @ 1000:6778 stops seeing the slot.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x00: ScriptEnd [DOS Op_01]");

	if (_castTableRunner)
		Log.castTableDeactivateAnimation(this);
	clearScript();
	return _castTableRunner ? kRemove : kOk;
}

OPCODE(0x01) {
	// DOS ActorOp_02_UnregisterAndEnd @ 1000:68e3 calls
	// UnregisterActor, which clears the script PC and active-table id but
	// leaves sprite/render fields intact. kRemove models the active-table
	// removal; do not clear `_mainSprite`/`_sprites` here.
	debugC(3, kDebugLevelAnimation, "anim opcode 0x01: UnregisterAndEnd (remove active entry) [DOS Op_02]");
	if (_castTableRunner)
		Log.castTableDeactivateAnimation(this);
	clearScript();

	return kRemove;
}

OPCODE(0x02) {
	uint16 left = shift();
	uint16 top = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x02: move to %d:%d", left, top);

	_position = Common::Point(left, top);
	setAnimationRecordPosition(left, top);

	return kOk;
}

OPCODE(0x03) {
	uint8 interval = uint8(shift());

	debugC(3, kDebugLevelAnimation, "anim opcode 0x03: set interval to %d", interval);

	_interval = interval;
	setAnimationRecordInterval(interval);

	return kOk;
}

OPCODE(0x04) {
	uint16 offset = shift();
	uint8 interval = uint8(_resources->globalWordAtByteOffset(offset));

	debugC(3, kDebugLevelAnimation, "anim opcode 0x04: set interval to %d (from var %d)", interval, offset / 2);

	_interval = interval;
	setAnimationRecordInterval(interval);

	return kOk;
}

OPCODE(0x05) {
	const int8 xoff = shiftByte();
	const int8 yoff = shiftByte();
	uint16 sprite = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x05: move by %d:%d, set main sprite to %d, frame done", xoff, yoff, sprite);

	_position.x += xoff;
	_position.y += yoff;
	setAnimationRecordPosition(_position);
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
	uint16 sprite = _resources->globalWordAtByteOffset(var);

	setMainSprite(sprite);

	debugC(3, kDebugLevelAnimation, "anim opcode 0x07: set main sprite to %d (from global word 0x%04x), frame done", sprite, var / 2);

	return kFrameDone;
}

OPCODE(0x08) {
	int8 left = shiftByte();
	int8 top = shiftByte();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x08: move by %d:%d, frame done", left, top);

	_position += Common::Point(left, top);
	setAnimationRecordPosition(_position);

	return kFrameDone;
}

OPCODE(0x0a) {
	uint16 left;
	uint16 top;
	uint16 sprite;
	left = shift();
	top = shift();
	sprite = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0a: run sprite %d at %d:%d", sprite, left, top);

	_position = Common::Point(left, top);
	setAnimationRecordPosition(left, top);
	setMainSprite(sprite);

	return kFrameDone;
}

OPCODE(0x0d) {
	const byte v = uint8(embeddedByte());
	setAnimationRecordSkipTimerCount(v);
	setAnimationRecordSkipTimerResumePc(_offset);

	debugC(3, kDebugLevelAnimation, "anim opcode 0x0d: SetTimerAndSkip = %u, resume 0x%04x [DOS Op_0e]",
		   v, _offset);

	return kOk;
}

OPCODE(0x0e) {
	const uint8 cur = animationRecordSkipTimerCount();
	if (cur != 0) {
		const uint8 next = uint8(cur - 1);
		setAnimationRecordSkipTimerCount(next);
		if (next != 0) {
			const uint16 resume = animationRecordSkipTimerResumePc();
			_offset = resume;
			debugC(3, kDebugLevelAnimation,
				   "anim opcode 0x0e: DecrementTimer %u → %u, loop 0x%04x [DOS Op_0f]",
				   cur, next, resume);
		} else {
			debugC(3, kDebugLevelAnimation,
				   "anim opcode 0x0e: DecrementTimer %u → 0, fall through [DOS Op_0f]", cur);
		}
	} else {
		debugC(3, kDebugLevelAnimation, "anim opcode 0x0e: DecrementTimer already zero [DOS Op_0f]");
	}

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
	_resources->setGlobalByte(var, 1);

	return kOk;
}

OPCODE(0x11) {
	uint16 var = shift();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x11: reset flag %d", var);
	_resources->setGlobalByte(var, 0);

	return kOk;
}

OPCODE(0x12) {
	uint16 var = shift();
	uint16 off = shift();
	byte ok = _resources->globalByte(var);

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
	setAnimationRecordTicksLeft(flags);

	return kFrameDone;
}

OPCODE(0x1a) {
	// DOS Op_1b SetField12ClearFlag16 @ 1000:6b4e:
	//   actor.field+0x12 = embedded byte; actor.field+0x16 = 0.
	// C++ uses `_zIndex` as the render-layer analog for field+0x12.
	byte v = embeddedByte();

	debugC(3, kDebugLevelAnimation, "anim opcode 0x1a: SetField12ClearFlag16 = %d [DOS Op_1b]", v);

	setAnimationRecordDrawLayer(v);
	setAnimationRecordAutoZoneLayerEnabled(false);
	_zIndex = dosSignedByte(v);

	return kOk;
}

OPCODE(0x1b) {
	// DOS Op_1c QueueMoveSlotMode0 @ 1000:6b5d: 3 shifts (a, b, c).
	//   Locates first free actor move-queue slot and writes (a, b, c, 0);
	//   on overflow sets pending error 0x0c.
	const uint16 arg1 = shift();
	const uint16 arg2 = shift();
	const uint16 arg3 = shift();
	const bool ok = queueAnimationMoveSlot(arg1, arg2, arg3, 0);
	if (!ok)
		Log.setPendingError(0x0c);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1b: QueueMoveSlotMode0 "
									"(a=%u,b=%u,c=%u) %s",
		   arg1, arg2, arg3, ok ? "ok" : "overflow -> pending 0x0c");

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
// (tied to Animation::_position / _mainSprite / _ticksLeft) or mirrors
// the DOS record write through sparse Animation fields. Byte consumption
// matches DOS exactly per the disassembly references.
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
	setAnimationRecordPosition(x, y);
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
	// Base Animation records are not registered in DOS's actor table, so
	// model LookupActorAndStartPath's not-found branch: write field+0x61
	// to the existing target-frame byte (+0x62) and call SetActorPosition
	// again. The inline word remains the field+0x8 sprite/target write.
	const byte face = uint8(embeddedByte());
	uint16 spriteId = shift();
	setPositionFromFrame(face);
	setPositionFromFrame(animationRecordTargetFrame());
	setAnimationRecordMainSprite(spriteId);
	setMainSprite(spriteId);
	copyAnimationIntervalToTicks();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x0b: FaceAndWalkWithFrame → setMainSprite(%u) "
									"[base Animation models LookupActorAndStartPath not-found branch]",
		   spriteId);
	return kFrameDone;
}

OPCODE(0x0c) {
	// DOS Op_0d FaceAndWalk @ 1000:6a0e: byte only.
	//   AL = embedded;   ES:[SI+0x61] = AL;          ; current frame
	//   CALL SetActorPosition + LookupActorAndStartPath;
	//   actor.field+0xa = zero-extended actor.field+0x10; end script.
	//   ADD BP, 0x2.
	// As in 0x0b, base Animation records have no actor-table slot, so the
	// fallback follows LookupActorAndStartPath's not-found branch.
	const byte face = uint8(embeddedByte());
	setPositionFromFrame(face);
	setPositionFromFrame(animationRecordTargetFrame());
	copyAnimationIntervalToTicks();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x0c: FaceAndWalk "
									"[base Animation models LookupActorAndStartPath not-found branch]");
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
	if (Log.cursorMode() != 0x80) {
		setAnimationRecordFacingPose(0);
		debugC(3, kDebugLevelAnimation,
			   "anim opcode 0x15: PickAnimationSet cursor_mode=%u != 0x80 -> field+0x68 = 0",
			   Log.cursorMode());
		return kOk;
	}

	const Common::Point screenCursor = Log.engine()->graphics()->cursorPosition();
	const int16 cursorX = int16(screenCursor.x + Log.cameraX());
	const int16 cursorY = int16(screenCursor.y + Log.cameraY());
	Common::Point spriteHotPoint(0, 0);
	if (mainSpriteId() != 0xffff) {
		const SpriteInfo info = _resources->getSpriteInfo(mainSpriteId());
		spriteHotPoint = info.hotPoint();
	}

	const int16 adjustedX = int16(_position.x) - spriteHotPoint.x;
	const int16 adjustedY = int16(_position.y) + spriteHotPoint.y;
	const uint8 width = animationRecordVisibleSpriteWidth();
	const uint8 height = animationRecordVisibleSpriteHeight();
	const int16 leftX = adjustedX;
	const int16 rightX = adjustedX + int16(width);
	const int16 topY = adjustedY - int16(height);
	const int16 botY = adjustedY;

	uint8 target;
	if (cursorY < topY) {
		if (cursorX < leftX)
			target = 8;
		else if (cursorX < rightX)
			target = 1;
		else
			target = 2;
	} else if (cursorY > botY) {
		if (cursorX < leftX)
			target = 6;
		else if (cursorX < rightX)
			target = 5;
		else
			target = 4;
	} else {
		if (cursorX < leftX)
			target = 7;
		else if (cursorX < rightX)
			target = 0x63;
		else
			target = 3;
	}

	const uint8 current = animationRecordFacingPose();
	if (current == 0x63 || target == 0x63 || target == current) {
		setAnimationRecordFacingPose(target);
		debugC(3, kDebugLevelAnimation,
			   "anim opcode 0x15: PickAnimationSet snap target=%u current=%u",
			   target, current);
		return kOk;
	}

	const int8 delta = dosSignedByte(uint8(target - current));
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

	setAnimationRecordFacingPose(next);
	debugC(3, kDebugLevelAnimation,
		   "anim opcode 0x15: PickAnimationSet rect=(%d..%d,%d..%d) cursor=(%d,%d) target=%u current=%u -> %u (delta=%d)",
		   leftX, rightX, topY, botY, cursorX, cursorY, target, current, next, int(delta));
	return kOk;
}

OPCODE(0x16) {
	// DOS Op_17 BranchIfAnimSetEquals @ 1000:6b17:
	//   AL = ES:[SI+0x68];  CMP AL, embedded;
	//   if equal: BP = ES:[BP+DI+0x2] (jump);
	//   else ADD BP, 4 (advance).
	const byte val = embeddedByte();
	const uint16 jumpTarget = shift();
	const uint8 current = animationRecordFacingPose();
	if (current == val) {
		_offset = jumpTarget;
		debugC(3, kDebugLevelAnimation, "anim opcode 0x16: BranchIfAnimSetEquals current=%u val=%u -> jump 0x%04x",
			   current, val, jumpTarget);
	} else {
		debugC(3, kDebugLevelAnimation,
			   "anim opcode 0x16: BranchIfAnimSetEquals current=%u val=%u -> no jump", current, val);
	}
	return kOk;
}

OPCODE(0x17) {
	// DOS Op_18 BranchIfMoodEquals @ 1000:6b29:
	//   if (ES:[SI+0x63] == embedded):
	//       AX = ES:[BP+DI+0x2];  DI = AX;  ES:[SI+0x2] = AX;  BP = 0;
	//   else ADD BP, 4.
	// On match DOS *rebases* the block: DI (block base) = jump target,
	// field+0x2 (code offset) = jump target, BP (running PC) = 0 — so the
	// next opcode is at ES:[jump target]. This is NOT the DI-relative
	// running-PC jump used by Op_10/Op_13/Op_17 (those keep DI and set BP).
	// The base Animation here drives cast-entry scripts whose `_baseOffset`
	// is the cast id's code offset (Logic::castTableRegister → CodePointer(id))
	// and is generally non-zero, so a plain `_offset = target` would land at
	// `_base + _baseOffset + target` instead of the segment-absolute target.
	// Mirror Actor::setActorCodeOffset's rebase exactly.
	const byte val = embeddedByte();
	const uint16 jumpTarget = shift();
	const uint8 mood = animationRecordMood();
	if (mood == val) {
		rebaseScript(jumpTarget);
		setAnimationRecordScriptBase(jumpTarget);
		debugC(3, kDebugLevelAnimation, "anim opcode 0x17: BranchIfMoodEquals mood=%u val=%u -> rebase to 0x%04x",
			   mood, val, jumpTarget);
	} else {
		debugC(3, kDebugLevelAnimation,
			   "anim opcode 0x17: BranchIfMoodEquals mood=%u val=%u -> no jump", mood, val);
	}
	return kOk;
}

OPCODE(0x18) {
	// DOS Op_19 SetField6d @ 1000:6b43: 1 shift.
	//   AX = ES:[BP+DI+0x2];  ES:[SI+0x6d] = AX (actually written as a word).
	const uint16 val = shift();
	setAnimationRecordPendingReadyAnimation(val);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x18: SetField6d = 0x%04x", val);
	return kOk;
}

OPCODE(0x1c) {
	// DOS Op_1d QueueMoveSlotMode1 @ 1000:6b95: 3 shifts (a, b, c).
	//   Locates first free slot in actor.field+0x19's 8-entry move
	//   queue (32 bytes total: 8 entries × 4 ints), writes (a, b, c, 1).
	//   On overflow: pending error 0xc.
	const uint16 arg1 = shift();
	const uint16 arg2 = shift();
	const uint16 arg3 = shift();
	const bool ok = queueAnimationMoveSlot(arg1, arg2, arg3, 1);
	if (!ok)
		Log.setPendingError(0x0c);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1c: QueueMoveSlotMode1 "
									"(a=%u,b=%u,c=%u) %s",
		   arg1, arg2, arg3, ok ? "ok" : "overflow -> pending 0x0c");
	return kOk;
}

OPCODE(0x1d) {
	// DOS Op_1e ClearFlag14 @ 1000:6bcd: ES:[SI+0x14] = 0.
	setAnimationField(0x14, 0);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1d: ClearFlag14 "
									"[field+0x14 = 0]");
	return kOk;
}

OPCODE(0x1e) {
	// DOS Op_1f ClearFlag15 @ 1000:6bd5: ES:[SI+0x15] = 0.
	setAnimationField(0x15, 0);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1e: ClearFlag15 "
									"[field+0x15 = 0]");
	return kOk;
}

OPCODE(0x1f) {
	// DOS Op_20 SetFlag15 @ 1000:6bdd: ES:[SI+0x15] = 1.
	setAnimationField(0x15, 1);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x1f: SetFlag15 "
									"[field+0x15 = 1]");
	return kOk;
}

OPCODE(0x20) {
	// DOS Op_21 SetCallbackPointer @ 1000:6be5:
	//   ES:[SI+0x5f] = BP+DI+0x2 (callback offset = next opcode addr);
	//   ES:[SI+0x5d] = ES (callback segment);
	//   ADD BP, 0xc (skip past 12-byte opcode = opcode + 1 pad + 10 inline body).
	const uint16 callbackPC = _baseOffset + _offset;
	setAnimationRecordActorCallbackOffset(callbackPC);
	setAnimationRecordActorCallbackSegment(animationCodeSegmentTag(_scriptInterpreter));
	(void)shift();
	(void)shift();
	(void)shift();
	(void)shift();
	(void)shift();
	debugC(3, kDebugLevelAnimation, "anim opcode 0x20: SetCallbackPointer "
									"cbSeg=0x%04x cbOff=0x%04x",
		   animationRecordActorCallbackSegment(), callbackPC);
	return kOk;
}

OPCODE(0x21) {
	// DOS Op_22 SetCallbackRelative @ 1000:6bf4: 1 shift (signed offset).
	//   if (offset != 0): callback_off = DI + offset (script-relative);
	//   else callback_off = 0 (clear);
	//   callback_seg = ES.
	const int16 off = int16(shift());
	uint16 cbOff = 0;
	if (off != 0)
		cbOff = uint16(int32(_baseOffset) + int32(off));
	setAnimationRecordActorCallbackOffset(cbOff);
	setAnimationRecordActorCallbackSegment(animationCodeSegmentTag(_scriptInterpreter));
	debugC(3, kDebugLevelAnimation, "anim opcode 0x21: SetCallbackRelative "
									"off=%d cbSeg=0x%04x cbOff=0x%04x",
		   off, animationRecordActorCallbackSegment(), cbOff);
	return kOk;
}

OPCODE(0x22) {
	// DOS Op_23 ClearCallback @ 1000:6c0a: ES:[SI+0x5d] = 0xffff.
	setAnimationRecordActorCallbackSegment(0xffff);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x22: ClearCallback "
									"[field+0x5d = 0xffff]");
	return kOk;
}

OPCODE(0x23) {
	// DOS Op_24 SetMood @ 1000:6c13: ES:[SI+0x63] = embedded byte.
	const byte mood = uint8(embeddedByte());
	setAnimationRecordMood(mood);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x23: SetMood "
									"[field+0x63 = %u]",
		   mood);
	return kOk;
}

OPCODE(0x24) {
	// DOS Op_25 SetField65 @ 1000:6c1e: ES:[SI+0x65] = embedded byte.
	const byte v = uint8(embeddedByte());
	setAnimationField(0x65, v);
	debugC(3, kDebugLevelAnimation, "anim opcode 0x24: SetField65 "
									"[field+0x65 = %u]",
		   v);
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

} // End of namespace Interspective
