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

#include "common/util.h"

#include "interspective/logic.h"
#include "interspective/innocent.h"
#include "interspective/inter.h"
#include "interspective/exit.h"
#include "interspective/graphics.h"
#include "interspective/program.h"
#include "interspective/animation.h"
#include "interspective/resources.h"
#include "interspective/room.h"
#include "interspective/util.h"

namespace Common {
	DECLARE_SINGLETON(Interspective::Logic);
}

namespace Interspective {

// Bubble formatter line-height. Mirrors DOS DAT_1000_885e (the font
// metric that LookupCharSprite reads for vertical advance). The C++
// engine's Graphics::kLineHeight = 12 is the same metric (loaded from
// the same font asset). Per-glyph widths come from
// Graphics::getGlyphWidth() which calls into the loaded font sprites
// (DOS LookupCharSprite analog).
static const uint16 kBubbleLineHeight = 12;  // matches Graphics::kLineHeight

static int16 stepCameraToward(int16 current, int16 target, int16 speed) {
	const int16 delta = target - current;
	if (delta == 0)
		return current;
	if (delta < 0) {
		const int16 step = MIN<int16>(-delta, speed);
		return current - step;
	}
	const int16 step = MIN<int16>(delta, speed);
	return current + step;
}

Logic::~Logic() {
	// Animations are owned by the Interpreter that registered them (via rememberAnimation);
	// they are deleted in Interpreter::~Interpreter, which runs as the SharedPtr<Interpreter>
	// members destruct after this body returns. Just clear the index to avoid stale pointers.
	_animations.clear();
}

void Logic::setEngine(Engine *e) {
	_engine = e;
	_resources = e->resources();
	_currentRoom = 0xffff;
	_currentBlock = 0xffff;
	_nextRoom = 0;
	_currentPlace = 0;
}


void Logic::init() {
	_toplevelInterpreter = Common::SharedPtr<Interpreter>(new Interpreter(this, _resources->mainBase(), "main code"));
}

void Logic::initCode() {
	debugC(2, kDebugLevelScript | kDebugLevelFlow, ">>>running initial code");
	_toplevelInterpreter->run(_resources->mainEntryPoint(), kCodeInitial);
	debugC(2, kDebugLevelScript | kDebugLevelFlow, "<<<finished initial code");
}

void Logic::tick() {
	++_frameCounter;

	if (_nextRoom)
		doChangeRoom();

	// Fire any armed post-move callback if the protagonist's walk just
	// completed. Run this before the room loop / queued ops so those see
	// the updated drag/cell/cursor state from the callback's effects —
	// matches DOS where RunPostMoveCallback fires inside the actor's
	// per-tick step routine, before script dispatch resumes.
	runPostMoveCallbackIfReady();

	if (_roomLoop.get()) {
//		gDebugLevel--; // room loops aren't that interesting
		debugC(3, kDebugLevelScript | kDebugLevelFlow, ">>>running room loop code");
		_roomLoop->run(kCodeRoomLoop);
		debugC(3, kDebugLevelScript | kDebugLevelFlow, "<<<finished room loop code");
//		gDebugLevel++;
	}

	runQueued();
	tickMotionText();
	updateScrollPosition();
}

void Logic::callAnimations() {
	if (!_animations.empty())
		debugC(4, kDebugLevelFlow | kDebugLevelAnimation, "running animations");
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end(); ++it) {
		Animation::Status ret = (*it)->tick();
		if (ret == Animation::kRemove) {
			// it will be deleted by its owner block
			Common::List<Animation *>::iterator _i = it;
			it++;
			_animations.erase(_i);
		}
	}
}

void Logic::clearRoomTransientAnimations() {
	for (Common::List<Animation *>::iterator it = _animations.begin(); it != _animations.end();) {
		if (!(*it)->isActor())
			it = _animations.erase(it);
		else
			++it;
	}
}

void Logic::setProtagonist(uint16 actor) {
	_protagonist = getActor(actor);
}

Actor *Logic::protagonist() const {
	return _protagonist;
}

void Logic::updateScrollPosition() {
	const int16 oldX = _cameraX;
	const int16 oldY = _cameraY;

	if (!_inputEnabled) {
		const int16 speedX = _slowCpu ? 4 : 8;
		const int16 speedY = _slowCpu ? 1 : 2;

		if (_cameraTargetX != 0xffff) {
			const int16 targetX = int16(_cameraTargetX);
			if (targetX == _cameraX)
				_cameraTargetX = 0xffff;
			else
				_cameraX = stepCameraToward(_cameraX, targetX, speedX);
		}

		if (_cameraTargetY != 0xffff) {
			const int16 targetY = int16(_cameraTargetY);
			if (targetY == _cameraY)
				_cameraTargetY = 0xffff;
			else
				_cameraY = stepCameraToward(_cameraY, targetY, speedY);
		}
	}

	_scrollChanged = oldX != _cameraX || oldY != _cameraY;
}

void Logic::changeRoom(uint16 newRoom) {
	// just schedule it, we'll execute on next tick
	_nextRoom = newRoom;

	if (_currentRoom == 0xffff)
		doChangeRoom(); // except if it's the first one
}

void Logic::doChangeRoom() {
	assert (_nextRoom);

	debugC(1, kDebugLevelFlow, "Interspective: changeRoom %u → %u", (uint)_currentRoom, (uint)_nextRoom);
	if (_nextRoom == _currentRoom) {
		_nextRoom = 0;
		return;
	}
	_currentRoom = _nextRoom;
	_nextRoom = 0;
	_roomLoop.reset();

	// DOS ApplyChangeRoomTransition sets g_flag_restart_room; MainGameLoop's
	// restart-room path then resets the cast table, actor render table, zone
	// counts, overlay count, anim-list count, no-step/step flags, and the
	// post-move callback before running the new room script. Mirror the
	// modeled pieces here for every room change, not only block changes.
	clearRoomTransientAnimations();
	castTableClearAll();
	_overlayQueue.clear();
	_drawCommandCount = 0;
	_postMoveCallback = PostMoveCallback();
	_zones.clear();
	_collisionZones.clear();
	_zonesB.clear();
	_walkboxes.clear();
	_animList.clear();
	_cameraX = 0;
	_cameraY = 0;
	_cameraTargetX = 0xffff;
	_cameraTargetY = 0xffff;
	_scrollChanged = false;
	_dialogCursor0 = _dialogCursor1 = _dialogClickGate = 0;
	_noStep = false;
	_stepPending = false;
	_roomActive = true;
	_hitTarget = 0;
	_inMapMode = false;
	_inputEnabled = true;
	if (_engine && _engine->graphics())
		_engine->graphics()->setFullscreen(false);
	_motionText.clear();
	_motionTextTicks = 0;

	uint16 newBlock = _resources->blockOfRoom(_currentRoom);

	if (newBlock != _currentBlock) {
		// Drop any deferred code or skip points still pointing into the outgoing block — its
		// Interpreter (and Program::_code) is about to be destroyed and any queued CodePointer
		// to it would be a use-after-free when runQueued() fires it.
		Interpreter *oldBlock = _blockInterpreter.get();
		if (oldBlock) {
			Common::List<DelayedRun>::iterator it = _queued.begin();
			while (it != _queued.end()) {
				if (it->code.interpreter() == oldBlock)
					it = _queued.erase(it);
				else
					++it;
			}
			if (_skipPoint.interpreter() == oldBlock)
				_skipPoint.reset();
		}

		// Block change: any animation (including main-code actors like
		// the protagonist whose _base was rebased into block code via
		// Op_be/Op_b9/etc.) holds a raw pointer into _blockProgram->_code.
		// Reassigning _blockProgram below frees that buffer; the next
		// tick would dereference freed memory and ASan-trip in
		// Animation::tick at `_base + _offset`. Find any such animation
		// and drop its _base now — the actor becomes inert until the
		// script re-attaches it (Op_bd/Op_be).
		// EXCEPTION: when a saved scene frame is holding a SharedPtr to
		// the outgoing _blockProgram, its _code buffer survives the
		// reassignment — Op_01's pop will restore the program. Skip the
		// drop in that case so the saved actors' _base pointers remain
		// valid for the popped scene to resume.
		Program *oldProgram = _blockProgram.get();
		const bool oldProgramPreserved = _savedScene && _savedScene->blockProgram == _blockProgram;
		if (oldProgram && !oldProgramPreserved) {
			const byte *lo = oldProgram->codeBegin();
			const byte *hi = oldProgram->codeEnd();
			foreach(Animation *, _animations)
				(*it)->dropBaseIfIn(lo, hi);
		}

		_currentBlock = newBlock;
		_blockProgram = Common::SharedPtr<Program>(_resources->loadCodeBlock(newBlock));
		// Reset per-block transient state. Per-room visual/runtime
		// state is reset above on every room change.
		_objectExitList.clear();

		char buf[100];
		snprintf(buf, 100, "block %d code", newBlock);

		_blockInterpreter = Common::SharedPtr<Interpreter>(new Interpreter(this, _blockProgram->base(), buf));
		_blockProgram->loadActors(_blockInterpreter.get());
		_blockProgram->loadExits(_blockInterpreter.get());

		debugC(2, kDebugLevelScript, ">>>running block entry code for block %d", newBlock);
		_blockInterpreter->run(_blockProgram->begin(), kCodeNewBlock);
		debugC(2, kDebugLevelScript, "<<<finished block entry code for block %d", newBlock);
	}

	_room = Common::SharedPtr<Room>(new Room(this));
	debugC(2, kDebugLevelScript, ">>>running room entry code for room %d", _currentRoom);
	_blockInterpreter->run(_blockProgram->roomHandler(_currentRoom), kCodeNewRoom);
	debugC(2, kDebugLevelScript, "<<<finished room entry code for room %d", _currentRoom);

	// (iter-27's unconditional `_protagonist->forceRoom(_currentRoom)`
	// removed iter-36 — it caused the protagonist sprite to be rendered
	// on top of the title-card logo and any other "no protagonist" room
	// because we were registering them in EVERY room, ignoring the
	// data-file room state. Replaced by Op_d6's boot-param substitution
	// path which seeds the protagonist's room ONLY when the boot-param
	// shortcut fires — matching what the skipped intro animation would
	// have done.)

	// Re-run setFrame on each actor so its position re-syncs to the new
	// room's frame table (if the actor's current frame index is defined
	// there). Same as the original behavior — disrupting the actor's
	// running script via setRoom turned out to be too aggressive (it
	// reset the script PC to the puppeteer's main-code start, which
	// for actors already in the middle of a sequence would land the PC
	// on data bytes mid-instruction). The "invisible character + stale
	// position" issue is best fixed by loading the DOS places table and
	// doing FindPlaceById on transitions; that's a follow-up iteration.
	foreach(Animation *, _animations)
		if ((*it)->isActor()) {
			Actor * const ac = static_cast<Actor *>(*it);
			ac->setFrame(ac->frameId());
		}
}

void Logic::runLater(const CodePointer &p, uint16 delay) {
	debugC(3, kDebugLevelScript, "will call %s after %d ticks", +p, delay);
	_queued.push_back(DelayedRun(p, delay, frameTicks()));
}

void Logic::runLaterWithMode(const CodePointer &p, uint16 mode, uint16 delay) {
	debugC(3, kDebugLevelScript, "will call %s after %d ticks in mode 0x%02x", +p, delay, mode);
	_queued.push_back(DelayedRun(p, delay, frameTicks(), mode, true));
}

void Logic::runLaterWithCurrentMode(const CodePointer &p, uint16 delay) {
	runLaterWithMode(p, _opcodeMode, delay);
}

uint16 Logic::deferredQueuedCount() const {
	uint16 count = 0;
	for (Common::List<DelayedRun>::const_iterator it = _queued.begin(); it != _queued.end(); ++it)
		if (!it->canceled && it->deferredMode != 0)
			++count;
	return count;
}

bool Logic::queueDeferred(const CodePointer &p) {
	static const uint16 kDeferredModeBase = 0x0b;
	static const uint16 kDeferredSlotCount = 8;

	for (uint16 slot = 0; slot < kDeferredSlotCount; ++slot) {
		const uint16 mode = kDeferredModeBase + slot;
		bool used = false;
		for (Common::List<DelayedRun>::const_iterator it = _queued.begin(); it != _queued.end(); ++it) {
			if (!it->canceled && it->deferredMode == mode) {
				used = true;
				break;
			}
		}
		if (!used) {
			debugC(3, kDebugLevelScript, "will call deferred %s in mode 0x%02x", +p, mode);
			// DOS RunDeferredScripts runs after init/new-room/new-block scripts
			// but before the first object/speech paint of the tick.
			const bool preDeferredPhase = _opcodeMode == kCodeInitial
			                           || _opcodeMode == kCodeNewRoom
			                           || _opcodeMode == kCodeNewBlock;
			const uint16 queuedTick = preDeferredPhase ? uint16(frameTicks() - 1) : frameTicks();
			_queued.push_back(DelayedRun(p, 0, queuedTick, mode, true, mode));
			return true;
		}
	}

	return false;
}

void Logic::startMotionText(uint16 ticks, const byte *text, uint16 length) {
	_motionTextTicks = ticks;
	const uint16 copyLen = length ? length : 1;
	_motionText.resize(copyLen);
	if (text && length)
		memcpy(&_motionText[0], text, length);
	else
		_motionText[0] = 0;
	if (_motionText[copyLen - 1] != 0)
		_motionText.push_back(0);
}

void Logic::tickMotionText() {
	if (_motionTextTicks)
		--_motionTextTicks;
}

void Logic::paintMotionText() {
	if (_motionTextTicks && !_motionText.empty())
		Graf.paintText(0, 0, 0xeb, &_motionText[0]);
}

bool Logic::enableObjectFlag1(uint16 id) {
	const uint16 exitCount = _blockProgram ? _blockProgram->exitsCount() : 0;
	if (id > exitCount) {
		setPendingError(0x14);
		return false;
	}

	setCellBit(id, 0);
	if (Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0)
		if (!exit->isEnabled())
			exit->setEnabled(true);
	return true;
}

bool Logic::disableObjectFlag1(uint16 id) {
	const uint16 exitCount = _blockProgram ? _blockProgram->exitsCount() : 0;
	if (id > exitCount) {
		setPendingError(0x14);
		return false;
	}

	clearCellBit(id, 0);
	if (Exit *exit = _blockProgram ? _blockProgram->getExit(id) : 0)
		if (exit->isEnabled())
			exit->setEnabled(false);
	return true;
}

// DOS Op_38_SwitchToScene @ 1000:3c58 saves the caller's PC plus a
// memcpy of the cast (0x642 bytes) and actor tables. C++ instead
// captures the entire _blockProgram (which owns _actors) plus the
// _blockInterpreter (owns _exits, animation registrations) by
// SharedPtr — semantically equivalent because changing the slot is
// the only mutator. Single slot (not stack) matches DOS exactly:
// `_g_block_pc_offset == 0` is the empty sentinel.
void Logic::saveSceneFrame(const CodePointer &resumePC) {
	SceneFrame *frame = new SceneFrame();
	frame->blockProgram = _blockProgram;
	frame->blockInterpreter = _blockInterpreter;
	frame->currentBlock = _currentBlock;
	frame->currentRoom = _currentRoom;
	frame->room = _room;
	frame->resumePC = resumePC;
	// Snapshot _animations so the sub-scene's loadActors-appended
	// entries can be unwound on pop (DOS RestoreActorTableBackup).
	frame->savedAnimations = _animations;
	// Snapshot the post-move callback record (DOS [0x65ab..0x65bb]).
	// The sub-scene starts with a clean slot and any callbacks it
	// arms get cleared on pop.
	frame->savedPostMoveCallback = _postMoveCallback;
	_postMoveCallback = PostMoveCallback();
	// Snapshot the cast table (DOS Op_38 calls SaveCastBackup which
	// memcpys 0x642 bytes from g_cast_table). Sub-scene starts empty.
	frame->savedCastTable = _castTable;
	castTableClearAll();
	_savedScene = Common::SharedPtr<SceneFrame>(frame);
}

// DOS Op_01 @ 1000:59a3 nested-pop path: when `_g_block_pc_offset != 0`,
// restores the saved PC, calls LoadCodeBlock, RestoreCastBackup,
// RestoreActorTableBackup, and returns WITHOUT setting g_break_loop —
// the dispatch loop continues at the restored PC.
//
// C++ restores the caller's _blockProgram/_blockInterpreter/Room state
// and returns the saved PC to the bytecode dispatcher. The dispatcher
// then transfers directly to that interpreter in the same script run,
// preserving DOS's "return without g_break_loop" behaviour.
CodePointer Logic::restoreSceneFrame() {
	if (!_savedScene)
		return CodePointer();
	SceneFrame frame = *_savedScene;
	_savedScene.reset();
	_blockProgram = frame.blockProgram;
	_blockInterpreter = frame.blockInterpreter;
	_currentBlock = frame.currentBlock;
	_currentRoom = frame.currentRoom;
	_room = frame.room;
	// Restore the _animations list to the pre-Op_38 state. The
	// sub-scene's loadActors appended new entries; replacing the
	// list drops them. The saved actors are still alive because the
	// SceneFrame held the old Program SharedPtr keeping their _code
	// buffer valid.
	_animations = frame.savedAnimations;
	// Restore the post-move callback slot (any sub-scene callback is
	// dropped — DOS Op_97/Op_98 do this by save/restoring the [0x65ab..]
	// register block).
	_postMoveCallback = frame.savedPostMoveCallback;
	// Restore cast table (DOS RestoreCastBackup memcpys g_cast_table
	// from the saved buffer).
	_castTable = frame.savedCastTable;
	debugC(2, kDebugLevelScript, "Op_01 popped scene; resuming immediately at %s", +frame.resumePC);
	return frame.resumePC;
}

// Mirrors DOS RunPostMoveCallback @ 1000:73a6. The DOS check sequence is:
//   if (protag.field+0x6f != 0)        return;         // blocked
//   if (protag.field+0x65 == 0)         return;        // not moving
//   if (post_callback_ptr == 0)         return;        // none armed
//   if (protag.field+0x61 == [0x6609]) → CALL [BP];   // fire
//   clear post_callback_ptr;                           // one-shot
// (DOS clears regardless of whether the frame matched, but only if it
// passed the first three guards. We use the simpler "fire when actor
// stops moving" model — the C++ walk completes when the protagonist's
// _framequeue empties, at which point Actor::isMoving() returns false.
// This collapses the per-tick frame-arrival check into a single
// edge: the tick where the queue just emptied. Functional outcome
// matches DOS for the only documented callback consumers, Op_91-0x93.)
void Logic::runPostMoveCallbackIfReady() {
	if (_postMoveCallback.kind == PostMoveCallback::kNone)
		return;
	if (!_protagonist)
		return;
	if (_protagonist->isMoving())
		return; // wait for walk to complete

	PostMoveCallback cb = _postMoveCallback;
	_postMoveCallback = PostMoveCallback(); // one-shot clear before dispatch

	debugC(2, kDebugLevelScript,
		"post-move callback firing: kind=%d cellId=%u arg0=%u arg1=%u",
		int(cb.kind), cb.cellId, cb.arg0, cb.arg1);

	switch (cb.kind) {
	case PostMoveCallback::kDisableMoveOptionalEnable:
		// DOS @ 0x49df: clearCellBit(cellId) + MovePersonToActor(arg0)
		// + (arg1 != 0 → EnableObjectFlag1 = setCellBit(arg1)).
		if (!disableObjectFlag1(cb.cellId))
			break;
		{
			const uint8 pendingBefore = pendingError();
			movePersonToActor(cb.arg0);
			if (pendingError() != pendingBefore)
				break;
		}
		if (cb.arg1 != 0)
			enableObjectFlag1(cb.arg1);
		break;
	case PostMoveCallback::kDisableEnableUnregister:
		// DOS @ 0x4a36: clearCellBit(cellId) + (arg1 != 0 → setCellBit(arg1))
		// + cursor=0 + drag=0. Matches the *intent* of the buggy DOS
		// continuation (DOS register juggling between the two CALLs is
		// broken — AX gets corrupted by DisableObjectFlag1's cell-byte
		// load — but the script-visible intent is the swap-and-cleanup).
		if (!disableObjectFlag1(cb.cellId))
			break;
		if (cb.arg1 != 0)
			if (!enableObjectFlag1(cb.arg1))
				break;
		setCursorMode(0);
		setDragTarget(0);
		break;
	case PostMoveCallback::kPlaceProtagonistAfterMove:
		// DOS @ 0x4376: place the protagonist in the destination
		// room/frame after the approach walk reaches the current entity.
		// The callback is inert in map mode apart from renderer flags.
		if (!_inMapMode && _protagonist) {
			const uint16 room = cb.cellId;
			const uint16 frame = uint8(cb.arg0);
			const uint16 nextFrame = uint8(cb.arg1);
			_protagonist->placeIn(room, frame, nextFrame);
			if (room != _currentRoom)
				changeRoom(room);
		}
		break;
	case PostMoveCallback::kNone:
	default:
		break;
	}
}

// DOS MovePersonToActor @ 1000:4706 (also entry of Op_84_handler).
//
// Disassembly trace:
//   if AX == 0   → JMP Op_8e (cursor=0, drag=0);
//   if AX > g_persons_count → pending error 0x16;
//   if g_cursor_mode == 0x20: ResetObjectAtActorPosition(g_drag_target);
//   g_drag_target = AX;
//   GetObjectOffset(AX) → ES:SI;
//   if (obj.room != g_current_location && obj.room != 0xffff):
//     CALL RetEmpty (returns CX = 0, DX = 0);
//     CX += g_camera_x;  DX += g_camera_y;
//     obj.x = CX;  obj.y = DX;
//   BX = (obj.room == 0xffff) ? 1 : 0;
//   AX = 2;  JMP BeginDrag_AfterRemoveExit;
//
// BeginDrag_AfterRemoveExit (mode=2, BX=0/1):
//   PrepareDragInteraction(drag_target):
//     g_cursor_mode = 0x20; g_drag_target = AX;
//     obj.room = 0;        // "carried" sentinel
//     CalcSpriteOffsetInGraphic(); save sprite-rect bytes;
//   compute screen-rel cursor pos from obj.x/y - camera
//     (or default 128,160 if BX==1);
//   SetCursorAndPosition(cursor_x, cursor_y).
//
// C++ port: capture the script-observable state changes. Per-object
// sprite-rect bytes / cursor-sprite-at-position rendering are the
// renderer's concern — _cursorMode + _dragTarget transitions plus
// the obj.room = 0 marker drive every script branch downstream.
void Logic::movePersonToActor(uint16 id) {
	if (id == 0) {
		// DOS tail-jump to Op_8e.
		setCursorMode(0);
		setDragTarget(0);
		return;
	}
	if (_resources && _resources->mainDat() && id > _resources->mainDat()->personsCount()) {
		setPendingError(0x16);
		return;
	}
		if (_cursorMode == 0x20 && _dragTarget != 0)
			resetObjectAtActorPosition(_dragTarget);

		setDragTarget(id);

		// If the object is in another (non-sentinel) room, snap its position
	// to the camera origin so the drag pickup happens "where the actor
	// is" rather than wherever the obj was previously drawn.
		const uint16 objRoom = getObjectRoom(id);
		if (objRoom != _currentRoom && objRoom != 0xffff)
			setObjectPosition(id, _cameraX, _cameraY);

		// PrepareDragInteraction subset: cursor-mode + drag-target + obj
		// "carried" room sentinel.
		setCursorMode(0x20);
		setObjectRoom(id, 0);
		if (objRoom == 0xffff)
			unregisterObjectExit(id);
}

bool Logic::prepareDragInteraction(uint16 id) {
	if (id == 0 || (_resources && _resources->mainDat() && id > _resources->mainDat()->personsCount())) {
		setPendingError(0x16);
		return false;
	}

	setCursorMode(0x20);
	setDragTarget(id);
	setObjectRoom(id, 0);
	return true;
}

// DOS ResetObjectAtActorPosition @ 1000:4837.
//
// Disassembly:
//   GetObjectOffset(AX) → ES:SI;
//   if obj.room == 0xffff: CALL 0x331f (default position seed);
//   CALL AddExitToList;  if JC: pending error 0x21;
//   < sprite-relative centering math: CX = (0xb6 - sprite_h) / 2
//                                      DX = (0x1f - sprite_???) / 2
//                                      then add per-frame sprite offset >
//   obj.room = 0xffff;        // mark as exit-mode
//   obj.x = CX;  obj.y = DX;
//   < more sprite metadata save >
//   set dirty flags.
//
	// C++ port: keep the DOS room sentinel and model dynamic-list membership
	// explicitly. The exact sprite-centering math is rendering-side; use the
	// protagonist position as the screen anchor when available.
void Logic::resetObjectAtActorPosition(uint16 id) {
	if (id == 0 || (_resources && _resources->mainDat() && id > _resources->mainDat()->personsCount())) {
		setPendingError(0x16);
		return;
	}

	if (getObjectRoom(id) == 0xffff)
		unregisterObjectExit(id);
	if (!registerObjectExit(id))
		return;

	setObjectRoom(id, 0xffff);
	const int16 x = _protagonist ? int16(_protagonist->position().x) : getObjectPosX(id);
	const int16 y = _protagonist ? int16(_protagonist->position().y) : getObjectPosY(id);
	setObjectPosition(id, x, y);
}

// DOS SendActorToTarget @ 1000:7323 dispatches MoveProtagonistToEntity
// @ 1000:7331 which switches on a "type" register (DX = 1 exit / 2
// object / 3 actor). The C++ port doesn't carry a separate type tag
// across opcode dispatch (DOS sets DX inside the opcode body before
// the call — Op_b5 sets DX=1, Op_b6 DX=2, Op_b7 DX=3). Instead, we
// resolve the target by id and try each entity table in order:
//   * Actor by id (1-based DOS actor id) → frame match.
//   * Exit by id (current block's exit list) → screen pos → nearest frame.
//   * Object by id (Logic::_objectRoom + _objectPos*) → nearest frame.
// First match wins. Cross-room targets are silent no-ops (matches DOS:
// MoveProtagonistToEntity returns early if the entity's room field
// doesn't match g_current_location, with no pending error).
bool Logic::sendActorToTarget(Actor *walker, uint16 targetId) {
	if (!walker) {
		walker = _protagonist;
		if (!walker)
			return false;
	}
	if (!_room)
		return false;

	// 1) Actor target — direct frame match.
	if (Actor *target = getActor(targetId)) {
		if (target->room() == _currentRoom) {
			walker->moveTo(target->frameId());
			return true;
		}
		return false;
	}

	// 2) Exit target — DOS uses GetExitOffset(id) → SI, then reads
	// SI[1] (= screen x) / SI[2] (= screen y) / SI[5] (= sprite flag).
	if (_blockProgram) {
		if (Exit *exit = _blockProgram->getExit(targetId)) {
			if (exit->room() == _currentRoom) {
				const uint16 frame = _room->nearestFrameTo(
					int16(exit->position().x),
					int16(exit->position().y));
				if (frame) {
					walker->moveTo(frame);
					return true;
				}
			}
			return false;
		}
	}

	// 3) Object target — same shape via Logic::_objectRoom/Pos.
	if (getObjectRoom(targetId) == _currentRoom) {
		const uint16 frame = _room->nearestFrameTo(
			getObjectPosX(targetId), getObjectPosY(targetId));
		if (frame) {
			walker->moveTo(frame);
			return true;
		}
	}
	return false;
}

bool Logic::sendActorToCurrentEntity(Actor *walker) {
	if (!walker) {
		walker = _protagonist;
		if (!walker)
			return false;
	}
	if (!_room)
		return false;

	const uint16 id = _currentEntityId;
	switch (_gameState) {
	case 1: { // exit
		if (id == 0) {
			setPendingError(0x14);
			return false;
		}
		if (!_blockProgram)
			return false;
		Exit *exit = _blockProgram->getExit(id);
		if (!exit)
			return false;
		if (exit->room() != _currentRoom)
			return false;
		const uint16 frame = _room->nearestFrameTo(
			int16(exit->position().x), int16(exit->position().y));
		if (!frame)
			return false;
		walker->moveTo(frame);
		return true;
	}
	case 2: { // object/person
		if (id == 0) {
			setPendingError(0x16);
			return false;
		}
		if (getObjectRoom(id) != _currentRoom)
			return false;
		const uint16 frame = _room->nearestFrameTo(getObjectPosX(id), getObjectPosY(id));
		if (!frame)
			return false;
		walker->moveTo(frame);
		return true;
	}
	case 3: { // actor
		Actor *target = getActor(id);
		if (!target) {
			setPendingError(0x17);
			return false;
		}
		if (target->room() != _currentRoom)
			return false;
		walker->moveTo(target->frameId());
		return true;
	}
	default:
		return false;
	}
}

// DOS Op_ba @ 1000:4fe5 / Op_bb @ 1000:4fde:
//   g_walk_speed_flag = 0/1;       // 0=fast, 1=slow
//   if (in_map_mode) RET;
//   if (id > g_anim_count_max) pending error 0x17;
//   if (id == g_main_character_id) g_break_inner = 1;
//   CheckActorAnimReady(id);
//   if (NOT ready) RegisterSampleSlot_LoadDefaultsAndMark; RET;
//   GetActorOffset(id) → ES:SI;
//   ES:[SI + 0x4] = arg2;          // screen x
//   ES:[SI + 0x6] = arg3;          // screen y
//   ES:[SI + 0x61] = 0;            // current frame
//   ResolveOpcodeArg1;             // anim selector (mode-dependent)
//   InitActorState();              // jump script to actor's main code
//
// C++ port: positional state goes through Actor::placeIn (DOS-aligned
// non-script-resetting placement). The walk_speed_flag has no C++
// analog (per-tick step rate is animation-driven in C++); the slowSpeed
// param is passed through for future hookup.
bool Logic::walkActorAnim(uint16 actorId, int16 destX, int16 destY, bool slowSpeed) {
	(void)slowSpeed;  // C++ animation tick rate is per-Animation, not per-walk.
	Actor *ac = getActor(actorId);
	if (!ac) {
		setPendingError(0x17);
		return false;
	}
	if (!_room)
		return false;

	// DOS sets ES:[SI+0x4]/[SI+0x6] = arg2/arg3 directly (raw screen
	// coords). We translate to a frame via nearestFrameTo so the
	// walk script can pathfind.
	const uint16 destFrame = _room->nearestFrameTo(destX, destY);
	if (destFrame)
		ac->moveTo(destFrame);
	return true;
}

bool Logic::actorIdle(const Actor *actor) const {
	if (!actor)
		return true;
	return !actor->isMoving() && !actor->isSpeaking();
}

// DOS Op_c3_RegisterCastEntry @ 1000:514a (full byte-for-byte spec):
//   ResolveOpcodeArg1 → arg1 (x);
//   ResolveOpcodeArg2 → arg2 (y);
//   ResolveOpcodeArg0 → arg0 (id);
//   for slot in g_cast_table[18]:
//     if (wActive == 0):
//       w_unk_02 = arg0;                    // entity id
//       wActive  = g_codeptr_es_save;        // caller code segment
//       wX       = arg1;
//       wY       = arg2;
//       p_data[0] = 0;                       // ┐
//       p_data[1] = 0;                       // │
//       p_data[2] = 0;                       // │ DOS bookkeeping init
//       p_data[3] = 0;                       // │ (renderer state in DOS)
//       p_data[6] = 1;                       // │ — frame counter
//       p_data[7] = 0;                       // │
//       p_data[8] = 0xff;                    // │ — sprite index sentinel
//       bRect_w   = 0xff;                    // │ — sprite-bounds sentinel
//       bRect_h   = 0xff;                    // │
//       p_data[10] = 0;                      // │
//       p_data[12] = 0;                      // ┘
//       return;
//   pending error 0x2a;
//
// C++ stores `active` as uint16 (0 = free; we use 1 since C++ has no
// "code segment"). The 81-byte `raw` array is initialized per the
// DOS spec — even though ScummVM doesn't read these fields itself,
// matching DOS bytes ensures Op_38/Op_97 backups round-trip exactly
// and any future renderer/script that does read them sees DOS values.
bool Logic::castTableRegister(uint16 id, int16 x, int16 y) {
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.active == 0) {
			e.active = 1;            // DOS stores caller seg; we use 1 (non-zero = active).
			e.id = id;
			e.x = x;
			e.y = y;
			// Re-init the bookkeeping per DOS Op_c3. Ghidra's CastEntry
			// layout is exact: raw[0]=bRect_w, raw[1]=bRect_h, and
			// raw[2 + N]=p_data[N].
			for (uint j = 0; j < 81; ++j) e.raw[j] = 0;
			e.raw[0] = 0xff;          // bRect_w
			e.raw[1] = 0xff;          // bRect_h
			e.raw[8] = 1;             // p_data[6] — frame counter
			e.raw[10] = 0xff;         // p_data[8] — sprite index
			// p_data[0/1/2/3/7/10/12] remain zero from the clear above.
			return true;
		}
	}
	// No free slot — DOS sets pending error 0x2a.
	setPendingError(0x2a);
	return false;
}

// DOS Op_c4_SetCastEntryPosition @ 1000:51a8 (BUG-ACCURATE port):
//
// Disassembly:
//   1000:51a8  CALL ResolveOpcodeArg1   ; AX = arg1
//   1000:51ab  MOV  CX, AX               ; CX = arg1  ← saved here…
//   1000:51ad  CALL ResolveOpcodeArg2   ; AX = arg2
//   1000:51b0  MOV  DX, AX               ; DX = arg2
//   1000:51b2  CALL ResolveOpcodeArg0   ; AX = arg0
//   1000:51b5  MOV  CX, 0x12             ; CX = 0x12 (loop count)
//                                          ↑ ARG1 IS CLOBBERED HERE
//   1000:51b8  MOV  SI, 0x1977
//   1000:51bb  CMP  [SI+0x2], AX         ; cmp slot.id, arg0
//   1000:51be  JZ   0x51c6               ; match → write
//   1000:51c0  ADD  SI, 0x59
//   1000:51c3  LOOP 0x51bb               ; LOOP decrements CX
//   1000:51c5  RET                        ; no match
//   1000:51c6  MOV  [SI+0x4], CX         ; wX = CX = remaining_loop_count
//                                          ↑ NOT arg1 — DOS bug
//   1000:51c9  MOV  [SI+0x6], DX         ; wY = DX = arg2  (correct)
//   1000:51cc  RET
//
// Effect: when slot N (0-indexed) matches, CX still holds (0x12 - N)
// after LOOP iterations. So the saved wX = (kCastTableCap - matched_idx).
// arg1 is silently discarded.
//
// To match DOS faithfully we reproduce the bug. If IUC scripts depend
// on the buggy wX values (or just don't observe them), divergent
// behaviour would be bug-compatible only by reproducing.
void Logic::castTableSetPos(uint16 id, int16 x, int16 y) {
	(void)x;  // DOS bug: arg1 is clobbered before the write.
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.active != 0 && e.id == id) {
			// DOS: wX = (0x12 - i_iterations_through_LOOP).
			// LOOP decrements CX before checking; so for match on i=0,
			// CX is still 0x12; for i=1, CX is 0x11; etc.
			// → wX = kCastTableCap - i.
			e.x = int16(kCastTableCap - i);
			e.y = y;
			return;
		}
	}
	// Silent no-op on miss (matches DOS — no pending error).
}

// DOS Op_c5_ClearCastEntry @ 1000:51cd:
//   pbVar1 = ResolveOpcodeArg0;  iVar2 = 0x12;  pCVar3 = g_cast_table;
//   do {
//     if (pCVar3->w_unk_02 == arg0) {
//       pCVar3->w_unk_02 = 0;
//       pCVar3->wActive  = 0;
//       return;
//     }
//     pCVar3 += 1;  iVar2 -= 1;
//   } while (iVar2 != 0);
//
// Note: DOS only zeros the FIRST 4 BYTES of the slot (wActive +
// w_unk_02). wX/wY/p_data/bRect_w/h are LEFT INTACT. C++ matches.
void Logic::castTableClear(uint16 id) {
	for (uint i = 0; i < _castTable.size(); ++i) {
		CastEntry &e = _castTable[i];
		if (e.active != 0 && e.id == id) {
			e.active = 0;
			e.id = 0;
			// Intentionally preserve x, y, raw[] — DOS leaves them.
			return;
		}
	}
}

// DOS ResetCastTable @ 1000:671d clears only wActive + w_unk_02 for
// all 18 slots. Position/raw renderer bytes are left as-is, like Op_c5.
void Logic::castTableClearAll() {
	for (uint i = 0; i < _castTable.size(); ++i) {
		_castTable[i].active = 0;
		_castTable[i].id = 0;
	}
}

// DOS FormatBubbleText_FullPath @ 1000:9333.
//
// Iterates the DI-pointed source byte stream, copying to the formatted
// buffer at 0x40b7 while expanding markup characters. Tracks word count
// (DAT_1000_94b5) and per-line pixel width (iVar7). At terminator (0x00):
//   total_height = (word_count * line_height + 2);
//   if (total_height <= line_height + 2) total_height += line_height;
// else if buffer overflow (1024 chars without terminator):
//   pending_error 0x11.
//
// Markup byte semantics (per DOS decompile + line-by-line trace):
//   0x00 → terminator. Emit final row, return total_height.
//   0x20 (' ') → emit + word_count++.
//   0x2d ('-') → emit + word_count++.
//   0x0d → emit row terminator (forced newline).
//   0x05 → inline literal until next 0x00; each char advances width;
//          spaces inside increment word_count. Then 2 trailing bytes
//          (DOS bookkeeping word) are absorbed into the buffer.
//   0x09 → 1-byte param = X-offset to advance (raw spacing).
//   0x07 → 1-byte param consumed (no text effect).
//   0x06 → 2-byte big-endian decimal number formatter (FormatDecimalNumber);
//          consumes 3 input bytes (marker + 2-byte value).
//   0x0a → conditional skip-block (3-byte param: 1-byte condition + 2-byte
//          jump-to-STX). If gameState bit at the indexed offset is FALSE,
//          skip forward to STX (Start-of-TeXt) marker.
//   0x0b → inverse of 0x0a (skip if TRUE).
//   0x02 → fall-through (treated as a printable / emit-via-LookupCharSprite).
//   else → emit via LookupCharSprite (= advance width by char's sprite width).
//
// C++ port: produces the cleaned text (without markup bytes) plus the
// dimensions DOS computes. Per-glyph widths come from
// Graphics::getGlyphWidth (the C++ analog of DOS LookupCharSprite —
// reads the actual font sprite metrics from the loaded font asset).
// Word/line counts and the total-height formula match DOS exactly.
Logic::FormattedBubble Logic::formatBubbleText(const byte *src) const {
	FormattedBubble out;
	out.lineCount = 1;          // DOS DAT_1000_94b5 init = 1
	out.totalHeight = 0;
	out.maxLineWidth = 0;
	out.truncated = false;
	if (!src) {
		out.totalHeight = kBubbleLineHeight * 2 + 2;  // DOS minimum
		return out;
	}

	Graphics *g = (_engine ? _engine->graphics() : 0);
	const byte *p = src;
	int currentWidth = 0;
	uint16 bytesEmitted = 0;
	const uint16 kBufferCap = 1024;     // DOS iVar5 init = 0x400

	// Returns DOS LookupCharSprite-equivalent width. Falls back to a
	// fixed 6 px width if Graphics isn't available (early-init path) —
	// in normal gameplay g is always set.
	auto charPixelWidth = [g](byte ch) -> uint16 {
		if (g) return g->getGlyphWidth(ch);
		return 6;
	};

	while (true) {
		if (bytesEmitted >= kBufferCap) {
			// DOS sets g_pendingErrorCode = 0x11 on overflow.
			out.truncated = true;
			break;
		}
		const byte b = *p++;
		if (b == 0x00) {
			// Terminator. Emit final row → return.
			break;
		}
		if (b == 0x20 || b == 0x2d) {
			// space / dash — word break.
			out.text += char(b);
			out.lineCount++;
			currentWidth += charPixelWidth(b);
			++bytesEmitted;
			continue;
		}
		if (b == 0x0d) {
			// Forced newline.
			out.text += '\n';
			++bytesEmitted;
			if (currentWidth > out.maxLineWidth)
				out.maxLineWidth = currentWidth;
			currentWidth = 0;
			continue;
		}
		if (b == 0x05) {
			// Literal-until-null.
			while (true) {
				const byte lit = *p++;
				if (lit == 0x00)
					break;
				if (lit == 0x20)
					out.lineCount++;
				out.text += char(lit);
				currentWidth += charPixelWidth(lit);
				++bytesEmitted;
				if (bytesEmitted >= kBufferCap) {
					out.truncated = true;
					return out;
				}
			}
			// Absorb 2 trailing bookkeeping bytes (DOS).
			p += 2;
			continue;
		}
		if (b == 0x09) {
			// 1-byte param = X-offset spacing.
			const byte amount = *p++;
			currentWidth += amount;
			out.lineCount++;
			continue;
		}
		if (b == 0x07) {
			// 1-byte param consumed (no text effect).
			(void)*p++;
			continue;
		}
		if (b == 0x06) {
			// Decimal-number formatter: 2-byte BE value.
			const uint16 num = (uint16(p[0]) << 8) | uint16(p[1]);
			p += 2;
			Common::String numStr = Common::String::format("%u", num);
			out.text += numStr;
			for (uint i = 0; i < numStr.size(); ++i)
				currentWidth += charPixelWidth(byte(numStr[i]));
			bytesEmitted += uint16(numStr.size());
			continue;
		}
		if (b == 0x0a || b == 0x0b) {
			// Conditional-skip markup: 3-byte param. Without a wired-up
			// game-state-bit lookup (DOS DAT_1000_009d table), we
			// conservatively keep the block (don't skip). The 3 bytes are
			// consumed; the SkipMarkupBlockToStx scan would advance p
			// further. We approximate by NOT skipping and consuming only
			// the 3-byte header.
			(void)*p++;  (void)*p++;  (void)*p++;
			continue;
		}
		if (b == 0x02) {
			// Fall-through to printable handling (DOS does the same).
		}
		// Default: emit char + advance width via per-glyph lookup.
		out.text += char(b);
		currentWidth += charPixelWidth(b);
		++bytesEmitted;
	}

	if (currentWidth > out.maxLineWidth)
		out.maxLineWidth = currentWidth;

	// DOS height formula: word_count * line_height + 2; minimum 2*line_height + 2.
	out.totalHeight = uint16(out.lineCount) * kBubbleLineHeight + 2;
	if (out.totalHeight <= kBubbleLineHeight + 2)
		out.totalHeight += kBubbleLineHeight;
	return out;
}

bool Logic::cancelDeferred(const CodePointer &p) {
	Common::List<DelayedRun>::iterator it = _queued.begin();
	while (it != _queued.end()) {
		if (!it->canceled && it->deferredMode != 0
				&& it->code.offset() == p.offset()
				&& it->code.interpreter() == p.interpreter()) {
			debugC(3, kDebugLevelScript, "cancel deferred %s mode 0x%02x", +p, it->deferredMode);
			const bool selfCancel = _runningQueuedMode != 0 && it->deferredMode == _runningQueuedMode;
			it->canceled = true;
			return selfCancel;
		}
		++it;
	}
	return false;
}

void Logic::runQueued() {
	if (_queued.empty()) return;

	Interpreter * const liveTopLevel = _toplevelInterpreter.get();
	Interpreter * const liveBlock = _blockInterpreter.get();

	Common::Queue<Common::List<DelayedRun>::iterator> toRemove;
	const uint16 entriesAtStart = uint16(_queued.size());
	debugC(2, kDebugLevelFlow | kDebugLevelScript, ">>>running queued code");
	Common::List<DelayedRun>::iterator it = _queued.begin();
	for (uint16 visited = 0; visited < entriesAtStart && it != _queued.end(); ++visited) {
		Common::List<DelayedRun>::iterator current = it;
		++it;

		if (current->canceled) {
			toRemove.push(current);
		} else if (current->delay == 0 && current->queuedTick == frameTicks()) {
			debugC(3, kDebugLevelScript, "deferred fresh %s until next tick", +current->code);
		} else if (current->delay) {
			debugC(3, kDebugLevelScript, "delayed %s, delay now %d", +current->code,
					current->delay);
			current->delay--;
		} else {
			Interpreter *target = current->code.interpreter();
			if (target != liveTopLevel && target != liveBlock) {
				warning("dropping stale queued CodePointer (interpreter %p not live)",
						(void *)target);
				toRemove.push(current);
				continue;
			}
			debugC(2, kDebugLevelFlow | kDebugLevelScript, ">>>running %s", +current->code);
			_runningQueued = &current->code;
			_runningQueuedMode = current->deferredMode;
			const uint16 savedOpcodeMode = _opcodeMode;
			if (current->hasRunMode)
				current->code.run(static_cast<OpcodeMode>(current->runMode));
			else
				current->code.run();
			_opcodeMode = savedOpcodeMode;
			_runningQueued = 0;
			_runningQueuedMode = 0;
			debugC(2, kDebugLevelFlow | kDebugLevelScript, "<<<finished %s", +current->code);
			toRemove.push(current);
		}
	}
	debugC(2, kDebugLevelFlow | kDebugLevelScript, "<<<finished queued code");

	while (!toRemove.empty())
		_queued.erase(toRemove.pop());
}

void Logic::addAnimation(Animation *anim) {
	_animations.push_back(anim);
}

void Logic::removeAnimation(Animation *anim) {
	_animations.remove(anim);
}

void Logic::setRoomLoop(const CodePointer &code) {
	_roomLoop = Common::SharedPtr<CodePointer>(new CodePointer(code));
}

/* counting starts with 1 */
Actor *Logic::getActor(uint16 id) const {
	if (id == 0)
		return nullptr;
	id--;
	if (id < _resources->mainDat()->actorsCount())
		return _resources->mainDat()->actor(id);
	id -= _resources->mainDat()->actorsCount();
	return _blockProgram ? _blockProgram->actor(id) : nullptr;
}

void Logic::setSkipPoint(const CodePointer &p) {
	_skipPoint = p;
}

bool Logic::redirectDeferredMode(uint16 mode, const CodePointer &target) {
	for (Common::List<DelayedRun>::iterator it = _queued.begin(); it != _queued.end(); ++it) {
		if (!it->canceled && it->deferredMode == mode) {
			debugC(2, kDebugLevelScript, "redirect deferred mode 0x%02x to %s", mode, +target);
			it->code = target;
			it->delay = 0;
			it->queuedTick = frameTicks();
			return true;
		}
	}
	return false;
}

void Logic::skipCutscene() {
	if (_skipPoint.isEmpty()) return;

	const CodePointer target = _skipPoint;
	const uint16 proc = _escBreakProc;
	clearEscBreakPoint();

	debugC(2, kDebugLevelScript, ">>>running animation skip code");
	if (proc >= 0x0b) {
		if (!redirectDeferredMode(proc, target))
			runLater(target);
	} else {
		target.run(static_cast<OpcodeMode>(proc));
	}
	debugC(2, kDebugLevelScript, "<<<finished animation skip code");
}

Animation *Logic::animation(uint16 offset) const {
	foreach_const (Animation *, _animations)
		if ((*it)->baseOffset() == offset)
			return (*it);

	return 0;
}


} // End of namespace Interspective
