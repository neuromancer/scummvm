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

#ifndef INTERSPECTIVE_ANIMATION_H
#define INTERSPECTIVE_ANIMATION_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/list.h"
#include "common/ptr.h"
#include "common/rect.h"

#include "interspective/value.h"

namespace Interspective {
//

class Graphics;
class Resources;
class Sprite;

class Animation {
public:
	enum Status_ {
		kOk,
		kRemove,
		kFrameDone
	};

	typedef EnumPack<Status_> Status;

	Animation(const CodePointer &code, Common::Point position);
	virtual ~Animation();

	uint16 baseOffset() const { return _baseOffset; }

	virtual void paint(Graphics *g);
	virtual Status tick();

	void runOnNextFrame(const CodePointer &cp);

	virtual void setAnimation(uint16 anim) { _offset = anim; }

	virtual bool isActor() const { return false; }
	bool scriptActive() const { return _base != 0; }
	uint16 mainSpriteId() const { return _mainSpriteId; }
	bool hasMainSpriteForDraw() const { return _mainSpriteId != 0xffff && _mainSprite.get(); }
	int16 drawY() const { return int16(_position.y); }
	bool castWaitComplete() const;
	int8 zIndex() const { return _zIndex; }
	void setCastTableRunner(bool v) { _castTableRunner = v; }

	// Used by Logic::doChangeRoom before freeing the outgoing block's
	// code buffer. If this animation's _base lies within the [low, high)
	// range, null it out so Animation::tick() short-circuits cleanly
	// instead of dereferencing freed memory next frame. Re-attaching to
	// a valid script later is the script's responsibility (Op_bd/be/b9).
	void dropBaseIfIn(const byte *low, const byte *high) {
		if (_base && _base >= low && _base < high) {
			_base = 0;
			_baseOffset = _offset = 0;
		}
	}

protected:
	class Sprite;

	enum AnimationRecordOffset {
		kAnimationOffsetScriptBase = 0x02,
		kAnimationOffsetLeft = 0x04,
		kAnimationOffsetTop = 0x06,
		kAnimationOffsetMainSprite = 0x08,
		kAnimationOffsetTicksLeft = 0x0a,
		kAnimationOffsetScriptPc = 0x0c,
		kAnimationOffsetSkipTimerResumePc = 0x0e,
		kAnimationOffsetInterval = 0x10,
		kAnimationOffsetSkipTimerCount = 0x11,
		kAnimationOffsetDrawLayer = 0x12,
		kAnimationOffsetSecondaryZone = 0x13,
		kAnimationOffsetAutoZoneLayer = 0x16,
		kAnimationOffsetVisibleSpriteWidth = 0x17,
		kAnimationOffsetVisibleSpriteHeight = 0x18,
		kAnimationOffsetMoveSlots = 0x19,
		kAnimationOffsetActorCallbackSegment = 0x5d,
		kAnimationOffsetActorCallbackOffset = 0x5f,
		kAnimationOffsetFrame = 0x61,
		kAnimationOffsetTargetFrame = 0x62,
		kAnimationOffsetMood = 0x63,
		kAnimationOffsetFacingPose = 0x68,
		kAnimationOffsetPendingReadyAnimation = 0x6d
	};

	uint16 shift();
	int8 shiftByte();
	int8 embeddedByte() const;
	uint8 animationField(uint8 off) const;
	uint16 animationFieldWord(uint8 off) const;
	void setAnimationField(uint8 off, uint8 v);
	void setAnimationFieldWord(uint8 off, uint16 v);
	void setAnimationRecordScriptBase(uint16 offset) { setAnimationFieldWord(kAnimationOffsetScriptBase, offset); }
	void setAnimationRecordPosition(uint16 left, uint16 top) {
		setAnimationFieldWord(kAnimationOffsetLeft, left);
		setAnimationFieldWord(kAnimationOffsetTop, top);
	}
	void setAnimationRecordPosition(Common::Point p) { setAnimationRecordPosition(uint16(p.x), uint16(p.y)); }
	void setAnimationRecordMainSprite(uint16 sprite) { setAnimationFieldWord(kAnimationOffsetMainSprite, sprite); }
	void setAnimationRecordTicksLeft(uint16 ticks) { setAnimationFieldWord(kAnimationOffsetTicksLeft, ticks); }
	void setAnimationRecordScriptPc(uint16 pc) { setAnimationFieldWord(kAnimationOffsetScriptPc, pc); }
	void setAnimationRecordInterval(uint8 interval) { setAnimationField(kAnimationOffsetInterval, interval); }
	uint8 animationRecordSkipTimerCount() const { return animationField(kAnimationOffsetSkipTimerCount); }
	void setAnimationRecordSkipTimerCount(uint8 timer) { setAnimationField(kAnimationOffsetSkipTimerCount, timer); }
	uint16 animationRecordSkipTimerResumePc() const { return animationFieldWord(kAnimationOffsetSkipTimerResumePc); }
	void setAnimationRecordSkipTimerResumePc(uint16 pc) { setAnimationFieldWord(kAnimationOffsetSkipTimerResumePc, pc); }
	void setAnimationRecordDrawLayer(uint8 drawLayer) { setAnimationField(kAnimationOffsetDrawLayer, drawLayer); }
	void setAnimationRecordSecondaryZone(uint8 zone) { setAnimationField(kAnimationOffsetSecondaryZone, zone); }
	void setAnimationRecordAutoZoneLayerEnabled(bool enabled) { setAnimationField(kAnimationOffsetAutoZoneLayer, enabled ? 1 : 0); }
	void setAnimationRecordFrame(uint8 frame) { setAnimationField(kAnimationOffsetFrame, frame); }
	uint8 animationRecordTargetFrame() const { return animationField(kAnimationOffsetTargetFrame); }
	uint8 animationRecordVisibleSpriteWidth() const { return animationField(kAnimationOffsetVisibleSpriteWidth); }
	uint8 animationRecordVisibleSpriteHeight() const { return animationField(kAnimationOffsetVisibleSpriteHeight); }
	uint8 animationRecordFacingPose() const { return animationField(kAnimationOffsetFacingPose); }
	void setAnimationRecordFacingPose(uint8 pose) { setAnimationField(kAnimationOffsetFacingPose, pose); }
	uint8 animationRecordMood() const { return animationField(kAnimationOffsetMood); }
	void setAnimationRecordMood(uint8 mood) { setAnimationField(kAnimationOffsetMood, mood); }
	void setAnimationRecordPendingReadyAnimation(uint16 animation) { setAnimationFieldWord(kAnimationOffsetPendingReadyAnimation, animation); }
	uint16 animationRecordActorCallbackSegment() const { return animationFieldWord(kAnimationOffsetActorCallbackSegment); }
	void setAnimationRecordActorCallbackSegment(uint16 segment) { setAnimationFieldWord(kAnimationOffsetActorCallbackSegment, segment); }
	void setAnimationRecordActorCallbackOffset(uint16 offset) { setAnimationFieldWord(kAnimationOffsetActorCallbackOffset, offset); }
	void clearAnimationRecordMoveSlot(uint slot) {
		const uint8 off = uint8(kAnimationOffsetMoveSlots + slot * 8);
		setAnimationFieldWord(off, 0xffff);
	}
	void setAnimationRecordMoveSlot(uint slot, uint16 sprite, uint16 x, uint16 y, uint8 mode) {
		const uint8 off = uint8(kAnimationOffsetMoveSlots + slot * 8);
		setAnimationFieldWord(off, sprite);
		setAnimationFieldWord(uint8(off + 2), x);
		setAnimationFieldWord(uint8(off + 4), y);
		setAnimationFieldWord(uint8(off + 6), mode);
	}
	void setPositionFromFrame(uint8 frame);
	void copyAnimationIntervalToTicks();
	void clearAnimationMoveSlots();
	bool queueAnimationMoveSlot(uint16 arg1, uint16 arg2, uint16 arg3, uint8 mode);
	void paintMoveSlot(Graphics *g, uint16 sprite, uint16 x, uint16 y, uint8 mode, const Common::Point &base) const;
	void paintAnimationMoveSlots(Graphics *g) const;
	void decrementAnimationTicksLeft();

	void setMainSprite(uint16 sprite);
	void clearMainSprite();
	void clearSprites();

	void handleTrigger();

	template<int opcode>
	Status opcodeHandler();

	template<int N>
	void init_opcodes();

	virtual Status op(byte code);

	Resources *_resources;
	int8 _interval;
	uint16 _ticksLeft;
	bool _explicitFrameDelay;
	int8 _zIndex;
	Common::Point _position;
	/** start of the animation code */
	byte *_base;
	/** current position  in the animation */
	uint16 _offset;
	char _debugInfo[50];
	Common::List<Sprite *> _sprites;
	Common::SharedPtr<Interspective::Sprite> _mainSprite;
	uint16 _mainSpriteId;
	uint8 _counter;
	bool _castTableRunner;
	uint16 _loopStart;
	/** offset of the animation from the start of its codeblock */
	uint16 _baseOffset;
	CodePointer _frameTrigger;
	Common::HashMap<uint8, uint8> _animationFields;
	struct AnimationMoveSlot {
		AnimationMoveSlot() : a(0), b(0), c(0), mode(0) {}
		AnimationMoveSlot(uint16 _a, uint16 _b, uint16 _c, uint8 _mode)
			: a(_a), b(_b), c(_c), mode(_mode) {}
		uint16 a;
		uint16 b;
		uint16 c;
		uint8 mode;
	};
	Common::Array<AnimationMoveSlot> _animationMoveSlots;

	bool _debugInvalid;

	// Ring of (offset, byte) pairs for the last 16 dispatched opcodes.
	// Dumped by the invalid-opcode error path to diagnose script PC
	// misalignment.
	struct OpRingEntry {
		uint16 pc;
		byte op;
	};
	OpRingEntry _opRing[16];
	uint8 _opRingIdx;

private:
	typedef Status (Animation::*OpcodeHandler)();
	OpcodeHandler _handlers[38];
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_ANIMATION_H
