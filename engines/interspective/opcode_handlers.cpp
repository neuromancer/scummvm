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

#include "interspective/inter.h"

#include "interspective/actor.h"
#include "interspective/animation.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/movie.h"
#include "interspective/musicparser.h"
#include "interspective/room.h"
#include "interspective/sound.h"
#include "interspective/util.h"

#include "common/events.h"
#include "common/util.h"

namespace Interspective {

static bool runDisableMoveOptionalEnable(Logic *logic, uint16 cellId, uint16 targetId, uint16 enableId) {
	logic->disableObjectFlag1(cellId);
	logic->movePersonToActor(targetId);
	if (enableId != 0)
		logic->enableObjectFlag1(enableId);
	return true;
}

static bool runDisableEnableUnregister(Logic *logic, uint16 cellId, uint16 enableId) {
	(void)enableId;
	// DOS 0x4a36 restores BX but never moves it back to AX before
	// EnableObjectFlag1; AX is the value left by DisableObjectFlag1.
	logic->enableObjectFlag1(logic->disableObjectFlag1ReturnAx(cellId));
	logic->clearDragInteractionLikeOp8e();
	return true;
}

static bool dosCellBitTest(uint8 cellByte, uint16 rawBit) {
	const uint8 count = uint8(((rawBit + 1) & 0x1f) % 9);
	if (count == 0)
		return false;
	return ((cellByte >> (count - 1)) & 1) != 0;
}

static int8 dosCellBitIndex(uint16 rawBit) {
	const uint8 count = uint8(((rawBit + 1) & 0x1f) % 9);
	return count == 0 ? -1 : int8(count - 1);
}

static byte *resolveDosResourcePointer(Value &arg, const CodePointer &current, byte **baseOut = 0) {
	byte *base = arg.rawBase();
	byte *ptr = arg.rawPointer();
	if (!ptr) {
		base = current.base();
		ptr = base ? base + uint16(arg) : nullptr;
	} else if (!base) {
		base = current.base();
	}
	if (baseOut)
		*baseOut = base;
	return ptr;
}

static uint16 dosResourceOffset(byte *base, byte *ptr) {
	if (!base || !ptr || ptr < base)
		return 0xffff;
	return uint16(ptr - base);
}

static void clearDosPascalBufferAt(byte *base, byte *ptr) {
	if (!ptr)
		return;

	const uint16 off = dosResourceOffset(base, ptr);
	if (off == 0xffff || off >= 0x8000) {
		ptr[1] = 0;
		return;
	}
	memset(ptr + 1, 0, 0x8000 - off - 1);
}

static void appendDosPascalByteAt(byte *ptr, byte ch) {
	if (!ptr)
		return;

	const int8 capacity = int8(ptr[0]);
	const int8 length = int8(ptr[1]);
	if (length < capacity) {
		const byte oldLength = ptr[1]++;
		ptr[oldLength + 2] = ch;
	}
}

static void popDosPascalByteAt(byte *ptr) {
	if (!ptr)
		return;

	const byte oldLength = ptr[1];
	if (oldLength != 0) {
		ptr[1] = oldLength - 1;
		ptr[oldLength + 1] = 0;
	}
}

static void applyFormattedTextLimit9bcc(uint16 limit, uint16 &height, uint16 &rows);

static uint16 speechDisplayTicks(const byte *text, uint16 maxLines) {
	Common::String normalized;
	for (const byte *p = text; p && *p; ++p)
		normalized += char(*p == '\n' ? '\r' : *p);
	Logic::FormattedBubble fb = Log.formatBubbleText(reinterpret_cast<const byte *>(normalized.c_str()));
	uint16 height = fb.totalHeight;
	uint16 rows = fb.rowCount;
	if (maxLines != 0)
		applyFormattedTextLimit9bcc(maxLines, height, rows);
	return uint8(height & 0xff);
}

// Speech subsystem helper: route text to the appropriate sink.
// In status mode, DOS displays subtitles (no actor bubble). Otherwise
// allocate from Logic's DOS-style six-slot speech pool.
//
// `maxLines` is the BX value carried into AllocSpeechSlot @ 1000:9b1f.
// Zero means "no forced page height"; non-zero values page the formatted
// text through the DOS 9bcc helper.
static void speakOrSubtitle(Actor *speaker, const Common::String &text, uint16 maxLines = 0) {
	const bool mainSpeaker = !speaker || speaker == Log.protagonist();
	if (Log.inStatusMode() && mainSpeaker) {
		// DOS status-mode: CheckSubtitleActive + RegisterSampleSlot_LoadDefaultsB
		// or QueueDeferredFormattedText. The shared status path seeds
		// CX=0xa4, DX=0x14, AX=2, BP=0xeb before QueueDeferredFormattedText.
		const uint16 length = uint16(text.size());
		if (length > 0)
			Graf.sayAt(reinterpret_cast<const byte *>(text.c_str()),
					   length, speechDisplayTicks(reinterpret_cast<const byte *>(text.c_str()), maxLines),
					   0xa4, 0x14, 0xeb, maxLines, Graphics::kSpeechBubbleType2, true);
		return;
	}
	if (!speaker)
		return;
	speaker->say(text, maxLines);
}

static bool sampleSlotWouldError() {
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return true;
	}
	return false;
}

enum SpeechDeferResult {
	kSpeechNoWait,
	kSpeechWait,
	kSpeechWaitError
};

static SpeechDeferResult waitForActiveSpeechSlotOwner(uint16 owner, const CodePointer &current) {
	if (!Log.speechSlotActiveForOwner(owner))
		return kSpeechNoWait;
	if (sampleSlotWouldError())
		return kSpeechWaitError;
	Log.queueSpeechSlotCallbackForOwner(owner, current);
	return kSpeechWait;
}

static SpeechDeferResult deferSpeechUntilReady(Actor *speaker, const CodePointer &current) {
	const bool mainSpeaker = !speaker || speaker == Log.protagonist();
	if (Log.inStatusMode() && mainSpeaker) {
		if (Graf.isSaying()) {
			if (sampleSlotWouldError())
				return kSpeechWaitError;
			Graf.runWhenSaid(current);
			return kSpeechWait;
		}
		return kSpeechNoWait;
	}
	if (!speaker)
		return kSpeechNoWait;
	const SpeechDeferResult speechWait = waitForActiveSpeechSlotOwner(Log.actorGlobalId(speaker), current);
	if (speechWait != kSpeechNoWait)
		return speechWait;
	if (speaker->isMoving()) {
		if (sampleSlotWouldError())
			return kSpeechWaitError;
		speaker->callMeWhenStill(current);
		return kSpeechWait;
	}
	return kSpeechNoWait;
}

enum MainSpeechTargetResult {
	kMainSpeechContinue,
	kMainSpeechDone,
	kMainSpeechWait,
	kMainSpeechError
};

static void setActorTargetMarker(Actor *actor) {
	// DOS SetActorTarget @ 1000:7087 stores BP into actor.field+0x69 and
	// writes marker 5 to field+0x67 only if the callback word is empty.
	// The shared speech path seeds BP from CS:[0x00bf], which has no writers
	// in the executable image and is zero, so preserving the zero word and
	// marker write is the observable actor-record side effect.
	if (actor && actor->readyCallbackOffset() == 0) {
		actor->setReadyCallbackOffset(0);
		actor->setReadyMarker(5);
	}
}

static bool sendActorToCurrentEntityCarryClear(Actor *protag) {
	// SendActorToTarget @ 1000:7323 returns carry set for the normal
	// protagonist path through MoveActorToTargetExit @ 1000:70da,
	// including the "already at target frame" case. The visible carry-clear
	// path in MoveProtagonistToEntity @ 1000:737e is the unplaced object
	// sentinel (object room == 0xffff), which returns before starting a walk.
	const bool unplacedObject = Log.gameState() == 2 && Log.getObjectRoom(Log.currentEntityId()) == 0xffff;
	if (!Log.sendActorToCurrentEntity(protag))
		return false;
	return unplacedObject;
}

static MainSpeechTargetResult speakAsMainAfterOptionalTargetWalk(Actor *protag,
																 const Common::String &text, uint16 maxLines, const CodePointer &current) {
	if (Log.inStatusMode() || Log.hitTarget() != 0)
		return kMainSpeechContinue;

	const bool carryClear = sendActorToCurrentEntityCarryClear(protag);
	if (!carryClear)
		return kMainSpeechContinue;

	if (Log.gameState() == 2)
		setActorTargetMarker(protag);

	if (protag) {
		const SpeechDeferResult speechWait =
			waitForActiveSpeechSlotOwner(Log.actorGlobalId(protag), current);
		if (speechWait == kSpeechWaitError)
			return kMainSpeechError;
		if (speechWait == kSpeechWait)
			return kMainSpeechWait;
	}

	Log.allocActorSpeechForPostMove(protag, text, maxLines);
	Log.setPostMoveCallback(Logic::PostMoveCallback::kActivateProtagonistSpeechAfterMove, 0, 0, 0);
	return kMainSpeechDone;
}

static bool failIfMainActorMissingForNonStatusSpeech(Actor *protag) {
	if (!protag && !Log.inStatusMode()) {
		Log.setPendingError(0x17);
		return true;
	}
	return false;
}

static SpeechDeferResult deferMainSpeechNoTargetUntilReady(Actor *protag, const CodePointer &current) {
	if (Log.inStatusMode())
		return deferSpeechUntilReady(protag, current);

	const SpeechDeferResult speechWait =
		waitForActiveSpeechSlotOwner(Log.protagonistId(), current);
	if (speechWait != kSpeechNoWait)
		return speechWait;

	if (failIfMainActorMissingForNonStatusSpeech(protag))
		return kSpeechWaitError;
	return deferSpeechUntilReady(protag, current);
}

static bool sayNarratorOrSubtitle(const byte *text, uint16 x, uint16 y, byte color, uint16 maxLines,
								  Graphics::SpeechBubbleMode bubbleMode, const CodePointer &current) {
	if (!text)
		return false;
	const uint16 length = (uint16)strlen(reinterpret_cast<const char *>(text));
	if (length == 0)
		return false;
	const uint16 ticks = speechDisplayTicks(text, maxLines);
	if (Log.inStatusMode() && Graf.isSaying()) {
		if (sampleSlotWouldError())
			return false;
		Graf.runWhenSaid(current);
		return true;
	}
	if (Log.inStatusMode())
		Graf.sayAt(text, length, ticks, x, y, color, maxLines, bubbleMode, true);
	else
		Log.allocNarratorSpeech(text, length, x, y, color, maxLines, uint8(bubbleMode));
	return false;
}

static bool waitForSpeechSlotOwner(uint16 owner, const CodePointer &next) {
	if (!Log.speechSlotActiveForOwner(owner))
		return false;
	Log.queueSpeechSlotCallbackForOwner(owner, next);
	return true;
}

static bool waitForUiTextSlot(const CodePointer &next) {
	if (!Log.uiTextSpeechSlotActive())
		return false;
	Log.queueUiTextSpeechSlotCallback(next);
	return true;
}

static bool waitForSubtitle(const CodePointer &next) {
	if (!Graf.isSaying())
		return false;
	Graf.runWhenSaid(next);
	return true;
}

static void initActorState(Actor *actor, const CodePointer &anim) {
	if (!actor)
		return;
	actor->setAnimation(anim);
}

static void initActorFromPuppeteer(Logic *logic, Actor *actor, uint16 actorId) {
	if (!logic || !actor || !logic->resources() || !logic->resources()->mainDat())
		return;
	const Puppeteer puppeteer = logic->resources()->mainDat()->getPuppeteer(actorId);
	initActorState(actor, CodePointer(puppeteer.mainCodeOffset(), logic->mainInterpreter()));
}

static void writeActorRoomTransition(Actor *actor, uint16 room, uint8 frame, uint8 target) {
	if (!actor)
		return;
	// DOS 0x77/0x78 tail writes only these fields; SetActorPosition is
	// exclusive to the 0x79/0x7a placement tail.
	actor->setRawFrame(frame);
	actor->setRawTargetFrame(target);
	if (actor == Log.protagonist())
		Log.setPostMoveTargetFrameMirror(target);
	actor->forceRoom(room);
}

static void requestRoomRestartTail(uint16 room) {
	if (room != Log.currentRoom())
		Log.changeRoom(room);
	else
		Log.restartRoom();
	Log.setLogicDirty();
	Log.setPaused();
}

static void clearDragInteractionLikeOp8e() {
	Log.clearDragInteractionLikeOp8e();
}

static void placeActorInRoomWithPosition(Actor *actor, uint16 room, uint8 frame, uint8 target) {
	if (!actor)
		return;
	actor->forceRoom(room);
	actor->setRawTargetFrame(target);
	actor->clearMoveQueue();
	actor->setFrame(frame);
}

static bool checkActorAnimReadyModeled(Actor *actor) {
	// DOS CheckActorAnimReady @ 1000:6415 returns carry set when the
	// actor is ready. Actor::animReady mirrors the field-level
	// predicate, including +0x64/+0x65/+0x6b/+0x69/+0x6f.
	return !actor || actor->animReady();
}

static bool retryCurrentOpcodeWhenActorReady(const CodePointer &current, Actor *actor) {
	Log.setBreakInner(true);
	if (sampleSlotWouldError())
		return false;
	if (actor)
		actor->callMeWithMode(current, Log.opcodeMode());
	else
		Log.runLaterWithCurrentMode(current);
	return true;
}

static uint16 actorAnimMaxId() {
	const uint16 mainActors = Log.engine()->resources()->mainDat()->actorsCount();
	const uint16 blockActors = Log.blockProgram() ? Log.blockProgram()->actorsCount() : 0;
	return uint16(mainActors + blockActors);
}

static void setBreakInnerIfProtagonistId(uint16 id) {
	if (id == Log.protagonistId())
		Log.setBreakInner(true);
}

static Actor *getActorOrPending(uint16 id) {
	if (id == 0) {
		Log.setPendingError(0x17);
		return 0;
	}
	const uint16 mainActors = Log.engine()->resources()->mainDat()->actorsCount();
	const uint16 blockActors = Log.blockProgram() ? Log.blockProgram()->actorsCount() : 0;
	if (id > mainActors + blockActors) {
		Log.setPendingError(0x17);
		return 0;
	}
	Actor *actor = Log.getActor(id);
	if (!actor) {
		Log.setPendingError(0x17);
		return 0;
	}
	Log.setImplicitActor(actor);
	return actor;
}

static void setActorReadyFields(Actor *actor, uint8 marker, uint16 callback) {
	if (!actor || actor->room() != Log.currentRoom())
		return;
	actor->setReadyMarker(marker);
	actor->setMovementWaitActive(true);
	actor->setReadyCallbackOffset(callback);
}

static void setActorReadyMarkerOnly(Actor *actor, uint8 marker) {
	if (!actor || actor->room() != Log.currentRoom())
		return;
	actor->setReadyMarker(marker);
	actor->setMovementWaitActive(true);
}

static uint8 actorDirectionToPoint(Actor *actor, int16 targetX, int16 targetY) {
	int8 spriteHotLeft = 0;
	int8 spriteHotTop = 0;
	if (actor && actor->mainSpriteId() != 0xffff) {
		const SpriteInfo info = Log.engine()->resources()->getSpriteInfo(actor->mainSpriteId());
		spriteHotLeft = info.hotLeft;
		spriteHotTop = info.hotTop;
	}

	const int16 adjustedX = int16(actor->position().x) - spriteHotLeft;
	const int16 adjustedY = int16(actor->position().y) + spriteHotTop;
	const int16 halfHeight = int16(actor->visibleSpriteHeight()) >> 1;
	const int16 leftX = adjustedX;
	const int16 rightX = adjustedX + int16(actor->visibleSpriteWidth());
	const int16 topY = adjustedY - halfHeight;
	const int16 bottomY = adjustedY;

	uint8 direction;
	if (targetY < topY) {
		if (targetX < leftX)
			direction = 8;
		else if (targetX < rightX)
			direction = 1;
		else
			direction = 2;
	} else if (targetY > bottomY) {
		if (targetX < leftX)
			direction = 6;
		else if (targetX < rightX)
			direction = 5;
		else
			direction = 4;
	} else {
		if (targetX < leftX)
			direction = 7;
		else if (targetX < rightX)
			direction = 0x63;
		else
			direction = 3;
	}
	return direction == 0x63 ? 1 : direction;
}

static bool checkActorIdleReadyModeled(Actor *actor) {
	// DOS CheckActorIdle @ 1000:645e returns carry set when the actor is
	// idle/ready. This is stricter than Actor::isFine(), which is a legacy
	// movement heuristic and does not model all DOS actor fields.
	return !actor || actor->idleReady();
}

static void setReadyCallbackOffset(Actor *actor, uint16 callback) {
	if (!actor)
		return;
	actor->setReadyCallbackOffset(callback);
}

static void queueExitTransition(Actor *actor, uint16 frame) {
	if (!actor)
		return;
	Log.clearPostMoveCallback();
	actor->stopSpeaking();
	setReadyCallbackOffset(actor, 0);
	if (actor == Log.protagonist())
		Log.setBreakInner(true);
	Log.setPostMoveTargetFrameMirror(uint8(frame));
	if (actor->room() == Log.currentRoom() && actor->frameId() != 0)
		actor->setRawTargetFrame(uint8(frame));
	actor->moveTo(frame);
	if (actor->movementWaitActive())
		Log.setPostMoveTargetFrameMirror(uint8(actor->frameId()));
}

static bool moveActorToTargetExit(Actor *actor, uint16 frame) {
	if (!actor)
		return false;
	setReadyCallbackOffset(actor, 0);
	if (actor == Log.protagonist()) {
		queueExitTransition(actor, frame);
		return true;
	}
	if (actor->room() != Log.currentRoom()) {
		actor->setFrame(frame);
	} else {
		if (actor->frameId() != 0)
			actor->setRawTargetFrame(uint8(frame));
		actor->moveTo(frame);
	}
	return false;
}

static bool sendActorToScriptEntityByType(Actor *actor, uint16 targetId, uint16 entityType) {
	if (!actor || !Log.room())
		return false;

	int16 targetX = 0;
	int16 targetY = 0;
	switch (entityType) {
	case 1: {
		Exit *exit = Log.blockProgram() ? Log.blockProgram()->getExit(targetId) : 0;
		if (!exit) {
			Log.setPendingError(0x14);
			return false;
		}
		const int16 exitX = int16(Log.recordField(1, targetId, 2, 2));
		const int16 exitY = int16(Log.recordField(1, targetId, 4, 2));
		if (Log.recordField(1, targetId, 0x0a, 1) == 0) {
			const uint16 spriteId = Log.recordField(1, targetId, 6, 2);
			const SpriteInfo info = Log.engine()->resources()->getSpriteInfo(spriteId);
			targetX = int16(exitX + int16(info.width) / 2);
		} else {
			targetX = exitX;
		}
		targetY = exitY;
		break;
	}
	case 2:
		if (targetId == 0) {
			Log.setPendingError(0x16);
			return false;
		}
		// MoveProtagonistToEntity @ 1000:737e returns CLC without walking
		// for unplaced object/person records.
		if (Log.getObjectRoom(targetId) == 0xffff)
			return true;
		targetX = int16(Log.getObjectPosX(targetId) + int16(Log.objectField(targetId, 0x10)) / 2);
		targetY = int16(Log.getObjectPosY(targetId) - 5);
		break;
	case 3: {
		Actor *target = Log.getActor(targetId);
		if (!target) {
			Log.setPendingError(0x17);
			return false;
		}
		targetX = int16(target->position().x);
		targetY = int16(target->position().y);
		break;
	}
	default: {
		const Common::Point cursor = Log.lockedCursorPosition();
		targetX = int16(cursor.x + Log.cameraX());
		targetY = int16(cursor.y + Log.cameraY());
		break;
	}
	}

	const uint16 frame = Log.room()->nearestFrameTo(targetX, targetY);
	if (frame == 0) {
		Log.setPendingError(0x31);
		return moveActorToTargetExit(actor, actor->frameId());
	}
	return moveActorToTargetExit(actor, frame);
}

static bool sendActorToCurrentScriptEntity(Actor *actor) {
	return sendActorToScriptEntityByType(actor, Log.currentEntityId(), Log.gameState());
}

static void clearVideoAndPushToScreen(Graphics *graphics) {
	if (!graphics)
		return;
	graphics->clearFramebuffer();
	graphics->updateScreen();
}

static void reloadLoadedBackdrop(Graphics *graphics) {
	if (!graphics)
		return;
	const uint16 id = Log.loadedBackdropId();
	if (id == 0)
		return;
	MainDat *main = Log.resources()->mainDat();
	if (!main || int16(id) > int16(main->imagesCount())) {
		Log.setPendingError(0x0a);
		return;
	}
	graphics->setBackdrop(id);
}

static bool showFormattedModalTextAndWait(const Logic::FormattedBubble &fb,
										  uint16 frames, const CodePointer &next) {
	if (fb.text.empty())
		return false;
	// The DOS formatted buffer now includes bubble-row centering records
	// emitted by EmitTextRowTerminator. This helper is only the C++ modal
	// stand-in that routes through Graphics::paintText, whose 0x0c control
	// has different no-width-byte semantics, so remove the synthetic row
	// records before passing it there.
	Common::String visible;
	bool atRowStart = true;
	for (uint i = 0; i < fb.text.size(); ++i) {
		const byte ch = byte(fb.text[i]);
		if (atRowStart && ch == kStringCenter && i + 1 < fb.text.size()) {
			++i;
			atRowStart = false;
			continue;
		}
		visible += char(ch);
		atRowStart = (ch == '\r' || ch == '\n');
	}
	const byte *out = reinterpret_cast<const byte *>(visible.c_str());
	const uint16 length = uint16(visible.size());
	if (Log.modalState().paletteMode != 0) {
		Log.modalState().activeText = visible;
		Graf.showVerbBubbleText(Log.modalState().paletteMode, out, MAX<uint16>(1, frames));
		return false;
	}
	Graf.say(out, length, MAX<uint16>(1, frames));
	Graf.runWhenSaid(next);
	return true;
}

static Common::String stripFormattedRowCenterRecords(const Logic::FormattedBubble &fb) {
	Common::String visible;
	bool atRowStart = true;
	for (uint i = 0; i < fb.text.size(); ++i) {
		const byte ch = byte(fb.text[i]);
		if (atRowStart && ch == kStringCenter && i + 1 < fb.text.size()) {
			++i;
			atRowStart = false;
			continue;
		}
		visible += char(ch);
		atRowStart = (ch == '\r' || ch == '\n');
	}
	return visible;
}

static bool formattedTextHasMenuOptions(const Common::String &text) {
	for (uint i = 0; i < text.size(); ++i) {
		const byte ch = byte(text[i]);
		if (ch != kStringMenuOption)
			continue;
		++i;
		while (i < text.size() && byte(text[i]) != 0)
			++i;
		if (i + 2 < text.size())
			return true;
		return false;
	}
	return false;
}

static bool formattedTextIsPureChoiceList(const Common::String &text) {
	bool sawChoice = false;
	for (uint i = 0; i < text.size();) {
		const byte ch = byte(text[i++]);
		if (ch == '\r' || ch == '\n' || ch == 0x04 || ch == kStringDefaultColour)
			continue;
		if (ch == kStringSetColour || ch == kStringAdvance || ch == kStringCenter) {
			if (i < text.size())
				++i;
			continue;
		}
		if (ch != kStringMenuOption)
			return false;

		sawChoice = true;
		while (i < text.size() && byte(text[i]) != 0)
			++i;
		if (i >= text.size())
			return false;
		++i; // label terminator
		if (i + 2 > text.size())
			return false;
		i += 2; // branch target
	}
	return sawChoice;
}

static uint16 modalChoiceLineWidth(const Common::String &text) {
	uint16 width = 0;
	for (uint i = 0; i < text.size(); ++i) {
		const byte ch = byte(text[i]);
		switch (ch) {
		case 0x04:
		case kStringDefaultColour:
			break;
		case kStringSetColour:
			if (i + 1 < text.size())
				++i;
			break;
		case kStringAdvance:
			if (i + 1 < text.size())
				width += byte(text[++i]);
			break;
		default:
			width += Graf.getGlyphWidth(ch);
			break;
		}
	}
	return width;
}

struct RawModalChoice {
	Common::Array<byte> line;
	uint16 target;
	bool terminal;

	RawModalChoice() : target(0xffff), terminal(false) {}
};

static bool runFormattedChoiceModal(const Logic::FormattedBubble &fb,
									uint16 rows, uint16 *selectedIndex, uint16 &target);
static void seedFormattedModalState(Logic::ModalState &ms,
									const Logic::FormattedBubble &fb, uint16 menuValue, uint16 rows,
									uint8 paletteMode, uint8 stashFlag);

static bool appendRawModalChoices(const byte *src, Common::Array<byte> &encoded,
								  Common::Array<RawModalChoice> &rawChoices, uint16 &choiceCount,
								  uint16 &maxTextWidth) {
	// LayoutVerbBubbleText_Right/Left @ 1000:8cb0 / 1000:8d1e read a raw list
	// of entries: one optional condition marker, NUL-terminated text, then
	// a 16-bit branch target. 0xff terminates the list. HandleVerbButton_
	// Submenu @ 1000:8b27 treats rows starting with 0x04 as terminal;
	// otherwise it re-formats the selected row as the next mode-3 bubble.
	if (!src)
		return false;

	Resources *res = Log.resources();
	const byte *p = src;
	const byte *const limit = src + 4096;
	rawChoices.clear();
	choiceCount = 0;
	maxTextWidth = 0;
	bool sawTerminator = false;

	while (p < limit) {
		if (*p == 0xff) {
			sawTerminator = true;
			break;
		}

		bool visible = true;
		const byte marker = *p;
		if (marker == 0x0a || marker == 0x0b) {
			const uint16 offset = READ_LE_UINT16(p + 1);
			const byte state = res ? *res->getGlobalByteVariable(offset) : 0;
			if ((marker == 0x0a && state == 0) || (marker == 0x0b && state != 0))
				visible = false;
			p += 3;
		} else if (marker == 0x0e) {
			const uint16 offset = READ_LE_UINT16(p + 1);
			const uint16 expected = READ_LE_UINT16(p + 3);
			const uint16 state = res ? READ_LE_UINT16(res->getGlobalWordVariable(offset / 2)) : 0;
			if (state != expected)
				visible = false;
			p += 5;
		}

		const byte *line = p;
		while (p < limit && *p != 0 && *p != 0xff)
			++p;
		if (p >= limit || *p == 0xff)
			break;
		const uint lineLen = uint(p - line);
		++p;
		if (p + 2 > limit)
			break;
		const uint16 target = READ_LE_UINT16(p);
		p += 2;

		// DOS LayoutVerbBubbleText_Left/Right @ 1000:8d1e / 1000:8cb0 only skips a row
		// on the 0xff terminator or a failed condition marker — it does NOT
		// skip empty-text rows: the emit block @8e3a stores the (zero-width)
		// rect, the branch target, and bumps the slot counter regardless. So
		// an empty-label row still consumes one of the 7 slots and carries its
		// target. (The loop below already tolerates lineLen==0, and the
		// rawChoice.terminal guard already protects the line[0] read.)
		if (!visible)
			continue;
		if (choiceCount >= 7)
			continue;

		if (choiceCount != 0)
			encoded.push_back('\r');
		encoded.push_back(kStringMenuOption);
		Common::String label;
		RawModalChoice rawChoice;
		rawChoice.target = target;
		rawChoice.terminal = lineLen != 0 && line[0] == 0x04;
		for (uint i = 0; i < lineLen; ++i) {
			label += char(line[i]);
			encoded.push_back(line[i]);
			rawChoice.line.push_back(line[i]);
		}
		rawChoice.line.push_back(0);
		encoded.push_back(0);
		encoded.push_back(byte(target & 0xff));
		encoded.push_back(byte(target >> 8));
		rawChoices.push_back(rawChoice);
		maxTextWidth = MAX<uint16>(maxTextWidth, modalChoiceLineWidth(label));
		++choiceCount;
	}

	if (!sawTerminator || choiceCount == 0)
		return false;

	encoded.push_back(0);
	return true;
}

static uint16 runEncodedChoiceModal(const byte *encoded,
									uint16 choiceCount, uint16 maxTextWidth, uint16 *selectedIndex) {
	(void)choiceCount;
	(void)maxTextWidth;
	return Graf.askVerbBubble(Log.modalState().paletteMode,
							  const_cast<byte *>(encoded), selectedIndex);
}

static bool runRawChoiceListModal(const byte *src, uint16 *selectedIndex, uint16 &target) {
	Common::Array<byte> encoded;
	Common::Array<RawModalChoice> rawChoices;
	uint16 choiceCount = 0;
	uint16 maxTextWidth = 0;
	if (!appendRawModalChoices(src, encoded, rawChoices, choiceCount, maxTextWidth))
		return false;

	target = runEncodedChoiceModal(&encoded[0], choiceCount, maxTextWidth, selectedIndex);
	if (!selectedIndex || *selectedIndex == 0xffff || *selectedIndex >= rawChoices.size()) {
		debugC(1, kDebugLevelScript, "raw modal choice cancelled");
		target = 0xffff;
		return true;
	}

	const uint16 rawSelectedIndex = *selectedIndex;
	const RawModalChoice &choice = rawChoices[rawSelectedIndex];
	if (choice.terminal) {
		target = choice.target;
		debugC(1, kDebugLevelScript, "raw modal terminal choice selected index=%u target=0x%04x",
			   rawSelectedIndex, target);
		return true;
	}

	// DOS HandleVerbButton_Submenu stores the raw row target first, then
	// changes the active modal registers to a freshly formatted mode-3
	// bubble unless the row starts with 0x04. Preserve that fallback target
	// when the continuation has no nested selection.
	Logic::FormattedBubble fb = Log.formatBubbleText(choice.line.empty() ? nullptr : &choice.line[0]);
	Logic::ModalState &ms = Log.modalState();
	seedFormattedModalState(ms, fb, fb.totalHeight, fb.rowCount, 3, ms.stashFlag);

	uint16 nestedIndex = 0xffff;
	uint16 nestedTarget = 0xffff;
	if (runFormattedChoiceModal(fb, fb.rowCount, &nestedIndex, nestedTarget)) {
		*selectedIndex = (nestedIndex == 0xffff) ? rawSelectedIndex : nestedIndex;
		target = (nestedTarget == 0xffff) ? choice.target : nestedTarget;
		if (fb.truncated)
			Log.setPendingError(0x11);
		debugC(1, kDebugLevelScript,
			   "raw modal submenu selected raw=%u nested=%u target=0x%04x fallback=0x%04x",
			   rawSelectedIndex, nestedIndex, target, choice.target);
		return true;
	}

	showFormattedModalTextAndWait(fb, fb.totalHeight, CodePointer());
	*selectedIndex = rawSelectedIndex;
	target = choice.target;
	if (fb.truncated)
		Log.setPendingError(0x11);
	debugC(1, kDebugLevelScript, "raw modal choice selected index=%u target=0x%04x",
		   selectedIndex ? *selectedIndex : 0xffff, target);
	return true;
}

static bool runFormattedChoiceModal(const Logic::FormattedBubble &fb,
									uint16 rows, uint16 *selectedIndex, uint16 &target) {
	Common::String visible = stripFormattedRowCenterRecords(fb);
	if (!formattedTextHasMenuOptions(visible))
		return false;

	if (!formattedTextIsPureChoiceList(visible)) {
		target = Graf.askVerbBubbleText(Log.modalState().paletteMode,
										reinterpret_cast<const byte *>(visible.c_str()), selectedIndex);
		debugC(1, kDebugLevelScript, "inline formatted modal choice selected index=%u target=0x%04x",
			   selectedIndex ? *selectedIndex : 0xffff, target);
		return true;
	}

	const uint16 widthPixels = uint16(fb.maxLineWidth + 0x40);
	target = runEncodedChoiceModal(reinterpret_cast<const byte *>(visible.c_str()), MAX<uint16>(1, rows),
								   widthPixels, selectedIndex);
	debugC(1, kDebugLevelScript, "formatted modal choice selected index=%u target=0x%04x",
		   selectedIndex ? *selectedIndex : 0xffff, target);
	return true;
}

static void finishVerbModalLoopState(Logic::ModalState &ms) {
	// RunVerbMenuModalLoop final cleanup clears the menu/button state and
	// palette override, then sets g_flag_logic_dirty/g_flag_change_room/
	// g_flag_refresh_iface before the script resumes. C++ models the repaint
	// side effect with Logic::_logicDirty.
	ms.menuChoiceCount = 0;
	ms.paletteMode = 0;
	ms.textContinuationPtr = 0;
	ms.menuDone = false;
	Log.lockCursorAndButtons(Log.lockedCursorPosition(), 0);
	Log.setLogicDirty();
}

static void applyFormattedTextLimit9bcc(uint16 limit, uint16 &height, uint16 &rows) {
	// Helper @ 1000:9bcc. Ghidra's function decompile currently shows
	// a bogus plain RET, but the raw bytes at the MZ image offset
	// 0x200+0x9bcc disassemble to:
	//   cmp dx,bx; jle ret; push dx/bx/ax; ax=dx; div bl; inc ax;
	//   bx=ax; pop ax; dx=0; div bx; ax+=2; pop bx/dx; dx=bx; ret
	if (int16(rows) > int16(limit)) {
		const uint8 divisor = uint8(limit & 0xff);
		if (divisor != 0) {
			const uint16 pages = uint16(rows / divisor + 1);
			height = uint16(height / pages + 2);
		}
	}
	rows = limit;
}

static void seedFormattedModalState(Logic::ModalState &ms,
									const Logic::FormattedBubble &fb, uint16 menuValue, uint16 rows,
									uint8 paletteMode, uint8 stashFlag) {
	ms.menuChoiceCount = menuValue;
	ms.menuMaxChoices = menuValue;
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = rows;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.activeText = stripFormattedRowCenterRecords(fb);
	ms.paletteMode = paletteMode;
	ms.stashFlag = stashFlag;
	ms.selectedItemIdx = 0xffff;
	ms.textContinuationPtr = 0;
	ms.menuDone = false;
}

#define OPCODE(num) template<> \
Interpreter::OpResult Interpreter::opcodeHandler<num>(ValueVector a, CodePointer current, CodePointer next)

OPCODE(0x00) {
	// nop
	debugC(2, kDebugLevelScript, "opcode 0x00: nop");
	return kThxBye;
}

OPCODE(0x01) {
	// DOS Op_01 @ 1000:59a3. Two paths:
	//   if (_g_block_pc_offset != 0)  // Op_38 has pushed a saved PC
	//       restore saved PC, LoadCodeBlock, RestoreCastBackup,
	//       RestoreActorTableBackup, return  (no break_loop — caller
	//       resumes from its saved PC)
	//   else
	//       g_break_loop = 1; return    (plain script exit)
	//
	// In C++ the snapshot is held in Logic::_savedScene (single slot,
	// matching DOS sentinel `_g_block_pc_offset == 0`). When restored,
	// the caller's _blockProgram/_blockInterpreter/Room are reinstated
	// and the returned CodePointer transfers the dispatcher directly to
	// the saved caller PC. No saved scene means the plain-exit path.
	debugC(2, kDebugLevelScript, "opcode 0x01: exit");
	CodePointer resume = Log.restoreSceneFrame();
	if (!resume.isEmpty())
		return resume;
	return kReturn;
}

OPCODE(0x02) {
	// check equality
	debugC(2, kDebugLevelScript, "opcode 0x02: if %s == %s", +a[0], +a[1]);
	unless(a[0] == a[1]) return kFail;
	return kThxBye;
}

OPCODE(0x03) {
	// check inequality
	debugC(2, kDebugLevelScript, "opcode 0x03: if %s != %s", +a[0], +a[1]);
	if (a[0] == a[1])
		return kFail;
	return kThxBye;
}

OPCODE(0x04) {
	// less than. DOS handler @ 1000:376c uses JL — *signed* comparison via JLE
	// in the inverse: skip when (int)a[1] <= (int)a[0]. Body runs when a[0] < a[1]
	// in signed two's-complement arithmetic. Value::operator< is unsigned so we
	// can't use it here without misclassifying scripts that store signed deltas
	// (e.g. negative scroll offsets, signed timer deltas).
	debugC(2, kDebugLevelScript, "opcode 0x04: if %s < %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) < int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x05) {
	// greater than (signed). DOS Op_05_IfGreater @ 1000:377f uses JG inverse logic.
	debugC(2, kDebugLevelScript, "opcode 0x05: if %s > %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) > int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x06) {
	// less or equal (signed). DOS Op_06_IfLessOrEqual @ 1000:3792 uses JLE — sets skip when (int)a[1] <
	// (int)a[0], i.e. body runs when (int)a[0] <= (int)a[1].
	debugC(2, kDebugLevelScript, "opcode 0x06: if %s <= %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) <= int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x07) {
	// greater or equal (signed). DOS Op_07_IfGreaterOrEqual @ 1000:37a5 uses JGE.
	debugC(2, kDebugLevelScript, "opcode 0x07: if %s >= %s (signed)", +a[0], +a[1]);
	if (!(int16(uint16(a[0])) >= int16(uint16(a[1]))))
		return kFail;
	return kThxBye;
}

OPCODE(0x08) {
	// bit-and check: skip if (a[0] & a[1]) == 0 — succeed if any bit overlaps.
	// DOS handler @ 1000:37b8.
	debugC(2, kDebugLevelScript, "opcode 0x08: if %s & %s", +a[0], +a[1]);
	unless((uint16(a[0]) & uint16(a[1])) != 0) return kFail;
	return kThxBye;
}

OPCODE(0x09) {
	// "either non-zero": skip only when both args are zero.
	// DOS handler @ 1000:37cb — sets g_skip_counter when a[0] == 0 && a[1] == 0.
	debugC(2, kDebugLevelScript, "opcode 0x09: if %s || %s", +a[0], +a[1]);
	unless(uint16(a[0]) != 0 || uint16(a[1]) != 0) return kFail;
	return kThxBye;
}

OPCODE(0x0f) {
	// check room
	debugC(2, kDebugLevelScript, "opcode 0x0f: if current room == %s then", +a[0]);
	unless(a[0] == _logic->currentRoom()) return kFail;
	return kThxBye;
}

OPCODE(0x12) {
	// DOS Op_12_IfSoundDeviceMask @ 1000:392b:
	//   if (((g_music_enabled | g_sfx_enabled) & arg0) == 0) skip;
	// `g_music_enabled` (CS:[0x000d]) and `g_sfx_enabled` (CS:[0x000e]) are
	// config bytes set by ParseConfigAndCmdLine + ParseSwitchString.
	// Ghidra/raw assembly show this opcode only ORs those bytes and ANDs
	// with arg0; it does not query the live ScummVM MIDI backend or mixer.
	const uint16 combined = _engine->dosSoundDeviceMask();
	const uint16 want = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x12: if sound type %u in mask 0x%02x", want, combined);
	if ((combined & want) == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x13) {
	// DOS Op_13_IfHitOrFlagFalse @ 1000:3945: skip if (hit_region != 0) || (step_pending == 0). Body runs
	// when there's an action pending but no hotspot was clicked — i.e. the user
	// pressed something with no target.
	debugC(2, kDebugLevelScript, "opcode 0x13: if pending action with no hit target");
	if (Log.hitTarget() != 0 || !Log.stepPending())
		return kFail;
	return kThxBye;
}

OPCODE(0xd8) {
	// DOS Op_d8_handler @ 1000:542d jumps to RegisterSampleSlot_Bare4
	// (BX=4), whose dispatcher calls CheckScrollDirty @ 1000:7684. It
	// resumes the post-opcode PC only once g_scroll_changed is clear.
	debugC(2, kDebugLevelScript, "opcode 0xd8: wait until scroll clean");
	if (sampleSlotWouldError())
		return kThxBye;
	if (Log.scrollChanged())
		_logic->runLaterWithCurrentMode(current);
	else
		_logic->runLaterWithCurrentMode(next);
	return kReturn;
}

OPCODE(0xda) {
	// Clear the per-room zone list (g_zone_count = 0).
	// DOS handler @ 1000:5467. Pairs with 0xd9 which adds entries.
	debugC(2, kDebugLevelScript, "opcode 0xda: clear zone list");
	Log.zonesClear();
	return kThxBye;
}

OPCODE(0xdc) {
	// Clear g_collision_zone_count (zone-A count, used by FindZoneAtPoint).
	// DOS handler @ 1000:54b8.
	debugC(2, kDebugLevelScript, "opcode 0xdc: clear collision zones");
	Log.collisionZonesClear();
	return kThxBye;
}

OPCODE(0xde) {
	// Clear g_zone_b_count (zone-B count).
	// DOS handler @ 1000:54fd.
	debugC(2, kDebugLevelScript, "opcode 0xde: clear zone-B");
	Log.zonesBClear();
	return kThxBye;
}

OPCODE(0xe2) {
	// Clear g_walkbox_count (DS:0x6617), not the backing table populated by
	// Op_df. DOS SetActorPosition indexes the backing memory directly, so
	// records beyond the current count deliberately retain stale values.
	// DOS handler @ 1000:5582.
	debugC(2, kDebugLevelScript, "opcode 0xe2: clear walkbox count");
	Log.actorFramesClearCount();
	return kThxBye;
}

OPCODE(0xf6) {
	// Set music volume to maximum. DOS handler @ 1000:5824 patches the music driver
	// state bytes directly to 0xff (volume) and 0x3f / 0 (mode-dependent flag).
	debugC(2, kDebugLevelScript, "opcode 0xf6: max music volume");
	const uint8 dosMusicMode = _engine->dosMusicEnabled();
	if (Music.isActive() && dosMusicMode != 0)
		Music.setMaxVolume(dosMusicMode);
	return kThxBye;
}

OPCODE(0xf8) {
	// Stop all music AND sfx (panic stop).
	// DOS handler @ 1000:5889 calls the music driver's "stop" entry, clears
	// g_current_tune_addr, then calls the sfx driver's "stop" if active.
	debugC(2, kDebugLevelScript, "opcode 0xf8: stop all music/sfx");
	if (_engine->dosMusicEnabled() != 0)
		Music.stopMusic();
	if (Sound *snd = _engine->sound())
		if (snd->isEnabled() && snd->isSfxPlaying())
			snd->stopAll();
	return kThxBye;
}

OPCODE(0x10) {
	// DOS Op_10_IfTimerExpired @ 1000:3903.
	//   ResolveOpcodeArg0 → AX
	//   if (AX == 0) skip
	//   if (AX > tick (signed JG)) skip
	//   else { StoreOpcodeArg0Value(0); run; }
	// SIGNED comparison. Pairs with Op_ed which writes the deadline.
	// C++ writes 0 back via `a[0] = 0` — works when arg0 is a
	// WordVariable/ByteVariable (reaches _ptr); no-op for Constant.
	int16 deadline = int16(uint16(a[0]));
	int16 now = int16(uint16(Log.frameTicks()));
	if (deadline != 0 && deadline <= now) {
		debugC(2, kDebugLevelScript, "opcode 0x10: timer fired (deadline=%d tick=%d)", deadline, now);
		a[0] = 0;
		return kThxBye;
	}
	debugC(3, kDebugLevelScript, "opcode 0x10: timer pending (deadline=%d tick=%d)", deadline, now);
	return kFail;
}

OPCODE(0x11) {
	// "if slow CPU" — body executes only when the startup calibration
	// set g_slow_cpu. DOS handler @ 1000:391d skips if DS:0x67b5 == 0.
	debugC(2, kDebugLevelScript, "opcode 0x11: if slow CPU");
	if (!Log.slowCpu())
		return kFail;
	return kThxBye;
}

OPCODE(0x17) {
	// DOS Op_17_IfExitMissing @ 1000:3996. Reads `exit_record[0]`
	// (the room field — kOffsetRoom = 0 in C++ Exit) at SI =
	// GetExitOffset(arg0); skips if it equals 0. GetExitOffset only
	// errors on id 0; it does not check against the loaded exit count.
	debugC(1, kDebugLevelScript, "opcode 0x17: if exit %s exists", +a[0]);
	uint16 room = 0;
	if (!_logic->blockProgram() || !_logic->blockProgram()->getExitRoomWord(uint16(a[0]), room)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	if (room == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x19) {
	// DOS Op_19_IfActorMissing @ 1000:39bc: skip when actor.field+0x59
	// (room) == 0 → body runs when actor IS placed somewhere.
	// Sets implicit actor (SI side-effect of GetActorOffset).
	debugC(1, kDebugLevelScript, "opcode 0x19: if actor %s in some room", +a[0]);
	Actor *ac = Log.getActor(a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1a) {
	// DOS Op_1a_IfExitPresent @ 1000:39d0. Inverse of 0x17: skips
	// when `exit_record[0] != 0` (exit room is set → exit "present").
	// Body runs when exit room == 0.
	debugC(1, kDebugLevelScript, "opcode 0x1a: if exit %s missing", +a[0]);
	uint16 room = 0;
	if (!_logic->blockProgram() || !_logic->blockProgram()->getExitRoomWord(uint16(a[0]), room)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	if (room != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1c) {
	// DOS Op_1c_IfActorPresent @ 1000:39f6: skip when actor.field+0x59
	// (room) != 0 → body runs when actor is MISSING. Inverse of 0x19.
	// Sets implicit actor (SI side-effect of GetActorOffset).
	debugC(1, kDebugLevelScript, "opcode 0x1c: if actor %s not placed", +a[0]);
	Actor *ac = Log.getActor(a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x1d) {
	// DOS Op_1d_IfActorAtRoomFrame @ 1000:3a10: arg1 = actor id,
	// arg0 = frame. Run if actor.room == current_loc AND
	// actor.frame == arg0. Sets implicit actor.
	debugC(1, kDebugLevelScript, "opcode 0x1d: if actor %s in current room AND at %s", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) != uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x1f) {
	// DOS Op_1f_IfActorNotAtRoomFrame @ 1000:3a39: arg1 = actor id,
	// arg0 = frame. Run if actor.room == current_loc AND
	// actor.frame != arg0. Sets implicit actor.
	debugC(1, kDebugLevelScript, "opcode 0x1f: if actor %s is in current room but not at %s then", +a[1], +a[0]);
	Actor *ac = Log.getActor(a[1]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) == uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x24) {
	// DOS Op_24_IfArgNonZero @ 1000:3ae1.
	debugC(2, kDebugLevelScript, "opcode 0x24: if (%s)", +a[0]);
	if (a[0] == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x25) {
	// DOS Op_25_IfArgZero @ 1000:3aef.
	debugC(2, kDebugLevelScript, "opcode 0x25: if not (%s)", +a[0]);
	if (a[0] != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x2c) {
	// else
	debugC(2, kDebugLevelScript, "opcode 0x2c: else");

	return kElse;
}

OPCODE(0x2d) {
	// end if
	debugC(2, kDebugLevelScript, "opcode 0x2d: end if");
	return kEndIf;
}

OPCODE(0x35) {
	// jump
	debugC(2, kDebugLevelScript, "opcode 0x35: jump to %s", +a[0]);
	return static_cast<CodePointer &>(a[0]);
}

OPCODE(0x36) {
	// DOS Op_36_Call @ 1000:3bf8:
	//   if (call_depth < 8) {
	//       stack[depth*4]   = g_codeptr_di_save;
	//       stack[depth*4+2] = g_branch_state;
	//       depth++;
	//       g_branch_state = 0;
	//       g_codeptr_di_save = arg0;
	//   } else g_pendingErrorCode = 5;
	// C++ uses native recursion for PC save/restore; we enforce the
	// same depth limit (and the branch_state save/restore around the
	// inner script).
	if (Log.callDepth() >= 8) {
		Log.setPendingError(0x05);
		return kThxBye;
	}
	const uint16 savedBranch = Log.branchState();
	Log.setBranchState(0);
	Log.setCallDepth(uint8(Log.callDepth() + 1));
	debugC(2, kDebugLevelScript, ">>>opcode 0x36: call procedure %s (depth=%u)", +a[0], Log.callDepth());
	CodePointer &p = static_cast<CodePointer &>(a[0]);
	p.run();
	debugC(2, kDebugLevelScript, "<<<opcode 0x36: returned (depth=%u)", Log.callDepth() - 1);
	Log.setCallDepth(uint8(Log.callDepth() - 1));
	Log.setBranchState(savedBranch);
	return kThxBye;
}

OPCODE(0x37) {
	// DOS Op_37_PopCaseStack @ 1000:3c2e:
	//   if (call_depth != 0) { depth--; PC = saved; branch_state = saved; }
	//   else g_pendingErrorCode = 6;
	// C++ side: depth tracking is symmetric with Op_36 — call_depth was
	// incremented at Op_36 entry and the inner Interpreter::run's
	// kReturn pops out (matching DOS's PC restore). Op_37 itself just
	// signals "end of procedure" via kReturn; the outer Op_36 then
	// decrements the counter and restores branch_state.
	// Underflow (Op_37 with no active call) → pending-error 6.
	if (Log.callDepth() == 0) {
		Log.setPendingError(0x06);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x37: return");
	return kReturn;
}

OPCODE(0x39) {
	// DOS Op_39_DeferRunMain @ 1000:3c88:
	//   Op_3a_CancelDeferredMain();   // remove any matching slot
	//   for slot in queue[0..7]:
	//       if slot.mode == 0:
	//           slot.mode = 0xb + idx;
	//           slot.code_offset = g_resourceSegment;
	//           slot.code_segment = arg0;
	//           return;
	//   g_pendingErrorCode = 0x1e;    // queue overflow
	debugC(2, kDebugLevelScript, "opcode 0x39: execute main %s later", +a[0]);
	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), _logic->mainInterpreter());
	const bool selfCancel = _logic->cancelDeferred(p);
	if (!_logic->queueDeferred(p)) {
		Log.setPendingError(0x1e);
		return kThxBye;
	}
	return selfCancel ? kReturn : kThxBye;
}

OPCODE(0x3b) {
	// DOS Op_3b_DeferRunBlock @ 1000:3c7f: same shape as Op_39 but
	// stores the current code segment (`g_codeptr_es_save`) instead of
	// the resource segment. Slot-cap 8; pending-error 0x1e on overflow.
	debugC(2, kDebugLevelScript, "opcode 0x3b: execute %s later", +a[0]);
	CodePointer p(static_cast<CodePointer &>(a[0]));
	const bool selfCancel = _logic->cancelDeferred(p);
	if (!_logic->queueDeferred(p)) {
		Log.setPendingError(0x1e);
		return kThxBye;
	}
	return selfCancel ? kReturn : kThxBye;
}

OPCODE(0x3d) {
	// DOS Op_3d_SetEscapeBreakPoint @ 1000:3d0b:
	//   g_break_target_proc = g_opcode_mode;      ; current dispatch mode
	//   g_break_target_seg  = g_codeptr_es_save;  ; target/current segment
	//   g_break_target_off  = arg0;               ; target offset
	//   g_esc_during_script = 1;                  ; flag (= !skipPoint.isEmpty in C++)
	// C++ collapses the DOS segment:offset target into CodePointer. When ESC
	// is handled, proc<0xb runs the target inline; proc>=0xb redirects the
	// deferred queue entry for that mode to the target.
	const uint16 srcPC = current.offset();
	debugC(2, kDebugLevelScript, "opcode 0x3d: ESC break (mode=%u srcPC=0x%04x → %s)",
		   Log.opcodeMode(), srcPC, +a[0]);
	Log.setEscBreakPoint(Log.opcodeMode(), srcPC, static_cast<CodePointer &>(a[0]));
	return kThxBye;
}

OPCODE(0x41) {
	// DOS Op_41_SpeakAsMainNoTarget @ 1000:3dae. Protagonist speaks;
	// if already speaking or moving, defer the dispatch via the
	// actor's silent/still callback.
	debugC(2, kDebugLevelScript, "opcode 0x41: protag says %s", +a[0]);
	Actor *protag = Log.protagonist();
	const SpeechDeferResult speechWait =
		deferMainSpeechNoTargetUntilReady(protag, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(protag, a[0]);
	return kThxBye;
}

OPCODE(0x43) {
	// DOS Op_43_SpeakAsActor @ 1000:3e10: arg0=actor id, arg1=text.
	const Common::String text = a[1];
	const uint16 actorId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x43: actor %u says %s",
		   actorId, text.c_str());
	if (actorId == Log.protagonistId()) {
		Actor *protag = Log.protagonist();
		if (!Log.inStatusMode()) {
			const SpeechDeferResult speechSlotWait = waitForActiveSpeechSlotOwner(actorId, current);
			if (speechSlotWait == kSpeechWaitError)
				return kThxBye;
			if (speechSlotWait == kSpeechWait)
				return kReturn;
		}
		if (failIfMainActorMissingForNonStatusSpeech(protag))
			return kThxBye;
		const SpeechDeferResult speechWait = deferSpeechUntilReady(protag, current);
		if (speechWait == kSpeechWaitError)
			return kThxBye;
		if (speechWait == kSpeechWait)
			return kReturn;
		speakOrSubtitle(protag, text);
		return kThxBye;
	}

	const SpeechDeferResult speechSlotWait = waitForActiveSpeechSlotOwner(actorId, current);
	if (speechSlotWait == kSpeechWaitError)
		return kThxBye;
	if (speechSlotWait == kSpeechWait)
		return kReturn;
	Actor *ac = getActorOrPending(actorId);
	if (!ac)
		return kThxBye;
	const SpeechDeferResult speechWait = deferSpeechUntilReady(ac, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(ac, text);
	Log.stashUiTextSpeechSlotForOwner(actorId);
	return kThxBye;
}

OPCODE(0x47) {
	// DOS Op_47_SpeakWithRect @ 1000:3eb6: 5 args (x, y, color, lines, text).
	//   if (g_in_status_mode == 0) AllocSpeechSlot_NoFormatting +
	//       stash arg2 in g_unknown_669a;
	//   else CheckSubtitleActive → RegisterSampleSlot_LoadDefaultsB or
	//       QueueDeferredFormattedText.
	// AllocSpeechSlot_NoFormatting allocates a NARRATOR bubble slot
	// (no actor — bubble at the explicit (x,y) position with the
	// given color). C++ uses Graphics::sayAt to render at (x, y)
	// with the color arg.
	const byte *text = static_cast<byte *>(a[4]);
	const uint16 maxLines = uint16(a[3]);
	const uint16 x = uint16(a[0]);
	const uint16 y = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	debugC(1, kDebugLevelScript, "opcode 0x47: narrator at (%u,%u) color=%u lines=%u text='%s'",
		   x, y, color, maxLines, text ? reinterpret_cast<const char *>(text) : "(null)");
	if (sayNarratorOrSubtitle(text, x, y, color, maxLines, Graphics::kSpeechBubbleType1, current))
		return kReturn;
	return kThxBye;
}

OPCODE(0x4a) {
	// DOS Op_4a_RegisterSampleByStatusMode @ 1000:3ed5: dispatches to
	// `RegisterSampleSlot_Bare2` (status mode, BX=3 subtitle predicate) or
	// `_Bare9` after loading AX=g_main_character_id (non-status, BX=1 speech
	// slot owner predicate). Both save the next PC through
	// `RegisterSampleSlot_Common` @ 1000:3154.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x4a: wait protag silent (status=%d)", Log.inStatusMode() ? 1 : 0);
	if (Log.inStatusMode()) {
		if (waitForSubtitle(next))
			return kReturn;
		Log.runLaterWithCurrentMode(next, 0);
		return kReturn;
	}
	if (waitForSpeechSlotOwner(Log.protagonistId(), next))
		return kReturn;
	Log.runLaterWithCurrentMode(next, 0);
	return kReturn;
}

OPCODE(0x4b) {
	// DOS Op_4b_RegisterSampleIfMainChar @ 1000:3ee7:
	//   non-status: ResolveOpcodeArg0; RegisterSampleSlot_Bare9 (always).
	//   status: compares the incoming dispatch-loop AX (the opcode byte 0x4b)
	//        with g_main_character_id; only equality reaches
	//        RegisterSampleSlot_Bare2. No arg0 resolution occurs there.
	// Both registrations land in RegisterSampleSlot_Common with the
	// branch_state/call_depth check and save the next PC.
	if (Log.inStatusMode()) {
		if (Log.protagonistId() != 0x4b)
			return kThxBye;
	} else {
		const uint16 owner = uint16(a[0]);
		if (Log.branchState() != 0 || Log.callDepth() != 0) {
			Log.setPendingError(0x39);
			return kThxBye;
		}
		debugC(2, kDebugLevelScript, "opcode 0x4b: wait actor %u silent", owner);
		if (waitForSpeechSlotOwner(owner, next))
			return kReturn;
		Log.runLaterWithCurrentMode(next, 0);
		return kReturn;
	}

	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x4b: wait status subtitle if main id is opcode byte");
	if (waitForSubtitle(next))
		return kReturn;
	Log.runLaterWithCurrentMode(next, 0);
	return kReturn;
}

OPCODE(0x54) {
	// DOS Op_54_RunMenuSelectAndBranch @ 1000:4011:
	//   CALL ResolveOpcodeArg4; MOV DI, AX   ; arg4 = option text
	//   MOV AX, [0x6712]                     ; AX = current code segment
	//   MOV ES, AX
	//   PUSH ES; PUSH DI                ; save (es:di) for modal text
	//   CALL ResolveOpcodeArg0; MOV CX, AX   ; arg0 = left
	//   CALL ResolveOpcodeArg1; MOV DX, AX   ; arg1 = top
	//   CALL ResolveOpcodeArg3; MOV BX, AX   ; arg3 = height/rows
	//   CALL ResolveOpcodeArg2;              ; arg2 = width/cols
	//   POP DI; POP ES                       ; restore es:di
	//   CALL RunModalLoop @ 1000:7ea2         ; modal!
	//   MOV AX, [0x66a2]                     ; selected item index
	//   CMP AX, 0xffff
	//   JZ skip                              ; cancelled -> keep next PC
	//   ; AX is valid index -> look up g_menu_item_indices[AX]:
	//   PUSH ds; POP DS; MOV SI, 0x4f1b
	//   ADD AX, AX; ADD SI, AX               ; SI = &indices[AX]
	//   MOV AX, [SI]                         ; AX = target code offset
	//   MOV [0x6710], AX                     ; g_codeptr_di_save = target
	//   skip: RET.
	//
	// = "show modal menu with option text (arg4) at (arg0, arg1) with
	// width/height hints (arg2/arg3); if user picks an item, branch by
	// writing the looked-up code offset to g_codeptr_di_save."
	//
	// C++ port: uses Graphics::ask which already implements a modal
	// bubble-frame text picker (matches DOS RunModalLoop semantics for
	// our purposes: polls events, hit-tests options, returns choice).
	// The result of Graphics::ask is `_optionValues[selected]` which
	// IS already the "looked-up index"; Graphics::ask integrates the
	// indices lookup into its option-rendering path, so we don't need
	// to re-apply the g_menu_item_indices mapping.
	byte *text = static_cast<byte *>(a[4]);
	const uint16 left = uint16(a[0]);
	const uint16 top = uint16(a[1]);
	const uint8 height = uint8(uint16(a[3]) & 0xff);
	const uint8 width = uint8(uint16(a[2]) & 0xff);
	debugC(1, kDebugLevelScript,
		   "opcode 0x54: RunMenuSelectAndBranch text='%s' at (%u,%u) size %ux%u",
		   text ? reinterpret_cast<const char *>(text) : "(null)",
		   left, top, width, height);
	uint16 selectedIndex = 0xffff;
	const uint16 result = _graphics->ask(left, top, width, height, text, &selectedIndex);
	Logic::ModalState &ms = Log.modalState();
	Log.setLogicDirty();
	if (result == 0xffff) {
		// User cancelled. DOS leaves g_menu_selected_item at 0xffff and
		// keeps the next interpreter PC unchanged.
		ms.selectedItemIdx = 0xffff;
		debugC(2, kDebugLevelScript, "opcode 0x54: modal cancelled");
		return kThxBye;
	}
	// Valid selection. Preserve DOS g_menu_selected_item in ModalState;
	// Graphics::ask returns the already-looked-up code offset.
	ms.selectedItemIdx = selectedIndex;
	return CodePointer(result, this);
}

OPCODE(0x55) {
	// DOS Op_55_DrawFormattedText @ 1000:404f: resolves arg3 text first,
	// runs PrepareTextStrippedForRender @ 1000:97d3 into the temporary text
	// buffer, then draws it at arg0/arg1 with arg2 color.
	const byte *text = static_cast<byte *>(a[3]);
	const byte *rawPointer = a[3].rawPointer();
	const byte *rawText = rawPointer ? rawPointer : text;
	bool truncated = false;
	Common::String prepared = _logic->prepareTextStrippedForRender(rawText, &truncated);
	const uint16 left = uint16(a[0]);
	const uint16 top = uint16(a[1]);
	const byte colour = uint8(uint16(a[2]) & 0xff);
	debugC(2, kDebugLevelScript, "opcode 0x55: paint prepared '%s' with colour %u at %u:%u",
		   prepared.c_str(), colour, left, top);
	if (Log.inStatusMode())
		_graphics->rememberStatusScreenText(left, top, colour, prepared);
	_graphics->paintTextOneDirty(left, top, colour,
								 reinterpret_cast<const byte *>(prepared.c_str()));
	if (truncated)
		Log.setPendingError(0x19);
	return kThxBye;
}

OPCODE(0x56) {
	// DOS Op_56_SendActorToTargetOrWait @ 1000:4069: 2 args.
	//   CheckMovementBlocked() tests g_unknown_66d6, the countdown used by
	//   UpdateActorMotion's transient text renderer.
	//   if countdown active: RegisterSampleSlot_LoadDefaultsD; RET;
	//   else:
	//       g_unknown_66d6 = arg0;      // frames
	//       DAT_1cb5_66d8  = current code segment
	//       DAT_1cb5_66da  = arg1;      // raw text/control string
	//
	// Earlier C++ treated this as a protagonist walk request and queued `next`
	// immediately. In the intro that advanced room-84's script through Op_57
	// and Op_d0 in the same queued pass. Model the DOS countdown/text gate
	// instead; queued-pass isolation in Logic::runQueued() handles the
	// RegisterSampleSlot side of the wait path.
	if (Log.motionTextActive()) {
		if (Log.branchState() != 0 || Log.callDepth() != 0) {
			Log.setPendingError(0x39);
			return kThxBye;
		}
		_logic->runLaterWithCurrentMode(current, 0);
		return kReturn;
	}
	const uint16 frames = uint16(a[0]);
	const byte *translatedText = static_cast<byte *>(a[1]);
	const byte *rawText = a[1].rawPointer();
	const byte *text = rawText ? rawText : translatedText;
	const uint16 textLength = rawText ? a[1].rawLength() : 0;
	debugC(2, kDebugLevelScript, "opcode 0x56: motion text frames=%u text='%s'",
		   frames, translatedText ? reinterpret_cast<const char *>(translatedText) : "(null)");
	Log.startMotionText(frames, text, textLength);
	return kThxBye;
}

OPCODE(0x57) {
	// DOS Op_57_RegisterSampleSlotDirect @ 1000:4084: 0 args.
	// Jumps to RegisterSampleSlot_Bare7 (BX=0x0a), so the saved next PC
	// resumes only when CheckMovementBlocked reports the motion-text
	// countdown at DS:0x66d6 is clear.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x57: wait for motion text clear");
	if (Log.motionTextActive()) {
		_logic->runLaterWithCurrentMode(current, 0);
		return kReturn;
	}
	_logic->runLaterWithCurrentMode(next, 0);
	return kReturn;
}

static inline uint8 wordRecordByte(uint16 value, uint8 baseOff, uint8 off) {
	return uint8((value >> ((off - baseOff) * 8)) & 0xff);
}

static inline uint16 wordRecordWithByte(uint16 oldValue, uint8 baseOff, uint8 off, uint8 value) {
	const uint shift = uint(off - baseOff) * 8;
	return uint16((oldValue & ~(0xffu << shift)) | (uint16(value) << shift));
}

static uint8 exitRecordByte(Logic *logic, Exit *exit, uint16 id, uint8 off) {
	if (off == 0 || off == 1)
		return wordRecordByte(exit->room(), 0, off);
	if (off == 2 || off == 3)
		return wordRecordByte(uint16(exit->position().x), 2, off);
	if (off == 4 || off == 5)
		return wordRecordByte(uint16(exit->position().y), 4, off);
	if (off == 6 || off == 7)
		return wordRecordByte(exit->spriteField(), 6, off);
	if (off == 0x0a)
		return exit->noSprite() ? 1 : 0;
	if (off == 0x0b)
		return exit->zIndex();
	return logic->exitField(id, off);
}

static uint16 exitRecordSizedLowWord(Logic *logic, Exit *exit, uint16 id, uint8 off, uint8 sz) {
	const uint8 lo = exitRecordByte(logic, exit, id, off);
	if (sz == 1)
		return lo;
	return uint16(lo) | (uint16(exitRecordByte(logic, exit, id, uint8(off + 1))) << 8);
}

static void writeExitRecordByte(Logic *logic, Exit *exit, uint16 id, uint8 off, uint8 value) {
	if (off == 0 || off == 1) {
		exit->setRoom(wordRecordWithByte(exit->room(), 0, off, value));
		return;
	}
	if (off == 2 || off == 3) {
		Common::Point p = exit->position();
		p.x = int16(wordRecordWithByte(uint16(p.x), 2, off, value));
		exit->setPosition(p);
		return;
	}
	if (off == 4 || off == 5) {
		Common::Point p = exit->position();
		p.y = int16(wordRecordWithByte(uint16(p.y), 4, off, value));
		exit->setPosition(p);
		return;
	}
	if (off == 6 || off == 7) {
		exit->setSpriteField(wordRecordWithByte(exit->spriteField(), 6, off, value));
		return;
	}
	if (off == 0x0a) {
		exit->setNoSprite(value != 0);
		return;
	}
	if (off == 0x0b) {
		exit->setZIndex(value);
		return;
	}
	logic->setExitField(id, off, value);
}

static void writeExitRecordSizedLowWord(Logic *logic, Exit *exit, uint16 id, uint8 off,
										uint8 sz, uint16 lowWord, uint16 highWord) {
	writeExitRecordByte(logic, exit, id, off, uint8(lowWord & 0xff));
	if (sz == 1)
		return;
	writeExitRecordByte(logic, exit, id, uint8(off + 1), uint8(lowWord >> 8));
	if (sz == 4) {
		writeExitRecordByte(logic, exit, id, uint8(off + 2), uint8(highWord & 0xff));
		writeExitRecordByte(logic, exit, id, uint8(off + 3), uint8(highWord >> 8));
	}
}

static uint8 objectRecordByte(Logic *logic, uint16 id, uint8 off) {
	if (off == 0 || off == 1)
		return wordRecordByte(logic->getObjectRoom(id), 0, off);
	if (off == 2 || off == 3)
		return wordRecordByte(uint16(logic->getObjectPosX(id)), 2, off);
	if (off == 4 || off == 5)
		return wordRecordByte(uint16(logic->getObjectPosY(id)), 4, off);
	return logic->objectField(id, off);
}

static uint16 objectRecordSizedLowWord(Logic *logic, uint16 id, uint8 off, uint8 sz) {
	const uint8 lo = objectRecordByte(logic, id, off);
	if (sz == 1)
		return lo;
	return uint16(lo) | (uint16(objectRecordByte(logic, id, uint8(off + 1))) << 8);
}

static void writeObjectRecordByte(Logic *logic, uint16 id, uint8 off, uint8 value) {
	if (off == 0 || off == 1) {
		logic->setObjectRoom(id, wordRecordWithByte(logic->getObjectRoom(id), 0, off, value));
		return;
	}
	if (off == 2 || off == 3) {
		const uint16 x = wordRecordWithByte(uint16(logic->getObjectPosX(id)), 2, off, value);
		logic->setObjectPosition(id, int16(x), logic->getObjectPosY(id));
		return;
	}
	if (off == 4 || off == 5) {
		const uint16 y = wordRecordWithByte(uint16(logic->getObjectPosY(id)), 4, off, value);
		logic->setObjectPosition(id, logic->getObjectPosX(id), int16(y));
		return;
	}
	logic->setObjectField(id, off, value);
}

static void writeObjectRecordSizedLowWord(Logic *logic, uint16 id, uint8 off,
										  uint8 sz, uint16 lowWord, uint16 highWord) {
	writeObjectRecordByte(logic, id, off, uint8(lowWord & 0xff));
	if (sz == 1)
		return;
	writeObjectRecordByte(logic, id, uint8(off + 1), uint8(lowWord >> 8));
	if (sz == 4) {
		writeObjectRecordByte(logic, id, uint8(off + 2), uint8(highWord & 0xff));
		writeObjectRecordByte(logic, id, uint8(off + 3), uint8(highWord >> 8));
	}
}

static uint8 actorRecordByte(Actor *actor, uint8 off) {
	if (off == Actor::kOffsetLeft || off == Actor::kOffsetLeft + 1)
		return wordRecordByte(uint16(actor->position().x), Actor::kOffsetLeft, off);
	if (off == Actor::kOffsetTop || off == Actor::kOffsetTop + 1)
		return wordRecordByte(uint16(actor->position().y), Actor::kOffsetTop, off);
	if (off == Actor::kOffsetMainSprite || off == Actor::kOffsetMainSprite + 1)
		return wordRecordByte(actor->mainSpriteId(), Actor::kOffsetMainSprite, off);
	if (off == Actor::kOffsetTicksLeft || off == Actor::kOffsetTicksLeft + 1)
		return wordRecordByte(uint16(actor->ticksLeft()), Actor::kOffsetTicksLeft, off);
	if (off == Actor::kOffsetInterval)
		return actor->interval();
	if (off == Actor::kOffsetActorCallbackSegment || off == Actor::kOffsetActorCallbackSegment + 1)
		return wordRecordByte(actor->actorCallbackSeg(), Actor::kOffsetActorCallbackSegment, off);
	if (off == Actor::kOffsetActorCallbackOffset || off == Actor::kOffsetActorCallbackOffset + 1)
		return wordRecordByte(actor->actorCallbackOff(), Actor::kOffsetActorCallbackOffset, off);
	if (off == Actor::kOffsetRoom || off == Actor::kOffsetRoom + 1)
		return wordRecordByte(actor->room(), Actor::kOffsetRoom, off);
	if (off == Actor::kOffsetFrame)
		return uint8(actor->frameId());
	if (off == Actor::kOffsetTargetFrame)
		return uint8(actor->targetFrameId());
	if (off == Actor::kOffsetAttentionNeeded && actor->isMoving())
		return 1;
	return actor->field(off);
}

static uint16 actorRecordSizedLowWord(Actor *actor, uint8 off, uint8 sz) {
	const uint8 lo = actorRecordByte(actor, off);
	if (sz == 1)
		return lo;
	return uint16(lo) | (uint16(actorRecordByte(actor, uint8(off + 1))) << 8);
}

static void writeActorRecordByte(Actor *actor, uint8 off, uint8 value) {
	actor->setField(off, value);
	if (off == Actor::kOffsetLeft || off == Actor::kOffsetLeft + 1) {
		Common::Point p = actor->position();
		p.x = int16(wordRecordWithByte(uint16(p.x), Actor::kOffsetLeft, off, value));
		actor->setRawPosition(p);
		return;
	}
	if (off == Actor::kOffsetTop || off == Actor::kOffsetTop + 1) {
		Common::Point p = actor->position();
		p.y = int16(wordRecordWithByte(uint16(p.y), Actor::kOffsetTop, off, value));
		actor->setRawPosition(p);
		return;
	}
	if (off == Actor::kOffsetMainSprite || off == Actor::kOffsetMainSprite + 1) {
		const uint16 sprite = wordRecordWithByte(actor->mainSpriteId(), Actor::kOffsetMainSprite, off, value);
		actor->setRawMainSprite(sprite);
		return;
	}
	if (off == Actor::kOffsetTicksLeft || off == Actor::kOffsetTicksLeft + 1) {
		const uint16 ticks = wordRecordWithByte(uint16(actor->ticksLeft()), Actor::kOffsetTicksLeft, off, value);
		actor->setRawTicksLeft(ticks);
		return;
	}
	if (off == Actor::kOffsetInterval) {
		actor->setRawInterval(value);
		return;
	}
	if (off == Actor::kOffsetActorCallbackSegment || off == Actor::kOffsetActorCallbackSegment + 1) {
		actor->setActorCallback(wordRecordWithByte(actor->actorCallbackSeg(), Actor::kOffsetActorCallbackSegment, off, value),
								actor->actorCallbackOff());
		return;
	}
	if (off == Actor::kOffsetActorCallbackOffset || off == Actor::kOffsetActorCallbackOffset + 1) {
		actor->setActorCallback(actor->actorCallbackSeg(),
								wordRecordWithByte(actor->actorCallbackOff(), Actor::kOffsetActorCallbackOffset, off, value));
		return;
	}
	if (off == Actor::kOffsetRoom || off == Actor::kOffsetRoom + 1) {
		actor->forceRoom(wordRecordWithByte(actor->room(), Actor::kOffsetRoom, off, value));
		return;
	}
	if (off == Actor::kOffsetFrame) {
		actor->setRawFrame(value);
		return;
	}
	if (off == Actor::kOffsetTargetFrame) {
		actor->setRawTargetFrame(value);
		return;
	}
}

static void writeActorRecordSizedLowWord(Actor *actor, uint8 off,
										 uint8 sz, uint16 lowWord, uint16 highWord) {
	if (sz != 1 && off == Actor::kOffsetMainSprite) {
		actor->setField(off, uint8(lowWord & 0xff));
		actor->setField(uint8(off + 1), uint8(lowWord >> 8));
		actor->setRawMainSprite(lowWord);
		if (sz == 4) {
			writeActorRecordByte(actor, uint8(off + 2), uint8(highWord & 0xff));
			writeActorRecordByte(actor, uint8(off + 3), uint8(highWord >> 8));
		}
		return;
	}
	writeActorRecordByte(actor, off, uint8(lowWord & 0xff));
	if (sz == 1)
		return;
	writeActorRecordByte(actor, uint8(off + 1), uint8(lowWord >> 8));
	if (sz == 4) {
		writeActorRecordByte(actor, uint8(off + 2), uint8(highWord & 0xff));
		writeActorRecordByte(actor, uint8(off + 3), uint8(highWord >> 8));
	}
}

static uint16 blockSegmentTag(Logic *logic) {
	return uint16(0x4000 + (logic->currentBlock() & 0x3fff));
}

static uint16 valueHighWordForSizedWrite(Logic *logic, Value &value) {
	if (!value.holdsCode())
		return 0;
	CodePointer &ptr = static_cast<CodePointer &>(value);
	if (ptr.interpreter() == logic->mainInterpreter())
		return 0x1cb5;
	if (ptr.interpreter() == logic->blockInterpreter())
		return blockSegmentTag(logic);
	return 0;
}

static bool dosPositiveIdExceedsMax(uint16 id, uint16 maxId) {
	return int16(id) > int16(maxId);
}

static bool dosIdIsNonPositive(uint16 id) {
	return int16(id) <= 0;
}

OPCODE(0x60) {
	// DOS Op_60_handler @ 1000:40c4: sets g_walk_speed_flag=1, then
	// jumps into Op_5f's shared table lookup. The flag makes
	// ResolveOpcodeArg0 / DS select g_seg_buffer_e (block bank), not
	// the currently executing script bank.
	Log.setWalkSpeedFlag(1);
	const uint16 searchKey = uint16(a[1]);
	const uint16 fieldOffset = uint16(a[2]);
	const uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	uint16 value = 0xffff;
	byte *pos = _logic->blockInterpreter() ? _logic->blockInterpreter()->rawCode(offset) : nullptr;
	if (!pos) {
		a[3] = value;
		return kThxBye;
	}
	const uint16 width = READ_LE_UINT16(pos);
	pos += 2;
	while (true) {
		const uint16 index = READ_LE_UINT16(pos);
		if (index == 0xffff) {
			value = index;
			break;
		}
		pos += 2;
		if (index == searchKey) {
			value = READ_LE_UINT16(pos + fieldOffset);
			break;
		}
		pos += width * 2;
	}

	a[3] = value;
	debugC(2, kDebugLevelScript, "opcode 0x60: table lookup arg0=0x%04x search=%u field=%u -> %u",
		   offset, searchKey, fieldOffset, value);
	return kThxBye;
}

OPCODE(0x63) {
	// DOS Op_63_ReadActorField @ 1000:4139:
	//   1. ResolveOpcodeArg0 (actor id);
	//   2. if (id > g_anim_count_max) → pending-error 0x13;
	//   3. GetActorOffset(id) → ES:SI;
	//   4. ValidateTypeAndWriteVar2: ResolveOpcodeArg1 → AX (low=offset,
	//      high=size); size must be 1/2/4 else pending-error 2;
	//      load byte/word fields from ES:[SI+off] into BX; for size 4,
	//      DOS first loads BX from ES:[SI+off], then loads CX from
	//      ES:[SI + BX + 2].
	//      WriteVarBySlot2_LHS chooses arg2's destination width and writes
	//      BX, plus CX only when arg2's slot is 4 bytes wide.
	// = "READ a sized field from actor record, store in arg2 LHS".
	const uint16 id = uint16(a[0]);
	const uint16 mainActors = _logic->resources()->mainDat()->actorsCount();
	const uint16 blockActors = _logic->blockProgram() ? _logic->blockProgram()->actorsCount() : 0;
	const uint16 maxActor = mainActors + blockActors;
	if (dosPositiveIdExceedsMax(id, maxActor)) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x17);
		recordId = 1;
	}
	Actor *actor = _logic->getActor(recordId);
	if (!actor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	const uint16 value = actorRecordSizedLowWord(actor, off, sz);
	a[2] = value;
	debugC(2, kDebugLevelScript, "opcode 0x63: actor[%u].field[+0x%02x size=%u] -> %u",
		   id, off, sz, value);
	return kThxBye;
}

OPCODE(0x6c) {
	// add: a[0] += a[1]
	// DOS handler @ 1000:42b1.
	const uint16 left = uint16(a[0]);
	const uint16 right = uint16(a[1]);
	const uint16 result = uint16(left + right);
	a[0] = result;
	debugC(2, kDebugLevelScript, "opcode 0x6c: %u += %u -> %u", left, right, result);
	return kThxBye;
}

OPCODE(0x6d) {
	// increment
	const uint16 oldValue = uint16(a[0]);
	const uint16 result = uint16(oldValue + 1);
	a[0] = result;
	debugC(2, kDebugLevelScript, "opcode 0x6d: %u++ -> %u", oldValue, result);
	return kThxBye;
}

OPCODE(0x6e) {
	// subtract: a[0] -= a[1]
	// DOS handler @ 1000:42c9.
	const uint16 left = uint16(a[0]);
	const uint16 right = uint16(a[1]);
	const uint16 result = uint16(left - right);
	a[0] = result;
	debugC(2, kDebugLevelScript, "opcode 0x6e: %u -= %u -> %u", left, right, result);
	return kThxBye;
}

OPCODE(0x6f) {
	// decrement
	const uint16 oldValue = uint16(a[0]);
	const uint16 result = uint16(oldValue - 1);
	a[0] = result;
	debugC(2, kDebugLevelScript, "opcode 0x6f: %u-- -> %u", oldValue, result);
	return kThxBye;
}

OPCODE(0x71) {
	// DOS handler @ 1000:42ea resolves arg0 into BX, arg1 into CX, then
	// writes arg0's old value through arg1 before writing arg1's old value
	// through arg0.
	const uint16 old0 = uint16(a[0]);
	const uint16 old1 = uint16(a[1]);
	a[1] = old0;
	a[0] = old1;
	debugC(2, kDebugLevelScript, "opcode 0x71: swap(%u, %u)", old0, old1);
	return kThxBye;
}

OPCODE(0x70) {
	// assign
	const uint16 value = uint16(a[1]);
	a[0] = value;
	debugC(2, kDebugLevelScript, "opcode 0x70: assign %u", value);
	return kThxBye;
}

OPCODE(0x72) {
	// assign 1
	a[0] = 1;
	debugC(2, kDebugLevelScript, "opcode 0x72: assign 1");
	return kThxBye;
}

OPCODE(0x73) {
	// assign 0
	a[0] = 0;
	debugC(2, kDebugLevelScript, "opcode 0x73: assign 0");
	return kThxBye;
}

OPCODE(0x77) {
	// DOS Op_77 @ 1000:433d = GoToRoomWithFrame(room, frame). It first
	// calls CheckActorAnimReady(g_main_character_id); the wait path calls
	// RegisterSampleSlot_LoadDefaultsAndMark, which retries this opcode from
	// the current-opcode snapshot saved by InterpretBytecode. Args are
	// arg0=room, arg1=frame.
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	const uint8 frame = uint8(uint16(a[1]));
	const uint16 room = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x77: go to room %u frame %u", room, frame);
	if (Log.inStatusMode()) {
		Log.restartRoom();
		Log.setLogicDirty();
		Log.setPaused();
		return kThxBye;
	}
	writeActorRoomTransition(ac, room, frame, frame);
	requestRoomRestartTail(room);
	return kThxBye;
}

OPCODE(0x79) {
	// DOS Op_79_PlaceActorInRoom @ 1000:43d3:
	//   if (in_map_mode) return;
	//   id=arg0, room=arg1, frame=arg2;
	//   UnregisterActor(id);                  ; clear from old room
	//   actor.field+0x61 = (byte)frame;       ; current_frame
	//   actor.field+0x62 = (byte)frame;       ; target_frame (= same as current)
	//   actor.field+0x59 = room;
	//   actor.field+0x6b = 0;                  ; walk-target word cleared
	//   SetActorPosition;                     ; position from frame
	//   FindPlaceById(id); InitActorState(id);
	//   if (room == g_current_location && target!=current_frame)
	//       MoveActorToTargetExit;            ; auto-walk
	const uint16 id = uint16(a[0]);
	const uint16 room = uint16(a[1]);
	const uint8 frame = uint8(uint16(a[2]));
	debugC(1, kDebugLevelScript, "opcode 0x79: move actor %u to room %u frame %u",
		   id, room, frame);
	if (Log.inStatusMode())
		return kThxBye;
	Actor *ac = _logic->getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	ac->unregister();
	placeActorInRoomWithPosition(ac, room, frame, frame);
	initActorFromPuppeteer(_logic, ac, id);
	return kThxBye;
}

OPCODE(0x74) {
	// Boolean toggle: if a[0] is zero set it to 1; otherwise set it to 0.
	// DOS handler @ 1000:430a — branches between Op_72 (=1) and Op_73 (=0).
	const uint16 oldValue = uint16(a[0]);
	const uint16 result = (oldValue == 0) ? 1 : 0;
	a[0] = result;
	debugC(2, kDebugLevelScript, "opcode 0x74: !%u -> %u", oldValue, result);
	return kThxBye;
}

OPCODE(0x3a) {
	// DOS Op_3a_CancelDeferredMain @ 1000:3cc7: removes the matching
	// entry from the deferred-script queue. Match key = (mode-segment,
	// offset). If the matched entry's mode equals `g_opcode_mode`
	// (= currently-running deferred script's mode), DOS sets
	// `g_break_loop = 1` so the running script exits this tick.
	// C++ `cancelDeferred` returns true in that self-cancel case;
	// translate to kReturn (= dispatcher exits the loop).
	debugC(2, kDebugLevelScript, "opcode 0x3a: cancel deferred (main) %s", +a[0]);
	const bool selfCancel = _logic->cancelDeferred(
		CodePointer(static_cast<CodePointer &>(a[0]).offset(), _logic->mainInterpreter()));
	return selfCancel ? kReturn : kThxBye;
}

OPCODE(0x3c) {
	// DOS Op_3c_CancelDeferredBlock @ 1000:3cc1: same shape as Op_3a
	// but matches against the current script segment (`g_codeptr_es_save`).
	debugC(2, kDebugLevelScript, "opcode 0x3c: cancel deferred (block) %s", +a[0]);
	const bool selfCancel = _logic->cancelDeferred(
		static_cast<CodePointer &>(a[0]));
	return selfCancel ? kReturn : kThxBye;
}

OPCODE(0x3e) {
	// DOS Op_3e_ClearEscapeBreakPoint @ 1000:3d23:
	//   g_esc_during_script = 0;
	// In C++, clearEscBreakPoint() resets all three fields plus the
	// skipPoint flag (mirrors `g_esc_during_script = 0` since the
	// "active" predicate in C++ is `!_skipPoint.isEmpty()`).
	debugC(2, kDebugLevelScript, "opcode 0x3e: clear ESC handler");
	Log.clearEscBreakPoint();
	return kThxBye;
}

OPCODE(0x4c) {
	// DOS Op_4c_RegisterSampleSpeechOrStatus @ 1000:3eff: status mode uses
	// Bare2 (BX=3, CheckSubtitleActive); non-status uses Bare3 (BX=2,
	// CheckUiText against the speech-slot pointer last stashed in
	// DS:0x669a). Both save the next PC through RegisterSampleSlot_Common.
	if (Log.branchState() != 0 || Log.callDepth() != 0) {
		Log.setPendingError(0x39);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x4c: wait speech/status=%d", Log.inStatusMode() ? 1 : 0);
	if (Log.inStatusMode()) {
		if (waitForSubtitle(next))
			return kReturn;
		Log.runLaterWithCurrentMode(next, 0);
		return kReturn;
	}
	if (waitForUiTextSlot(next))
		return kReturn;
	Log.runLaterWithCurrentMode(next, 0);
	return kReturn;
}

OPCODE(0x7b) {
	// DOS Op_7b_SetObjectFlag1 @ 1000:4459:
	//   if (arg0 > active block exit count) pending-error 0x14;
	//   else if cell bit 0 not set: set cellByte[arg0] |= 1.
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7b: set cell bit 0 on entity %u", id);
	if (!Log.cellBit(id, 0)) {
		Log.setCellBit(id, 0);
		Log.setLogicDirty();
	}
	if (Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(id) : 0)
		if (!exit->isEnabled())
			exit->setEnabled(true);
	return kThxBye;
}

OPCODE(0x7c) {
	// DOS Op_7c_ClearObjectFlag1 @ 1000:4476:
	//   if (arg0 > active block exit count) pending-error 0x14;
	//   else if cell bit 0 set: clear cellByte[arg0] ^= 1.
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7c: clear cell bit 0 on entity %u", id);
	if (Log.cellBit(id, 0)) {
		Log.clearCellBit(id, 0);
		Log.setLogicDirty();
	}
	if (Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(id) : 0)
		if (exit->isEnabled())
			exit->setEnabled(false);
	return kThxBye;
}

OPCODE(0x95) {
	// Dispatch-table entry 0x95 points to DOS helper @ 1000:4a52:
	// clear g_flag_no_step (DS:0x6747) and g_flag_step_pending (DS:0x6748).
	debugC(1, kDebugLevelScript, "opcode 0x95: unlock control");
	Log.setNoStep(false);
	Log.setStepPending(false);
	return kThxBye;
}

OPCODE(0x96) {
	// Dispatch-table entry 0x96 points to DOS helper @ 1000:4a4c:
	// set g_flag_no_step (DS:0x6747) without touching step-pending.
	debugC(1, kDebugLevelScript, "opcode 0x96: lock control");
	Log.setNoStep(true);
	return kThxBye;
}

OPCODE(0x99) {
	// DOS Op_99 @ 1000:4bed: status-mode no-op; otherwise checks the
	// protagonist through MaybeRegisterActorSample, which queues the
	// post-opcode PC in the active opcode mode while the actor is still
	// active in the current room.
	debugC(2, kDebugLevelScript, "opcode 0x99: wait for protagonist to exit");
	if (Log.inStatusMode())
		return kThxBye;

	Actor *ac = _logic->protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (ac->room() != _logic->currentRoom())
		return kThxBye;
	if (sampleSlotWouldError())
		return kThxBye;

	ac->callMeWithMode(next, Log.opcodeMode());
	return kReturn;
}

OPCODE(0x9a) {
	// wait for actor to exit
	debugC(2, kDebugLevelScript, "opcode 0x9a: wait for actor %s to exit", +a[0]);
	if (Log.inStatusMode())
		return kThxBye;

	Actor *ac = _logic->getActor(a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (ac->room() != _logic->currentRoom())
		return kThxBye;
	if (sampleSlotWouldError())
		return kThxBye;

	ac->callMeWithMode(next, Log.opcodeMode());
	return kReturn;
}

OPCODE(0x9b) {
	// delay
	debugC(2, kDebugLevelScript, "opcode 0x9b: delay %s frames", +a[0]);
	if (sampleSlotWouldError())
		return kThxBye;
	_logic->runLaterWithCurrentMode(next, a[0]);
	return kReturn;
}

OPCODE(0x9c) {
	// DOS Op_9c_handler @ 1000:4c1e: status-mode no-op. Non-status resolves
	// arg1 timeout first, then arg0 actor id, and registers type 6 only
	// while the actor is outside the current room.
	if (Log.inStatusMode())
		return kThxBye;

	const uint16 timeout = uint16(a[1]);
	const uint16 actorId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x9c: wait until actor %u enters or %u ticks",
		   actorId, timeout);
	Actor *ac = _logic->getActor(actorId);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (ac->room() != _logic->currentRoom()) {
		ac->setRawTicksLeft(timeout);
		if (sampleSlotWouldError())
			return kThxBye;
		ac->tellMeWithMode(next, timeout, Log.opcodeMode());
		return kReturn;
	}
	return kThxBye;
}

OPCODE(0x9d) {
	// DOS Op_9d_SetProtagonist @ 1000:4c44: ResolveOpcodeArg0 once and
	// store AX directly to CS:[0x010f].
	const uint16 actorId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x9d: set protagonist(%u)", actorId);
	_logic->setProtagonist(actorId);
	return kThxBye;
}

OPCODE(0x9e) {
	// DOS Op_9e @ 1000:4c4c. nargs=1 in dispatch table. Saves
	// g_main_character_id and uses it as the target actor; arg0 is the
	// frame id. DOS field assignments (after GetActorOffset(prot)):
	//   field+0x61 = arg0   (current frame)
	//   field+0x62 = arg0   (target frame)
	//   field+0x6b = 0      (walk speed)
	// Then SetActorPosition copies frame[arg0]'s X/Y into the actor.
	// FindPlaceById + InitActorState run after; FindPlaceById resolves
	// the protagonist puppeteer record and InitActorState restarts that
	// actor's main-code animation.
	const uint8 frame = uint8(uint16(a[0]));
	debugC(2, kDebugLevelScript, "opcode 0x9e: warp protagonist to frame %u", frame);
	Log.setPostMoveTargetFrameMirror(frame);
	if (Actor *ac = Log.protagonist()) {
		ac->placeIn(ac->room(), frame);
		initActorFromPuppeteer(_logic, ac, Log.protagonistId());
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}

OPCODE(0xab) {
	// DOS Op_ab_handler @ 1000:4e3e: protagonist CheckActorIdle gate,
	// then ResolveOpcodeArg0 and QueueExitTransition. The queue helper
	// sets g_break_inner, but InterpretBytecode does not stop on that flag.
	if (Log.inStatusMode())
		return kThxBye;
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}

	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}

	const uint16 targetFrame = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xab: queue protag exit transition to frame %u", targetFrame);
	queueExitTransition(ac, targetFrame);
	return kThxBye;
}

OPCODE(0xad) {
	// DOS Op_ad_handler @ 1000:4e8c: actor-targeted move (any actor id).
	// CheckActorIdle gates through the same current-opcode retry path as
	// Op_ab; the ready path resolves arg1 and calls MoveActorToTargetExit.
	//
	// MoveActorToTargetExit @ 1000:70da dispatches by actor type:
	//   - Protagonist: QueueExitTransition (cancel speech + walk +
	//     g_break_inner=1, but no interpreter break).
	//   - Non-protag IN g_actor_table[20] (active in current room):
	//     setup walk via FindActorPath (pathfinder).
	//   - Non-protag NOT in active table (offscreen):
	//     warp via SI[0x61]=target_frame + SetActorPosition (using
	//     actor.room's frame table).
	//
	const uint16 actorId = uint16(a[0]);
	Actor *ac = _logic->getActor(actorId);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}

	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}

	const uint16 targetFrame = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xad: move actor %u to frame %u next",
		   actorId, targetFrame);
	moveActorToTargetExit(ac, targetFrame);
	return kThxBye;
}

OPCODE(0xb9) {
	// DOS Op_b9_WalkActorWaitWithBreak @ 1000:5026: block/slow actor animation,
	// status-mode no-op, validate actor id, retry current opcode while
	// CheckActorAnimReady says the actor is still active, set g_break_inner
	// if arg0 is the protagonist, then InitActorState(arg1). InterpretBytecode
	// does not stop on g_break_inner; it continues after the ready path.
	Log.setWalkSpeedFlag(1);
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 id = uint16(a[0]);
	if (dosPositiveIdExceedsMax(id, actorAnimMaxId())) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	setBreakInnerIfProtagonistId(id);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}

	CodePointer anim(static_cast<CodePointer &>(a[1]).offset(), Log.blockInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xb9: set actor %u block animation to %s", id, +anim);
	initActorState(ac, anim);
	return kThxBye;
}

OPCODE(0xbc) {
	// DOS Op_bc_handler @ 1000:5085: no-op in status mode, otherwise resolve
	// actor id, validate against GetActorOffset-style bounds, then
	// UnregisterActor. DOS clears only actor fields +0/+2 and removes the
	// id from g_actor_table.
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 id = uint16(a[0]);
	Actor *ac = _logic->getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0xbc: unregister actor %u", id);
	ac->unregister();
	return kThxBye;
}

OPCODE(0xbd) {
	// DOS Op_bd_handler @ 1000:50ea: slow protagonist animation, same
	// current-opcode retry gate as Op_b9. The ready path sets
	// g_break_inner but returns normally; InterpretBytecode keeps running.
	Log.setWalkSpeedFlag(0);
	if (Log.inStatusMode())
		return kThxBye;
	Log.setBreakInner(true);
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}

	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), Log.mainInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xbd: set protagonist animation to %s", +p);
	initActorState(ac, p);
	return kThxBye;
}

OPCODE(0xbe) {
	// DOS Op_be_handler @ 1000:50e3: fast protagonist animation, same
	// current-opcode retry gate as Op_b9. The ready path sets
	// g_break_inner but returns normally; InterpretBytecode keeps running.
	Log.setWalkSpeedFlag(1);
	if (Log.inStatusMode())
		return kThxBye;
	Log.setBreakInner(true);
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}

	CodePointer p(static_cast<CodePointer &>(a[0]).offset(), Log.blockInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xbe: set protagonist block animation to %s", +p);
	initActorState(ac, p);
	return kThxBye;
}

OPCODE(0xc2) {
	// DOS Op_c2_handler @ 1000:5140: walks g_cast_table (18 slots),
	// finds first inactive (wActive==0), seeds it with arg0 (script
	// offset), current ES (script segment), and locked cursor x/y as
	// initial position. C++ mirrors this in Logic::_castTable; the cast
	// renderer state bytes are initialized by castTableRegister.
	const Common::Point p = Log.lockedCursorPosition();
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc2: RegisterCastEntry id=%u at cursor (%d,%d)",
		   id, p.x, p.y);
	Log.castTableRegister(id, int16(p.x), int16(p.y), current.interpreter());
	return kThxBye;
}

OPCODE(0xc6) {
	// DOS Op_c6_handler @ 1000:51eb: Resolve arg0 then register sample
	// slot type 8 through RegisterSampleSlot_Common. The saved slot keeps
	// AX=arg0 and the post-opcode PC; DispatchRoomAnimation maps type 8 to
	// FindCastByActorId, resuming when no active cast entry for that captured
	// id remains or its cast script has reached the 0xff sentinel.
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc6: wait on cast entry %u", id);
	if (sampleSlotWouldError())
		return kThxBye;
	Log.runLaterWhenCastEntryInactive(id, next);
	return kReturn;
}

OPCODE(0xc7) {
	// DOS Op_c7_handler @ 1000:51f2 plays the video synchronously. ESC
	// only exits PlayVideoAnimationLoop; it does not dispatch the
	// Op_3d skip target from inside the movie player. After playback DOS
	// calls AllocBuffersB, clears the screen, sets g_in_fade, then sets
	// logic-dirty/change-room/refresh-interface flags; the change-room flag
	// reloads g_loaded_backdrop_id and restores the room palette target.
	const uint16 frameDelay = uint16(a[1]);
	byte *movieName = static_cast<byte *>(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc7: play movie %s with slowness %u",
		   movieName ? reinterpret_cast<char *>(movieName) : "(null)", frameDelay);
	Movie *m = Movie::fromFile(reinterpret_cast<char *>(movieName));
	m->setFrameDelay(frameDelay);
	m->play();
	delete m;
	Log.resetMovieGraphicSlots();
	if (_graphics) {
		_graphics->clearFramebuffer();
		_graphics->willFadein();
	}
	reloadLoadedBackdrop(_graphics);
	Log.setLogicDirty();
	return kThxBye;
}

OPCODE(0xc8) {
	// DOS Op_c8_handler @ 1000:5222: ClearVideoAndPushToScreen +
	// g_loaded_backdrop_id (DS:0x666a) = arg0 + SetBackdropImage. This
	// loads the named backdrop graphic into the room buffer.
	//
	// Difference from Op_c9: Op_c8 immediately loads a backdrop image.
	// Op_c9 sets the savegame "current place" id and only triggers a reload
	// when in status mode.
	clearVideoAndPushToScreen(_graphics);
	const uint16 id = uint16(a[0]);
	MainDat *main = _logic->resources()->mainDat();
	if (!main || dosPositiveIdExceedsMax(id, main->imagesCount())) {
		Log.setPendingError(0x0a);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0xc8: set backdrop(%u)", id);
	_logic->setLoadedBackdropId(id);
	_graphics->setBackdrop(id);
	return kThxBye;
}

OPCODE(0xc9) {
	// DOS Op_c9_handler @ 1000:522f:
	//   DAT_1000_0111 = arg0           ; set "current place" id (CS:[0x111])
	//   if (g_in_status_mode != 0) {
	//     ClearVideoAndPushToScreen();
	//     g_flag_change_room = 1;     ; trigger room reload
	//   }
	// CS:[0x111] is read by the status-screen RestoreBackdrop path and by
	// the save/load state path, so outside status mode this only records the
	// place id. In status mode the pending change-room flag reloads that
	// place backdrop on the next status-loop pass; C++ applies it directly.
	const uint16 place = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc9: set current place to %u", place);
	_logic->setCurrentPlace(place);
	if (Log.inStatusMode()) {
		clearVideoAndPushToScreen(_graphics);
		MainDat *main = _logic->resources()->mainDat();
		if (!main || dosPositiveIdExceedsMax(place, main->imagesCount())) {
			Log.setPendingError(0x0a);
			return kThxBye;
		}
		_logic->setLoadedBackdropId(place);
		_graphics->setBackdrop(place);
	}
	return kThxBye;
}

OPCODE(0xcb) {
	// DOS Op_cb_handler @ 1000:5275 → calls LoadGraphicToSlot @ 1000:1f49:
	//   if ((int16)arg0 > graphic_count) pending-error 0xa;
	//   else: type = image_directory[(arg0-1)*4].type_word;
	//     type ∈ {1,2,3} → DecodeImage to small slot (DS:0x676f..0x6773)
	//     type ∈ {4,5,6,7} → DecodeFullScreenImage to fullscreen slot
	//       (DS:0x6779/0x677b/0x6775/0x6777); the optional embedded
	//       palette read is shared by all four when [0x66c6] != 0.
	//     other         → pending-error 0xa.
	//   On match, store arg0 in the corresponding slot global.
	const uint16 id = uint16(a[0]);
	MainDat *main = _logic->resources()->mainDat();
	if (!main || dosPositiveIdExceedsMax(id, main->imagesCount())) {
		Log.setPendingError(0x0a);
		return kThxBye;
	}
	const uint16 type = main->imageType(id);
	debugC(1, kDebugLevelScript, "opcode 0xcb: load graphic %u (type=%u)", id, type);
	bool fullscreen = false;
	switch (type) {
	case 1:
		Log.setGraphicSlot(0, id);
		break;
	case 2:
		Log.setGraphicSlot(1, id);
		break;
	case 3:
		Log.setGraphicSlot(2, id);
		break;
	case 6:
		Log.setGraphicSlot(3, id);
		fullscreen = true;
		break;
	case 7:
		Log.setGraphicSlot(4, id);
		fullscreen = true;
		break;
	case 4:
		Log.setGraphicSlot(5, id);
		fullscreen = true;
		break;
	case 5:
		Log.setGraphicSlot(6, id);
		fullscreen = true;
		break;
	default:
		Log.setPendingError(0x0a);
		return kThxBye;
	}

	// DOS decodes the selected graphic into its slot immediately. The C++
	// slot backing is the Resources image cache used by sprite cuts.
	_logic->resources()->loadImage(id);

	if (fullscreen && Log.modalState().paletteMode != 0)
		Graf.loadGraphicPalette(id);
	return kThxBye;
}

OPCODE(0xcc) {
	// DOS Op_cc_handler @ 1000:527b:
	//   g_fullscreen_gate_active = 1;
	//   if (CS:[0x52a3] != 0) Op_35_Jump(arg0);
	//   CS:[0x52a3] = 1; g_full_redraw_pending = 0;
	//   SetBackdropDimensions(0xc8); SetCursorMode(2). The following
	//   ApplyChangeRoomTransition restores the default cursor from
	//   DS:0x667a while g_fullscreen_gate_active is still set.
	debugC(1, kDebugLevelScript, "opcode 0xcc: enter fullscreen gate");
	Log.setFullscreenGateActive(true);
	if (Log.fullscreenGateInitialized())
		return static_cast<CodePointer &>(a[0]);
	Log.setFullscreenGateInitialized(true);
	Graf.setFullscreen(true);
	Log.setCursorMode(2);
	return kThxBye;
}

OPCODE(0xce) {
	// DOS Op_ce_handler @ 1000:52a4: start cutscene.
	//   1. g_room_active = 0
	//   2. SetBackdropDimensions(0xc8) (fullscreen)
	//   3. g_flag_misc_1 = 1 (dirty flag)
	//   4. Calls raw lock helper @ 1000:4a4c (dispatch-table opcode 0x96)
	debugC(2, kDebugLevelScript, "opcode 0xce: start cutscene");
	Graf.setFullscreen(true);
	Log.setRoomActive(false);
	Log.setNoStep(true);
	return kThxBye;
}

OPCODE(0xcf) {
	// DOS Op_cf_handler @ 1000:52ca: full fade-to-black if not already in
	// fade, then ClearScreenBuffer, g_in_fade=1, g_room_active=0. If ESC
	// is seen while g_esc_during_script is armed, the fade loop sets
	// g_break_loop and the interpreter yields after the side effects below.
	debugC(1, kDebugLevelScript, "opcode 0xcf: fadeout");
	bool completed = true;
	if (!Graf.inFade()) {
		completed = _graphics->fadeOut();
		_graphics->clearFramebuffer();
	}
	Graf.setInFade(true);
	Log.setRoomActive(false);
	return completed ? kThxBye : kReturn;
}

OPCODE(0xd0) {
	// DOS Op_d0_handler @ 1000:531e: fade palette entries 0xa0..0xff
	// only. If ESC is seen while an Op_3d break point is armed, it sets
	// g_break_loop, zeroes the interface palette range, updates the
	// palette, and returns. Mirror g_break_loop with kReturn so
	// HandleEscDuringScript runs before the script continues into any
	// following movie/cutscene opcodes.
	debugC(1, kDebugLevelScript, "opcode 0xd0: partial fadeout");
	if (!Graf.inFade()) {
		if (!Graf.fadeOut(Graphics::kPartialFade)) {
			Graf.clearPaletteRange(160, 96, false);
			return kReturn;
		}
	}
	return kThxBye;
}

OPCODE(0xd1) {
	debugC(2, kDebugLevelScript, "opcode 0xd1: fadein next paint");
	_graphics->willFadein();
	return kThxBye;
}

OPCODE(0xd2) {
	debugC(2, kDebugLevelScript, "opcode 0xd2: will fadein partially");
	Graf.willFadein(Graphics::kPartialFade);
	return kThxBye;
}

OPCODE(0xd6) {
	// DOS Op_d6_handler @ 1000:5422:
	//   g_input_enabled = 0;
	//   ResolveOpcodeArg0();
	//   ApplyChangeRoomTransition();
	Log.setInputEnabled(false);
	const uint16 room = uint16(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0xd6: change room(%u)", room);
	_logic->changeRoom(room);
	return kThxBye;
}

OPCODE(0xdb) {
	// DOS Op_db_handler @ 1000:546e: append to g_collision_zone[24].
	// Args 0..3 are the rectangle words; arg4 is read via ReadVarBySlot_RHS,
	// must be < 13, and is stored as value - 1. Overflow raises 0x2e;
	// invalid slot raises 0x1f.
	if (Log.collisionZones().size() >= 24) {
		Log.setPendingError(0x2e);
		return kThxBye;
	}
	const int16 slot = a[4].signd();
	if (slot >= 13) {
		Log.setPendingError(0x1f);
		return kThxBye;
	}
	Logic::CollisionZone z = {
		uint16(a[0]), uint16(a[1]), uint16(a[2]), uint16(a[3]), int16(slot - 1)};
	debugC(1, kDebugLevelScript, "opcode 0xdb: add collision zone %u:%u-%u:%u slot=%d",
		   z.a, z.b, z.c, z.d, z.slot);
	Log.collisionZonesAdd(z);
	return kThxBye;
}

OPCODE(0xdf) {
	// DOS Op_df_handler @ 1000:5504: append one 12-byte frame/walkbox
	// record while g_walkbox_count < 0xfd, else pending-error 0x30.
	if (Log.actorFrameCount() >= 0xfd) {
		Log.setPendingError(0x30);
		return kThxBye;
	}
	const int16 left = a[0].signd();
	const int16 top = a[1].signd();
	Common::Array<byte> nexts;
	nexts.resize(8);
	for (int i = 0; i < 4; i++) {
		uint16 val = a[i + 2];
		nexts[2 * i] = val & 0xff;
		nexts[2 * i + 1] = val >> 8;
	}
	debugC(2, kDebugLevelScript, "opcode 0xdf: add actor frame %d %d %d %d %d %d %d %d %d %d", left, top, nexts[0], nexts[1], nexts[2], nexts[3], nexts[4], nexts[5], nexts[6], nexts[7]);
	Log.actorFramesAdd(Common::Point(left, top), nexts);
	return kThxBye;
}

OPCODE(0xe5) {
	// DOS Op_e5_handler @ 1000:5604: 0 args. Clears g_anim_list_count = 0.
	// = reset cutscene anim-list to empty.
	debugC(1, kDebugLevelScript, "opcode 0xe5: anim-list clear");
	Log.animListClear();
	return kThxBye;
}

OPCODE(0xe6) {
	// set room loop code
	debugC(2, kDebugLevelScript, "opcode 0xe6: set room loop to %s", +a[0]);
	assert(a[0].holdsCode());
	_logic->setRoomLoop(static_cast<CodePointer &>(a[0]));
	return kThxBye;
}

OPCODE(0xed) {
	// Set timer deadline: a[0] = frame_tick_counter + a[1]
	// DOS handler @ 1000:568c. Pairs with Op_10 which fires when reached.
	const uint16 now = Log.frameTicks();
	const uint16 delay = uint16(a[1]);
	const uint16 deadline = uint16(now + delay);
	a[0] = deadline;
	debugC(2, kDebugLevelScript, "opcode 0xed: set deadline = tick(%u) + %u -> %u",
		   now, delay, deadline);
	return kThxBye;
}

OPCODE(0xef) {
	// DOS GetRandomBitsBelow returns a value in the inclusive 1..arg0
	// range. The engine RNG helper is 0..max inclusive, so shift it.
	const uint16 max = uint16(a[0]);
	const uint16 value = max ? uint16(_engine->getRandom(uint16(max - 1)) + 1) : 0;
	a[1] = value;
	debugC(2, kDebugLevelScript, "opcode 0xef: random1(%u) -> %u", max, value);
	return kThxBye;
}

OPCODE(0xf0) {
	// DOS Op_load_sfx @ 1000:56d9: 1 arg = SFX id.
	//   if (g_sfx_enabled) {
	//       if (arg0 != pbRam0002324e) {
	//           PlaySfxSound(arg0);
	//           update slot caches, last_played = arg0, secondary = 0;
	//       }
	//   }
	// Routes through Sound::playSfx which handles the short-circuit
	// and slot-cache state transitions per DOS.
	if (Sound *snd = _engine->sound()) {
		if (!snd->isEnabled()) {
			debugC(1, kDebugLevelScript, "opcode 0xf0: load_sfx skipped (sfx disabled)");
			return kThxBye;
		}
		const uint16 id = uint16(a[0]);
		snd->playSfx(id);
		debugC(1, kDebugLevelScript, "opcode 0xf0: load_sfx id=%u", id);
	}
	return kThxBye;
}

OPCODE(0xf4) {
	// DOS Op_f4_handler @ 1000:5782 waits through
	// RegisterSampleSlot_LoadDefaultsE (BX=7, CheckMusicPlaying) while a
	// tune is active. Only the carry-set "not playing" path starts arg0.
	if (!Music.isActive() || _engine->dosMusicEnabled() == 0) {
		if (Log.opcodeMode() > 10) {
			if (sampleSlotWouldError())
				return kThxBye;
			_logic->runLaterWithCurrentMode(next, 1);
			return kReturn;
		}
		return kThxBye;
	}
	if (Music.hasCurrentTune()) {
		if (sampleSlotWouldError())
			return kThxBye;
		_logic->runLaterWithCurrentMode(current);
		return kReturn;
	}
	// The arg is a near offset into the main bytecode (IUC_MAIN.DAT) that
	// points at a music script: tune index (uint16) followed by
	// kSetBeat/kJump/kStop bytecodes. Even when called from a block, the
	// offset is always relative to the main interpreter — music scripts live
	// in the global file, not in per-block bytecode.
	const uint16 scriptOff = static_cast<CodePointer &>(a[0]).offset();
	const byte *script = Log.mainInterpreter()->rawCode(scriptOff);
	const uint16 tune = READ_LE_UINT16(script);
	debugC(1, kDebugLevelScript, "opcode 0xf4: play music script at main offset 0x%04x", scriptOff);
	if (tune == 0)
		return kThxBye;
	static int op_f4_calls = 0;
	op_f4_calls++;
	if (op_f4_calls <= 3)
		warning("Interspective music: opcode 0xf4 emitted (call #%d, script offset 0x%04x)",
				op_f4_calls, scriptOff);
	Music.loadMusic(script);
	return kThxBye;
}

OPCODE(0xf7) {
	// stop music
	debugC(2, kDebugLevelScript, "opcode 0xf7: stop music");
	if (Music.isActive() && _engine->dosMusicEnabled() != 0 && Music.hasCurrentTune())
		Music.requestStopCurrent();
	return kThxBye;
}

OPCODE(0xf9) {
	// DOS Op_f9_handler @ 1000:58cc:
	//   arg1 = state byte (0=disable channel, non-zero=enable)
	//   arg0 = which: 1=music, 2=sfx
	// On music-disable: stop the current tune (silence + clear current_tune_addr).
	// On music-disable: also stops any active sfx (cascade in DOS).
	// On sfx-disable: clears g_sfx_active.
	const uint8 state = uint8(uint16(a[1]));
	const uint16 which = uint16(a[0]);
	const bool isMusic = (which == 1);
	debugC(1, kDebugLevelScript, "opcode 0xf9: set %s to %u", isMusic ? "music" : "sfx", state);
	if (isMusic) {
		Music.setActive(state != 0);
		if (state == 0) {
			if (_engine->dosMusicEnabled() != 0)
				Music.stopMusic();
			if (Sound *snd = _engine->sound())
				if (snd->isEnabled() && snd->isSfxPlaying())
					snd->stopAll();
		}
	} else if (which == 2) {
		if (Sound *snd = _engine->sound())
			snd->setActive(state != 0);
	}
	return kThxBye;
}

OPCODE(0xfc) {
	// DOS Op_fc_handler @ 1000:5996:
	//   Resolve arg0; arg0 != 0 -> ShutdownAndExit.
	//   arg0 == 0 -> tail-jump to HandleSpecialKey's menu branch
	//   @ 1000:b82d, which only opens RunModalLoop while
	//   g_fullscreen_gate_active is clear. Status mode is not a blocker.
	//   Menu choice 2 exits, choice 1 requests restart, otherwise it
	//   returns normally.
	// The room-999 status screen labels this action as "Quit"; route that
	// control straight to engine shutdown instead of opening ScummVM's
	// unrelated global menu.
	const uint16 quitMode = uint16(a[0]);
	if (quitMode != 0 || Log.inStatusMode()) {
		debugC(2, kDebugLevelScript, "opcode 0xfc: quit%s",
			   Log.inStatusMode() && quitMode == 0 ? " from status screen" : " unconditionally");
		_engine->quitGame();
		return kThxBye;
	}
	if (Log.fullscreenGateActive()) {
		debugC(2, kDebugLevelScript, "opcode 0xfc: menu request ignored while fullscreen gate is active");
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0xfc: open main menu");
	_engine->openMainMenuDialog();
	return kThxBye;
}

// ============================================================================
// Translated from the DOS binary handlers. State the engine doesn't track
// (object table, walkboxes, drag/cursor mode, last-resolved actor) is held
// on Logic via the new state slots (Logic::verbMode / cursorMode / etc).
// Per-room scratch buffers that affect script-visible state are mirrored on
// Logic or on the relevant Actor/Animation record.
// ============================================================================

OPCODE(0x0a) {
	// DOS Op_0a_IfModeIs80OrFlag @ 1000:37de:
	//   if ((cursor_mode == 0x80 || step_pending) && (cursor_mode & arg0)) return;
	//   else skip;
	// arg0 is a bitmask of cursor-mode bits the script handles. The opcode is
	// "do this branch when the current cursor mode matches the mask AND we're
	// in a state to act (system mode or pending action)".
	uint16 mask = uint16(a[0]);
	uint16 cm = Log.cursorMode();
	debugC(2, kDebugLevelScript, "opcode 0x0a: if (cursor==0x80||step) && (cursor & %u)", mask);
	if ((cm == 0x80 || Log.stepPending()) && (cm & mask) != 0)
		return kThxBye;
	return kFail;
}

OPCODE(0x0b) {
	// DOS Op_0b_IfMode40AndFlag @ 1000:37ff:
	//   if (step_pending && cursor==0x40 && arg0 == g_drag_target_mode40)
	//       return; else skip;
	// Note: DOS reads `g_drag_target_mode40` @ DS:0x667e, NOT the
	// regular `g_drag_target` @ DS:0x667c (which Op_0e uses). The
	// mode-40 slot is set exclusively by Op_76_BeginDragWithTarget
	// @ 1000:4325 together with `_g_cursor_mode = 0x40`.
	// In C++ this is `Logic::_dragTargetMode40`; Op_76 populates it
	// and sets cursor mode 0x40.
	uint16 mask = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x0b: if step && cursor==0x40 && dragMode40==%u", mask);
	if (Log.stepPending() && Log.cursorMode() == 0x40 && Log.dragTargetMode40() == mask)
		return kThxBye;
	return kFail;
}

OPCODE(0x0c) {
	// DOS Op_0c_IfNotInStatusMode @ 1000:38ab: skip if NOT in status mode → body runs ONLY in status mode.
	debugC(2, kDebugLevelScript, "opcode 0x0c: if in status mode");
	if (!Log.inStatusMode())
		return kFail;
	return kThxBye;
}

OPCODE(0x0d) {
	// DOS Op_0d_IfDragNotMatchTarget @ 1000:38d7: skip if (!step || cursor!=0x20 || drag==0). Body runs when
	// actively dragging an object (verb-on-object pre-action).
	debugC(2, kDebugLevelScript, "opcode 0x0d: if dragging (cursor=0x20 + drag set)");
	if (!Log.stepPending() || Log.cursorMode() != 0x20 || Log.dragTarget() == 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x0e) {
	// DOS Op_0e_IfDragMatchesArg @ 1000:38b9: like 0x0d but only when dragTarget == arg0 (verb-on-this-object).
	debugC(2, kDebugLevelScript, "opcode 0x0e: if dragging && dragTarget == %s", +a[0]);
	if (!Log.stepPending() || Log.cursorMode() != 0x20 || Log.dragTarget() != uint16(a[0]))
		return kFail;
	return kThxBye;
}

OPCODE(0x14) {
	// DOS Op_14_IfFreshGameState @ 1000:395a: fail if current entity type != 0.
	debugC(2, kDebugLevelScript, "opcode 0x14: if current entity type == 0");
	if (Log.gameState() != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x15) {
	// DOS Op_15_IfCellBitSet @ 1000:3968.
	//   ResolveOpcodeArg1 → bit_idx; if > 7 → SetError15ArgOutOfRange (halt)
	//   ResolveOpcodeArg0 → id; if signed id > CS:[0x47] block exit/cell count → error 0x14
	//   RCR AL by (bit_idx + 1) through carry; skip if carry is clear.
	// Net for the common path (id < max, bit ∈ [0,7]): tests
	// cellByte[id] bit `bit_idx` (LSB-indexed). Body runs if bit SET.
	const uint16 rawBit = uint16(a[1]);
	if (int16(rawBit) > 7) {
		Log.setPendingError(0x15);
		return kThxBye;
	}
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (int16(id) > int16(exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const bool set = dosCellBitTest(Log.cellByte(id), rawBit);
	debugC(2, kDebugLevelScript, "opcode 0x15: if cell bit %u of entity %s set (=%s)",
		   uint8(rawBit), +a[0], set ? "yes" : "no");
	return set ? kThxBye : kFail;
}

OPCODE(0x16) {
	// DOS Op_16 @ 1000:3991 sets CX=1 then tail-jumps into the same
	// cell-byte tester used by Op_15. The common tail increments CX
	// before `RCR AL,CL`, so this tests bit 1 of cellByte[arg0], after
	// the same signed id bound check against the active block count.
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (int16(id) > int16(exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const bool set = Log.cellBit(id, 1);
	debugC(2, kDebugLevelScript, "opcode 0x16: if cell bit 1 of entity %s set (=%s)",
		   +a[0], set ? "yes" : "no");
	return set ? kThxBye : kFail;
}

OPCODE(0x18) {
	// DOS Op_18 @ 1000:39a9: SETS skip_counter when Object[a[0]].room == 0
	// (i.e. SKIPS the body when the object is missing). Net semantics: the
	// conditional body executes when the object is PRESENT. Ghidra's label
	// "IfObjectMissing" describes the SKIP condition, not the run condition.
	// Without a loaded Object table we default to "present" → run body.
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const bool missing = Log.isObjectMissing(id);
	debugC(2, kDebugLevelScript, "opcode 0x18: if object %s present (room=%u%s)",
		   +a[0], Log.getObjectRoom(id), Log.hasObjectRoom(id) ? "" : " default");
	return missing ? kFail : kThxBye;
}

OPCODE(0x1b) {
	// DOS Op_1b @ 1000:39e3: SETS skip_counter when Object[a[0]].room != 0
	// (i.e. SKIPS the body when the object is PRESENT). Net semantics: the
	// conditional body executes when the object is MISSING. Inverse of 0x18.
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const bool missing = Log.isObjectMissing(id);
	debugC(2, kDebugLevelScript, "opcode 0x1b: if object %s missing (room=%u%s)",
		   +a[0], Log.getObjectRoom(id), Log.hasObjectRoom(id) ? "" : " default");
	return missing ? kThxBye : kFail;
}

OPCODE(0x1e) {
	// DOS Op_1e_IfMainActorAtRoomFrame @ 1000:3a0a:
	//   MOV AX, CS:[0x010f] (g_main_character_id), then tail into
	//   the Op_1d room/frame tester. Run if protagonist.room ==
	//   current_loc AND byte field+0x61 == low(arg0).
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x1e: if protagonist at frame %s", +a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) != uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x20) {
	// DOS Op_20_IfMainActorNotAtRoomFrame @ 1000:3a33:
	// protagonist variant of Op_1f; same room check, byte frame mismatch.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x20: if protagonist not at frame %s", +a[0]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (ac->room() != Log.currentRoom() || uint8(ac->frameId()) == uint8(uint16(a[0])))
		return kFail;
	return kThxBye;
}

OPCODE(0x21) {
	// DOS Op_21_IfObjectUnplaced @ 1000:3a75: SETS skip_counter when Object[a[0]].room != -1
	// (i.e. SKIPS the body when the object IS placed). Net semantics: the
	// conditional body executes when the object is NOT placed (room == 0xffff).
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const uint16 room = Log.getObjectRoom(id);
	const bool unplaced = (room == uint16(0xffff));
	debugC(2, kDebugLevelScript, "opcode 0x21: if object %s unplaced (room=%u%s)",
		   +a[0], room, Log.hasObjectRoom(id) ? "" : " default");
	return unplaced ? kThxBye : kFail;
}

OPCODE(0x22) {
	// DOS Op_22_IfStringEqualsBuf @ 1000:3a88:
	//   compare arg0 (null-terminated chars) against parser buffer
	//   (DS:0x4fa9 capacity, 0x4faa length, 0x4fab+ chars). Match if
	//   both end together (all chars equal up to length, then null
	//   on arg0 side). Skip on mismatch.
	// C++ models the parser buffer via `Logic::_parserBuffer`. Filled
	// by Op_e9 (append), cleared by Op_e7, popped by Op_eb.
	const byte *s = static_cast<byte *>(a[0]);
	const Common::String &buf = Log.parserBuffer();
	debugC(2, kDebugLevelScript, "opcode 0x22: if input '%s' == arg0 '%s'",
		   buf.c_str(), s ? reinterpret_cast<const char *>(s) : "(null)");
	if (!s)
		return kFail;
	if (strlen(reinterpret_cast<const char *>(s)) != buf.size())
		return kFail;
	if (memcmp(s, buf.c_str(), buf.size()) != 0)
		return kFail;
	return kThxBye;
}

OPCODE(0x23) {
	// DOS Op_23_IfStringsEqual @ 1000:3a9c.
	//   arg0 has 2-byte header (byte 0 unused, byte 1 = length), chars at +2.
	//   arg1 is null-terminated chars.
	//   Compare byte-by-byte for `length` chars; both must end together.
	// In C++ both arg0 and arg1 are `ParametrizedString` instances:
	//   `static_cast<byte *>(a[i])` returns the translated, null-terminated
	//   `_translateBuf`; `uint16(a[i])` returns the `_length` field. The
	//   DOS arg0/arg1 format asymmetry (length-prefix vs null-term) is
	//   flattened by the C++ argument loader. The Ghidra-faithful
	//   comparison is "are the two translated strings equal?". `_length`
	//   includes the terminating NUL in C++, while DOS's CL counter does
	//   not, so use strlen() for the payload length.
	const byte *s = static_cast<byte *>(a[0]);
	const byte *t = static_cast<byte *>(a[1]);
	const uint16 sLen = s ? uint16(strlen(reinterpret_cast<const char *>(s))) : 0;
	debugC(2, kDebugLevelScript, "opcode 0x23: if %s == %s", +a[0], +a[1]);
	if (!s || !t)
		return kFail;
	for (uint16 i = 0; i < sLen; ++i) {
		if (t[i] == 0 || s[i] != t[i]) // arg1 ended early or mismatch
			return kFail;
	}
	if (t[sLen] != 0) // arg1 has more chars beyond arg0's length
		return kFail;
	return kThxBye;
}

OPCODE(0x26) {
	// DOS Op_26_RunCheckActorIfStepCursor4 @ 1000:382f:
	//   if (step_pending && cursor == 4) {
	//       AX = g_main_character_id;
	//       CheckActorScripting(AX);    // CF=1 if actor.f6f==0 && word f6b==0
	//       if (CF == 1) JMP Op_41_SpeakAsMainNoTarget;
	//   }
	// Net: speak-as-main only if the main char is "idle" (both
	// scripting fields 0). Otherwise no-op.
	if (!Log.stepPending() || Log.cursorMode() != 4)
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(protag);
	// CheckActorScripting: idle iff both field+0x6f (byte) and
	// word field+0x6b are zero.
	const bool idle = !protag->movementWaitActive() && protag->walkQueueLength() == 0;
	debugC(2, kDebugLevelScript, "opcode 0x26: step+cursor==4, protag idle=%d", int(idle));
	if (!idle)
		return kThxBye;
	// Tail-jump to Op_41: speak as main, no target.
	const SpeechDeferResult speechWait = deferSpeechUntilReady(protag, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(protag, a[0]);
	return kThxBye;
}

OPCODE(0x27) {
	// DOS Op_27_RunOp3fIfStepCursor4 @ 1000:381d:
	//   if (g_flag_step_pending && g_cursor_mode == 4)
	//       Op_3f_SpeakAsMainCharacter();
	// nargs=1 — when the gate fires, dispatches into Op_3f with the same
	// arg list (which Op_3f's ResolveOpcodeArg0 will consume).
	debugC(2, kDebugLevelScript, "opcode 0x27: if step && cursor==4, speak as main");
	if (Log.stepPending() && Log.cursorMode() == 4) {
		Actor *protag = Log.protagonist();
		const MainSpeechTargetResult targetSpeech =
			speakAsMainAfterOptionalTargetWalk(protag, a[0], 0, current);
		if (targetSpeech == kMainSpeechError)
			return kThxBye;
		if (targetSpeech == kMainSpeechWait)
			return kReturn;
		if (targetSpeech == kMainSpeechDone)
			return kThxBye;
		const SpeechDeferResult speechWait =
			deferMainSpeechNoTargetUntilReady(protag, current);
		if (speechWait == kSpeechWaitError)
			return kThxBye;
		if (speechWait == kSpeechWait)
			return kReturn;
		speakOrSubtitle(protag, a[0]);
	}
	return kThxBye;
}

OPCODE(0x28) {
	// DOS Op_28_IfModeIs80 @ 1000:384a:
	//   if (cursor_mode == 0x80) {
	//       arg0 = anim mask;  CycleAllAnimationsByMask(arg0);
	//       arg1 = text;       DrawCenteredOverlayText(arg1);
	//   }
	// fall through (no skip_counter).
	if (Log.cursorMode() != 0x80)
		return kThxBye;
	debugC(2, kDebugLevelScript, "opcode 0x28: cursor==0x80, anim mask=%s text=%s",
		   +a[0], +a[1]);

	// Side effect 1: CycleAllAnimationsByMask @ 1000:c8a1 advances 5
	// cursor-overlay animation slots based on arg0 bits (1, 2, 0x10,
	// 4, 8), in slot order CS:[0xa9], [0xab], [0xaf], [0xad], [0xb1].
	// Each selected slot advances its frame index and redraws the old frame
	// at the slot's x/y via DrawSprite @ 1000:a748.
	const uint16 mask = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x28: cycle cursor-overlay anims mask=0x%02x", mask);
	const uint16 animationBits[] = {0x01, 0x02, 0x10, 0x04, 0x08};
	for (uint i = 0; i < ARRAYSIZE(animationBits); ++i) {
		if ((mask & animationBits[i]) == 0)
			continue;
		uint16 spriteId = 0;
		uint16 x = 0;
		uint16 y = 0;
		if (_logic->resources()->mainDat()->cycleCursorOverlayAnimation(animationBits[i], spriteId, x, y)) {
			_graphics->setInterfaceOverlaySprite(animationBits[i], spriteId, x, y);
			Sprite *sprite = _logic->resources()->loadSprite(spriteId);
			_graphics->paint(sprite, Common::Point(x, y), Graphics::kPaintPositionIsTop);
			delete sprite;
		}
	}

	const byte *text = a[1].rawPointer();
	if (!text)
		text = static_cast<byte *>(a[1]);
	if (text) {
		// DrawCenteredOverlayText @ 1000:c581 calls c5fa once per line:
		// copy up to 100 raw bytes until CR/NUL, measure that copied line,
		// error 0x2c when width > 0x38, then draw shadow and foreground.
		// It draws from the raw argument pointer, not the translated string
		// buffer, and queues one fixed dirty rect after all lines. C++ also
		// remembers the result because paintInterface() redraws the base
		// interface every frame, unlike DOS's dirty-rect-presented buffer.
		if (!_graphics->setStatusOverlayText(text)) {
			Log.setPendingError(0x2c);
			return kThxBye;
		}
		_graphics->markDirtyRect(Common::Rect(4, 0xb4, 4 + 0x38, 0xb4 + 0x12));
	}
	return kThxBye;
}

OPCODE(0x29) {
	// DOS Op_29_IfMode10AndFlag @ 1000:3863:
	//   if (step_pending && cursor == 0x10) {
	//       SendActorToTarget();       ; protag walks to current entity
	//       SetPostMoveCallback(BP=0x4376, AX=arg0, BX=CX=arg1);
	//   }
	// Falls through unconditionally (no skip_counter).
	if (Log.stepPending() && Log.cursorMode() == 0x10) {
		debugC(2, kDebugLevelScript,
			   "opcode 0x29: walk current entity, then place protag room %s frame %s",
			   +a[0], +a[1]);
		if (sendActorToCurrentEntityCarryClear(Log.protagonist())) {
			Log.setPostMoveCallback(
				Logic::PostMoveCallback::kPlaceProtagonistAfterMove,
				uint16(a[0]),
				uint8(uint16(a[1])),
				uint8(uint16(a[1])));
		}
	}
	return kThxBye;
}

OPCODE(0x2a) {
	// DOS Op_2a_IfMode10AndFlag2 @ 1000:387e: 3-arg variant of 0x29.
	if (Log.stepPending() && Log.cursorMode() == 0x10) {
		debugC(2, kDebugLevelScript,
			   "opcode 0x2a: walk current entity, then place protag room %s frame %s next %s",
			   +a[0], +a[1], +a[2]);
		if (sendActorToCurrentEntityCarryClear(Log.protagonist())) {
			Log.setPostMoveCallback(
				Logic::PostMoveCallback::kPlaceProtagonistAfterMove,
				uint16(a[0]),
				uint8(uint16(a[1])),
				uint8(uint16(a[2])));
		}
	}
	return kThxBye;
}

OPCODE(0x2b) {
	// DOS Op_2b_BranchOnFrameMismatch @ 1000:3a5c.
	// Reads `g_main_character_id` (CS:[0x10f]), GetActorOffset → SI =
	// protagonist record. If actor.frame != arg0:
	//   `g_codeptr_di_save = arg1` — IMMEDIATE jump to arg1. Body
	//   between Op_2b and the destination is SKIPPED.
	// If equal: fall through.
	Actor *ac = Log.protagonist();
	debugC(2, kDebugLevelScript, "opcode 0x2b: jump unless protagonist at %s -> %s", +a[0], +a[1]);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Log.setImplicitActor(ac);
	if (uint8(ac->frameId()) != uint8(uint16(a[0])))
		return static_cast<CodePointer &>(a[1]);
	return kThxBye;
}

OPCODE(0x2e) {
	// DOS Op_2e_RestoreBranchFromSave @ 1000:3b16:
	//   g_branch_state = g_codeptr_di_save;
	// Saves the CURRENT script PC (= start of switch loop body) so
	// Op_2f..Op_34 can jump back here on case mismatch ("try next
	// case"). The dispatcher passes the next-instruction CodePointer
	// as `next` — that's the position right after Op_2e itself, which
	// is exactly what DOS captures.
	debugC(2, kDebugLevelScript, "opcode 0x2e: branch-state = current PC");
	Log.setBranchState(next.offset());
	return kThxBye;
}

// Case-comparison family (DOS Op_2f..Op_34 @ 1000:3b1d..1000:3bc8). All
// share the structure:
//   if (g_branch_state == 0) g_pendingErrorCode = 4; return;
//   if (SKIP_COND) g_codeptr_di_save = g_branch_state; return;  // jump back
//   g_branch_state = 0; return;                                  // case match
// SKIP_COND varies per opcode (the case label = SKIP condition):
//   0x2f CaseNotEqual:        arg0 != arg1
//   0x30 CaseEqual:           arg0 == arg1
//   0x31 CaseGreater:         arg1 <= arg0 (signed)  → run if arg0 < arg1
//   0x32 CaseLess:            arg0 <= arg1 (signed)  → run if arg0 > arg1
//   0x33 CaseGreaterOrEqual:  arg1 < arg0 (signed)   → run if arg0 <= arg1
//   0x34 CaseLessOrEqual:     arg0 < arg1 (signed)   → run if arg0 >= arg1
// (Note: Ghidra labels describe the SKIP condition, not the run condition.)
//
// Pending-error (rule 2): code 4 = "no active switch". The interpreter
// halts on this pending error just like the DOS main loop.

OPCODE(0x2f) {
	debugC(2, kDebugLevelScript, "opcode 0x2f: case-not-equal %s vs %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) {
		Log.setPendingError(0x04);
		return kThxBye;
	}
	if (uint16(a[0]) != uint16(a[1]))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x30) {
	debugC(2, kDebugLevelScript, "opcode 0x30: case-equal %s vs %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) {
		Log.setPendingError(0x04);
		return kThxBye;
	}
	if (uint16(a[0]) == uint16(a[1]))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x31) {
	debugC(2, kDebugLevelScript, "opcode 0x31: case-greater (skip if a0>=a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) {
		Log.setPendingError(0x04);
		return kThxBye;
	}
	if (int16(uint16(a[0])) >= int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x32) {
	debugC(2, kDebugLevelScript, "opcode 0x32: case-less (skip if a0<=a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) {
		Log.setPendingError(0x04);
		return kThxBye;
	}
	if (int16(uint16(a[0])) <= int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x33) {
	debugC(2, kDebugLevelScript, "opcode 0x33: case-ge (skip if a0>a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) {
		Log.setPendingError(0x04);
		return kThxBye;
	}
	if (int16(uint16(a[0])) > int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}
OPCODE(0x34) {
	debugC(2, kDebugLevelScript, "opcode 0x34: case-le (skip if a0<a1 signed) %s %s", +a[0], +a[1]);
	const uint16 bs = Log.branchState();
	if (bs == 0) {
		Log.setPendingError(0x04);
		return kThxBye;
	}
	if (int16(uint16(a[0])) < int16(uint16(a[1])))
		return CodePointer(bs, this);
	Log.setBranchState(0);
	return kThxBye;
}

OPCODE(0x38) {
	// DOS Op_38_SwitchToScene @ 1000:3c58:
	//   SaveCastBackup;  // memcpy cast table (0x642 bytes)
	//   SaveActorTableBackup;
	//   ResolveOpcodeArg0;  // arg0 = new scene id
	//   LoadRoomLevelHeader;  // second prog table: roomProgramCount + scene id
	//   _g_block_pc_offset  = g_codeptr_es_save;  // save caller PC
	//   _g_block_pc_segment = g_codeptr_di_save;
	//   g_codeptr_es_save = g_seg_buffer_e;       // jump to new scene
	//   g_codeptr_di_save = g_room_list_ptr;      // offset 2
	// = "call into a sub-scene". Op_01_handler's nested-pop path
	// then restores the saved PC.
	const uint16 sceneId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x38: switch to scene %u (push)", sceneId);
	return Log.switchToScene(sceneId, next);
}

// Speech variants (DOS speech-opcode range @ 1000:3da2..1000:3e68). The engine routes everything via
// Actor::say, which queues a speech bubble for the calling actor. Variants
// differ by speaker (main vs identified actor) and target (none vs hotspot).
OPCODE(0x3f) {
	// DOS Op_3f_SpeakAsMainCharacter @ 1000:3d29: speak text (arg0)
	// as the protagonist. Status-mode → subtitle; else → speech-slot
	// allocation for the main char. If no explicit hit-region is
	// active DOS first sends the protagonist toward the current entity.
	debugC(1, kDebugLevelScript, "opcode 0x3f: main says %s", +a[0]);
	Actor *protag = Log.protagonist();
	const MainSpeechTargetResult targetSpeech =
		speakAsMainAfterOptionalTargetWalk(protag, a[0], 0, current);
	if (targetSpeech == kMainSpeechError)
		return kThxBye;
	if (targetSpeech == kMainSpeechWait)
		return kReturn;
	if (targetSpeech == kMainSpeechDone)
		return kThxBye;
	const SpeechDeferResult speechWait =
		deferMainSpeechNoTargetUntilReady(protag, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(protag, a[0]);
	return kThxBye;
}
OPCODE(0x40) {
	// DOS Op_40_SpeakAtTarget @ 1000:3da2: arg0=maxLines, arg1=text.
	// The pre-walk path still uses the current entity globals through
	// SendActorToTarget @ 1000:7323; arg0 is carried in BX to the speech
	// pagination helper, not used as an actor/object id.
	Actor *protag = Log.protagonist();
	const uint16 maxLines = uint16(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x40: main says %s maxLines=%u",
		   +a[1], maxLines);
	const MainSpeechTargetResult targetSpeech =
		speakAsMainAfterOptionalTargetWalk(protag, a[1], maxLines, current);
	if (targetSpeech == kMainSpeechError)
		return kThxBye;
	if (targetSpeech == kMainSpeechWait)
		return kReturn;
	if (targetSpeech == kMainSpeechDone)
		return kThxBye;
	const SpeechDeferResult speechWait =
		deferMainSpeechNoTargetUntilReady(protag, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(protag, a[1], maxLines);
	return kThxBye;
}
OPCODE(0x42) {
	// DOS Op_42_SpeakAsMainAtTarget @ 1000:3e04: arg0=maxLines, arg1=text.
	Actor *protag = Log.protagonist();
	const uint16 maxLines = uint16(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x42: main says %s maxLines=%u",
		   +a[1], maxLines);
	const SpeechDeferResult speechWait =
		deferMainSpeechNoTargetUntilReady(protag, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(protag, a[1], maxLines);
	return kThxBye;
}
OPCODE(0x44) {
	// DOS Op_44_SpeakAsActorAtTarget @ 1000:3e4f: arg0=actor id,
	// arg1=maxLines, arg2=text.
	const uint16 maxLines = uint16(a[1]);
	const Common::String text = a[2];
	const uint16 actorId = uint16(a[0]);
	debugC(1, kDebugLevelScript, "opcode 0x44: actor %u says %s maxLines=%u",
		   actorId, text.c_str(), maxLines);

	if (actorId == Log.protagonistId()) {
		Actor *protag = Log.protagonist();
		const SpeechDeferResult speechWait =
			deferMainSpeechNoTargetUntilReady(protag, current);
		if (speechWait == kSpeechWaitError)
			return kThxBye;
		if (speechWait == kSpeechWait)
			return kReturn;
		speakOrSubtitle(protag, text, maxLines);
		return kThxBye;
	}

	const SpeechDeferResult speechSlotWait = waitForActiveSpeechSlotOwner(actorId, current);
	if (speechSlotWait == kSpeechWaitError)
		return kThxBye;
	if (speechSlotWait == kSpeechWait)
		return kReturn;
	Actor *ac = getActorOrPending(actorId);
	if (!ac)
		return kThxBye;
	const SpeechDeferResult speechWait = deferSpeechUntilReady(ac, current);
	if (speechWait == kSpeechWaitError)
		return kThxBye;
	if (speechWait == kSpeechWait)
		return kReturn;
	speakOrSubtitle(ac, text, maxLines);
	Log.stashUiTextSpeechSlotForOwner(actorId);
	return kThxBye;
}
OPCODE(0x45) {
	// DOS Op_45_SpeakWithDelay @ 1000:3e68: 4 args (x, y, color, text).
	//   if (!map_mode) AllocSpeechSlot_NoFormatting + stash arg2;
	//   else status-mode subtitle.
	// AllocSpeechSlot_NoFormatting = narrator-style bubble at the
	// explicit (x, y) with color — NOT tied to any actor.
	const byte *text = static_cast<byte *>(a[3]);
	const uint16 x = uint16(a[0]);
	const uint16 y = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	debugC(1, kDebugLevelScript, "opcode 0x45: narrator at (%u,%u) color=%u text='%s'",
		   x, y, color, text ? reinterpret_cast<const char *>(text) : "(null)");
	if (sayNarratorOrSubtitle(text, x, y, color, 0, Graphics::kSpeechBubbleType1, current))
		return kReturn;
	return kThxBye;
}
OPCODE(0x46) {
	// DOS Op_46_SpeakWithDelayAlt @ 1000:3e5e: identical body to 0x45
	// except it seeds AX=2 for the alternate bubble mode before the
	// shared explicit-position narrator path stores the color hint.
	const byte *text = static_cast<byte *>(a[3]);
	const uint16 x = uint16(a[0]);
	const uint16 y = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	debugC(1, kDebugLevelScript, "opcode 0x46: narrator (alt) at (%u,%u) color=%u text='%s'",
		   x, y, color, text ? reinterpret_cast<const char *>(text) : "(null)");
	if (sayNarratorOrSubtitle(text, x, y, color, 0, Graphics::kSpeechBubbleType2, current))
		return kReturn;
	return kThxBye;
}

// 0x48..0x53: speech / sample / menu / text-bubble family. DOS dispatches
// these as speech, sample-registration, menu, and text-bubble handlers.
// Modal text/menu handlers stop dispatch with kReturn while their
// RunVerbMenuModalLoop-equivalent visible text is active.
OPCODE(0x48) {
	// DOS Op_48_SpeakWithRectAndPos @ 1000:3ea7: 5 args (x, y, color,
	// lines, text). Same shared-tail as Op_47 but reads via
	// ReadVarBySlot_RHS (different argument-resolution path); for
	// the script-observable behaviour the args have the same meaning.
	const byte *text = static_cast<byte *>(a[4]);
	const uint16 maxLines = uint16(a[3]);
	const uint16 x = uint16(a[0]);
	const uint16 y = uint16(a[1]);
	const byte color = uint8(uint16(a[2]) & 0xff);
	debugC(1, kDebugLevelScript, "opcode 0x48: narrator at (%u,%u) color=%u lines=%u text='%s'",
		   x, y, color, maxLines, text ? reinterpret_cast<const char *>(text) : "(null)");
	if (sayNarratorOrSubtitle(text, x, y, color, maxLines, Graphics::kSpeechBubbleType2, current))
		return kReturn;
	return kThxBye;
}
OPCODE(0x49) {
	// DOS Op_49_SetActorFlag70 @ 1000:3ec5: a[0]=actor id, a[1]=byte.
	// Sets Actor[a[0]].field_0x70 = (byte)a[1].
	const uint8 v = uint8(uint16(a[1]));
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x49: actor %s flag70 = %u", +a[0], v);
	Actor *ac = getActorOrPending(id);
	if (!ac)
		return kThxBye;
	ac->setSpeechColor(v);
	Log.setActorFlag70(id, v);
	return kThxBye;
}
OPCODE(0x4d) {
	// DOS Op_4d_StashMenuArgs @ 1000:3f0c:
	//   pbRam00023206 = ResolveOpcodeArg0;   // [DS:0x66b6] = arg0
	//   pbRam00023208 = ResolveOpcodeArg1;   // [DS:0x66b8] = arg1
	//   DAT_1cb5_6741 = 0;                   // [DS:0x6741] = 0 (clear stash flag)
	// Stashes the (positioning, hint) pair for the next bubble/menu op
	// (0x4f / 0x51 read these via the secondary-arg path). The
	// stash flag is RESET so Op_53 (DrawFixedTextBubbleStashed) takes
	// its non-stashed branch unless Op_50/0x51 fires in between.
	const uint16 stash0 = uint16(a[0]);
	Log.setMenuStashFirstArg(stash0);
	const uint16 stash1 = uint16(a[1]);
	Log.setMenuStashSecondArg(stash1);
	debugC(2, kDebugLevelScript, "opcode 0x4d: stash menu args (%u, %u)", stash0, stash1);
	// Sync new ModalState.stashFlag (canonical for Op_53's branch).
	Log.modalState().stashFlag = 0;
	return kThxBye;
}
OPCODE(0x4e) {
	// DOS Op_4e_DrawTextRectWithChoices @ 1000:3f1e:
	//   CALL ResolveOpcodeArg0;     ; AX = text ptr
	//   MOV DI, AX                   ; DI = text source for formatter
	//   CALL FormatBubbleText_FullPath; AX = total_height (low),
	//                                   ; DX = row count,
	//                                   ; CX = width minus frame bias
	//   MOV [0x66c2], AX             ; menu_choice_count = formatter result
	//   MOV [0x66c4], AX             ; menu_max_choices  = same
	//   MOV AX, CX                   ; AX = adjusted text width
	//   MOV BX, DX                   ; BX = row count
	//   PUSH ds; POP ES              ; ES = data segment
	//   MOV DI, 0x40b7               ; DI = formatted-buffer base
	//   MOV [0x66c6], 3              ; palette mode = 3 (text-rect+choices)
	//   MOV [0x6741], 0              ; clear stash flag
	//   JMP SetRectAndApply           ; → 0x3f86 → JMP RunVerbMenuModalLoop.
	const byte *text = static_cast<byte *>(a[0]);
	const byte *formatText = a[0].rawPointer() ? a[0].rawPointer() : text;
	Logic::FormattedBubble fb = _logic->formatBubbleText(formatText);
	Logic::ModalState &ms = Log.modalState();
	seedFormattedModalState(ms, fb, fb.totalHeight, fb.rowCount, 3, 0);
	debugC(1, kDebugLevelScript, "opcode 0x4e: DrawTextRectWithChoices text='%s' lines=%u h=%u",
		   text ? reinterpret_cast<const char *>(text) : "(null)",
		   fb.lineCount, fb.totalHeight);
	uint16 selectedIndex = 0xffff;
	uint16 target = 0xffff;
	if (runFormattedChoiceModal(fb, fb.rowCount, &selectedIndex, target)) {
		ms.selectedItemIdx = selectedIndex;
		finishVerbModalLoopState(ms);
		if (fb.truncated)
			Log.setPendingError(0x11);
		if (target == 0xffff)
			return kThxBye;
		return CodePointer(target, this);
	}
	const bool wait = showFormattedModalTextAndWait(fb, fb.totalHeight, next);
	finishVerbModalLoopState(ms);
	if (fb.truncated)
		Log.setPendingError(0x11);
	return wait ? kReturn : kThxBye;
}
OPCODE(0x4f) {
	// DOS Op_4f_DrawTextRectWithChoicesAlt @ 1000:3f45:
	//   CALL ResolveOpcodeArg1;      ; AX = text ptr (NOTE: arg1 first!)
	//   MOV DI, AX
	//   CALL FormatBubbleText_FullPath
	//   PUSH AX; PUSH DX             ; save formatter result + height
	//   CALL ResolveOpcodeArg0;      ; AX = arg0 (extra param)
	//   MOV BX, AX
	//   POP DX; POP AX
	//   CALL 9bcc                    ; limit returned height/rows
	//   MOV [0x66c2], AX; MOV [0x66c4], AX
	//   JMP into Op_4e's tail at 0x3f2c (= MOV AX, CX; ... fall through
	//                                    to SetRectAndApply with mode=3,
	//                                    stash=0).
	// = "DrawTextRectWithChoices but using arg1 as text, with arg0 as
	// the row/page limit for the formatter helper. Final state matches
	// Op_4e after the adjusted AX/DX pair."
	const byte *text = static_cast<byte *>(a[1]);
	const byte *formatText = a[1].rawPointer() ? a[1].rawPointer() : text;
	Logic::FormattedBubble fb = _logic->formatBubbleText(formatText);
	Logic::ModalState &ms = Log.modalState();
	uint16 menuValue = fb.totalHeight;
	uint16 rows = fb.rowCount;
	const uint16 limit = uint16(a[0]);
	applyFormattedTextLimit9bcc(limit, menuValue, rows);
	seedFormattedModalState(ms, fb, menuValue, rows, 3, 0);
	debugC(1, kDebugLevelScript, "opcode 0x4f: DrawTextRectWithChoicesAlt text='%s' limit=%u",
		   text ? reinterpret_cast<const char *>(text) : "(null)", limit);
	uint16 selectedIndex = 0xffff;
	uint16 target = 0xffff;
	if (runFormattedChoiceModal(fb, rows, &selectedIndex, target)) {
		ms.selectedItemIdx = selectedIndex;
		finishVerbModalLoopState(ms);
		if (fb.truncated)
			Log.setPendingError(0x11);
		if (target == 0xffff)
			return kThxBye;
		return CodePointer(target, this);
	}
	const bool wait = showFormattedModalTextAndWait(fb, menuValue, next);
	finishVerbModalLoopState(ms);
	if (fb.truncated)
		Log.setPendingError(0x11);
	return wait ? kReturn : kThxBye;
}
OPCODE(0x50) {
	// DOS Op_50_OpenVerbMenuModal @ 1000:3f61:
	//   CALL ResolveOpcodeArg0;     ; AX = text ptr
	//   MOV DI, AX
	//   CALL FormatBubbleText_FullPath
	//   MOV [0x66c2], AX; MOV [0x66c4], AX
	//   MOV AX, CX; MOV BX, DX
	//   PUSH ds; POP ES; MOV DI, 0x40b7
	//   MOV [0x66c6], 1            ; palette mode = 1 (verb-menu modal)
	//   MOV [0x6741], 1            ; SET stash flag
	//   ; falls through to SetRectAndApply.
	const byte *text = static_cast<byte *>(a[0]);
	const byte *formatText = a[0].rawPointer() ? a[0].rawPointer() : text;
	Logic::FormattedBubble fb = _logic->formatBubbleText(formatText);
	Logic::ModalState &ms = Log.modalState();
	seedFormattedModalState(ms, fb, fb.totalHeight, fb.rowCount, 1, 1);
	debugC(1, kDebugLevelScript, "opcode 0x50: OpenVerbMenuModal text='%s'",
		   text ? reinterpret_cast<const char *>(text) : "(null)");
	uint16 selectedIndex = 0xffff;
	uint16 target = 0xffff;
	if (runFormattedChoiceModal(fb, fb.rowCount, &selectedIndex, target)) {
		ms.selectedItemIdx = selectedIndex;
		finishVerbModalLoopState(ms);
		if (fb.truncated)
			Log.setPendingError(0x11);
		if (target == 0xffff)
			return kThxBye;
		return CodePointer(target, this);
	}
	const bool wait = showFormattedModalTextAndWait(fb, fb.totalHeight, next);
	finishVerbModalLoopState(ms);
	if (fb.truncated)
		Log.setPendingError(0x11);
	return wait ? kReturn : kThxBye;
}
OPCODE(0x51) {
	// DOS Op_51_OpenVerbMenuModalAlt @ 1000:3f99:
	//   CALL ResolveOpcodeArg1;     ; arg1 = text
	//   MOV DI, AX
	//   CALL FormatBubbleText_FullPath
	//   PUSH AX; PUSH DX
	//   CALL ResolveOpcodeArg0;     ; arg0 = positioning hint
	//   MOV BX, AX
	//   POP DX; POP AX
	//   CALL 9bcc                   ; limit returned height/rows
	//   MOV [0x66c2], AX; MOV [0x66c4], AX
	//   JMP 0x3f6f (Op_50's tail: palette=1, stash=1).
	const byte *text = static_cast<byte *>(a[1]);
	const byte *formatText = a[1].rawPointer() ? a[1].rawPointer() : text;
	Logic::FormattedBubble fb = _logic->formatBubbleText(formatText);
	Logic::ModalState &ms = Log.modalState();
	uint16 menuValue = fb.totalHeight;
	uint16 rows = fb.rowCount;
	const uint16 limit = uint16(a[0]);
	applyFormattedTextLimit9bcc(limit, menuValue, rows);
	seedFormattedModalState(ms, fb, menuValue, rows, 1, 1);
	debugC(1, kDebugLevelScript, "opcode 0x51: OpenVerbMenuModalAlt text='%s' limit=%u",
		   text ? reinterpret_cast<const char *>(text) : "(null)", limit);
	uint16 selectedIndex = 0xffff;
	uint16 target = 0xffff;
	if (runFormattedChoiceModal(fb, rows, &selectedIndex, target)) {
		ms.selectedItemIdx = selectedIndex;
		finishVerbModalLoopState(ms);
		if (fb.truncated)
			Log.setPendingError(0x11);
		if (target == 0xffff)
			return kThxBye;
		return CodePointer(target, this);
	}
	const bool wait = showFormattedModalTextAndWait(fb, menuValue, next);
	finishVerbModalLoopState(ms);
	if (fb.truncated)
		Log.setPendingError(0x11);
	return wait ? kReturn : kThxBye;
}
OPCODE(0x52) {
	// DOS Op_52_DrawFixedTextBubble @ 1000:3ff6:
	//   CALL ResolveOpcodeArg0;     ; AX = text
	//   MOV DI, AX
	//   CALL MeasureVerbBubbleTextHeight @ 1000:8eb7
	//                                ; → updates a different metric
	//                                  (uses already-formatted text)
	//   MOV [0x66c6], 2;             ; palette mode = 2 (fixed bubble)
	//   MOV [0x66c2], 0;             ; choice count = 0 (no choices)
	//   MOV [0x6741], 0;             ; clear stash
	//   JMP SetRectAndApply.
	const byte *text = static_cast<byte *>(a[0]);
	const byte *measureText = a[0].rawPointer() ? a[0].rawPointer() : text;
	Logic::FormattedBubble fb = _logic->measureVerbBubbleText(measureText);
	Logic::ModalState &ms = Log.modalState();
	ms.menuChoiceCount = 0; // DOS sets [0x66c2] = 0 explicitly
	// menuMaxChoices retains its current value; DOS doesn't write [0x66c4] here.
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.lineCount;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.paletteMode = 2;
	ms.stashFlag = 0;
	ms.selectedItemIdx = 0xffff;
	ms.textContinuationPtr = 0;
	ms.menuDone = false;
	debugC(1, kDebugLevelScript, "opcode 0x52: DrawFixedTextBubble text='%s' h=%u",
		   text ? reinterpret_cast<const char *>(text) : "(null)", fb.totalHeight);
	uint16 selectedIndex = 0xffff;
	uint16 target = 0xffff;
	if (runRawChoiceListModal(measureText, &selectedIndex, target)) {
		ms.selectedItemIdx = selectedIndex;
		finishVerbModalLoopState(ms);
		if (fb.truncated)
			Log.setPendingError(0x11);
		if (target == 0xffff)
			return kThxBye;
		return CodePointer(target, this);
	}
	const bool wait = showFormattedModalTextAndWait(fb, fb.totalHeight, next);
	finishVerbModalLoopState(ms);
	if (fb.truncated)
		Log.setPendingError(0x11);
	return wait ? kReturn : kThxBye;
}
OPCODE(0x53) {
	// DOS Op_53_DrawFixedTextBubbleStashed @ 1000:3fb5:
	//   CALL ResolveOpcodeArg0;     ; AX = text
	//   MOV DI, AX
	//   CMP [0x6741], 0
	//   JZ fallthrough_fixed         ; not stashed → normal bubble (Op_52 path)
	//   ; STASHED PATH:
	//   MOV SI, [0x66be]; MOV [0x66b2], SI  ; saved AX = active AX
	//   MOV SI, [0x66c0]; MOV [0x66b4], SI  ; saved BX = active BX
	//   MOV SI, [0x66bc]; MOV [0x66b0], SI  ; saved ES
	//   MOV SI, [0x66ba]; MOV [0x66ae], SI  ; saved DI
	//   CALL MeasureVerbBubbleTextHeight
	//   MOV [0x66c6], 4              ; palette = 4 (stashed-bubble)
	//   MOV [0x66c2], 0              ; choices = 0
	//   MOV [0x6741], 0              ; clear stash flag
	//   JMP SetRectAndApply
	//   fallthrough_fixed:           ; same as Op_52 but DI already set
	//     CALL MeasureVerbBubbleTextHeight
	//     MOV [0x66c6], 2; MOV [0x66c2], 0; MOV [0x6741], 0
	//     JMP SetRectAndApply
	const byte *text = static_cast<byte *>(a[0]);
	const byte *measureText = a[0].rawPointer() ? a[0].rawPointer() : text;
	Logic::ModalState &ms = Log.modalState();
	const bool useStash = ms.stashFlag != 0;
	if (useStash) {
		// Stash the active modal slot into the saved slot.
		ms.savedAx = ms.activeAx;
		ms.savedBx = ms.activeBx;
		ms.savedEs = ms.activeEs;
		ms.savedDi = ms.activeDi;
		ms.savedText = ms.activeText;
	}
	Logic::FormattedBubble fb = _logic->measureVerbBubbleText(measureText);
	if (useStash) {
		ms.paletteMode = 4;
		ms.menuChoiceCount = 0;
		ms.stashFlag = 0;
		debugC(1, kDebugLevelScript, "opcode 0x53: DrawFixedTextBubbleStashed (STASHED) text='%s'",
			   text ? reinterpret_cast<const char *>(text) : "(null)");
	} else {
		// Same as Op_52.
		ms.paletteMode = 2;
		ms.menuChoiceCount = 0;
		ms.stashFlag = 0;
		debugC(1, kDebugLevelScript, "opcode 0x53: DrawFixedTextBubbleStashed (FIXED, no stash) text='%s'",
			   text ? reinterpret_cast<const char *>(text) : "(null)");
	}
	ms.activeAx = fb.maxLineWidth;
	ms.activeBx = fb.lineCount;
	ms.activeEs = 0;
	ms.activeDi = 0x40b7;
	ms.selectedItemIdx = 0xffff;
	ms.textContinuationPtr = 0;
	ms.menuDone = false;
	uint16 selectedIndex = 0xffff;
	uint16 target = 0xffff;
	if (runRawChoiceListModal(measureText, &selectedIndex, target)) {
		ms.selectedItemIdx = selectedIndex;
		finishVerbModalLoopState(ms);
		if (fb.truncated)
			Log.setPendingError(0x11);
		if (target == 0xffff)
			return kThxBye;
		return CodePointer(target, this);
	}
	const bool wait = showFormattedModalTextAndWait(fb, fb.totalHeight, next);
	finishVerbModalLoopState(ms);
	if (fb.truncated)
		Log.setPendingError(0x11);
	return wait ? kReturn : kThxBye;
}

// 0x58..0x5e: state-getter family. Each is `MOV BX,[DS:slot]; JMP
// StoreOpcodeArg0Value` — a one-instruction read-and-store. The
// LABELS in Ghidra (Op_58_StoreCursorMode etc.) were AUTO-GENERATED
// and bear no relation to what's actually read. Cross-checked
// against actual disassembly addresses 1000:408f..1000:40bd.
OPCODE(0x58) {
	// DOS Op_58 @ 1000:408f: BX = [DS:0x661b] = g_draw_command_count.
	// Count of pending draw commands queued via AddDrawCommand.
	// C++ tracks via Logic::_drawCommandCount; reset per frame and
	// incremented at draw-command queue sites.
	a[0] = Log.drawCommandCount();
	debugC(2, kDebugLevelScript, "opcode 0x58: %s = g_draw_command_count (%u)",
		   +a[0], Log.drawCommandCount());
	return kThxBye;
}
OPCODE(0x59) {
	// DOS Op_59 @ 1000:4096: BX = [DS:0x661f] = g_exit_count.
	// = number of exits currently loaded for the active block.
	a[0] = uint16(_logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0);
	debugC(2, kDebugLevelScript, "opcode 0x59: %s = g_exit_count (%u)", +a[0], uint16(a[0]));
	return kThxBye;
}
OPCODE(0x5a) {
	// DOS Op_5a @ 1000:409d: BX = [DS:0x666e] = current entity type.
	a[0] = Log.gameState();
	debugC(2, kDebugLevelScript, "opcode 0x5a: %s = current entity type (%u)", +a[0], Log.gameState());
	return kThxBye;
}
OPCODE(0x5b) {
	// DOS Op_5b @ 1000:40a4: BX = [DS:0x666c] = current-entity-id.
	// Read by entity scripts during dispatch to know which entity
	// "this" script is for. C++ tracks via Logic::_currentEntityId,
	// updated at script-dispatch sites.
	a[0] = Log.currentEntityId();
	debugC(2, kDebugLevelScript, "opcode 0x5b: %s = current-entity-id (%u)",
		   +a[0], Log.currentEntityId());
	return kThxBye;
}
OPCODE(0x5c) {
	// DOS Op_5c @ 1000:40ab: BX = [DS:0x6670] = g_game_score.
	a[0] = Log.gameScore();
	debugC(2, kDebugLevelScript, "opcode 0x5c: %s = g_game_score (%u)", +a[0], Log.gameScore());
	return kThxBye;
}
OPCODE(0x5d) {
	// DOS Op_5d @ 1000:40b2: store score percent + tenths.
	//   ComputePercentTenths @ 1000:7e24:
	//     score = g_game_score; max = CS:[0x91];
	//     if score==0 || max==0: BX=0, CX=0;
	//     else: BX = (score*100)/max;        ; integer percent
	//           rem = (score*100)%max;
	//           CX = (rem*10)/max;            ; tenths digit
	//   StoreOpcodeArg0Value(BX);  // write percent to arg0 LHS
	//   WriteVarBySlot_LHS(CX);    // write tenths to arg1 LHS
	const uint16 score = Log.gameScore();
	const uint16 maxScore = Log.maxGameScore();
	uint16 percent = 0;
	uint16 tenths = 0;
	if (score != 0 && maxScore != 0) {
		// DOS `MUL 100` produces DX:AX, then the routine clears DX before
		// `DIV max`, so only the low 16 bits of score*100 participate.
		const uint16 sc100 = uint16(score * 100);
		percent = uint16(sc100 / maxScore);
		const uint16 rem = sc100 % maxScore;
		if (rem != 0)
			tenths = uint16(uint16(rem * 10) / maxScore);
	}
	a[0] = percent;
	a[1] = tenths;
	debugC(2, kDebugLevelScript, "opcode 0x5d: score percent=%u.%u (score=%u/%u)",
		   percent, tenths, score, maxScore);
	return kThxBye;
}
OPCODE(0x5e) {
	// DOS Op_5e @ 1000:40bd: BX = [DS:0x667c] = g_drag_target.
	a[0] = Log.dragTarget();
	debugC(2, kDebugLevelScript, "opcode 0x5e: %s = g_drag_target (%u)", +a[0], Log.dragTarget());
	return kThxBye;
}
OPCODE(0x5f) {
	// DOS Op_5f_TableLookupResource @ 1000:40cb:
	//   walk_speed_flag = 0;             ; resource segment
	//   arg1 = search value, arg2 = field offset, arg0 = table ptr,
	//   arg3 = destination LHS.
	//   width_words = arg0[0]; (entry length minus index)
	//   loop entries; if entry[0] matches arg1, BX = entry[2+arg2];
	//   else BX = 0xffff.
	//   WriteVarBySlot3_LHS(BX) → arg3.
	// Sister of Op_60 (same algorithm, different memory bank). DOS sets
	// g_walk_speed_flag=0 here, so arg0 is resolved against the resource
	// bank regardless of which script bank is currently executing.
	Log.setWalkSpeedFlag(0);
	const uint16 searchKey = uint16(a[1]);
	const uint16 fieldOffset = uint16(a[2]);
	const uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	byte *base = _logic->mainInterpreter() ? _logic->mainInterpreter()->rawCode(offset) : nullptr;
	uint16 value = 0xffff;
	if (base) {
		byte *pos = base;
		const uint16 width = READ_LE_UINT16(pos);
		pos += 2;
		while (true) {
			const uint16 index = READ_LE_UINT16(pos);
			if (index == 0xffff)
				break;
			pos += 2;
			if (index == searchKey) {
				value = READ_LE_UINT16(pos + fieldOffset);
				break;
			}
			pos += width * 2;
		}
	}
	a[3] = value;
	debugC(2, kDebugLevelScript, "opcode 0x5f: table lookup arg0=0x%04x search=%u field=%u -> %u",
		   offset, searchKey, fieldOffset, value);
	return kThxBye;
}

// 0x61..0x65: entity-field assign / table-lookup-assign family.
// DOS uses a "the LHS of arg2 was already resolved to an
// entity-record-relative pointer during arg parsing" mechanism that
// the C++ Value system doesn't directly model. The functional effect
// of each: arg2's LHS receives arg1's value (or in 0x64/0x65, the
// matched entry's segment is written to arg2's LHS). C++ implements
// these via the ValueVector's `a[2] = arg1` write, which dispatches
// to the underlying Value's operator= (writes to the variable slot
// when the slot is WordVariable/ByteVariable, no-op for Constant).
OPCODE(0x61) {
	// DOS Op_61_ReadExitField @ 1000:411b:
	//   1. ResolveOpcodeArg0 (exit id);
	//   2. if (id > g_exit_count) → pending-error 0x13;
	//   3. GetExitOffset(id) → ES:SI;
	//   4. ValidateTypeAndWriteVar2 @ 1000:4146:
	//      ResolveOpcodeArg1 → AX (low=offset, high=size);
	//      size==1 -> BL = byte ptr ES:[offset + SI];
	//      size==2 -> BX = word ptr ES:[offset + SI];
	//      size==4 -> BX = word ptr ES:[offset + SI], then CX is read
	//                 from word ptr ES:[SI + BX + 2].
	//      else → pending-error 2;
	//      WriteVarBySlot2_LHS chooses arg2's destination width and writes
	//      BX, plus CX only when arg2's slot is 4 bytes wide.
	// = "READ a sized field from the exit record at (arg1.lo) of width
	// (arg1.hi), store in arg2 LHS". Exit record fields beyond the
	// modeled room/position/z-index bytes fall through to Logic._exitFields
	// sparse storage (zero-default).
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x14);
		recordId = 1;
	}
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(recordId) : 0;
	if (!exit) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const uint16 value = exitRecordSizedLowWord(_logic, exit, recordId, off, sz);
	debugC(2, kDebugLevelScript, "opcode 0x61: ReadExitField id=%u off=0x%02x sz=%u → %u",
		   id, off, sz, value);
	a[2] = value;
	return kThxBye;
}
OPCODE(0x62) {
	// DOS Op_62_ReadObjectField @ 1000:412a: same shape as 0x61 with
	// GetObjectOffset and g_persons_count bound. Read object[id]'s
	// sized field at offset (arg1.lo) into arg2 LHS.
	const uint16 id = uint16(a[0]);
	const uint16 objectCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(id, objectCount)) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		recordId = 1;
	}
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	const uint16 value = objectRecordSizedLowWord(_logic, recordId, off, sz);
	debugC(2, kDebugLevelScript, "opcode 0x62: ReadObjectField id=%u off=0x%02x sz=%u → %u",
		   id, off, sz, value);
	a[2] = value;
	return kThxBye;
}
// Op_64 / Op_65 helper — both opcodes share the same table-iterate +
// match-key + write-segment loop; the only differences are
// (1) which segment value is written (resource vs block), and
// (2) the walk_speed_flag setting before arg resolution. The DOS
// flag is a side-channel that arg-parsing can read; in C++ it's
// captured directly via the segValue parameter.
//
// DOS Op_64 / Op_65 algorithm @ 1000:418c / 1000:4185 → shared body:
//   walk_speed_flag = 0 (Op_64) or 1 (Op_65);
//   ResolveOpcodeArg3, ResolveOpcodeArg1 → CX (search key),
//   ResolveOpcodeArg2 → DX (field offset),
//   ResolveOpcodeArg0 → SI (table base);
//   BX = walk_speed_flag ? g_seg_buffer_e : g_resourceSegment;
//   MOV DS, BX;                            ; switch to source segment
//   DI = [SI];                              ; first word = entry stride (words)
//   SI += 2;
//   if (DI == 0) → pending error 0x1a;
//   DI *= 2;                                ; stride bytes
//   loop:
//     AX = [SI];  SI += 2;                  ; entry's first word = key
//     if (AX == 0xffff) → pending error 0x1a;  ; sentinel before match
//     if (CX == AX) match → goto write;
//     SI += DI;                              ; advance by entry stride
//     loop;
//   write: SI += DX;                          ; SI = entry.field
//          [SI] = BX;                         ; write segment id
//
// C++ algorithm: identical iterate+match. The C++ engine models DOS
// segment words with the same tags used by code-pointer sized writes and
// actor/animation records: 0x1cb5 for the main/resource bank and a stable
// per-block tag for the loaded block bank. Op_64/0x65 write those modeled
// segment words into the matched table field.
static void doTableLookupAssign(Logic *logic, ValueVector &a, Interpreter *bank,
								uint16 segValue,
								const char *opname, uint8 dbgOpcode) {
	const uint16 ignoredArg3 = uint16(a[3]);
	(void)ignoredArg3;
	const uint16 searchKey = uint16(a[1]);
	const uint16 fieldOffset = uint16(a[2]);
	const uint16 offset = static_cast<CodePointer &>(a[0]).offset();
	byte *base = bank ? bank->rawCode(offset) : nullptr;
	if (!base) {
		logic->setPendingError(0x1a);
		return;
	}
	byte *pos = base;
	const uint16 width = READ_LE_UINT16(pos);
	if (width == 0) {
		logic->setPendingError(0x1a);
		return;
	}
	pos += 2;
	bool matched = false;
	while (true) {
		const uint16 index = READ_LE_UINT16(pos);
		if (index == 0xffff)
			break;
		pos += 2;
		if (index == searchKey) {
			WRITE_LE_UINT16(pos + fieldOffset, segValue);
			matched = true;
			break;
		}
		pos += width * 2;
	}
	if (!matched)
		logic->setPendingError(0x1a);
	debugC(2, kDebugLevelScript, "opcode 0x%02x: %s (table @ 0x%04x search=%u field=%u segValue=0x%04x match=%d)",
		   dbgOpcode, opname, offset, searchKey, fieldOffset, segValue, int(matched));
}

OPCODE(0x64) {
	// DOS Op_64_TableLookupAssignMain @ 1000:418c. Writes
	// g_resourceSegment into the matched table entry's field-at-arg2
	// offset. In the C++ segment model, the main/resource bank tag is
	// 0x1cb5.
	Log.setWalkSpeedFlag(0);
	doTableLookupAssign(_logic, a, _logic->mainInterpreter(),
						/* segValue = */ 0x1cb5,
						"TableLookupAssignMain", 0x64);
	return kThxBye;
}
OPCODE(0x65) {
	// DOS Op_65_TableLookupAssignBlock @ 1000:4185. Writes
	// g_seg_buffer_e into the matched table entry. In the C++ segment
	// model, the loaded block bank tag is `0x4000 + currentBlock`.
	Log.setWalkSpeedFlag(1);
	const uint16 blockSeg = blockSegmentTag(_logic);
	doTableLookupAssign(_logic, a, _logic->blockInterpreter(),
						/* segValue = */ blockSeg,
						"TableLookupAssignBlock", 0x65);
	return kThxBye;
}
OPCODE(0x66) {
	// DOS Op_66_WriteExitFieldSized @ 1000:41ee:
	//   signed if arg0 > g_exit_count -> pending-error 0x13;
	//   else GetExitOffset(arg0); lower-bound errors keep the
	//   record-table base and still enter WriteSizedFieldAtSi:
	//     arg1 = (size << 8) | offset; arg2 = value.
	//     size==1 byte; size==2 word; size==4 dword.
	// Exit field offsets in C++ Exit class (per kOffset* enums):
	//   0 = _room (word), 2 = _position (word*2), 6 = _sprite (word) etc.
	// Modeled room/position/z-index bytes update the Exit object;
	// unmodeled bytes fall through to Logic._exitFields sparse storage.
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x14);
		recordId = 1;
	}
	Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(recordId) : 0;
	if (!exit) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const uint16 value = uint16(a[2]);
	const uint16 highValue = valueHighWordForSizedWrite(_logic, a[2]);
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	writeExitRecordSizedLowWord(_logic, exit, recordId, off, sz, value, highValue);
	debugC(2, kDebugLevelScript, "opcode 0x66: exit[%u].field[+0x%02x size=%u] = %u",
		   id, off, sz, value);
	return kThxBye;
}
OPCODE(0x67) {
	// DOS Op_67_WriteObjectFieldSized @ 1000:41fe: same shape as 0x66
	// but via signed g_persons_count bound and GetObjectOffset. Object
	// lower-bound errors keep the pending error but continue into the
	// shared write tail using the object-table base.
	// record fields (per data-file layout): 0 = room (word), 2 = x (word),
	// 4 = y (word). C++ stores these in Logic._objectRoom/Pos*.
	const uint16 id = uint16(a[0]);
	const uint16 objectCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(id, objectCount)) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		recordId = 1;
	}
	const uint16 value = uint16(a[2]);
	const uint16 highValue = valueHighWordForSizedWrite(_logic, a[2]);
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x67: object[%u].field[+0x%02x size=%u] = %u",
		   id, off, sz, value);
	writeObjectRecordSizedLowWord(_logic, recordId, off, sz, value, highValue);
	return kThxBye;
}
OPCODE(0x68) {
	// DOS Op_68_WriteActorFieldSized @ 1000:4211:
	//   signed if arg0 > g_anim_count_max -> pending-error 0x13;
	//   GetActorOffset(arg0); lower-bound errors keep the actor-table
	//   base and still resolve arg2 (value), arg1 (field+size);
	//   if size==1: actor[off] = (byte)value;
	//   if size==2: actor[off..off+1] = value;
	//   if size==4: actor[off..off+3] = value (high word from BX/code segment);
	//   else pending-error 2.
	const uint16 id = uint16(a[0]);
	const uint16 mainActors = _logic->resources()->mainDat()->actorsCount();
	const uint16 blockActors = _logic->blockProgram() ? _logic->blockProgram()->actorsCount() : 0;
	const uint16 maxActor = mainActors + blockActors;
	if (dosPositiveIdExceedsMax(id, maxActor)) {
		Log.setPendingError(0x13);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x17);
		recordId = 1;
	}
	Actor *ac = Log.getActor(recordId);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	const uint16 value = uint16(a[2]);
	const uint16 highValue = valueHighWordForSizedWrite(_logic, a[2]);
	const uint16 fieldEnc = uint16(a[1]);
	const uint8 off = uint8(fieldEnc & 0xff);
	const uint8 sz = uint8(fieldEnc >> 8);
	if (sz != 1 && sz != 2 && sz != 4) {
		Log.setPendingError(0x02);
		return kThxBye;
	}
	writeActorRecordSizedLowWord(ac, off, sz, value, highValue);
	debugC(2, kDebugLevelScript, "opcode 0x68: actor[%u].field[%u] = %u (size=%u)",
		   id, off, value, sz);
	return kThxBye;
}
OPCODE(0x69) {
	// DOS Op_69_SetCellBitDefault @ 1000:425c: 1 arg. Sets BIT 1 (the "default
	// bit") on cellByte[a[0]] after the shared signed max-id check.
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x69: set cell bit 1 (default) on entity %u", id);
	Log.setCellBit(id, 1);
	return kThxBye;
}
OPCODE(0x6a) {
	// DOS Op_6a_SetCellBit @ 1000:4261: 2 args.
	//   signed if (arg1 > 7) pending-error 0x15;
	//   signed if (arg0 > g_exit_count) pending-error 0x14;
	//   set the bit selected by x86 RCL count `(arg1 + 1) & 0x1f`.
	const uint16 raw = uint16(a[1]);
	if (int16(raw) > 7) {
		Log.setPendingError(0x15);
		return kThxBye;
	}
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const int8 bit = dosCellBitIndex(raw);
	debugC(2, kDebugLevelScript, "opcode 0x6a: set cell bit raw=%u resolved=%d on entity %u",
		   raw, bit, id);
	if (bit >= 0)
		Log.setCellBit(id, uint8(bit));
	return kThxBye;
}
OPCODE(0x6b) {
	// DOS Op_6b_ClearCellBit @ 1000:4281: 2 args. Same bound checks
	// as 0x6a, then clears bit `arg1` of cellByte[arg0].
	const uint16 raw = uint16(a[1]);
	if (int16(raw) > 7) {
		Log.setPendingError(0x15);
		return kThxBye;
	}
	const uint16 id = uint16(a[0]);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(id, exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	const int8 bit = dosCellBitIndex(raw);
	debugC(2, kDebugLevelScript, "opcode 0x6b: clear cell bit raw=%u resolved=%d on entity %u",
		   raw, bit, id);
	if (bit >= 0)
		Log.clearCellBit(id, uint8(bit));
	return kThxBye;
}

OPCODE(0x75) {
	// DOS Op_75_SetCursorMode @ 1000:4313:
	//   g_cursor_mode = arg0; g_drag_step_idx = 0;
	//   g_flag_step_pending = 0.
	const uint16 mode = uint16(a[0]);
	Log.setCursorMode(mode);
	Log.setStepPending(false);
	debugC(2, kDebugLevelScript, "opcode 0x75: set cursor mode %u", mode);
	return kThxBye;
}
OPCODE(0x76) {
	// DOS Op_76_BeginDragWithTarget @ 1000:4325:
	//   pbRam000231ce = arg0;       ; g_drag_target_mode40 = arg0
	//   _g_drag_step_idx = 0;
	//   _g_cursor_mode = 0x40;      ; HARDCODED 0x40 (not arg0!)
	//   g_flag_step_pending = 0;
	// Op_76 always enters cursor mode 0x40 and stores arg0 as the drag
	// target for Op_0b's later check.
	const uint16 target = uint16(a[0]);
	Log.setDragTargetMode40(target);
	Log.setCursorMode(0x40);
	Log.setStepPending(false);
	debugC(2, kDebugLevelScript, "opcode 0x76: begin drag mode=0x40 target=%u", target);
	return kThxBye;
}
OPCODE(0x78) {
	// DOS Op_78_CheckActorAnimReadyAlt @ 1000:4359. Same current-opcode
	// retry gate as Op_77; args are arg0=room, arg1=current frame,
	// arg2=target frame.
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	const uint8 frame = uint8(uint16(a[1]));
	const uint8 target = uint8(uint16(a[2]));
	const uint16 room = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x78: go to room %u frame curr=%u target=%u",
		   room, frame, target);
	if (Log.inStatusMode()) {
		Log.restartRoom();
		Log.setLogicDirty();
		Log.setPaused();
		return kThxBye;
	}
	writeActorRoomTransition(protag, room, frame, target);
	requestRoomRestartTail(room);
	return kThxBye;
}
OPCODE(0x7a) {
	// DOS Op_7a_PlaceActorInRoomXY @ 1000:4443. nargs=4 per dispatch
	// table. Same shape as Op_79 but with a separate target frame:
	//   a[0] = actor id
	//   a[1] = room
	//   a[2] = current frame (-> field+0x61)
	//   a[3] = target frame  (-> field+0x62)
	// DOS sequence: UnregisterActor, set fields, SetActorPosition (X/Y
	// from frame[a[2]]), FindPlaceById, InitActorState. If the new room
	// matches g_current_location and target!=current, MoveActorToTargetExit.
	const uint16 id = uint16(a[0]);
	const uint16 room = uint16(a[1]);
	const uint8 frame = uint8(uint16(a[2]));
	const uint8 target = uint8(uint16(a[3]));
	debugC(1, kDebugLevelScript, "opcode 0x7a: place actor %u in room %u frame %u target %u",
		   id, room, frame, target);
	if (Log.inStatusMode())
		return kThxBye;
	Actor *ac = _logic->getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	ac->unregister();
	placeActorInRoomWithPosition(ac, room, frame, target);
	initActorFromPuppeteer(_logic, ac, id);
	if (room == Log.currentRoom() && target != frame) {
		moveActorToTargetExit(ac, target);
		return kThxBye;
	}
	return kThxBye;
}
OPCODE(0x7d) {
	// DOS Op_7d_MoveObjectFlag1 @ 1000:4493:
	//   ResolveOpcodeArg0; DisableObjectFlag1 (clear arg0's bit 0);
	//   ResolveOpcodeArg1; if arg1 > active block exit count → error 0x14;
	//     else EnableObjectFlag1 (set arg1's bit 0).
	const uint16 src = uint16(a[0]);
	_logic->disableObjectFlag1(src);
	const uint16 dst = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0x7d: move cell bit 0: %u → %u", src, dst);
	const uint16 exitCount = _logic->blockProgram() ? _logic->blockProgram()->exitsCount() : 0;
	if (dosPositiveIdExceedsMax(dst, exitCount)) {
		Log.setPendingError(0x14);
		return kThxBye;
	}
	if (!_logic->enableObjectFlag1(dst))
		return kThxBye;
	return kThxBye;
}

OPCODE(0x7e) {
	// DOS Op_7e_QueueOverlay @ 1000:44a8:
	//   arg0 = entity type (1=exit, 2=object, 3=actor);
	//   arg1 = entity id;
	//   look up (sprite, x, y):
	//     type 1 (exit):  GetExitOffset; sprite=[+6], x=[+2], y=[+4]
	//     type 2 (object): GetObjectOffset; sprite=[+6], x=[+2], y=[+4]
	//     type 3 (actor): GetActorOffset; sprite=[+8], x=[+4], y=[+6]
	//     else: pending-error 0x34.
	//   push (sprite, x, y) to overlay queue at DS:0x37b7. Cap 250
	//   entries (counter at DS:0x6621). Overflow → pending-error 0x35.
	//   set g_flag_misc_3 = 1, call DrawBackdropTile (immediate draw).
	const uint16 type = uint16(a[0]);
	const uint16 id = uint16(a[1]);
	uint16 sprite = 0;
	int16 x = 0;
	int16 y = 0;
	switch (type) {
	case 1: { // exit
		uint16 recordId = id;
		if (dosIdIsNonPositive(id)) {
			Log.setPendingError(0x14);
			recordId = 1;
		}
		Exit *exit = _logic->blockProgram() ? _logic->blockProgram()->getExit(recordId) : 0;
		if (exit) {
			sprite = exitRecordSizedLowWord(_logic, exit, recordId, 6, 2);
			x = int16(exitRecordSizedLowWord(_logic, exit, recordId, 2, 2));
			y = int16(exitRecordSizedLowWord(_logic, exit, recordId, 4, 2));
		} else {
			sprite = uint16(_logic->exitField(recordId, 6)) | (uint16(_logic->exitField(recordId, 7)) << 8);
			x = int16(uint16(_logic->exitField(recordId, 2)) | (uint16(_logic->exitField(recordId, 3)) << 8));
			y = int16(uint16(_logic->exitField(recordId, 4)) | (uint16(_logic->exitField(recordId, 5)) << 8));
		}
		break;
	}
	case 2: { // object
		uint16 recordId = id;
		if (dosIdIsNonPositive(id)) {
			Log.setPendingError(0x16);
			recordId = 1;
		}
		sprite = objectRecordSizedLowWord(_logic, recordId, 6, 2);
		x = int16(objectRecordSizedLowWord(_logic, recordId, 2, 2));
		y = int16(objectRecordSizedLowWord(_logic, recordId, 4, 2));
		break;
	}
	case 3: { // actor
		uint16 recordId = id;
		Actor *ac = Log.getActor(recordId);
		if (!ac) {
			Log.setPendingError(0x17);
			recordId = 1;
			ac = Log.getActor(recordId);
		}
		if (!ac)
			return kThxBye;
		sprite = actorRecordSizedLowWord(ac, Actor::kOffsetMainSprite, 2);
		x = int16(actorRecordSizedLowWord(ac, Actor::kOffsetLeft, 2));
		y = int16(actorRecordSizedLowWord(ac, Actor::kOffsetTop, 2));
		break;
	}
	default:
		Log.setPendingError(0x34);
		return kThxBye;
	}
	debugC(2, kDebugLevelScript, "opcode 0x7e: queue overlay type=%u id=%u sprite=%u pos=%d,%d",
		   type, id, sprite, x, y);
	if (!Log.overlayQueuePush(sprite, x, y)) {
		Log.setPendingError(0x35);
		return kThxBye;
	}
	Sprite *overlay = _logic->resources()->loadSprite(sprite);
	_graphics->paint(overlay, Common::Point(x, y), Graphics::kPaintCameraRelative);
	delete overlay;
	return kThxBye;
}
OPCODE(0x7f) {
	// DOS Op_7f_PlaceObjectInRoom @ 1000:452f: set Object[a[0]].room = a[1],
	// Object[a[0]].position = -1, Object[a[0]].field4 = 0. Marks logic dirty.
	// Used both to place an object in a scene AND to add it to the player's
	// inventory (room == kInventoryRoom). Op_18 / Op_1b / Op_21 read this.
	const uint16 id = uint16(a[0]);
	const uint16 objectCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(id, objectCount)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (id == Log.dragTarget()) {
		clearDragInteractionLikeOp8e();
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		recordId = 1;
	}
	if (Log.getObjectRoom(recordId) == 0xffff)
		Log.unregisterObjectExit(id);
	const uint16 room = uint16(a[1]);
	debugC(1, kDebugLevelScript, "opcode 0x7f: place object %u in room %u", id, room);
	Log.setObjectRoom(recordId, room);
	Log.setObjectPosition(recordId, -1, 0);
	Log.setLogicDirty();
	return kThxBye;
}

// 0x80..0x94: Object placement / hotspot manipulation. Each handler must
// respect its declared nargs from opcodes_nargs.data.
OPCODE(0x80) {
	// DOS Op_80_handler @ 1000:457f:
	//   if (arg0 > g_persons_count) pending-error 0x16;
	//   else GetObjectOffset(arg0); object[+0] = arg1 (room),
	//        object[+2] = arg2 (x), object[+4] = arg3 (y);
	//        if room == 0xffff: AddExitToList; if carry set, pending-error 0x21;
	//        on successful room==0xffff: ClampSpriteOnScreen + g_flag_logic_dirty = 1;
	//        g_flag_misc_1 = 1.
	const uint16 id = uint16(a[0]);
	const uint16 objectCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(id, objectCount)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		recordId = 1;
	}
	const int16 x = int16(uint16(a[2]));
	const int16 y = int16(uint16(a[3]));
	const uint16 room = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0x80: place object %u at room %u pos %dx%d",
		   id, room, x, y);
	Log.setObjectRoom(recordId, room);
	Log.setObjectPosition(recordId, x, y);
	if (room == 0xffff) {
		if (!Log.registerObjectExit(id))
			return kThxBye;
		Log.clampObjectExitToScreen(recordId);
		Log.setLogicDirty();
	}
	return kThxBye;
}
OPCODE(0x81) {
	// DOS Op_81 @ 1000:45ce: same as Op_80 but room = current_location.
	// nargs=3 (id, x, y).
	const uint16 id = uint16(a[0]);
	const uint16 objectCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(id, objectCount)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	uint16 recordId = id;
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		recordId = 1;
	}
	const int16 x = int16(uint16(a[1]));
	const int16 y = int16(uint16(a[2]));
	const uint16 room = Log.currentRoom();
	debugC(2, kDebugLevelScript, "opcode 0x81: place object %u at current room %u pos %dx%d",
		   id, room, x, y);
	Log.setObjectPosition(recordId, x, y);
	Log.setObjectRoom(recordId, room);
	if (room == 0xffff) {
		if (!Log.registerObjectExit(id))
			return kThxBye;
		Log.clampObjectExitToScreen(recordId);
		Log.setLogicDirty();
	}
	return kThxBye;
}
OPCODE(0x82) {
	// DOS Op_82_handler @ 1000:45f0: SWAP two objects' first 3 fields
	// (room, x, y) atomically. Bound checks both ids; if either is
	// the drag target → PrepareDragInteraction. If either object's
	// room is -1 (unplaced) → RemapEntityRefById to fix references.
	const uint16 a0 = uint16(a[0]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(a0, personsCount)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (a0 == Log.dragTarget()) {
		const uint16 dragTarget = uint16(a[1]);
		Log.prepareDragInteraction(dragTarget);
	}
	uint16 recordA = a0;
	if (dosIdIsNonPositive(a0)) {
		Log.setPendingError(0x16);
		recordA = 1;
	}
	if (Log.getObjectRoom(recordA) == 0xffff) {
		const uint16 remapTarget = uint16(a[1]);
		Log.remapObjectExit(a0, remapTarget);
	}
	const uint16 b0 = uint16(a[1]);
	if (dosPositiveIdExceedsMax(b0, personsCount)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (b0 == Log.dragTarget()) {
		const uint16 dragTarget = uint16(a[0]);
		Log.prepareDragInteraction(dragTarget);
	}
	uint16 recordB = b0;
	if (dosIdIsNonPositive(b0)) {
		Log.setPendingError(0x16);
		recordB = 1;
	}
	if (Log.getObjectRoom(recordB) == 0xffff) {
		const uint16 remapTarget = uint16(a[0]);
		Log.remapObjectExit(b0, remapTarget);
	}
	const uint16 ra = Log.getObjectRoom(recordA);
	const uint16 rb = Log.getObjectRoom(recordB);
	const int16 xa = Log.getObjectPosX(recordA);
	const int16 ya = Log.getObjectPosY(recordA);
	const int16 xb = Log.getObjectPosX(recordB);
	const int16 yb = Log.getObjectPosY(recordB);
	debugC(2, kDebugLevelScript, "opcode 0x82: swap objects %u<->%u (room+pos)", a0, b0);
	Log.setObjectRoom(recordA, rb);
	Log.setObjectRoom(recordB, ra);
	Log.setObjectPosition(recordA, xb, yb);
	Log.setObjectPosition(recordB, xa, ya);
	Log.clampObjectExitToScreen(recordB);
	Log.clampObjectExitToScreen(recordA);
	Log.setLogicDirty();
	return kThxBye;
}
static bool hotspotZoneContains(int16 x, int16 y) {
	const Common::Array<Logic::Zone> &zones = Log.zones();
	for (uint i = 0; i < zones.size(); ++i) {
		const Logic::Zone &z = zones[i];
		if (int16(z.a) <= x && x <= int16(z.c) &&
			int16(z.b) <= y && y <= int16(z.d))
			return true;
	}
	return false;
}

static bool inventoryRegionContains(Common::Point point) {
	return point.x >= 0x80 && point.x < 0x136 && point.y >= 0xa0 && point.y < 0xbf;
}

static SpriteInfo objectPrimarySpriteInfo(uint16 id) {
	const uint16 sprite = uint16(Log.objectField(id, 6)) | (uint16(Log.objectField(id, 7)) << 8);
	if (sprite == 0xffff)
		return SpriteInfo();
	return Log.engine()->resources()->getSpriteInfo(sprite);
}

static void pauseAndLockCursor() {
	// DOS PauseAndLockCursor @ 1000:34c2 sets g_flag_paused,
	// g_flag_misc_1, g_flag_logic_dirty, and clamps the software cursor
	// bounds to the full playfield.
	Log.setPaused();
	Log.setLogicDirty();
}

static bool handleHotspotInteraction(uint16 id, Common::Point point) {
	// Mirrors the observable state changes of HandleHotspotInteraction
	// @ 1000:3353 for the object/drag opcodes. The original first checks
	// the hit-region list, then the active Op_d9 zone list, and only then
	// queues walk/post-move placement.
	if (id == 0)
		return false;

	if (inventoryRegionContains(point)) {
		if (!Log.placeObjectInInventoryAtDosPoint(id, point))
			return false;
		return true;
	}

	const int16 worldX = int16(point.x + Log.cameraX());
	const int16 worldY = int16(point.y + Log.cameraY());
	if (!hotspotZoneContains(worldX, worldY))
		return false;

	if (Log.drawCommandCount() > 0x18 || Log.objectField(id, 0x0d) == 1) {
		return false;
	}

	Actor *protag = Log.protagonist();
	if (!protag || !Log.room())
		return false;

	const uint16 frame = Log.room()->nearestFrameTo(worldX, worldY);
	if (frame == 0) {
		Log.setPendingError(0x31);
		return false;
	}

	const Actor::Frame target = Log.room()->getFrame(frame);
	const SpriteInfo info = objectPrimarySpriteInfo(id);
	const int16 targetX = int16(target.position().x);
	const int16 targetY = int16(target.position().y);
	const int16 zoneCheckY = int16(targetY + 5 + int16(info.hotTop));
	if (!hotspotZoneContains(targetX, zoneCheckY))
		return false;

	queueExitTransition(protag, frame);
	if (protag->readyCallbackOffset() == 0)
		protag->setReadyMarker(5);

	const int16 placeX = int16(targetX - int16(info.width) / 2);
	const int16 placeY = int16(targetY + 5);
	Log.setPostMoveCallback(Logic::PostMoveCallback::kPlaceObjectAfterHotspotMove,
							id, uint16(placeX), uint16(placeY));
	return true;
}

OPCODE(0x83) {
	// DOS Op_83_handler @ 1000:4684:
	//   arg0 > persons_count -> 0x16. If arg0 is the current drag target:
	//   Op_8e, then PrepareDragInteraction(arg1), then dirty tail.
	//   Otherwise copy object[arg0]'s room/x/y into object[arg1], clear
	//   object[arg0].room, remap dynamic-exit-list references for any side
	//   whose room was -1, clamp arg1, and mark logic dirty.
	const uint16 a0 = uint16(a[0]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(a0, personsCount)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (a0 == Log.dragTarget()) {
		clearDragInteractionLikeOp8e();
		const uint16 a1 = uint16(a[1]);
		Log.prepareDragInteraction(a1);
		debugC(2, kDebugLevelScript, "opcode 0x83: transfer drag object %u -> %u", a0, a1);
		Log.setLogicDirty();
		return kThxBye;
	}
	if (dosIdIsNonPositive(a0)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}

	const uint16 room0 = Log.getObjectRoom(a0);
	const int16 x0 = Log.getObjectPosX(a0);
	const int16 y0 = Log.getObjectPosY(a0);
	if (room0 == 0xffff) {
		const uint16 remapTarget = uint16(a[1]);
		Log.remapObjectExit(a0, remapTarget);
	}
	const uint16 a1 = uint16(a[1]);
	if (dosPositiveIdExceedsMax(a1, personsCount) || dosIdIsNonPositive(a1)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (Log.getObjectRoom(a1) == 0xffff) {
		const uint16 remapTarget = uint16(a[0]);
		Log.remapObjectExit(a1, remapTarget);
	}

	debugC(2, kDebugLevelScript, "opcode 0x83: move object %u record to %u and clear source", a0, a1);
	Log.setObjectRoom(a1, room0);
	Log.setObjectPosition(a1, x0, y0);
	Log.setObjectRoom(a0, 0);
	Log.clampObjectExitToScreen(a1);
	Log.setLogicDirty();
	return kThxBye;
}
OPCODE(0x84) {
	// DOS Op_84_handler @ 1000:4703:
	//   if (arg0 == 0) Op_8e (UnregisterActor); return;
	//   if (arg0 > persons_count) pending-error 0x16;
	//   else:
	//     if (cursor==0x20) ResetObjectAtActorPosition;
	//     g_drag_target = arg0;
	//     GetObjectOffset(arg0); if (obj.room != current_loc &&
	//       obj.room != -1) obj.x/y = camera+offset;
	//     BeginDrag_AfterRemoveExit.
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0x84: begin drag with object %u (movePersonToActor)", id);
	Log.movePersonToActor(id);
	return kThxBye;
}
OPCODE(0x85) {
	// DOS Op_85 @ 1000:4762: SEARCH for first object whose room == arg0,
	// write its 1-based index to a[1] (destination var slot). 2 args.
	const uint16 searchRoom = uint16(a[0]);
	uint16 found = 0;
	const uint16 personsCount = Log.engine()->resources()->mainDat()->personsCount();
	for (uint16 i = 1; i <= personsCount; ++i) {
		if (Log.getObjectRoom(i) == searchRoom) {
			found = i;
			break;
		}
	}
	a[1] = found;
	debugC(2, kDebugLevelScript, "opcode 0x85: find object in room %u -> id %u",
		   searchRoom, found);
	return kThxBye;
}
OPCODE(0x86) {
	// DOS Op_86_handler @ 1000:4789:
	//   room = arg2; start = arg0 ? arg0 : 1;
	//   if (positive start > persons_count) write 0 to arg1 LHS, return;
	//   for (id = start; id <= persons_count && obj[id].room != arg2; id++);
	//   write id to arg1 LHS.
	// = "find next object (starting id arg0) with room == arg2".
	const uint16 searchRoom = uint16(a[2]);
	const uint16 rawStart = uint16(a[0]);
	const uint16 startId = rawStart == 0 ? 1 : rawStart;
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	uint16 found = 0;
	if (rawStart != 0 && dosPositiveIdExceedsMax(startId, personsCount)) {
		found = 0;
	} else if (rawStart != 0 && dosIdIsNonPositive(startId)) {
		Log.setPendingError(0x16);
	} else if (startId <= personsCount) {
		found = startId;
		while (found <= personsCount) {
			if (Log.getObjectRoom(found) == searchRoom)
				break;
			found++;
		}
		if (found > personsCount)
			found = 0;
	}
	a[1] = found;
	debugC(2, kDebugLevelScript, "opcode 0x86: search obj room=%u from %u -> id=%u", searchRoom, startId, found);
	return kThxBye;
}
OPCODE(0x87) {
	// DOS Op_87 @ 1000:47a4:
	//   DI = g_drag_target; RetEmpty; HandleHotspotInteraction;
	//   if AX != 0 PauseAndLockCursor else pending-error 0x25.
	const uint16 id = Log.dragTarget();
	const Common::Point cursor = Log.lockedCursorPosition();
	if (!handleHotspotInteraction(id, cursor)) {
		Log.setPendingError(0x25);
		return kThxBye;
	}
	pauseAndLockCursor();
	debugC(2, kDebugLevelScript, "opcode 0x87: drag-target hotspot interaction object %u", id);
	return kThxBye;
}
OPCODE(0x88) {
	// DOS Op_88_handler @ 1000:47bd:
	//   RetEmpty;  arg0 = ResolveOpcodeArg0;
	//   if (arg0 == g_drag_target):
	//     RetEmpty;  result = HandleHotspotInteraction();
	//     if (result != 0): PauseAndLockCursor; return;
	//   else:
	//     if (arg0 > g_persons_count): pending error 0x16; return;
	//     result = HandleHotspotInteraction();
	//     if (result != 0): g_flag_misc_1 = 1; return;
	//   pending error 0x25.
	// HandleHotspotInteraction @ 1000:3353 checks the dynamic object-exit
	// list and the active Op_d9 zone table, then may queue the protagonist
	// walk + PlaceObjectInRoom post-move callback.
	const uint16 id = uint16(a[0]);
	const bool isDragTarget = (id == Log.dragTarget());
	if (!isDragTarget) {
		if (dosPositiveIdExceedsMax(id, _logic->resources()->mainDat()->personsCount())) {
			Log.setPendingError(0x16);
			return kThxBye;
		}
	}
	const Common::Point cursor = Log.lockedCursorPosition();
	if (!handleHotspotInteraction(id, cursor)) {
		Log.setPendingError(0x25);
		debugC(2, kDebugLevelScript, "opcode 0x88: hotspot interaction object %u → not registered (pending 0x25)", id);
		return kThxBye;
	}
	if (isDragTarget)
		pauseAndLockCursor();
	else
		Log.setLogicDirty();
	debugC(2, kDebugLevelScript, "opcode 0x88: hotspot interaction object %u (drag=%u) → hit", id, Log.dragTarget());
	return kThxBye;
}
OPCODE(0x89) {
	// DOS Op_89_handler @ 1000:47f7:
	//   bound-check arg0; obj[arg0].room = arg1; obj[arg0].x = -1;
	//   obj[arg0].y = -1; if (arg0 == drag_target) PauseAndLockCursor.
	// = "place object in room, mark position as 'sentinel' (-1,-1)".
	const uint16 id = uint16(a[0]);
	const uint16 personsCount = _logic->resources()->mainDat()->personsCount();
	if (dosPositiveIdExceedsMax(id, personsCount) || dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	const uint16 room = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0x89: place object %u in room %u (sentinel pos)", id, room);
	Log.setObjectRoom(id, room);
	Log.setObjectPosition(id, -1, -1);
	if (id == Log.dragTarget())
		pauseAndLockCursor();
	return kThxBye;
}
OPCODE(0x8a) {
	// DOS Op_8a_handler @ 1000:47e6: resolves arg1 into CX and arg2
	// into DX, then shares Op_88's arg0/HandleHotspotInteraction tail.
	Common::Point point = Common::Point(int16(uint16(a[1])), int16(uint16(a[2])));
	const uint16 id = uint16(a[0]);
	const bool isDragTarget = (id == Log.dragTarget());
	if (!isDragTarget) {
		if (dosPositiveIdExceedsMax(id, _logic->resources()->mainDat()->personsCount())) {
			Log.setPendingError(0x16);
			return kThxBye;
		}
	} else {
		point = Log.lockedCursorPosition();
	}
	if (!handleHotspotInteraction(id, point)) {
		Log.setPendingError(0x25);
		debugC(2, kDebugLevelScript, "opcode 0x8a: hotspot interaction object %u (3-arg) → not registered (pending 0x25)", id);
		return kThxBye;
	}
	if (isDragTarget)
		pauseAndLockCursor();
	else
		Log.setLogicDirty();
	debugC(2, kDebugLevelScript, "opcode 0x8a: hotspot interaction object %u (3-arg) → hit", id);
	return kThxBye;
}
OPCODE(0x8b) {
	// DOS Op_8b_handler @ 1000:482e: 0 args.
	//   ResetObjectAtActorPosition(g_drag_target);  // place currently-
	//                                                  dragged obj at
	//                                                  actor's spot
	//   Op_8e (cursor=1, drag=0).                  // unregister
	const uint16 dragId = Log.dragTarget();
	Log.resetObjectAtActorPosition(dragId);
	clearDragInteractionLikeOp8e();
	debugC(2, kDebugLevelScript, "opcode 0x8b: reset drag obj %u at actor pos + unregister", dragId);
	return kThxBye;
}
OPCODE(0x8c) {
	// DOS Op_8c_handler @ 1000:48c4:
	//   if (arg0 == drag_target) → Op_8b_handler (reset + unregister);
	//   else: GetObjectOffset(arg0); if obj.room != -1
	//         → ResetObjectAtActorPosition(arg0).
	const uint16 id = uint16(a[0]);
	if (id == Log.dragTarget()) {
		debugC(2, kDebugLevelScript, "opcode 0x8c: drag target %u → reset + unregister", id);
		Log.resetObjectAtActorPosition(id);
		clearDragInteractionLikeOp8e();
		return kThxBye;
	}
	if (dosIdIsNonPositive(id)) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (Log.getObjectRoom(id) != 0xffff) {
		debugC(2, kDebugLevelScript, "opcode 0x8c: reset object %u at actor pos", id);
		Log.resetObjectAtActorPosition(id);
	} else {
		debugC(2, kDebugLevelScript, "opcode 0x8c: object %u room=-1, skip reset", id);
	}
	return kThxBye;
}
OPCODE(0x8d) {
	// DOS Op_8d_handler @ 1000:48df:
	//   ResolveOpcodeArg0; GetObjectOffset(id) → ES:SI;
	//   if (obj.room == -1) RemoveExitFromList(id);
	//   AddExitToList(id); if carry → pending-error 0x21;
	//   ResolveOpcodeArg1/2; jump to the common object-exit placement
	//   tail at 0x486b, which writes obj.room = -1 plus sprite-adjusted
	//   x/y and marks dirty.
	const uint16 id = uint16(a[0]);
	if (id == 0) {
		Log.setPendingError(0x16);
		return kThxBye;
	}
	if (Log.getObjectRoom(id) == 0xffff)
		Log.unregisterObjectExit(id);
	if (!Log.registerObjectExit(id))
		return kThxBye;
	const int16 x = int16(uint16(a[1]));
	const int16 y = int16(uint16(a[2]));
	debugC(2, kDebugLevelScript, "opcode 0x8d: register obj %u as exit at pos %d,%d",
		   id, x, y);
	Log.placeObjectExitAtDosPosition(id, x, y);
	return kThxBye;
}
OPCODE(0x8e) {
	// DOS Op_8e @ 1000:490e: 0 args. Sets g_flag_paused=1,
	// g_flag_misc_1=1, SetCursorMode(1), g_drag_target=0.
	// = "unregister current drag/cursor interaction".
	debugC(2, kDebugLevelScript, "opcode 0x8e: unregister actor / drag");
	clearDragInteractionLikeOp8e();
	return kThxBye;
}
OPCODE(0x8f) {
	// DOS Op_8f_handler @ 1000:4925:
	//   if (g_game_state != 1): pending error 0xe;
	//   else JMP trampoline @ 1000:49df with
	//        AX = [0x666c] (currentEntityId), BX = arg0, CX = 0.
	// Trampoline @ 1000:49df (executed inline, NOT post-move):
	//   PUSH CX; PUSH BX;
	//   CALL DisableObjectFlag1(AX = currentEntityId);
	//   POP AX; CALL MovePersonToActor(AX = arg0);
	//   POP AX; if (AX != 0) JMP EnableObjectFlag1(arg1).
	// = clearCellBit(currentEntityId) + movePersonToActor(arg0).
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	const uint16 target = uint16(a[0]);
	const uint16 cellId = Log.currentEntityId();
	if (runDisableMoveOptionalEnable(_logic, cellId, target, 0))
		debugC(2, kDebugLevelScript, "opcode 0x8f: disable cell %u + movePersonToActor %u",
			   cellId, target);
	return kThxBye;
}
OPCODE(0x90) {
	// DOS Op_90_handler @ 1000:4941: 2-arg variant of Op_8f. Same
	// trampoline at 0x49df but CX = arg1 → trampoline runs
	// EnableObjectFlag1(arg1) after move when arg1 != 0.
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	const uint16 target = uint16(a[0]);
	const uint16 enableId = uint16(a[1]);
	const uint16 cellId = Log.currentEntityId();
	if (runDisableMoveOptionalEnable(_logic, cellId, target, enableId))
		debugC(2, kDebugLevelScript, "opcode 0x90: disable cell %u + movePersonToActor %u + enable cell %u",
			   cellId, target, enableId);
	return kThxBye;
}
OPCODE(0x91) {
	// DOS Op_91_handler @ 1000:4960: gate (g_flag_step_pending +
	// g_cursor_mode==1). game==1 →
	//   CALL SendActorToTarget() for the current clicked entity;
	//   if carry set, jump to trampoline @ 1000:49df immediately;
	//   otherwise SetActorTarget and SetPostMoveCallback(BP=0x49df,
	//   BX=arg0, CX=0, AX=currentEntityId).
	if (!Log.stepPending() || Log.cursorMode() != 1)
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	const uint16 cellId = Log.currentEntityId();
	Actor *protag = Log.protagonist();
	const bool waitForWalk = Log.sendActorToCurrentEntity(protag) && protag && protag->isMoving();
	const uint16 target = uint16(a[0]);
	if (waitForWalk) {
		Log.setPostMoveCallback(
			Logic::PostMoveCallback::kDisableMoveOptionalEnable,
			cellId,
			target,
			0);
		debugC(2, kDebugLevelScript, "opcode 0x91: walk current entity + arm post-move callback (cellId=%u obj=%u)",
			   cellId, target);
	} else {
		if (runDisableMoveOptionalEnable(_logic, cellId, target, 0))
			debugC(2, kDebugLevelScript, "opcode 0x91: immediate disable cell %u + movePersonToActor %u",
				   cellId, target);
	}
	return kThxBye;
}
OPCODE(0x92) {
	// DOS Op_92_handler @ 1000:499e: 2-arg variant of Op_91
	// with CX=arg1 for the optional EnableObjectFlag1 tail.
	if (!Log.stepPending() || Log.cursorMode() != 1)
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0e);
		return kThxBye;
	}
	const uint16 cellId = Log.currentEntityId();
	Actor *protag = Log.protagonist();
	const bool waitForWalk = Log.sendActorToCurrentEntity(protag) && protag && protag->isMoving();
	const uint16 target = uint16(a[0]);
	const uint16 enableId = uint16(a[1]);
	if (waitForWalk) {
		Log.setPostMoveCallback(
			Logic::PostMoveCallback::kDisableMoveOptionalEnable,
			cellId,
			target,
			enableId);
		debugC(2, kDebugLevelScript, "opcode 0x92: walk current entity + arm post-move callback (cellId=%u obj=%u enable=%u)",
			   cellId, target, enableId);
	} else {
		if (runDisableMoveOptionalEnable(_logic, cellId, target, enableId))
			debugC(2, kDebugLevelScript, "opcode 0x92: immediate disable cell %u + movePersonToActor %u + enable cell %u",
				   cellId, target, enableId);
	}
	return kThxBye;
}
OPCODE(0x93) {
	// DOS Op_93_handler @ 1000:49f1: gate (step + cursor==0x20 +
	// arg0 == g_drag_target). Then walk to the current clicked entity;
	// if carry set, run trampoline @ 1000:4a36 immediately; otherwise
	// arm SetPostMoveCallback(BP=0x4a36, BX=arg1, AX=currentEntityId).
	// The 0x4a36 tail restores BX but uses AX as left by
	// DisableObjectFlag1 for EnableObjectFlag1, matching DOS exactly.
	if (!Log.stepPending() || Log.cursorMode() != 0x20)
		return kThxBye;
	const uint16 dragId = uint16(a[0]);
	if (dragId != Log.dragTarget())
		return kThxBye;
	if (Log.gameState() != 1) {
		Log.setPendingError(0x0f);
		return kThxBye;
	}
	const uint16 cellId = Log.currentEntityId();
	Actor *protag = Log.protagonist();
	const bool waitForWalk = Log.sendActorToCurrentEntity(protag) && protag && protag->isMoving();
	if (waitForWalk) {
		const uint16 savedBx = uint16(a[1]);
		Log.setPostMoveCallback(
			Logic::PostMoveCallback::kDisableEnableUnregister,
			cellId,
			dragId,
			savedBx);
		debugC(2, kDebugLevelScript, "opcode 0x93: walk current entity + arm DOS 0x4a36 unregister callback drag=%u saved-bx=%u",
			   dragId, savedBx);
	} else {
		if (runDisableEnableUnregister(_logic, cellId, 0))
			debugC(2, kDebugLevelScript, "opcode 0x93: immediate unregister drag=%u after DOS 0x4a36 tail (cell=%u)",
				   dragId, cellId);
	}
	return kThxBye;
}
OPCODE(0x94) {
	// DOS Op_94_handler @ 1000:4a41: 0 args. Just sets
	// g_flag_misc_1 = 1 and g_flag_logic_dirty = 1. Repaint trigger.
	debugC(2, kDebugLevelScript, "opcode 0x94: mark logic dirty");
	Log.setLogicDirty();
	return kThxBye;
}

OPCODE(0x97) {
	// DOS Op_97_BackupCutscenePCState @ 1000:4a5d:
	//   GetActorOffset(g_main_character_id) → ES:SI;
	//   [0x5ef1] = [0x6609];                  // target frame mirror
	//   [0x5ee9] = ES:[SI+0x69];               // walk callback (word)
	//   [0x5ef2] = ES:[SI+0x62];               // target frame (byte)
	//   [0x5ef3] = ES:[SI+0x67];               // walk-callback flag (byte)
	//   ES:[SI+0x67] = 0; ES:[SI+0x6b] = 0; ES:[SI+0x62] = 0;
	//   memcpy([0x5ef4], [0x65ab], 20);        // post_callback_ptr block
	//   [0x65ab] = 0; [0x5f08] = 0;
	//   for slot in g_speech_slots[6]:         // find main char's slot
	//     if (slot.frames_left != 0 && slot.owner == main_char) {
	//        memcpy([0x5f08], slot, 17); slot.owner = 0xffff; break;
	//     }
	//   for slot in g_room_script_slots[19]:    // find main char's room script
	//     if (slot[0] != 0 && slot[4] == main_char && slot[6] == 0) {
	//        [0x5eed] = slot[0]; [0x5eef] = slot[2];
	//        slot[0] = 0; [0x5eeb] = index; break;
	//     }
	//   g_break_inner = 1. InterpretBytecode does not stop on this flag.
	//
	// C++: capture the matching modeled state on Logic::_cutsceneBackup.
	// Speech is now kept in Logic's six-slot pool; backup marks the live
	// protag slot owner 0xffff, as DOS does before RecycleStaleSpeechSlots.
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Logic::CutsceneBackup &b = Log.cutsceneBackup();
	if (b.active) {
		// Re-entry without intervening Op_98. DOS would just overwrite
		// the slot; we match.
		warning("opcode 0x97: cutscene backup already active — overwriting");
	}
	b.active = true;
	b.targetFrame = uint8(protag->targetFrameId());
	b.readyMarker = protag->readyMarker();
	b.readyCallbackOffset = protag->readyCallbackOffset();
	b.targetFrameMirror = Log.postMoveTargetFrameMirror();
	// Clear protag fields the way DOS does (field+0x67/+0x6b/+0x62).
	// 0x6b is a word in DOS (`MOV word ptr ES:[SI+0x6b], 0`); we clear
	// both bytes via the sparse map.
	protag->setReadyMarker(0);
	protag->setWalkQueueLength(0);
	protag->setRawTargetFrame(0);
	// Capture and clear post-move callback ([0x65ab..0x65bb]).
	b.savedCallback = Log.postMoveCallback();
	Log.clearPostMoveCallback();
	// Capture protag speech (DOS speech-slot pool entry) and clear.
	b.hadSpeech = Log.backupSpeechSlotForOwner(protag->id(), b.speechText);
	b.roomScriptWait = Actor::RoomScriptWaitSnapshot();
	protag->takeRoomScriptWait(b.roomScriptWait);
	Log.setBreakInner(true);
	debugC(2, kDebugLevelScript,
		   "opcode 0x97: BackupCutscenePCState — fields(69=0x%04x 62=0x%02x 67=0x%02x) callback=%d speech='%s' roomWait=%d",
		   b.readyCallbackOffset, b.targetFrame, b.readyMarker,
		   int(b.savedCallback.kind), b.speechText.c_str(), b.roomScriptWait.valid ? 1 : 0);
	return kThxBye;
}
OPCODE(0x98) {
	// DOS Op_98_RestoreCutscenePCState @ 1000:4b40: reverse of Op_97.
	//   GetActorOffset(g_main_character_id) → ES:SI;
	//   [0x6609] = [0x5ef1];  ES:[SI+0x69] = [0x5ee9];
	//   ES:[SI+0x67] = [0x5ef3]; ES:[SI+0x62] = [0x5ef2];
	//   CALL LookupActorAndStartPath();         // re-engage walk
	//   memcpy([0x65ab], [0x5ef4], 20);         // restore post_callback
	//   for slot in g_speech_slots[6]:           // find FREE slot
	//     if (slot.frames_left == 0) { memcpy(slot, [0x5f08], 17); break; }
	//   if ([0x5eeb] != 0xffff):                 // restore room script slot
	//     g_room_script_slots[index].word0 = [0x5eed];
	//     g_room_script_slots[index].word2 = [0x5eef];
	//     g_room_script_slots[index].word4 = main_char;
	//     g_room_script_slots[index].word6 = 0;
	//   g_break_inner = 1. InterpretBytecode does not stop on this flag.
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Logic::CutsceneBackup &b = Log.cutsceneBackup();
	// Restore protag fields.
	protag->setReadyCallbackOffset(b.readyCallbackOffset);
	protag->setReadyMarker(b.readyMarker);
	protag->setRawTargetFrame(b.targetFrame);
	Log.setPostMoveTargetFrameMirror(b.targetFrameMirror);
	// LookupActorAndStartPath re-engages the walk toward field+0x62.
	// For a zero target, DOS still enters FindActorPath, which clears the
	// queued-path word but does not warp the actor to frame 0.
	if (b.targetFrame != 0) {
		protag->moveTo(b.targetFrame);
	} else {
		if (Log.room()) {
			const Actor::Frame targetFrame = Log.room()->getFrame(0);
			const bool destinationIsLeft =
				int16(targetFrame.position().x) <= int16(protag->position().x);
			protag->setTurnTieBreaker(destinationIsLeft ? 1 : 0);
		}
		protag->clearMoveQueue();
	}
	// Restore post-move callback record.
	Log.setPostMoveCallback(b.savedCallback);
	// Restore speech by allocating the first free DOS-style slot.
	if (b.hadSpeech)
		Log.restoreActorSpeechSlot(protag, b.speechText);
	if (b.roomScriptWait.valid)
		protag->restoreRoomScriptWait(b.roomScriptWait);
	Log.setBreakInner(true);
	debugC(2, kDebugLevelScript,
		   "opcode 0x98: RestoreCutscenePCState — fields(69=0x%04x 62=0x%02x 67=0x%02x) callback=%d speech='%s' roomWait=%d",
		   b.readyCallbackOffset, b.targetFrame, b.readyMarker,
		   int(b.savedCallback.kind), b.speechText.c_str(), b.roomScriptWait.valid ? 1 : 0);
	b.active = false;
	return kThxBye;
}

// 0x9f..0xaa: actor placement / animation readiness family. The placement
// handlers write frame fields, clear the walk word, call SetActorPosition,
// then run InitActorState. The wait handlers call CheckActorAnimReady and
// retry the current opcode through DOS's sample-slot path until ready.

OPCODE(0x9f) {
	// DOS Op_9f @ 1000:4c95: g_walk_speed_flag=0 selects the main
	// resource segment for arg1. The target actor is the protagonist
	// (CS:[0x10f]); arg0 is the frame.
	Log.setWalkSpeedFlag(0);
	CodePointer anim(static_cast<CodePointer &>(a[1]).offset(), Log.mainInterpreter());
	const uint8 frame = uint8(uint16(a[0]));
	debugC(2, kDebugLevelScript, "opcode 0x9f: protagonist frame %u animation %s", frame, +anim);
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	ac->placeIn(ac->room(), frame);
	initActorState(ac, anim);
	return kThxBye;
}
OPCODE(0xa0) {
	// DOS Op_a0 @ 1000:4c8e: same as Op_9f, but
	// g_walk_speed_flag=1 selects the loaded block-code segment for arg1.
	Log.setWalkSpeedFlag(1);
	CodePointer anim(static_cast<CodePointer &>(a[1]).offset(), Log.blockInterpreter());
	const uint8 frame = uint8(uint16(a[0]));
	debugC(2, kDebugLevelScript, "opcode 0xa0: protagonist frame %u block animation %s", frame, +anim);
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	ac->placeIn(ac->room(), frame);
	initActorState(ac, anim);
	return kThxBye;
}
OPCODE(0xa1) {
	// DOS Op_a1 @ 1000:4c59. nargs=2. Disassembly:
	//   AX = arg0 (BX), AX = arg1; PUSH BX, PUSH AX; AX = BX (= arg0)
	//   GetActorOffset(arg0)        ; SI = actor for arg0
	//   POP AX                      ; AX = arg1 (the frame)
	//   actor.field+0x61 = AL       ; current frame = arg1
	//   actor.field+0x62 = AL       ; target frame  = arg1
	//   actor.field+0x6b = 0        ; walk speed
	//   SetActorPosition()          ; X/Y from frame[arg1]
	//   POP BX → FindPlaceById(arg0)
	//   InitActorState()
	// So a[0] is the ACTOR ID and a[1] is the FRAME ID. C++ maps
	// FindPlaceById + InitActorState to the same puppeteer init helper
	// used by the adjacent DOS placement handlers.
	const uint16 id = uint16(a[0]);
	const uint8 frame = uint8(uint16(a[1]));
	debugC(2, kDebugLevelScript, "opcode 0xa1: warp actor %u to frame %u", id, frame);
	if (Actor *ac = Log.getActor(id)) {
		ac->placeIn(ac->room(), frame);
		initActorFromPuppeteer(_logic, ac, id);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa2) {
	// DOS Op_a2 @ 1000:4cb0. nargs=3. Disassembly order:
	//   ResolveOpcodeArg2 → CX                ; arg2 = code offset
	//   ResolveOpcodeArg0 → BX                ; arg0 = actor id
	//   ResolveOpcodeArg1 → AX                ; arg1 = frame
	//   PUSH CX, PUSH BX, PUSH AX
	//   AX = BX (= arg0); GetActorOffset      ; SI = actor for arg0
	//   POP AX                                 ; AX = arg1 (frame)
	//   field+0x61 = AL  (current frame)
	//   field+0x62 = AL  (target frame)
	//   field+0x6b = 0
	//   SetActorPosition                       ; X/Y from frame[arg1]
	//   POP AX (arg0) → CS:[0x37 or 0x35]
	//   POP DI (arg2) → InitActorState         ; sets actor.code_offset = arg2
	// Critical: the previous C++ misread a[0] as the frame (was passing
	// arg0 to setFrame, but arg0 is the actor id).
	Log.setWalkSpeedFlag(0);
	CodePointer anim(static_cast<CodePointer &>(a[2]).offset(), Log.mainInterpreter());
	const uint16 actorId = uint16(a[0]);
	const uint8 frame = uint8(uint16(a[1]));
	debugC(2, kDebugLevelScript, "opcode 0xa2: warp actor %u to frame %u code-offset %s",
		   actorId, frame, +anim);
	if (Actor *ac = Log.getActor(actorId)) {
		ac->placeIn(ac->room(), frame);
		initActorState(ac, anim);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa3) {
	// DOS Op_a3_handler @ 1000:4ca9: same as 0xa2 but with g_walk_speed_flag=1,
	// which selects the loaded block-code segment for arg2.
	Log.setWalkSpeedFlag(1);
	CodePointer anim(static_cast<CodePointer &>(a[2]).offset(), Log.blockInterpreter());
	const uint16 actorId = uint16(a[0]);
	const uint8 frame = uint8(uint16(a[1]));
	debugC(2, kDebugLevelScript, "opcode 0xa3: actor %u frame %u block code-offset %s",
		   actorId, frame, +anim);
	if (Actor *ac = Log.getActor(actorId)) {
		ac->placeIn(ac->room(), frame);
		initActorState(ac, anim);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa4) {
	// DOS Op_a4_handler @ 1000:4d47: if not in status mode, wait for protagonist animation
	// to finish (CheckActorAnimReady on g_main_character_id). The script blocks
	// here until the actor stops moving.
	if (Log.inStatusMode())
		return kThxBye;
	const uint8 marker = uint8(uint16(a[0]));
	debugC(2, kDebugLevelScript, "opcode 0xa4: wait protagonist anim ready marker %u", marker);
	if (Actor *ac = Log.protagonist()) {
		if (!checkActorAnimReadyModeled(ac)) {
			if (!retryCurrentOpcodeWhenActorReady(current, ac))
				return kThxBye;
			return kReturn;
		}
		setActorReadyFields(ac, marker, 0);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa5) {
	// DOS Op_a5_handler @ 1000:4d5c: same as 0xa4 with one extra arg consumed.
	if (Log.inStatusMode())
		return kThxBye;
	const uint8 marker = uint8(uint16(a[0]));
	const uint16 callback = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xa5: wait protagonist anim ready marker %u callback %u",
		   marker, callback);
	if (Actor *ac = Log.protagonist()) {
		if (!checkActorAnimReadyModeled(ac)) {
			if (!retryCurrentOpcodeWhenActorReady(current, ac))
				return kThxBye;
			return kReturn;
		}
		setActorReadyFields(ac, marker, callback);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa6) {
	// DOS Op_a6_handler @ 1000:4cfb: wait for actor `arg0`'s animation to finish.
	// arg1 is copied into actor.field+0x67 on the ready path.
	if (Log.inStatusMode())
		return kThxBye;
	const uint8 marker = uint8(uint16(a[1]));
	const uint16 actorId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xa6: wait actor %u anim ready marker %u",
		   actorId, marker);
	if (Actor *ac = Log.getActor(actorId)) {
		if (!checkActorAnimReadyModeled(ac)) {
			if (!retryCurrentOpcodeWhenActorReady(current, ac))
				return kThxBye;
			return kReturn;
		}
		setActorReadyFields(ac, marker, 0);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa7) {
	// DOS Op_a7_handler @ 1000:4d0f: wait actor `arg0` anim ready, 3-arg variant
	// with arg1 -> field+0x67 and arg2 -> field+0x69.
	if (Log.inStatusMode())
		return kThxBye;
	const uint8 marker = uint8(uint16(a[1]));
	const uint16 callback = uint16(a[2]);
	const uint16 actorId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xa7: wait actor %u anim ready marker %u callback %u",
		   actorId, marker, callback);
	if (Actor *ac = Log.getActor(actorId)) {
		if (!checkActorAnimReadyModeled(ac)) {
			if (!retryCurrentOpcodeWhenActorReady(current, ac))
				return kThxBye;
			return kReturn;
		}
		setActorReadyFields(ac, marker, callback);
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa8) {
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 actorId = uint16(a[0]);
	const uint16 entityType = uint16(a[1]);
	const uint16 targetId = uint16(a[2]);
	debugC(2, kDebugLevelScript, "opcode 0xa8: actor %u sub-action by entity type %u compare %u",
		   actorId, entityType, targetId);
	int16 targetX = 0;
	int16 targetY = 0;
	if (entityType == 1) {
		// DOS GetExitOffset @ 1000:c31c: lower bound ONLY (id<=0 -> err 0x14),
		// NO upper bound. The a8/a9 caller (0x4dae) does NOT early-return on
		// the error -- it reads the exit-table base coords and falls through
		// to the actor-ready / direction-marker tail.
		uint16 exitId = targetId;
		if (dosIdIsNonPositive(targetId)) {
			Log.setPendingError(0x14);
			exitId = 1; // DOS leaves SI at the exit-table base = record 1
		}
		Exit *exit = Log.blockProgram() ? Log.blockProgram()->getExit(exitId) : 0;
		targetX = exit ? int16(exit->position().x) : 0;
		targetY = exit ? int16(exit->position().y) : 0;
	} else if (entityType == 2) {
		// DOS GetObjectOffset @ 1000:c301: lower bound ONLY (id<=0 -> err 0x16),
		// NO upper bound (Op_62/Op_80 add `cmp ax,[cs:0x6b]` themselves; the
		// a8/a9 tail does not). The caller falls through, no early return.
		uint16 objId = targetId;
		if (dosIdIsNonPositive(targetId)) {
			Log.setPendingError(0x16);
			objId = 1; // DOS leaves SI at the object-table base = record 1
		}
		targetX = Log.getObjectPosX(objId);
		targetY = Log.getObjectPosY(objId);
	} else if (entityType == 3) {
		if (actorId == targetId)
			return kThxBye; // DOS cmp cx,ax; jz -> faithful self-target return
		// DOS GetActorOffset @ 1000:c337 has a real two-tier id bound
		// (err 0x17 path @ 1000:507d), but on failure it errs+RETs and the caller still falls
		// through to the marker tail instead of aborting the opcode.
		Actor *target = Log.getActor(targetId);
		if (!target)
			Log.setPendingError(0x17);
		targetX = target ? int16(target->position().x) : 0;
		targetY = target ? int16(target->position().y) : 0;
	} else {
		const Common::Point cursor = Log.lockedCursorPosition();
		targetX = int16(cursor.x + Log.cameraX());
		targetY = int16(cursor.y + Log.cameraY());
	}

	if (Actor *actor = Log.getActor(actorId)) {
		if (!checkActorAnimReadyModeled(actor)) {
			if (!retryCurrentOpcodeWhenActorReady(current, actor))
				return kThxBye;
			return kReturn;
		}
		setActorReadyMarkerOnly(actor, actorDirectionToPoint(actor, targetX, targetY));
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xa9) {
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 entityType = uint16(a[0]);
	const uint16 targetId = uint16(a[1]);
	const uint16 actorId = Log.protagonistId();
	debugC(2, kDebugLevelScript, "opcode 0xa9: protagonist %u sub-action by entity type %u compare %u",
		   actorId, entityType, targetId);
	int16 targetX = 0;
	int16 targetY = 0;
	if (entityType == 1) {
		// DOS GetExitOffset @ 1000:c31c: lower bound ONLY (id<=0 -> err 0x14),
		// NO upper bound. The a8/a9 caller (0x4dae) does NOT early-return on
		// the error -- it reads the exit-table base coords and falls through
		// to the actor-ready / direction-marker tail.
		uint16 exitId = targetId;
		if (dosIdIsNonPositive(targetId)) {
			Log.setPendingError(0x14);
			exitId = 1; // DOS leaves SI at the exit-table base = record 1
		}
		Exit *exit = Log.blockProgram() ? Log.blockProgram()->getExit(exitId) : 0;
		targetX = exit ? int16(exit->position().x) : 0;
		targetY = exit ? int16(exit->position().y) : 0;
	} else if (entityType == 2) {
		// DOS GetObjectOffset @ 1000:c301: lower bound ONLY (id<=0 -> err 0x16),
		// NO upper bound (Op_62/Op_80 add `cmp ax,[cs:0x6b]` themselves; the
		// a8/a9 tail does not). The caller falls through, no early return.
		uint16 objId = targetId;
		if (dosIdIsNonPositive(targetId)) {
			Log.setPendingError(0x16);
			objId = 1; // DOS leaves SI at the object-table base = record 1
		}
		targetX = Log.getObjectPosX(objId);
		targetY = Log.getObjectPosY(objId);
	} else if (entityType == 3) {
		if (actorId == targetId)
			return kThxBye; // DOS cmp cx,ax; jz -> faithful self-target return
		// DOS GetActorOffset @ 1000:c337 has a real two-tier id bound
		// (err 0x17 path @ 1000:507d), but on failure it errs+RETs and the caller still falls
		// through to the marker tail instead of aborting the opcode.
		Actor *target = Log.getActor(targetId);
		if (!target)
			Log.setPendingError(0x17);
		targetX = target ? int16(target->position().x) : 0;
		targetY = target ? int16(target->position().y) : 0;
	} else {
		const Common::Point cursor = Log.lockedCursorPosition();
		targetX = int16(cursor.x + Log.cameraX());
		targetY = int16(cursor.y + Log.cameraY());
	}
	Actor *actor = Log.protagonist();
	if (!actor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(actor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, actor))
			return kThxBye;
		return kReturn;
	}
	setActorReadyMarkerOnly(actor, actorDirectionToPoint(actor, targetX, targetY));
	return kThxBye;
}
OPCODE(0xaa) {
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 targetId = uint16(a[0]);
	const uint16 actorId = Log.protagonistId();
	debugC(2, kDebugLevelScript, "opcode 0xaa: protagonist %u sub-action unless self %u",
		   actorId, targetId);
	if (actorId == targetId)
		return kThxBye;
	Actor *target = Log.getActor(targetId);
	if (!target) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Actor *actor = Log.protagonist();
	if (!actor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(actor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, actor))
			return kThxBye;
		return kReturn;
	}
	setActorReadyMarkerOnly(actor, actorDirectionToPoint(actor, int16(target->position().x), int16(target->position().y)));
	return kThxBye;
}
OPCODE(0xac) {
	// DOS Op_ac_handler @ 1000:4e5c: Op_ab plus callback word arg1
	// written to protagonist actor field+0x69 after QueueExitTransition.
	// The ready path returns normally; only the idle retry path yields.
	if (Log.inStatusMode())
		return kThxBye;
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetFrame = uint16(a[0]);
	queueExitTransition(ac, targetFrame);
	const uint16 callback = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xac: queue protag exit transition to frame %u callback %u",
		   targetFrame, callback);
	setReadyCallbackOffset(ac, callback);
	return kThxBye;
}

// 0xae..0xb8: walk variants. These share the modeled DOS actor idle
// gate, target-frame movement helpers, and callback field writes.
OPCODE(0xae) {
	// DOS Op_ae_WaitActorIdleByArg @ 1000:4ea2:
	//   arg0 = actor id;
	//   CheckActorIdle(id);
	//   if (NOT idle) RegisterSampleSlot...; RET;  // yield
	//   arg1 = target frame;  MoveActorToTargetExit(id, frame);
	//   GetActorOffset(id) → ES:SI;
	//   arg2 = callback BP;  ES:[SI + 0x69] = arg2.  // walk-callback
	const uint16 id = uint16(a[0]);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetFrame = uint16(a[1]);
	moveActorToTargetExit(ac, targetFrame);
	const uint16 cb = uint16(a[2]);
	debugC(2, kDebugLevelScript, "opcode 0xae: actor %u walk to frame %u + callback %u",
		   id, targetFrame, cb);
	setReadyCallbackOffset(ac, cb);
	return kThxBye;
}
OPCODE(0xaf) {
	// DOS Op_af_WaitActorIdle @ 1000:4f7c:
	//   if (in_map_mode) RET;
	//   CheckActorIdle(g_main_character_id);
	//   if (NOT idle) RegisterSampleSlot...; RET;
	//   SendActorToTarget();    // current entity id/type globals.
	if (Log.inStatusMode())
		return kThxBye;
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xaf: protagonist walk to current entity");
	sendActorToCurrentScriptEntity(ac);
	return kThxBye;
}
OPCODE(0xb0) {
	// DOS Op_b0_WaitActorIdle2 @ 1000:4fb1: same as Op_af but ALSO
	// writes arg0's value to actor.field+0x69 (walk-callback target).
	if (Log.inStatusMode())
		return kThxBye;
	Actor *ac = Log.protagonist();
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	sendActorToCurrentScriptEntity(ac);
	const uint16 cb = uint16(a[0]);
	setReadyCallbackOffset(ac, cb);
	debugC(2, kDebugLevelScript, "opcode 0xb0: protagonist walk current entity + cb=%u", cb);
	return kThxBye;
}
OPCODE(0xb1) {
	// DOS Op_b1_WaitActorIdle3 @ 1000:4eee:
	//   if (in_map_mode) RET;
	//   CheckActorIdle(<implicit>);
	//   if (NOT idle) yield;
	//   arg0 resolved (target id);  MoveProtagonistToEntity.
	if (Log.inStatusMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xb1: protagonist walk to exit %u", targetId);
	sendActorToScriptEntityByType(protag, targetId, 1);
	return kThxBye;
}
OPCODE(0xb2) {
	// DOS Op_b2_WaitActorIdle4 @ 1000:4ec8: same as Op_b1.
	if (Log.inStatusMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xb2: protagonist walk to object %u", targetId);
	sendActorToScriptEntityByType(protag, targetId, 2);
	return kThxBye;
}
OPCODE(0xb3) {
	// DOS Op_b3_WaitActorIdle5 @ 1000:4f0b: same as Op_b1.
	if (Log.inStatusMode())
		return kThxBye;
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetId = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xb3: protagonist walk to actor %u", targetId);
	sendActorToScriptEntityByType(protag, targetId, 3);
	return kThxBye;
}
OPCODE(0xb4) {
	// DOS Op_b4_handler @ 1000:4f97: wait actor arg0 idle, then send that
	// actor to the current entity via MoveProtagonistToEntity_Wrapper.
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 actorId = uint16(a[0]);
	Actor *ac = Log.getActor(actorId);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	debugC(2, kDebugLevelScript, "opcode 0xb4: actor %u walk to current entity", actorId);
	sendActorToCurrentScriptEntity(ac);
	return kThxBye;
}
OPCODE(0xb5) {
	// DOS Op_b5_handler @ 1000:4f48: wait actor arg0, then move that
	// actor to exit arg1 (DX=1) through MoveProtagonistToEntity.
	//   if (in_map_mode) RET;
	//   arg0 = actor id;  CheckActorIdle(arg0);
	//   if (NOT idle) RegisterSampleSlot_LoadDefaultsAndMark; RET;  // yield
	//   arg1 = exit id;  DX = 1 (exit type);  BX = arg0;
	//   MoveProtagonistToEntity (resolves entity → walkbox → frame).
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 actorId = uint16(a[0]);
	Actor *actor = Log.getActor(actorId);
	if (!actor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(actor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, actor))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetId = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xb5: actor %u walk to exit %u", actorId, targetId);
	sendActorToScriptEntityByType(actor, targetId, 1);
	return kThxBye;
}
OPCODE(0xb6) {
	// DOS Op_b6_handler @ 1000:4f28: actor arg0 to object arg1 (DX=2).
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 actorId = uint16(a[0]);
	Actor *actor = Log.getActor(actorId);
	if (!actor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(actor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, actor))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetId = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xb6: actor %u walk to object %u", actorId, targetId);
	sendActorToScriptEntityByType(actor, targetId, 2);
	return kThxBye;
}
OPCODE(0xb7) {
	// DOS Op_b7_handler @ 1000:4f62: actor arg0 to actor target arg1 (DX=3).
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 actorId = uint16(a[0]);
	Actor *actor = Log.getActor(actorId);
	if (!actor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorIdleReadyModeled(actor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, actor))
			return kThxBye;
		return kReturn;
	}
	const uint16 targetId = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xb7: actor %u walk to actor %u", actorId, targetId);
	sendActorToScriptEntityByType(actor, targetId, 3);
	return kThxBye;
}
OPCODE(0xb8) {
	// DOS Op_b8_WalkActorWaitWithBreakFast @ 1000:502d:
	//   g_walk_speed_flag = 0;
	//   if (in_map_mode) RET;
	//   arg0 = actor_id;
	//   if (id > g_anim_count_max) pending error 0x17;
	//   if (id == g_main_character_id) g_break_inner = 1;  // state flag only
	//   CheckActorAnimReady(id);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   arg1 = anim selector; InitActorState(id) — re-run actor's main code.
	// InterpretBytecode does not stop on g_break_inner after the ready path.
	Log.setWalkSpeedFlag(0);
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 id = uint16(a[0]);
	if (dosPositiveIdExceedsMax(id, actorAnimMaxId())) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	setBreakInnerIfProtagonistId(id);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	CodePointer anim(static_cast<CodePointer &>(a[1]).offset(), Log.mainInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xb8: actor %u main animation %s", id, +anim);
	initActorState(ac, anim);
	return kThxBye;
}
OPCODE(0xba) {
	// DOS Op_ba_WalkActorAnimFast @ 1000:4fe5:
	//   g_walk_speed_flag = 0;
	//   if (in_map_mode) RET;
	//   arg2 = screen_x; arg3 = screen_y;  (resolved BEFORE id check)
	//   arg0 = actor_id;
	//   if (id > g_anim_count_max) pending error 0x17;
	//   CheckActorAnimReady(id);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   GetActorOffset(id) → ES:SI;
	//   ES:[SI + 0x4] = arg2;  ES:[SI + 0x6] = arg3;  ES:[SI + 0x61] = 0;
	//   if (in_map_mode) RET;
	//   arg0 = id (re-resolved);  CheckActorAnimReady(id);
	//   if (id == g_main_character_id) g_break_inner = 1;  // state flag only
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   arg1 = anim;  InitActorState(id).
	Log.setWalkSpeedFlag(0);
	if (Log.inStatusMode())
		return kThxBye;
	const int16 destX = int16(uint16(a[2]));
	const int16 destY = int16(uint16(a[3]));
	const uint16 initialId = uint16(a[0]);
	if (dosPositiveIdExceedsMax(initialId, actorAnimMaxId())) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Actor *initialActor = Log.getActor(initialId);
	if (!initialActor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(initialActor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, initialActor))
			return kThxBye;
		return kReturn;
	}
	initialActor->setRawFrame(0);
	initialActor->setRawPosition(Common::Point(destX, destY));
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 id = uint16(a[0]);
	if (dosPositiveIdExceedsMax(id, actorAnimMaxId())) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	setBreakInnerIfProtagonistId(id);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	CodePointer anim(static_cast<CodePointer &>(a[1]).offset(), Log.mainInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xba: actor %u raw-position (%d,%d) main animation %s",
		   id, destX, destY, +anim);
	initActorState(ac, anim);
	return kThxBye;
}
OPCODE(0xbb) {
	// DOS Op_bb_WalkActorAnimSlow @ 1000:4fde: identical to 0xba but
	// g_walk_speed_flag = 1 and the block-code bank is selected.
	Log.setWalkSpeedFlag(1);
	if (Log.inStatusMode())
		return kThxBye;
	const int16 destX = int16(uint16(a[2]));
	const int16 destY = int16(uint16(a[3]));
	const uint16 initialId = uint16(a[0]);
	if (dosPositiveIdExceedsMax(initialId, actorAnimMaxId())) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	Actor *initialActor = Log.getActor(initialId);
	if (!initialActor) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(initialActor)) {
		if (!retryCurrentOpcodeWhenActorReady(current, initialActor))
			return kThxBye;
		return kReturn;
	}
	initialActor->setRawFrame(0);
	initialActor->setRawPosition(Common::Point(destX, destY));
	if (Log.inStatusMode())
		return kThxBye;
	const uint16 id = uint16(a[0]);
	if (dosPositiveIdExceedsMax(id, actorAnimMaxId())) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	setBreakInnerIfProtagonistId(id);
	Actor *ac = Log.getActor(id);
	if (!ac) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(ac)) {
		if (!retryCurrentOpcodeWhenActorReady(current, ac))
			return kThxBye;
		return kReturn;
	}
	CodePointer anim(static_cast<CodePointer &>(a[1]).offset(), Log.blockInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xbb: actor %u raw-position (%d,%d) block animation %s",
		   id, destX, destY, +anim);
	initActorState(ac, anim);
	return kThxBye;
}

OPCODE(0xbf) {
	// DOS Op_bf_WaitProtagonistAnimBreak @ 1000:50a1:
	//   g_walk_speed_flag = 0;
	//   if (in_map_mode) RET;
	//   g_break_inner = 1;  // state flag only
	//   CheckActorAnimReady(<implicit = main_char>);
	//   if (NOT ready) RegisterSampleSlot...; RET;
	//   arg1, arg2 = screen x/y;
	//   GetActorOffset(main_char) → ES:SI;
	//   ES:[SI + 0x61] = 0;
	//   ES:[SI + 0x4] = arg1;  ES:[SI + 0x6] = arg2;
	//   if (in_map_mode) RET;
	//   g_break_inner = 1;  CheckActorAnimReady; if NOT ready yield;
	//   arg0 = anim selector;  InitActorState(main_char).
	Log.setWalkSpeedFlag(0);
	if (Log.inStatusMode())
		return kThxBye;
	Log.setBreakInner(true);
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	const int16 destX = int16(uint16(a[1]));
	const int16 destY = int16(uint16(a[2]));
	protag->setRawFrame(0);
	protag->setRawPosition(Common::Point(destX, destY));
	if (Log.inStatusMode())
		return kThxBye;
	Log.setBreakInner(true);
	if (!checkActorAnimReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	CodePointer anim(static_cast<CodePointer &>(a[0]).offset(), Log.mainInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xbf: protagonist raw-position (%d,%d) main animation %s",
		   destX, destY, +anim);
	initActorState(protag, anim);
	return kThxBye;
}

// 0xc0..0xc5: cast/actor pos.
OPCODE(0xc0) {
	// DOS Op_c0_WaitProtagonistAnimBreakFast @ 1000:509a: same as Op_bf
	// but g_walk_speed_flag = 1 (this opcode entry is just 7 bytes
	// before Op_bf @ 1000:50a1, falling through into the same body).
	Log.setWalkSpeedFlag(1);
	if (Log.inStatusMode())
		return kThxBye;
	Log.setBreakInner(true);
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (!checkActorAnimReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	const int16 destX = int16(uint16(a[1]));
	const int16 destY = int16(uint16(a[2]));
	protag->setRawFrame(0);
	protag->setRawPosition(Common::Point(destX, destY));
	if (Log.inStatusMode())
		return kThxBye;
	Log.setBreakInner(true);
	if (!checkActorAnimReadyModeled(protag)) {
		if (!retryCurrentOpcodeWhenActorReady(current, protag))
			return kThxBye;
		return kReturn;
	}
	CodePointer anim(static_cast<CodePointer &>(a[0]).offset(), Log.blockInterpreter());
	debugC(2, kDebugLevelScript, "opcode 0xc0: protagonist raw-position (%d,%d) block animation %s",
		   destX, destY, +anim);
	initActorState(protag, anim);
	return kThxBye;
}
OPCODE(0xc1) {
	// DOS Op_c1_UnregisterActor @ 1000:5131:
	//   if (g_in_status_mode != 0) RET;
	//   AX = g_main_character_id;
	//   CALL UnregisterActor(AX);   // 0x66ed
	//
	// UnregisterActor (0x66ed) — full disassembly:
	//   GetActorOffset(AX) → ES:SI;
	//   ES:[SI + 0]  = 0;        // clear actor.field+0 (script segment)
	//   ES:[SI + 2]  = 0;        // clear actor.field+2 (script offset)
	//   CX = 0x14;  DI = 0x25fb; // g_actor_table base (20 slots × 0x2e)
	//   loop:
	//     if ([DI] == AX) { [DI] = 0; RET; }   // clear matching wId
	//     DI += 0x2e;  LOOP;
	//   RET;  // no match
	//
	// C++ mirrors the actor field +0/+2 script-PC clear and active-table
	// removal without resetting unrelated sprite, path, or timer fields.
	if (Log.inStatusMode())
		return kThxBye;
	if (Actor *protag = Log.protagonist()) {
		protag->unregister();
		debugC(2, kDebugLevelScript, "opcode 0xc1: UnregisterActor — protagonist script PC cleared");
	} else {
		Log.setPendingError(0x17);
	}
	return kThxBye;
}
OPCODE(0xc3) {
	// DOS Op_c3_RegisterCastEntry @ 1000:514a:
	//   Resolve args 1, 2, 0;
	//   Find first slot where wActive == 0 in g_cast_table[18];
	//   slot.w_unk_02 = arg0 (id);  slot.wActive = caller_seg;
	//   slot.wX = arg1;  slot.wY = arg2;
	//   Init bookkeeping bytes (frame=1, sprite_idx=0xff, rect=0xffff…);
	//   else: pending error 0x2a.
	const int16 x = int16(uint16(a[1]));
	const int16 y = int16(uint16(a[2]));
	const uint16 id = uint16(a[0]);
	const bool ok = Log.castTableRegister(id, x, y, current.interpreter());
	debugC(2, kDebugLevelScript, "opcode 0xc3: RegisterCastEntry id=%u pos=(%d,%d) %s",
		   id, x, y, ok ? "ok" : "FAIL (table full → pending 0x2a)");
	return kThxBye;
}
OPCODE(0xc4) {
	// DOS Op_c4_SetCastEntryPosition @ 1000:51a8 — BUG-ACCURATE port.
	// DOS clobbers arg1 (saved in CX) with the loop counter immediately
	// before the search loop, so the matched slot's wX is overwritten
	// with (kCastTableCap - matched_index), not arg1. arg2 (in DX) is
	// preserved and written correctly to wY. See Logic::castTableSetPos
	// for the full disassembly trace and reproduction note.
	const int16 x = int16(uint16(a[1])); // resolved + passed for trace; DOS discards
	const int16 y = int16(uint16(a[2]));
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc4: SetCastEntryPosition id=%u (arg1=%d ignored — DOS bug, wY=%d)",
		   id, x, y);
	Log.castTableSetPos(id, x, y);
	return kThxBye;
}
OPCODE(0xc5) {
	// DOS Op_c5_ClearCastEntry @ 1000:51cd:
	//   Resolve arg0 (id);
	//   Find slot where w_unk_02 == arg0;
	//   if found: w_unk_02 = 0; wActive = 0;  // free slot
	//   else: silent no-op.
	const uint16 id = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xc5: ClearCastEntry id=%u", id);
	Log.castTableClear(id);
	return kThxBye;
}

OPCODE(0xca) {
	// DOS Op_ca_PatchGraphicEntry @ 1000:5246:
	//   if ((int16)arg0 > g_graphic_count) pending-error 0xa;
	//   else: image_directory[(arg0-1)*4].type_word = arg1.
	// This patches the first word of the IUC_MAIN image directory entry,
	// not the iuc_graf.dat byte offset. Op_cb/LoadGraphicToSlot reads the
	// same word to choose the destination graphic slot.
	const uint16 id = uint16(a[0]);
	MainDat *main = _logic->resources()->mainDat();
	if (!main || dosPositiveIdExceedsMax(id, main->imagesCount())) {
		Log.setPendingError(0x0a);
		return kThxBye;
	}
	const uint16 type = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xca: patch image_directory[%u].type = %u", id, type);
	main->patchImageType(id, type);
	return kThxBye;
}
OPCODE(0xcd) {
	// DOS Op_cd_RestoreRoomActive @ 1000:52b7: end cutscene — mirror of 0xce.
	//   1. g_room_active = 1
	//   2. SetBackdropDimensions(0x98) (restore interface area)
	//   3. g_flag_misc_1 = 1
	//   4. Calls raw unlock helper @ 1000:4a52 (dispatch-table opcode 0x95),
	//      clearing no-step and step-pending.
	debugC(2, kDebugLevelScript, "opcode 0xcd: end cutscene / restore room active");
	Graf.setFullscreen(false);
	Log.setRoomActive(true);
	Log.setNoStep(false);
	Log.setStepPending(false);
	return kThxBye;
}

// 0xd3..0xd9: palette, camera, and zone helpers.
OPCODE(0xd3) {
	// DOS Op_d3_ClearRoomPalette @ 1000:53a7 clears the first 0x1e0
	// palette bytes, i.e. VGA entries 0..159. The interface range
	// 160..255 is left alone.
	debugC(2, kDebugLevelScript, "opcode 0xd3: clear room palette");
	Graf.clearPaletteRange(0, 160);
	return kThxBye;
}
OPCODE(0xd4) {
	// DOS Op_d4_SetCameraTarget @ 1000:53d3:
	//   _g_target_x = arg0; _g_target_y = arg1;
	//   g_input_enabled = 0;
	// Sets camera scroll TARGET (engine smoothly pans toward it).
	const uint16 tx = uint16(a[0]);
	const uint16 ty = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xd4: camera target = (%u, %u)", tx, ty);
	Log.setCameraTarget(tx, ty);
	Log.setInputEnabled(false);
	return kThxBye;
}
OPCODE(0xd5) {
	// DOS Op_d5_SetCameraInstant @ 1000:53e5:
	//   g_camera_x = arg0; g_camera_y = arg1;
	//   g_input_enabled = 0;
	//   _g_target_x = 0xffff; _g_target_y = 0xffff;
	//   g_flag_misc_3 = 1; (mark for redraw)
	// Sets camera position INSTANTLY (no scroll).
	const int16 cx = int16(uint16(a[0]));
	const int16 cy = int16(uint16(a[1]));
	debugC(2, kDebugLevelScript, "opcode 0xd5: camera instant (%d, %d)", cx, cy);
	Log.setCameraXY(cx, cy);
	Log.setCameraTarget(0xffff, 0xffff);
	Log.setInputEnabled(false);
	Log.setLogicDirty();
	return kThxBye;
}
OPCODE(0xd7) {
	// DOS Op_d7_handler @ 1000:5408:
	//   g_input_enabled = 1;
	//   if protagonist.room != g_current_location:
	//       ApplyChangeRoomTransition(protagonist.room)
	debugC(2, kDebugLevelScript, "opcode 0xd7: enable input and follow protagonist room");
	Log.setInputEnabled(true);
	Actor *protag = Log.protagonist();
	if (!protag) {
		Log.setPendingError(0x17);
		return kThxBye;
	}
	if (protag->room() != Log.currentRoom())
		Log.changeRoom(protag->room());
	return kThxBye;
}
OPCODE(0xd9) {
	// DOS Op_d9_handler @ 1000:5430: add zone entry to g_zone[8] (4 uint16 args, 8-byte
	// stride). Overflow sets g_pendingErrorCode = 0x27.
	if (Log.zones().size() >= 8) {
		Log.setPendingError(0x27);
		return kThxBye;
	}
	Logic::Zone z = {uint16(a[0]), uint16(a[1]), uint16(a[2]), uint16(a[3])};
	debugC(2, kDebugLevelScript, "opcode 0xd9: add zone (%u,%u,%u,%u)",
		   z.a, z.b, z.c, z.d);
	Log.zonesAdd(z);
	return kThxBye;
}

OPCODE(0xdd) {
	// DOS Op_dd_handler @ 1000:54bf: add zone-B entry. 4 uint16 args + 1 var slot value
	// (ReadVarBySlot_RHS) at offset +0x679. Overflow at 30 sets error 0x32.
	// VM args 0..3 + arg 4 (variable). Stride 10 bytes per entry.
	if (Log.zonesB().size() >= 30) {
		Log.setPendingError(0x32);
		return kThxBye;
	}
	Logic::ZoneB z = {
		uint16(a[0]), uint16(a[1]), uint16(a[2]), uint16(a[3]), uint16(a[4])};
	debugC(2, kDebugLevelScript, "opcode 0xdd: add zone-B (%u,%u,%u,%u, var=%u)",
		   z.a, z.b, z.c, z.d, z.var);
	Log.zonesBAdd(z);
	return kThxBye;
}

// 0xe0..0xec: frame-table mutators, anim-list state, and parser helpers.
OPCODE(0xe0) {
	// DOS Op_e0 @ 1000:5548: InvalidateFrame. Sets frame[arg0].x = .y = 999.
	// findPath skips frames with this sentinel — used to remove a frame
	// from the walkable graph mid-cutscene (e.g. blocking a path).
	const uint16 frame = uint16(a[0]);
	debugC(2, kDebugLevelScript, "opcode 0xe0: InvalidateFrame %u", frame);
	if (frame >= 0xfd) {
		Log.setPendingError(0x30);
		return kThxBye;
	}
	Log.actorFrameInvalidate(frame);
	return kThxBye;
}
OPCODE(0xe1) {
	// DOS Op_e1 @ 1000:5564: SetFramePosition. Overwrites frame[arg0]'s
	// (x, y) with arg1, arg2. Used to dynamically move a walkable point.
	const uint16 frame = uint16(a[0]);
	if (frame >= 0xfd) {
		Log.setPendingError(0x30);
		return kThxBye;
	}
	const int16 x = int16(uint16(a[1]));
	const int16 y = int16(uint16(a[2]));
	debugC(2, kDebugLevelScript, "opcode 0xe1: SetFramePosition frame=%u (%d,%d)", frame, x, y);
	Log.actorFrameSetPosition(frame, x, y);
	return kThxBye;
}
OPCODE(0xe3) {
	// DOS Op_e3_handler @ 1000:5589:
	//   pbRam000231b2 = arg0 + 3;       // DS:0x6662
	//   pbRam000231b4 = arg1 + 0x9b;    // DS:0x6664
	//   _g_unknown_6660 = arg2;         // DS:0x6660 (gate for DispatchDialogClick)
	//   g_flag_logic_dirty = 1;
	// Stashes anim-list cursor pointers used by DispatchDialogClick @
	// 1000:b316 when iterating g_anim_list (per Op_e4 entries).
	const uint16 cursor0 = uint16(uint16(a[0]) + 3);
	const uint16 cursor1 = uint16(uint16(a[1]) + 0x9b);
	const uint16 gate = uint16(a[2]);
	Log.setDialogCursors(cursor0, cursor1, gate);
	Log.setLogicDirty(true);
	debugC(2, kDebugLevelScript, "opcode 0xe3: stash anim-list cursor (cursor0=0x%04x cursor1=0x%04x gate=%u)",
		   cursor0, cursor1, gate);
	return kThxBye;
}
OPCODE(0xe4) {
	// DOS Op_e4_handler @ 1000:55a7:
	//   if (anim_list_count >= 8) pending-error 0xb;
	//   else: append (arg3, arg2, arg0+3, arg1+0x9b, arg0+9, arg1+0xa1, 0xffff)
	//         to anim_list[anim_list_count]; ++count.
	// = "queue cutscene anim entry". Args are pose / position deltas.
	if (Log.animListCount() >= 8) {
		Log.setPendingError(0x0b);
		return kThxBye;
	}
	const uint16 arg3 = uint16(a[3]);
	const uint16 arg2 = uint16(a[2]);
	const uint16 arg0 = uint16(a[0]);
	const uint16 arg1 = uint16(a[1]);
	debugC(2, kDebugLevelScript, "opcode 0xe4: anim-list append (%u, %u, %u, %u)",
		   arg0, arg1, arg2, arg3);
	Log.animListAppend(arg0, arg1, arg2, arg3);
	return kThxBye;
}
OPCODE(0xe7) {
	// DOS Op_e7 @ 1000:5612: 0 args. Calls ClearBytesUntilWrap on
	// the parser buffer at DS:0x4fa9. Clears the buffer (sets
	// length=0 and zeroes chars).
	debugC(2, kDebugLevelScript, "opcode 0xe7: parser buffer cleared");
	Log.parserBufferClear();
	return kThxBye;
}
OPCODE(0xe8) {
	// DOS Op_e8 @ 1000:561d: arg0 = resource-segment Pascal buffer.
	// Resolves arg0 to an offset, switches DS to g_resourceSegment, and
	// falls through to the same ClearBytesUntilWrap helper used by Op_e7.
	byte *base = nullptr;
	byte *ptr = resolveDosResourcePointer(a[0], current, &base);
	debugC(2, kDebugLevelScript, "opcode 0xe8: clear pstring buffer at 0x%04x",
		   dosResourceOffset(base, ptr));
	clearDosPascalBufferAt(base, ptr);
	return kThxBye;
}
OPCODE(0xe9) {
	// DOS Op_e9 @ 1000:5634: 1 arg (char). Appends arg0 byte to the
	// parser buffer at DS:0x4fa9 if length < capacity. The disassembly
	// shows `*(byte *)0x4faa` length increment + char store.
	const byte ch = uint8(uint16(a[0]) & 0xff);
	debugC(2, kDebugLevelScript, "opcode 0xe9: parser append '%c' (0x%02x)",
		   ch >= 0x20 && ch < 0x7f ? ch : '.', ch);
	Log.parserBufferAppend(ch);
	return kThxBye;
}
OPCODE(0xea) {
	// DOS Op_ea @ 1000:5642: Pascal-string append-byte. arg0 = string ptr,
	// arg1 = byte to append. If string.length < string.capacity, increments
	// length and writes byte at end. Uses signed byte comparison (`JGE`).
	byte *base = nullptr;
	byte *ptr = resolveDosResourcePointer(a[0], current, &base);
	const byte ch = uint8(uint16(a[1]) & 0xff);
	debugC(2, kDebugLevelScript, "opcode 0xea: pstring append byte at 0x%04x, byte=0x%02x",
		   dosResourceOffset(base, ptr), ch);
	appendDosPascalByteAt(ptr, ch);
	return kThxBye;
}
OPCODE(0xeb) {
	// DOS Op_eb @ 1000:5665: 0 args. Calls PopLastCharOfPascalString
	// on the parser buffer (length-- if length > 0, zero last char).
	debugC(2, kDebugLevelScript, "opcode 0xeb: parser pop last char");
	Log.parserBufferPop();
	return kThxBye;
}
OPCODE(0xec) {
	// DOS Op_ec @ 1000:5670: Pascal-string truncate-by-length. arg0 = string
	// ptr; if length > 0, decrements length and zeroes last char.
	byte *base = nullptr;
	byte *ptr = resolveDosResourcePointer(a[0], current, &base);
	debugC(2, kDebugLevelScript, "opcode 0xec: pstring truncate at 0x%04x",
		   dosResourceOffset(base, ptr));
	popDosPascalByteAt(ptr);
	return kThxBye;
}

OPCODE(0xee) {
	// DOS Op_ee_handler @ 1000:5698:
	//   if (arg0 >= g_score_event_count CS:[0x93]) pending-error 0x2f;
	//   else:
	//     entry = score_table[arg0*2]   ; CS:[0x95 + arg0*2]
	//     if (entry+1 byte == 0):       ; not yet claimed
	//         g_game_score += entry word
	//         entry+1 byte = 1          ; mark claimed
	// = "claim a score event". In iuc_main.dat these are footer fields
	// +0x34 (count) and +0x36 (table pointer), copied to CS:[0x93/0x95].
	const uint16 eventId = uint16(a[0]);
	MainDat *main = _logic->resources()->mainDat();
	if (!main || eventId >= main->scoreEventCount()) {
		Log.setPendingError(0x2f);
		return kThxBye;
	}

	uint16 delta = 0;
	if (!main->claimScoreEvent(eventId, delta)) {
		debugC(3, kDebugLevelScript, "opcode 0xee: score event %u already claimed", eventId);
		return kThxBye;
	}

	debugC(2, kDebugLevelScript, "opcode 0xee: claim score event %u (+%u, score %u -> %u)",
		   eventId, delta, Log.gameScore(), uint16(Log.gameScore() + delta));
	Log.addGameScore(delta);
	return kThxBye;
}

// 0xf1..0xf5: music/sfx beyond the core 0xf4 (play music) / 0xf7 (stop) /
// 0xf8 (panic stop) handled above.
OPCODE(0xf1) {
	// DOS Op_f1_handler @ 1000:5725: 2 args.
	//   if (g_sfx_enabled) {
	//       Op_load_sfx(arg0);          // primary play (Op_f0 inline)
	//       if (arg1 != pbRam00023250) {
	//           PlaySfxSound(arg1);
	//           cache arg1 at [0x6700], slot at [0x6706/0x6708].
	//       }
	//   }
	// Routes through Sound::playSfxPair which chains Op_f0 + secondary.
	if (Sound *snd = _engine->sound()) {
		if (!snd->isEnabled()) {
			debugC(1, kDebugLevelScript, "opcode 0xf1: load_sfx pair skipped (sfx disabled)");
			return kThxBye;
		}
		const uint16 primary = uint16(a[0]);
		snd->playSfx(primary);
		const uint16 secondary = uint16(a[1]);
		snd->playSecondarySfx(secondary);
		debugC(1, kDebugLevelScript, "opcode 0xf1: load_sfx pair primary=%u secondary=%u",
			   primary, secondary);
	}
	return kThxBye;
}
OPCODE(0xf2) {
	// DOS Op_f2_handler @ 1000:575a: 1 arg.
	//   if (g_sfx_enabled) DispatchSfxRangeCheck(arg0).
	// DispatchSfxRangeCheck @ 1000:606d: validates arg0 against the
	// active slot range [0x6702..0x6704] and [0x6706..0x6708]; if in
	// range, replays via driver dispatch. Routes through
	// Sound::rangeCheck.
	if (Sound *snd = _engine->sound()) {
		if (!snd->isEnabled()) {
			debugC(1, kDebugLevelScript, "opcode 0xf2: sfx range check skipped (sfx disabled)");
			return kThxBye;
		}
		const uint16 id = uint16(a[0]);
		snd->rangeCheck(id);
		debugC(1, kDebugLevelScript, "opcode 0xf2: sfx range check id=%u", id);
	}
	return kThxBye;
}
OPCODE(0xf3) {
	// DOS Op_f3 @ 1000:5769: nargs=0 per opcodes_nargs.data. Calls
	// RegisterSampleSlot_Bare8 (BX=0xb: CheckSfxPlaying) when SFX is
	// active/enabled, else AX=1 + RegisterSampleSlot_Bare5. Because the
	// C++ wait model re-runs this opcode instead of storing a native DOS
	// sample slot, retry only while CheckSfxPlaying would return "busy".
	debugC(2, kDebugLevelScript, "opcode 0xf3: wait for sfx stop");
	if (sampleSlotWouldError())
		return kThxBye;
	if (Sound *snd = _engine->sound()) {
		if (snd->isEnabled() && snd->isActive()) {
			if (!snd->isSfxPlaying())
				return kThxBye;
			_logic->runLaterWithCurrentMode(current);
		} else {
			_logic->runLaterWithCurrentMode(next, 1);
		}
	} else {
		_logic->runLaterWithCurrentMode(next, 1);
	}
	return kReturn;
}
OPCODE(0xf5) {
	// DOS Op_f5 @ 1000:5812: nargs=0 per opcodes_nargs.data. Calls
	// RegisterSampleSlot_Bare6 (BX=7: CheckMusicPlaying) when music is
	// enabled. CheckMusicPlaying @ 1000:5c78 returns carry clear while the
	// driver current-tune word is nonzero and carry set once it is zero; the
	// saved script resumes only on that carry-set path. Disabled music uses
	// AX=1 + RegisterSampleSlot_Bare5 (BX=5: one-tick countdown). It is NOT
	// a beat-set and has no opcode arguments to read.
	debugC(2, kDebugLevelScript, "opcode 0xf5: wait for music stop");
	if (_engine->dosMusicEnabled() != 0) {
		if (!Music.hasCurrentTune())
			return kThxBye;
		if (sampleSlotWouldError())
			return kThxBye;
		_logic->runLaterWithCurrentMode(current);
	} else {
		if (sampleSlotWouldError())
			return kThxBye;
		_logic->runLaterWithCurrentMode(next, 1);
	}
	return kReturn;
}

OPCODE(0xfa) {
	// DOS Op_fa_handler @ 1000:58ed. Zero args. Stores the
	// protagonist/current-place pair in the save-state staging words,
	// opens the save-slot/name dialogs, formats the progress percent
	// into the saved data image, writes the selected save, then sets
	// g_flag_misc_1 and g_flag_change_room before returning normally.
	// ScummVM's frontend save dialog supplies the slot/name modal;
	// after it returns we mirror the DOS refresh flags by reloading the
	// current backdrop target and marking logic dirty.
	debugC(1, kDebugLevelScript, "opcode 0xfa: save game requested");
	_engine->saveGameDialog();
	reloadLoadedBackdrop(_graphics);
	Log.setLogicDirty();
	return kThxBye;
}
OPCODE(0xfb) {
	// DOS Op_fb_handler @ 1000:593c. Zero args. The slot picker cancel path
	// only redraws and returns. The successful load path restores the
	// staged protagonist/current-place words, sets g_flag_misc_1 and
	// g_flag_change_room, restores the non-status room backup, then sets
	// g_break_loop. ScummVM loadGameDialog/loadGameStream performs the
	// modal load. Engine::loadGameStream mirrors LoadGame_ReadFromDisk's
	// restore-time slot side effect; Logic::restoreRoomFromBackup
	// mirrors RestoreRoomFromBackup's reload/reset tail; kReturn mirrors
	// the successful g_break_loop path.
	debugC(1, kDebugLevelScript, "opcode 0xfb: load game requested (ScummVM hotkey to load)");
	if (_engine->loadGameDialog()) {
		if (!Log.inStatusMode())
			Log.restoreRoomFromBackup();
		else
			Log.setLogicDirty();
		return kReturn; // DOS sets g_break_loop after a successful load.
	}
	reloadLoadedBackdrop(_graphics);
	return kThxBye;
}

OPCODE(0xfd) {
	// DOS Op_fd_handler @ 1000:4087 resolves one arg and stores AX into
	// DAT_1000_885e, which FormatBubbleText_Inner reads as the text-height
	// multiplier.
	const uint16 lineHeight = uint16(a[0]);
	Log.setBubbleLineHeight(lineHeight);
	debugC(2, kDebugLevelScript, "opcode 0xfd: bubble line height = %u", lineHeight);
	return kThxBye;
}

} // End of namespace Interspective
