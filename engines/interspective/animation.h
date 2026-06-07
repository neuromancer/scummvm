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
	bool hasDosMainSpriteForDraw() const { return _mainSpriteId != 0xffff && _mainSprite.get(); }
	int16 dosDrawY() const { return int16(_position.y); }
	bool castWaitCompleteLikeDos() const;
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

	uint16 shift();
	int8 shiftByte();
	int8 embeddedByte() const;
	uint8 animationDosField(uint8 off) const;
	uint16 animationDosFieldWord(uint8 off) const;
	void setAnimationDosField(uint8 off, uint8 v);
	void setAnimationDosFieldWord(uint8 off, uint16 v);
	void setPositionFromFrameLikeDos(uint8 frame);
	void copyIntervalToTicksLikeDos();
	void clearAnimationMoveSlotsLikeDos();
	bool queueAnimationMoveSlotLikeDos(uint16 arg1, uint16 arg2, uint16 arg3, uint8 mode);
	void paintMoveSlotLikeDos(Graphics *g, uint16 sprite, uint16 x, uint16 y, uint8 mode, const Common::Point &base) const;
	void paintAnimationMoveSlotsLikeDos(Graphics *g) const;
	void decrementAnimationTicksLeftLikeDos();

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
	Common::HashMap<uint8, uint8> _animationDosFields;
	struct AnimationMoveSlot {
		AnimationMoveSlot() : a(0), b(0), c(0), mode(0) {}
		AnimationMoveSlot(uint16 _a, uint16 _b, uint16 _c, uint8 _mode)
			: a(_a), b(_b), c(_c), mode(_mode) {}
		uint16 a, b, c;
		uint8 mode;
	};
	Common::Array<AnimationMoveSlot> _animationMoveSlots;

	bool _debugInvalid;

	// Forensic ring of (offset, byte) pairs for the last 16 dispatched
	// opcodes. Dumped by the "invalid opcode" error path so we can trace
	// which prior handler over/under-consumed and walked the script PC
	// off the rails.
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
