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

#ifndef INTERSPECTIVE_LOGIC_H
#define INTERSPECTIVE_LOGIC_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/list.h"
#include "common/queue.h"
#include "common/singleton.h"
#include "common/str.h"

#include "interspective/inter.h"
#include "interspective/mapfile.h"
#include "interspective/prog_dat.h"
#include "interspective/program.h"
#include "interspective/room.h"
#include "interspective/value.h"

namespace Common {
class Serializer;
}

namespace Interspective {
//

class Actor;
class Animation;
class Debugger;
class Engine;
class Graphics;
class Music;
class Resources;

class Logic : public Common::Singleton<Logic> {
public:
	Logic()
		: _frameCounter(0),
		  _gameState(0),
		  _inStatusMode(false),
		  _fullscreenGateActive(false),
		  _fullscreenGateInitialized(false),
		  _enteringStatusScreen(false),
		  _roomActive(true),
		  _logicDirty(false),
		  _forceRoomRestart(false),
		  _paused(false),
		  _stepPending(false),
		  _autoCloseTimer(0),
		  _noStep(false),
		  _breakInner(false),
		  _menuStashA(0),
		  _menuStashB(0),
		  _menuStashConsumed(false),
		  _defaultCursorMode(0x10),
		  _cursorMode(0x10),
		  _cursorStepIndex(0),
		  _dragTarget(0),
		  _dragTargetMode40(0),
		  _implicitActor(0),
		  _hitTarget(0),
		  _switchValue(0),
		  _switchTarget(0),
		  _branchState(0),
		  _pendingError(0),
		  _lastErrorCode(0),
		  _gameScore(0),
		  _maxGameScore(100), // overwritten from iuc_main.dat during resource load
		  _currentEntityId(0),
		  _drawCommandCount(0),
		  _actorFrameCount(0),
		  _walkSpeedFlag(0),
		  _dialogCursor0(0), _dialogCursor1(0), _dialogClickGate(0),
		  _postMoveTargetFrameMirror(0),
		  _opcodeMode(0),
		  _escBreakProc(0),
		  _escBreakSrcPC(0),
		  _escBreakPending(false),
		  _bubbleLineHeight(12),
		  _parserBufferCapacity(60),
		  _callDepth(0),
		  _runningQueued(0),
		  _runningQueuedMode(0),
		  _motionTextTicks(0),
		  _slowCpu(false),
		  _cameraX(0), _cameraY(0),
		  _cameraTargetX(0xffff), _cameraTargetY(0xffff),
		  _scrollDx(0), _scrollDy(0),
		  _scrollChanged(false),
		  _inputEnabled(true),
		  _cursorLockedPos(160, 100),
		  _buttonsLocked(0),
		  _rightClickCycleCooldown(4),
		  _speechSkipInput(false),
		  _uiTextSpeechSlot(0xffff),
		  _loadedBackdropId(0),
		  _loadBlockOverrideId(0xffff),
		  _statusSaveOverrideActive(false) {
		_protagonist = nullptr;
		_protagonistId = 0;
		for (int i = 0; i < 7; ++i)
			_graphicSlots[i] = 0;
		_castTable.resize(kCastTableCap);
		_activeActorIds.resize(kActiveActorTableSlots);
		_speechSlots.resize(kSpeechSlotCount);
	}
	~Logic();

	// Monotonically increasing per-tick counter — wraps at uint16 to mirror the DOS
	// g_tick_counter at DS:0x6666. Used by Op_10 (timer-fire) and Op_ed (set-deadline).
	uint16 frameTicks() const { return uint16(_frameCounter); }

	// VM state (mirrors DS:0x6XXX globals from the binary).
	// Legacy alias: earlier C++ called DOS `g_cursor_mode` "verb mode".
	// Ghidra names the real global at DS:0x6678; DS:0x665d is
	// `g_drag_step_idx`, not the cursor/verb mode predicate used by
	// opcodes 0x0a/0x0b/0x0d/0x0e and the cursor gate helpers.
	uint16 verbMode() const { return _cursorMode; }
	void setVerbMode(uint16 v) { setCursorMode(v); }
	// DS:0x666e — current entity type for the active entity script:
	// 0 = none, 1 = exit, 2 = object/person, 3 = actor.
	uint16 gameState() const { return _gameState; }
	void setGameState(uint16 s) { _gameState = s; }
	bool inStatusMode() const { return _inStatusMode; }
	void setInStatusMode(bool v) { _inStatusMode = v; }
	bool fullscreenGateActive() const { return _fullscreenGateActive; }
	void setFullscreenGateActive(bool v) { _fullscreenGateActive = v; }
	bool fullscreenGateInitialized() const { return _fullscreenGateInitialized; }
	void setFullscreenGateInitialized(bool v) { _fullscreenGateInitialized = v; }
	// DOS g_room_active (DS:0x6740). Cutscene/fullscreen opcodes clear it
	// to block room interactions; restore opcodes set it again.
	bool roomActive() const { return _roomActive; }
	void setRoomActive(bool v) { _roomActive = v; }
	bool logicDirty() const { return _logicDirty; }
	void setLogicDirty(bool v = true) { _logicDirty = v; }
	bool paused() const { return _paused; }
	void setPaused(bool v = true) { _paused = v; }
	bool stepPending() const { return _stepPending; }
	void setStepPending(bool v) { _stepPending = v; }
	void tickRightClickCycleCooldownLikeDos() {
		if (!_fullscreenGateActive && !_noStep && _rightClickCycleCooldown != 0)
			--_rightClickCycleCooldown;
	}
	void cycleCursorModeByRightClickLikeDos();
	void activateStatusButtonHotkeyLikeDos();
	bool setVerbModeFromHitRegionLikeDos(uint16 hitRegion);
	uint16 updateAutoCloseTimerSpriteLikeDos();
	// DOS g_flag_no_step (DS:0x6747). Dispatch-table opcode 0x96 sets it to
	// lock player input during cutscenes. Opcode 0x95 clears both _noStep
	// and _stepPending.
	bool noStep() const { return _noStep; }
	void setNoStep(bool v) { _noStep = v; }
	// DOS g_break_inner (DS:0x672f). InterpretBytecode clears it at each
	// top-level script entry; protagonist walk helpers set it so click
	// dispatch does not auto-send the actor a second time.
	bool breakInner() const { return _breakInner; }
	void setBreakInner(bool v) { _breakInner = v; }
	// DOS g_walk_speed_flag (DS:0x674d). Several opcode pairs set this
	// byte before resolving code/table pointers: 0 = main/resource bank,
	// 1 = block bank.
	uint8 walkSpeedFlag() const { return _walkSpeedFlag; }
	void setWalkSpeedFlag(uint8 v) { _walkSpeedFlag = v; }

	// Per-room geometry tables, mirrors of the DOS g_zone / g_collision_zone /
	// g_zone_b / g_walkbox arrays. Populated by Op_d9 / Op_dd, cleared by
	// Op_da / Op_dc / Op_de / Op_e2. Pathfinding and hotspot dispatch will
	// read these once implemented.
	struct Zone {
		uint16 a, b, c, d; // 4 raw uint16 args (typically a bbox)
	};
	struct CollisionZone {
		uint16 a, b, c, d;
		int16 slot; // Op_db stores ReadVarBySlot_RHS() - 1
	};
	struct ZoneB {
		uint16 a, b, c, d, var; // Op_dd writes 5 fields per entry
	};
	void zonesClear() { _zones.clear(); }
	void zonesAdd(const Zone &z) {
		if (_zones.size() < 8)
			_zones.push_back(z);
	}
	const Common::Array<Zone> &zones() const { return _zones; }

	void collisionZonesClear() { _collisionZones.clear(); }
	void collisionZonesAdd(const CollisionZone &z) {
		if (_collisionZones.size() < 24)
			_collisionZones.push_back(z);
	}
	const Common::Array<CollisionZone> &collisionZones() const { return _collisionZones; }

	void zonesBClear() { _zonesB.clear(); }
	void zonesBAdd(const ZoneB &z) {
		if (_zonesB.size() < 30)
			_zonesB.push_back(z);
	}
	const Common::Array<ZoneB> &zonesB() const { return _zonesB; }

	void walkboxesClear() { _walkboxes.clear(); }
	const Common::Array<Zone> &walkboxes() const { return _walkboxes; }

	// DOS keeps the Op_df frame/walkbox table in global memory at
	// DS:0x0791. Op_e2 and room restarts clear only the count at DS:0x6617;
	// SetActorPosition still indexes the backing table without checking that
	// count. Keep count and backing storage separate so out-of-count frame
	// IDs retain the same stale records DOS would read.
	void actorFramesClearCount();
	void actorFramesAdd(Common::Point p, const Common::Array<byte> &nexts);
	Actor::Frame actorFrame(uint16 index) const;
	uint16 actorFrameCount() const { return _actorFrameCount; }
	void actorFrameInvalidate(uint16 index);
	void actorFrameSetPosition(uint16 index, int16 x, int16 y);

	// Object-table runtime state. Mirrors the per-object 18-byte record
	// (DOS GetObjectOffset, stride 0x12) field at +0 (room id). Op_7f writes
	// it; Op_18 / Op_1b / Op_21 read it. Entries absent from the sparse map
	// default to the DOS "unplaced" sentinel.
	//
	// Convention (matches DOS):
	//   room == 0           — destroyed/missing (Op_18 fires)
	//   room == -1 (0xffff) — never placed yet (Op_21 fires)
	//   room == kInventoryRoom — carried (in player inventory)
	//   otherwise           — placed in that scene
	enum { kInventoryRoom = 0xfffe };
	void setObjectRoom(uint16 objId, uint16 room) { _objectRoom[objId] = room; }
	bool hasObjectRoom(uint16 objId) const { return _objectRoom.contains(objId); }
	uint16 getObjectRoom(uint16 objId) const {
		Common::HashMap<uint16, uint16>::const_iterator it = _objectRoom.find(objId);
		return it == _objectRoom.end() ? uint16(0xffff) : it->_value;
	}
	bool isObjectMissing(uint16 objId) const {
		return hasObjectRoom(objId) && getObjectRoom(objId) == 0;
	}
	bool isObjectCarried(uint16 objId) const {
		return getObjectRoom(objId) == kInventoryRoom;
	}
	// Object position (DOS object record fields +2 and +4: x, y).
	// Set by Op_80 (with explicit pos) and Op_81 (current-room pos).
	void setObjectPosition(uint16 objId, int16 x, int16 y) {
		_objectPosX[objId] = x;
		_objectPosY[objId] = y;
	}
	int16 getObjectPosX(uint16 objId) const {
		Common::HashMap<uint16, int16>::const_iterator it = _objectPosX.find(objId);
		return it == _objectPosX.end() ? 0 : it->_value;
	}
	int16 getObjectPosY(uint16 objId) const {
		Common::HashMap<uint16, int16>::const_iterator it = _objectPosY.find(objId);
		return it == _objectPosY.end() ? 0 : it->_value;
	}
	void clearObjectRooms() {
		_objectRoom.clear();
		_objectExitList.clear();
	}
	uint16 objectRoomCount() const { return _objectRoom.size(); }

	// Sparse storage for DOS object record fields not first-class
	// members of Logic. Keyed by (objId, fieldOffset). Used by Op_67
	// (WriteObjectFieldSized) for offsets beyond room/x/y. Mirrors
	// Actor::_dosFields semantics — absent keys read as 0.
	uint8 objectField(uint16 objId, uint8 off) const {
		Common::HashMap<uint32, uint8>::const_iterator it = _objectFields.find((uint32(objId) << 8) | off);
		return it == _objectFields.end() ? 0 : it->_value;
	}
	void setObjectField(uint16 objId, uint8 off, uint8 v) {
		const uint32 key = (uint32(objId) << 8) | off;
		if (v == 0)
			_objectFields.erase(key);
		else
			_objectFields[key] = v;
	}
	uint8 exitField(uint16 exitId, uint8 off) const {
		Common::HashMap<uint32, uint8>::const_iterator it = _exitFields.find((uint32(exitId) << 8) | off);
		return it == _exitFields.end() ? 0 : it->_value;
	}
	void setExitField(uint16 exitId, uint8 off, uint8 v) {
		const uint32 key = (uint32(exitId) << 8) | off;
		if (v == 0)
			_exitFields.erase(key);
		else
			_exitFields[key] = v;
	}
	uint16 dosRecordField(uint8 selector, uint16 id, uint8 off, uint8 size) const;
	void setDosRecordField(uint8 selector, uint16 id, uint8 off, uint8 size, uint16 value);

	// Per room-program-group, per-entity cell byte. DOS ResetCellMap fills
	// the whole room-cell buffer with 1, and ReadCellByteAtOffset selects
	// the slice via g_room_index_in_block, not the literal room id. Bit 0
	// gates click dispatch/visibility; bits 1..7 are script flags. We store
	// only values that differ from the DOS default byte 1.
	uint8 cellByte(uint16 id) const {
		Common::HashMap<uint32, uint8>::const_iterator it = _cellBits.find(cellKey(id));
		return it == _cellBits.end() ? 1 : it->_value;
	}
	bool cellBit(uint16 id, uint8 bit) const {
		return ((cellByte(id) >> (bit & 7)) & 1) != 0;
	}
	void setCellBit(uint16 id, uint8 bit) {
		storeCellByte(id, uint8(cellByte(id) | (1 << (bit & 7))));
	}
	void clearCellBit(uint16 id, uint8 bit) {
		uint8 v = uint8(cellByte(id) & ~(1 << (bit & 7)));
		storeCellByte(id, v);
	}
	bool enableObjectFlag1(uint16 id);
	bool disableObjectFlag1(uint16 id);
	uint16 disableObjectFlag1ReturnAx(uint16 id);
	void clearAllCellBits() { _cellBits.clear(); }

	// Per-actor flag70 (DOS Actor.field_0x70). Op_49 writes; nothing reads
	// yet. Stored sparsely on Logic; absent ids read as 0.
	void setActorFlag70(uint16 id, uint8 v) {
		if (v == 0)
			_actorFlag70.erase(id);
		else
			_actorFlag70[id] = v;
	}
	uint8 actorFlag70(uint16 id) const {
		Common::HashMap<uint16, uint8>::const_iterator it = _actorFlag70.find(id);
		return it == _actorFlag70.end() ? 0 : it->_value;
	}

	// Stashed menu args (DOS pbRam00023206/0x23208). Op_4d writes both;
	// 0x4f / 0x51 / 0x53 read them. uRam00023291 = stash-consumed flag.
	void setMenuStash(uint16 a0, uint16 a1) {
		_menuStashA = a0;
		_menuStashB = a1;
		_menuStashConsumed = false;
	}
	void setMenuStashFirstArgLikeDos(uint16 a0) { _menuStashA = a0; }
	void setMenuStashSecondArgLikeDos(uint16 a1) {
		_menuStashB = a1;
		_menuStashConsumed = false;
	}
	uint16 menuStashA() const { return _menuStashA; }
	uint16 menuStashB() const { return _menuStashB; }
	bool menuStashConsumed() const { return _menuStashConsumed; }
	void setMenuStashConsumed(bool v) { _menuStashConsumed = v; }
	uint16 defaultCursorMode() const { return _defaultCursorMode; }
	void setDefaultCursorMode(uint16 v) { _defaultCursorMode = v; }
	uint16 cursorMode() const { return _cursorMode; }
	void setCursorMode(uint16 v) {
		_cursorMode = v;
		_cursorStepIndex = 0;
		_stepPending = false;
	}
	uint16 cursorStepIndex() const { return _cursorStepIndex; }
	void setCursorStepIndex(uint16 v) { _cursorStepIndex = v; }
	uint16 dragTarget() const { return _dragTarget; }
	void setDragTarget(uint16 v) { _dragTarget = v; }
	void clearDragInteractionLikeOp8e() {
		setPaused();
		setCursorMode(1);
		setDragTarget(0);
	}

	// Object placement / drag-state subsystem. Mirrors DOS
	// MovePersonToActor @ 1000:4706 and ResetObjectAtActorPosition @
	// 1000:4837 — the helpers underlying the drag-and-drop opcode
	// family (Op_84/0x8b/0x8c/0x8d/0x8f/0x90 and the post-move-callback
	// continuations of Op_91/0x92/0x93). The DOS routines are tied to
	// per-object sprite-offset bytes and the dynamic exit list. The
	// C++ port models the script-observable subset:
	//   * dragTarget / cursorMode = 0x20  — drag-active state
	//   * obj.room = 0                     — "carried" sentinel (DOS
	//                                        clears obj record's room
	//                                        word in PrepareDragInteraction)
	//   * obj.x/y = camera-relative pos    — for cross-room placement
	// Dynamic object exits are tracked separately from static block exits;
	// room 0xffff remains the DOS room word while `_objectExitList` models
	// AddExitToList / RemoveExitFromList membership. DOS also uses that
	// global list for the visible inventory strip.
	void movePersonToActor(uint16 id);
	void resetObjectAtActorPosition(uint16 id);
	void placeObjectExitAtDosPosition(uint16 id, int16 x, int16 y);
	void clampObjectExitToScreenLikeDos(uint16 id);
	bool prepareDragInteraction(uint16 id);
	void beginDragAfterRemoveExitLikeDos(uint16 id, bool removeExit);
	bool placeObjectInInventoryAtDosPoint(uint16 id, Common::Point screen);
	const Common::Array<uint16> &objectExitList() const { return _objectExitList; }
	bool isObjectExitRegistered(uint16 id) const {
		for (uint i = 0; i < _objectExitList.size(); ++i)
			if (_objectExitList[i] == id)
				return true;
		return false;
	}
	bool registerObjectExit(uint16 id, bool setErrorOnOverflow = true) {
		if (_objectExitList.size() >= 20) {
			if (setErrorOnOverflow)
				setPendingError(0x21);
			return false;
		}
		_objectExitList.push_back(id);
		return true;
	}
	bool unregisterObjectExit(uint16 id) {
		for (uint i = 0; i < _objectExitList.size(); ++i) {
			if (_objectExitList[i] == id) {
				_objectExitList.remove_at(i);
				return true;
			}
		}
		setPendingError(0x22);
		return false;
	}
	bool remapObjectExit(uint16 fromId, uint16 toId) {
		for (uint i = 0; i < _objectExitList.size(); ++i) {
			if (_objectExitList[i] == fromId) {
				_objectExitList[i] = toId;
				return true;
			}
		}
		setPendingError(0x22);
		return false;
	}

	// Walk driver. Mirrors DOS SendActorToTarget @ 1000:7323 →
	// MoveProtagonistToEntity @ 1000:7331 → FindNearestExitToPoint @
	// 1000:72a2 → MoveActorToTargetExit @ 1000:70da. Resolves target
	// entity (actor / exit / object) → screen point → nearest walkable
	// frame in the room → starts pathfinding by populating the actor's
	// `_framequeue`. Returns true on success (walk queued), false if
	// the target is unresolvable or in a different room (DOS treats
	// this as a no-op with no error).
	//
	// `actor` defaults to the protagonist, matching the DOS
	// MoveProtagonistToEntity entry point. The 'walker' parameter
	// exists for the actor-walk opcodes (0xae/0xb8/0xb9/0xba/0xbb)
	// that move a non-protag actor.
	bool sendActorToTarget(Actor *walker, uint16 targetId);
	bool sendActorToEntityByType(Actor *walker, uint16 targetId, uint16 entityType);
	bool sendActorToCurrentEntity(Actor *walker);

	// "Walk-actor-anim" variant — DOS Op_ba/Op_bb @ 1000:4fde/0x4fe5.
	// Sets walker.field+0x4/+0x6 (= screen pos), field+0x61 (= 0,
	// "no current frame"), then InitActorState which jumps the script
	// to the actor's main code. Used for cutscene-driven actor walks
	// where the script provides the destination explicitly. Returns
	// true on success, false on actor-id OOB.
	bool walkActorAnim(uint16 actorId, int16 destX, int16 destY, bool slowSpeed);

	// CheckActorIdle equivalent (DOS @ 1000:645e). Combines
	// Actor::isMoving() and Actor::isSpeaking() into the
	// "ready to receive a new command" predicate. Walk-family opcodes
	// gate on this; if the actor is busy, the opcode yields by
	// re-queuing itself for next tick (DOS does the same via
	// RegisterSampleSlot_LoadDefaultsAndMark — pushes a deferred
	// re-dispatch).
	bool actorIdle(const Actor *actor) const;

	// Cast table — DOS g_cast_table @ DS:0x1977, 18 entries × 0x59
	// (89) bytes. Each entry holds an entity id, screen pos, and the
	// per-cast rendering bookkeeping (sprite anim state, frame counter,
	// rect width/height, mask flags, etc.) DOS uses during its per-tick
	// draw pass to render registered entities. The C++ port preserves
	// every byte that DOS Op_c3/Op_c5 writes so a faithful round-trip
	// is possible; renderer-internal fields are tracked separately by
	// ScummVM but kept here for state-mirror fidelity.
	//
	// DOS struct layout (89 bytes total):
	//   +0x00  wActive  (uint16): 0 = free, non-zero = caller code seg
	//   +0x02  w_unk_02 (uint16): entity id (Op_c3 arg0)
	//   +0x04  wX       (int16):  screen X
	//   +0x06  wY       (int16):  screen Y
	//   +0x08  bRect_w  (uint8)
	//   +0x09  bRect_h  (uint8)
	//   +0x0a..0x58     p_data[79]
	//
	// Op_c3 init spec (only fields DOS writes; rest left as zero from
	// table allocation):
	//   p_data[0] = 0           p_data[1] = 0
	//   p_data[2] = 0           p_data[3] = 0
	//   p_data[6] = 1           (frame counter)
	//   p_data[7] = 0
	//   p_data[8] = 0xff        (sprite index sentinel)
	//   bRect_w = 0xff          bRect_h = 0xff   (sprite-bounds sentinel)
	//   p_data[10] = 0          p_data[12] = 0
	//
	// The table is scene-scoped: Op_38 push captures it via SceneFrame;
	// Op_01 pop restores. Room restart in `doChangeRoom` clears active/id
	// fields (DOS MainGameLoop LAB_1000_063e calls ResetCastTable @ 1000:671d).
	struct CastEntry {
		uint16 active;            // wActive: 0 = free, non-zero = active
		uint16 id;                // w_unk_02
		int16 x, y;               // wX, wY
		uint8 raw[81];            // raw[0/1]=bRect_w/h, raw[2+n]=p_data[n]
		Interpreter *interpreter; // C++ mirror of wActive's caller code segment
		Animation *animation;     // C++ runner for the cast-entry actor script
		CastEntry() : active(0), id(0), x(0), y(0), interpreter(0), animation(0) {
			for (uint8 i = 0; i < 81; ++i)
				raw[i] = 0;
		}
	};
	enum { kCastTableCap = 18 };
	bool castTableRegister(uint16 id, int16 x, int16 y, Interpreter *interpreter);
	void castTableSetPos(uint16 id, int16 x, int16 y);
	void castTableClear(uint16 id);
	void castTableDeactivateAnimation(Animation *animation);
	void castTableClearAll();
	bool castEntryActiveLikeDos(uint16 id) const;
	void runLaterWhenCastEntryInactive(uint16 id, const CodePointer &p);
	const Common::Array<CastEntry> &castTable() const { return _castTable; }

	// Text-bubble + verb-menu modal subsystem.
	// Mirrors DOS DS:0x66ae..0x66c6 register slots and DS:0x6741 stash
	// flag. Used by Op_4d-0x54 (and downstream RunVerbMenuModalLoop @
	// 1000:8730 / RunModalLoop @ 1000:7ea2). The state is a register
	// snapshot that DOS pushes through SetRectAndApply @ 1000:3f86 and
	// then the modal loop reads.
	//
	// Field roles per DOS disassembly:
	//   activeDi/activeEs    — modal "active address" (script PC for
	//                          chained text continuation; falls back to
	//                          0x40b7 = formatted-text buffer base)
	//   activeAx/activeBx    — modal screen position (left, top)
	//   savedDi/savedEs/savedAx/savedBx — Op_53 stash slot (when
	//                          stashFlag was set, Op_53 copies active*
	//                          here before its own bubble runs)
	//   menuChoiceCount      — DOS [0x66c2] (current choice index)
	//   menuMaxChoices       — DOS [0x66c4] (total choices in this menu)
	//   paletteMode          — DOS [0x66c6]:
	//                            1 = verb-menu modal (Op_50/0x51)
	//                            2 = fixed text bubble (Op_52)
	//                            3 = text rect with choices (Op_4e/0x4f)
	//                            4 = stashed text bubble (Op_53 follow-up)
	//   stashFlag            — DOS [0x6741] (DAT_1cb5_6741):
	//                            set by Op_50/0x51 (verb-menu opens),
	//                            cleared by Op_52/Op_53 (next bubble
	//                            consumes the stash). Op_53 branches on
	//                            its value.
	//   selectedItemIdx      — DOS [0x66a2] (modal-loop selection result)
	//   textContinuationPtr  — DOS [0x6713]-related (g_text_continuation_ptr)
	//   menuDone             — DOS g_menu_done flag (modal-loop exit)
	struct ModalState {
		uint16 activeDi, activeEs;
		uint16 activeAx, activeBx;
		uint16 savedDi, savedEs;
		uint16 savedAx, savedBx;
		uint16 menuChoiceCount;
		uint16 menuMaxChoices;
		uint8 paletteMode;
		uint8 stashFlag;
		uint16 selectedItemIdx;
		uint16 textContinuationPtr;
		Common::String activeText;
		Common::String savedText;
		bool menuDone;
		ModalState() : activeDi(0), activeEs(0), activeAx(0), activeBx(0),
					   savedDi(0), savedEs(0), savedAx(0), savedBx(0),
					   menuChoiceCount(0), menuMaxChoices(0),
					   paletteMode(0), stashFlag(0),
					   selectedItemIdx(0xffff),
					   textContinuationPtr(0),
					   menuDone(false) {}
	};
	ModalState &modalState() { return _modalState; }
	const ModalState &modalState() const { return _modalState; }

	// FormatBubbleText mirrors DOS FormatBubbleText_FullPath @ 1000:9333.
	// Processes a markup-encoded string, copies the DOS formatted text
	// buffer (including render-control bytes and synthetic row-center
	// records), and returns dimensions.
	//
	// Markup characters (DOS-faithful):
	//   0x00 — terminator (end of input).
	//   0x05 — inline-literal-until-null: copy bytes verbatim until
	//          another 0x00 byte, then absorb 2 trailing bytes (DOS
	//          bookkeeping for the literal block).
	//   0x06 — decimal-number formatter (2-byte global-word offset).
	//   0x07 — color marker + single-byte parameter copied to output.
	//   0x09 — tab/spacing marker + 1-byte X-offset copied to output.
	//   0x0a — conditional skip (2-byte global-byte offset);
	//          if game-state bit is FALSE, skip following block to STX.
	//   0x0b — conditional skip inverse (2-byte param); skip if TRUE.
	//   0x0d — forced row terminator.
	//   0x0c — synthetic row-center marker emitted as
	//          (0x0c, row_width_byte) before each rendered row.
	//   0x02 — conditional-block terminator, consumed but not copied.
	//   0x20 / 0x2d — word-break increment word count.
	//   else — emit char via the char sprite (LookupCharSprite analog).
	//
	// Returns DOS AX/CX/DX equivalents: total pixel height, adjusted
	// max width, and explicit row count.
	struct FormattedBubble {
		Common::String text; // DOS formatted text buffer
		uint16 lineCount;    // logical line / word count
		uint16 rowCount;     // DOS DX return: explicit rendered rows
		uint16 totalHeight;  // pixel height (DOS formula)
		uint16 maxLineWidth; // DOS CX return: widest line minus frame bias
		bool truncated;      // true if buffer overflowed (DOS sets pending error 0x11)
	};
	FormattedBubble formatBubbleText(const byte *src) const;
	FormattedBubble measureVerbBubbleText(const byte *src) const;
	Common::String prepareTextStrippedForRender(const byte *src, bool *truncated) const;
	uint16 bubbleLineHeight() const { return _bubbleLineHeight; }
	void setBubbleLineHeight(uint16 h) { _bubbleLineHeight = h; }
	// Second drag-target slot (DS:0x667e — `g_drag_target_mode40`),
	// distinct from `_dragTarget` (DS:0x667c). Written by Op_76
	// alongside `_g_cursor_mode = 0x40`. Read by Op_0b only.
	uint16 dragTargetMode40() const { return _dragTargetMode40; }
	void setDragTargetMode40(uint16 v) { _dragTargetMode40 = v; }
	// "Implicit actor" — DOS opcodes that take an actor id call
	// GetActorOffset(id) which sets SI to the actor record. Keep the
	// last resolved actor modeled explicitly for opcodes that reuse SI.
	// Op_1e/Op_20 were audited separately: they load
	// g_main_character_id directly, not this implicit actor.
	Actor *implicitActor() const { return _implicitActor; }
	void setImplicitActor(Actor *ac) { _implicitActor = ac; }
	// DOS g_current_hit_region (DS:0x6672): 0 = playfield, 1 = inventory,
	// 2 = status, 3..8 = verb buttons, 0xffff = no current region.
	uint16 hitTarget() const { return _hitTarget; }
	void setHitTarget(uint16 v) { _hitTarget = v; }
	uint16 switchValue() const { return _switchValue; }
	void setSwitchValue(uint16 v) { _switchValue = v; }
	uint16 switchTarget() const { return _switchTarget; }
	void setSwitchTarget(uint16 v) { _switchTarget = v; }
	// `g_branch_state` (DS:0x671c) — script PC saved by Op_2e at the
	// switch dispatcher's loop-head, then jumped to by Op_2f..Op_34
	// when the case mismatches (= "go try the next case"). 0 = no
	// active switch (rule-2-faithful: case ops without an active
	// switch hit pending-error 0x04).
	uint16 branchState() const { return _branchState; }
	void setBranchState(uint16 v) { _branchState = v; }
	// `g_game_score` (DS:0x6670) — running score (read by Op_5c,
	// incremented by Op_ee per claimed score event).
	uint16 gameScore() const { return _gameScore; }
	void setGameScore(uint16 v) { _gameScore = v; }
	void addGameScore(uint16 delta) { _gameScore += delta; }
	// Score-system divisor (CS:[0x91]). Used by Op_5d to compute
	// percent and tenths display. Loaded from iuc_main.dat footer +0x32.
	uint16 maxGameScore() const { return _maxGameScore; }
	void setMaxGameScore(uint16 v) { _maxGameScore = v; }
	// Per-event "claimed" flag (DOS CS:[0x95+i*2+1]). Op_ee marks
	// the event as claimed; the score is incremented only on first
	// claim. Sparse map — absent keys = unclaimed.
	bool isScoreEventClaimed(uint16 eventId) const {
		return _scoreEventClaimed.contains(eventId);
	}
	void markScoreEventClaimed(uint16 eventId) {
		_scoreEventClaimed[eventId] = true;
	}
	void clearScoreEventClaims() { _scoreEventClaimed.clear(); }

	// Current-entity id (DAT_1cb5_666c) — index of the entity whose
	// script is currently running. Set by RunEntityScript before
	// dispatching an entity script. Read by Op_5b. C++ updates this
	// at the dispatch sites that mirror RunEntityScript.
	uint16 currentEntityId() const { return _currentEntityId; }
	void setCurrentEntityId(uint16 id) { _currentEntityId = id; }

	// `g_draw_command_buf` (DS:0x3d93, entries are type/id/layer words)
	// and `g_draw_command_count` (DS:0x661b). Rebuilt from room exits
	// and current-room object records by the renderer, then consumed by
	// DrawAllRoomObjects.
	struct DrawCommand {
		DrawCommand() : type(0), id(0), layer(0) {}
		DrawCommand(uint8 t, uint16 i, int16 l) : type(t), id(i), layer(l) {}
		uint8 type;  // 1 = exit, 2 = object
		uint16 id;   // 1-based id in the corresponding table
		int16 layer; // signed extension of DOS CL in AddDrawCommand
	};
	uint16 drawCommandCount() const { return _drawCommandCount; }
	void clearDrawCommands() {
		_drawCommands.clear();
		_drawCommandCount = 0;
		_visibleNoSpriteExits.clear();
	}
	bool addDrawCommand(uint8 type, uint16 id, int16 layer) {
		if (_drawCommands.size() >= 0x1f) {
			setPendingError(0x28);
			return false;
		}
		_drawCommands.push_back(DrawCommand(type, id, layer));
		_drawCommandCount = uint16(_drawCommands.size());
		return true;
	}
	const Common::Array<DrawCommand> &drawCommands() const { return _drawCommands; }
	bool addVisibleNoSpriteExitLikeDos(uint16 id) {
		if (_visibleNoSpriteExits.size() >= 0x23) {
			setPendingError(0x29);
			return false;
		}
		_visibleNoSpriteExits.push_back(id);
		return true;
	}
	const Common::Array<uint16> &visibleNoSpriteExitsLikeDos() const { return _visibleNoSpriteExits; }

	// Anim-list (DOS DS:0x3f2d..., counter at `g_anim_list_count`,
	// 8-entry cap). Op_e4 appends an entry; Op_e5 clears. Each entry
	// mirrors the seven words written by Op_e4: two direct args plus
	// four derived rectangle/cursor bounds and a 0xffff sentinel.
	struct AnimListEntry {
		uint16 arg3, arg2;
		uint16 x0, y0, x1, y1;
		uint16 sentinel;
	};
	void animListClear() { _animList.clear(); }
	bool animListAppend(uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) {
		if (_animList.size() >= 8)
			return false;
		AnimListEntry e = {
			arg3, arg2,
			uint16(arg0 + 3), uint16(arg1 + 0x9b),
			uint16(arg0 + 9), uint16(arg1 + 0xa1),
			0xffff};
		_animList.push_back(e);
		return true;
	}
	uint16 animListCount() const { return uint16(_animList.size()); }
	const Common::Array<AnimListEntry> &animList() const { return _animList; }

	// Anim-list cursor stash (DOS pbRam000231b2/b4 + g_unknown_6660).
	// Op_e3 sets these as cursor pointers used by DispatchDialogClick
	// when iterating the anim-list. Stored as raw values; the +3/+0x9b
	// offsets are computed at write time to mirror DOS field layout.
	uint16 dialogCursor0() const { return _dialogCursor0; }
	uint16 dialogCursor1() const { return _dialogCursor1; }
	uint16 dialogClickGate() const { return _dialogClickGate; }
	void setDialogCursors(uint16 c0, uint16 c1, uint16 gate) {
		_dialogCursor0 = c0;
		_dialogCursor1 = c1;
		_dialogClickGate = gate;
	}

	// Post-move callback subsystem. Mirrors DOS [DS:0x65ab..] register-
	// state record and RunPostMoveCallback @ 1000:73a6.
	//
	// DOS SetPostMoveCallback (0x7400) saves BP, BX, CX, DX, ES, DI,
	// DS, SI, AX into the slot. RunPostMoveCallback fires the saved
	// callback (CALL [BP]) when the protagonist's current frame
	// matches a saved target frame ([0x6609]); the callback runs an
	// in-engine continuation (e.g., 0x49df, 0x4a36) that updates
	// game state. The callback is one-shot and cleared after firing.
	//
	// In C++ the in-engine continuations are enumerated explicitly —
	// only a small set of code paths register post-move callbacks —
	// so the saved register state collapses to (kind + named args).
	// The fire guards still mirror DOS fields +0x6f/+0x65 and the
	// frame byte mirror at DS:0x6609.
	struct PostMoveCallback {
		enum Kind {
			kNone = 0,
			// DOS trampoline @ 0x49df:
			//   PUSH CX; PUSH BX; CALL DisableObjectFlag1(AX);
			//   POP AX; CALL MovePersonToActor(AX);
			//   POP AX; if (AX != 0) JMP EnableObjectFlag1.
			// = clearCellBit(cellId) + setDragTarget(arg0) +
			//   (arg1 != 0 ? setCellBit(arg1) : nothing).
			// Used by Op_91/Op_92.
			kDisableMoveOptionalEnable = 1,
			// DOS trampoline @ 0x4a36:
			//   PUSH BX; CALL DisableObjectFlag1(AX);
			//   POP BX; CALL EnableObjectFlag1(AX);  [BX is restored
			//     but never moved back to AX, so AX is the value left by
			//     DisableObjectFlag1.]
			//   JMP Op_8e (cursor=1, drag=0).
			// = clearCellBit(cellId) + DOS AX-dependent EnableObjectFlag1 +
			//   setCursorMode(1) + setDragTarget(0). Used by Op_93.
			kDisableEnableUnregister = 2,
			// DOS callback @ 0x4376:
			//   if !status: protag.frame=arg0, protag.nextFrame=arg1,
			//   protag.room=currentRoom=cellId, then SetActorPosition.
			// Used by Op_29/Op_2a after walking to the current entity.
			kPlaceProtagonistAfterMove = 3,
			// DOS callback @ 0xc408 (`PlaceObjectInRoom`), armed by
			// HandleHotspotInteraction @ 0x3353 after QueueExitTransition.
			// C++ stores object id in cellId, adjusted X in arg0, and
			// target-bottom Y in arg1.
			kPlaceObjectAfterHotspotMove = 4,
			// DOS callback @ 0x9be9, armed by Op_3f/0x40 after
			// allocating an inactive protagonist speech slot while the
			// protagonist walks to the current entity.
			kActivateProtagonistSpeechAfterMove = 5,
			// DOS callback @ 0x3297 (`BeginDrag_AfterRemoveExit`),
			// armed by HandleSecondaryClick @ 0x3258 after walking to a
			// room object with cursor mode 1.
			kBeginDragAfterMove = 6,
		};
		Kind kind;
		uint16 cellId; // DOS AX = currentEntityId at register time
		uint16 arg0;   // DOS BX = target person/object id
		uint16 arg1;   // DOS CX = optional second cell id (0 = no enable)
		PostMoveCallback() : kind(kNone), cellId(0), arg0(0), arg1(0) {}
	};
	void setPostMoveCallback(PostMoveCallback::Kind kind, uint16 cellId, uint16 arg0, uint16 arg1) {
		_postMoveCallback.kind = kind;
		_postMoveCallback.cellId = cellId;
		_postMoveCallback.arg0 = arg0;
		_postMoveCallback.arg1 = arg1;
	}
	const PostMoveCallback &postMoveCallback() const { return _postMoveCallback; }
	void setPostMoveCallback(const PostMoveCallback &cb) { _postMoveCallback = cb; }
	bool hasPostMoveCallback() const { return _postMoveCallback.kind != PostMoveCallback::kNone; }
	void clearPostMoveCallback() { _postMoveCallback = PostMoveCallback(); }
	uint8 postMoveTargetFrameMirror() const { return _postMoveTargetFrameMirror; }
	void setPostMoveTargetFrameMirror(uint8 frame) { _postMoveTargetFrameMirror = frame; }
	void runPostMoveCallbackIfReady();
	void resetRoomScriptSlotLikeDos(uint16 mode) { resetQueuedRunMode(mode); }

	// Cutscene-PC state backup (DOS Op_97 @ 1000:4a5d / Op_98 @ 1000:4b40).
	// Single-slot save/restore of the protagonist's walk callback fields,
	// the post-move callback record, active speech, and the one room-script
	// wait slot whose owner is the protagonist and whose type word is zero.
	struct CutsceneBackup {
		bool active;
		// Protagonist DOS fields cleared by Op_97, restored by Op_98:
		//   field+0x69 (word): walk callback target  → SetActorTarget
		//   field+0x62 (byte): target frame byte
		//   field+0x67 (byte): walk-callback status flag (e.g., 5 = armed)
		uint16 actorField69;
		uint8 actorField62;
		uint8 actorField67;
		// [0x6609]: protag's target/current-frame mirror used by
		// RunPostMoveCallback's frame match.
		uint8 targetFrameMirror;
		// [0x65ab..]: post-move callback register record.
		PostMoveCallback savedCallback;
		// Protagonist speech: text + active flag. DOS captures the
		// 17-byte speech-slot record at [0x5f08]; we capture only what
		// our Speech model holds (text), since other slot fields like
		// frames-left/total are derived state.
		Common::String speechText;
		bool hadSpeech;
		// DOS [0x5eeb/0x5eed/0x5eef]: one g_room_script_slots entry
		// with owner == main character and type word 0. In C++ this is
		// the mode-preserving Actor callback queued by Op_99/Op_9a.
		Actor::RoomScriptWaitSnapshot roomScriptWait;
		CutsceneBackup() : active(false), actorField69(0), actorField62(0),
						   actorField67(0), targetFrameMirror(0), hadSpeech(false) {}
	};
	CutsceneBackup &cutsceneBackup() { return _cutsceneBackup; }
	const CutsceneBackup &cutsceneBackup() const { return _cutsceneBackup; }

	// Camera (DOS g_camera_x/y, g_target_x/y at DS:0x67??). Op_d4 sets
	// scroll target (smooth pan); Op_d5 sets camera position
	// instantly. 0xffff target = no active scroll.
	int16 cameraX() const { return _cameraX; }
	int16 cameraY() const { return _cameraY; }
	void setCameraXY(int16 x, int16 y) {
		_cameraX = x;
		_cameraY = y;
	}
	uint16 cameraTargetX() const { return _cameraTargetX; }
	uint16 cameraTargetY() const { return _cameraTargetY; }
	void setCameraTarget(uint16 x, uint16 y) {
		_cameraTargetX = x;
		_cameraTargetY = y;
	}
	bool scrollChanged() const { return _scrollChanged; }
	bool inputEnabled() const { return _inputEnabled; }
	void setInputEnabled(bool e) { _inputEnabled = e; }
	// DOS copies live cursor/buttons to locked globals before dispatch.
	// RetEmpty @ 1000:bb58 reads these locked coordinates, so click-time
	// object placement and movement must not resample the later live cursor.
	void lockCursorAndButtonsLikeDos(Common::Point pos, uint8 buttons) {
		_cursorLockedPos = pos;
		_buttonsLocked = buttons;
	}
	Common::Point lockedCursorPositionLikeDos() const { return _cursorLockedPos; }
	uint8 buttonsLockedLikeDos() const { return _buttonsLocked; }
	void setLoadedBackdropId(uint16 id) { _loadedBackdropId = id; }
	uint16 loadedBackdropId() const { return _loadedBackdropId; }
	void resetMovieGraphicSlotsLikeDos() {
		// AllocBuffersB @ 1000:10f8 clears the six allocated graphic-slot
		// words at 0x676f,0x6771,0x6773,0x6775,0x6777,0x6779.
		_graphicSlots[0] = _graphicSlots[1] = _graphicSlots[2] = 0;
		_graphicSlots[3] = _graphicSlots[4] = _graphicSlots[5] = 0;
	}

	// Graphic-slot tracking (DOS DS:0x676f..0x677b — 7 slots × 2 bytes).
	// Op_cb (LoadGraphicToSlot) writes the current graphic id into one
	// of these slots based on the graphic's type byte. Slot indices:
	//   type 1 → 0x676f (foreground graphic A)
	//   type 2 → 0x6771 (foreground graphic B)
	//   type 3 → 0x6773 (foreground graphic C)
	//   type 4 → 0x6779 (full-screen alt 1)
	//   type 5 → 0x677b (full-screen alt 2)
	//   type 6 → 0x6775 (full-screen)
	//   type 7 → 0x6777 (full-screen palette)
	// 0 = no graphic loaded.
	void setGraphicSlot(uint8 slot, uint16 graphicId) {
		if (slot < 7)
			_graphicSlots[slot] = graphicId;
	}
	uint16 graphicSlot(uint8 slot) const {
		return slot < 7 ? _graphicSlots[slot] : 0;
	}

	// Backdrop-overlay queue (DS:0x37b7..0x37b7+250*6, counter at
	// DS:0x6621 = `g_unknown_6621`). Op_7e queues a (sprite, x, y)
	// triple for the chosen entity; DrawBackdropOverlays draws them
	// after the backdrop on each frame. Reset on room change.
	struct OverlayEntry {
		uint16 sprite;
		int16 x;
		int16 y;
	};
	void overlayQueueClear() { _overlayQueue.clear(); }
	bool overlayQueuePush(uint16 sprite, int16 x, int16 y) {
		// DOS cap = 250 entries; pending-error 0x35 on overflow.
		if (_overlayQueue.size() >= 250)
			return false;
		OverlayEntry e = {sprite, x, y};
		_overlayQueue.push_back(e);
		return true;
	}
	const Common::Array<OverlayEntry> &overlayQueue() const { return _overlayQueue; }

	// `g_opcode_mode` (DS:0x670e) — mode of the currently dispatching
	// script (entity type for entity scripts, slot index 0xb..0x12 for
	// deferred queue slots). Set by RunEntityScript and runQueued
	// before invoking the script. Read by Op_3a/Op_3d for deferred-mode
	// dispatch decisions.
	uint16 opcodeMode() const { return _opcodeMode; }
	void setOpcodeMode(uint16 m) { _opcodeMode = m; }

	// ESC-handler break point (DOS g_break_target_proc/target segment/target
	// offset + g_esc_during_script). Op_3d stores the segment:offset target;
	// C++ keeps that pair as a CodePointer while retaining a legacy debug/save
	// word for the dispatch site.
	uint16 escBreakProc() const { return _escBreakProc; }
	uint16 escBreakSrcPC() const { return _escBreakSrcPC; }
	bool escBreakPending() const { return _escBreakPending; }
	void setEscBreakPoint(uint16 proc, uint16 srcPC, const CodePointer &target) {
		_escBreakProc = proc;
		_escBreakSrcPC = srcPC;
		_skipPoint = target;
		_escBreakPending = false;
	}
	void clearEscBreakPoint() {
		_escBreakProc = 0;
		_escBreakSrcPC = 0;
		_skipPoint.reset();
		_escBreakPending = false;
	}

	// Parser-buffer (DOS Pascal-string @ DS:0x4fa9..0x4faa..[chars]):
	//   [0x4fa9] = max capacity (set at boot)
	//   [0x4faa] = current length (Pascal-style)
	//   [0x4fab+] = chars
	// Op_e9 appends; Op_e7 clears; Op_eb pops last char; Op_22
	// compares against arg0. C++ models as a simple Common::String
	// with a max-capacity bound (mirrors DOS 0x4fa9 byte). The
	// default capacity (165 = 0x4faa - 0x4fab + 0x100? actually max
	// observed in DOS is set at boot; default to 60 chars — same
	// length as DOS savegame name field).
	const Common::String &parserBuffer() const { return _parserBuffer; }
	uint8 parserBufferCapacity() const { return _parserBufferCapacity; }
	void setParserBufferCapacity(uint8 cap) { _parserBufferCapacity = cap; }
	void parserBufferAppend(byte ch) {
		if (_parserBuffer.size() < _parserBufferCapacity)
			_parserBuffer += char(ch);
	}
	void parserBufferClear() { _parserBuffer.clear(); }
	void parserBufferPop() {
		if (!_parserBuffer.empty())
			_parserBuffer.deleteLastChar();
	}

	// `g_pendingErrorCode` (1000:0003) — error code raised by ~95 DOS
	// sites. DOS's MainGameLoop calls DisplayIllError (1000:35cd) each
	// frame: it shows a one-shot "ILL Error <code> (<mode>)" overlay,
	// clears the code, and *continues* (sets g_flag_room_loaded=1) — DOS
	// errors are RECOVERABLE, not fatal. C++ mirrors the one-shot dedup
	// via `_lastErrorCode` and reports-then-continues instead of aborting.
	uint8 pendingError() const { return _pendingError; }
	void setPendingError(uint8 code) { _pendingError = code; }
	void clearPendingError() { _pendingError = 0; }
	// DOS g_lastErrorCode (CS:0x5) one-shot dedup so the same error is
	// reported only once until a different code is raised.
	uint8 lastErrorCode() const { return _lastErrorCode; }
	void setLastErrorCode(uint8 code) { _lastErrorCode = code; }
	// DOS DisplayIllError opcode-mode-name table (g_opcode_mode index).
	const char *opcodeModeName() const {
		switch (_opcodeMode) {
		case 0:
			return "(Initial)";
		case 1:
			return "(New room)";
		case 2:
			return "(Room loop)";
		case 3:
			return "(Game loop)";
		case 4:
			return "(Item)";
		case 5:
			return "(Scan)";
		case 6:
		case 7:
			return "(Status)";
		case 8:
			return "(New block)";
		default:
			return "(Miss/Act)";
		}
	}
	// `g_skip_counter_b` (DS:0x65e? = call-stack depth, max 8 per
	// DOS Op_36/Op_37). C++ uses native recursion for the actual
	// frame management; this counter only enforces the same limit
	// so we can raise pending-error 0x05 on overflow / 0x06 on
	// underflow.
	uint8 callDepth() const { return _callDepth; }
	void setCallDepth(uint8 d) { _callDepth = d; }
	bool slowCpu() const { return _slowCpu; }

	void setEngine(Engine *e);

	void init();
	void initCode();

	// set actor# of the protagonist
	void setProtagonist(uint16);
	Actor *protagonist() const;
	uint16 protagonistId() const { return _protagonistId; }

	void changeRoom(uint16);
	void restartRoomLikeDos();

	Engine *engine() { return _engine; }

	void tick();
	void callAnimations();
	void runGlobalRoomLoop();
	void runRoomLoop();
	void runPostAnimationScripts();

	void addAnimation(Animation *anim);
	void removeAnimation(Animation *anim);
	void setRoomLoop(const CodePointer &code);
	void resetActiveActorTableLikeDos();
	bool registerActiveActorLikeDos(uint16 id);
	void unregisterActiveActorLikeDos(uint16 id);
	bool activeActorLikeDos(uint16 id) const;

	const Common::List<Animation *> animations() const { return _animations; }
	Room *room() const { return _room.get(); }
	uint16 roomNumber() const { return _currentRoom; }
	uint16 currentRoom() const { return _currentRoom; }
	bool roomChangePending() const { return _nextRoom != 0; }
	uint16 currentBlock() const { return _currentBlock; }
	Actor *getActor(uint16 id) const;
	uint16 actorGlobalId(const Actor *actor) const;

	Program *blockProgram() const { return _blockProgram.get(); }
	Interpreter *blockInterpreter() const { return _blockInterpreter.get(); }
	Interpreter *mainInterpreter() const { return _toplevelInterpreter.get(); }
	void runLater(const CodePointer &, uint16 delay = 0);
	void runLaterWithMode(const CodePointer &, uint16 mode, uint16 delay = 0);
	void runLaterWithCurrentMode(const CodePointer &, uint16 delay = 0);
	bool queueDeferred(const CodePointer &p);
	uint16 deferredQueuedCount() const;
	void syncCodePointerLikeDos(Common::Serializer &s, CodePointer &p) const;
	// Remove the first deferred entry whose CodePointer matches `p`.
	// DOS also clears the mode-specific delayed-run entry for that
	// deferred slot via ResetDispatchModeEntry @ 1000:3198.
	// Returns true when the canceled slot has the currently-running
	// deferred mode, mirroring DOS `g_break_loop = 1`.
	bool cancelDeferred(const CodePointer &p);

	bool motionTextActive() const { return _motionTextTicks != 0; }
	void startMotionText(uint16 ticks, const byte *text, uint16 length);
	void tickMotionText();
	void paintMotionText();

	// DOS speech slots (DS:0x4e63, 6 entries * 17 bytes). Both actor
	// bubbles and explicit-position narrator bubbles share this fixed pool.
	enum { kSpeechSlotCount = 6 };
	bool allocActorSpeech(Actor *actor, const Common::String &text, uint16 maxLines = 0);
	bool allocActorSpeechAt(Actor *actor, const Common::String &text, Common::Point pos, uint16 maxLines = 0);
	bool allocActorSpeechForPostMove(Actor *actor, const Common::String &text, uint16 maxLines = 0);
	void activateActorSpeechAfterPostMoveLikeDos(Actor *actor);
	bool allocNarratorSpeech(const byte *text, uint16 length, uint16 x, uint16 y,
							 byte color, uint16 maxLines, uint8 type);
	bool speechSlotActiveForOwner(uint16 owner) const;
	bool anySpeechSlotActive() const;
	bool uiTextSpeechSlotActiveLikeDos() const;
	void stashUiTextSpeechSlotForOwnerLikeDos(uint16 owner);
	const Common::String &speechTextForOwner(uint16 owner) const;
	void clearSpeechForOwner(uint16 owner);
	void setSpeechSkipInput(bool pressed) { _speechSkipInput = pressed; }
	void queueSpeechSlotCallbackForOwner(uint16 owner, const CodePointer &cp);
	void queueSpeechSlotCallbackForAnyActive(const CodePointer &cp);
	void queueUiTextSpeechSlotCallbackLikeDos(const CodePointer &cp);
	bool backupSpeechSlotForOwner(uint16 owner, Common::String &text);
	bool restoreActorSpeechSlot(Actor *actor, const Common::String &text);
	void recycleStaleSpeechSlotsLikeDos();
	void paintSpeechSlots(Graphics *g);
	void paintDirtyObjectPlacementsLikeDos(Graphics *g, int16 layer);

	bool canSkipCutscene() const { return !_skipPoint.isEmpty(); }
	void setSkipPoint(const CodePointer &);
	void requestSkipCutscene();
	bool handleEscDuringScript();
	void resetSpeechSlotsLikeDos();
	void skipCutscene();

	// "Current place" id (DOS CS:[0x111], a savegame state identifier
	// — not the same as the room number). Set by Op_c9 / Op_fb.
	void setCurrentPlace(uint16 p) { _currentPlace = p; }
	uint16 currentPlace() const { return _currentPlace; }

	Animation *animation(uint16 offset) const;

	Music *music() const { return _music; }
	void setMusic(Music *m) { _music = m; }
	Resources *resources() const { return _resources; }
	void setLoadBlockImageOverride(uint16 blockId, const Common::Array<byte> &data);
	void synchronize(Common::Serializer &s);

	// DOS scene-snapshot slot (`_g_block_pc_offset` @ 0x6718 et al.).
	// Op_38 (1000:3c58) saves the current scene; Op_01 (1000:59a3)
	// restores it. DOS uses a single slot, not a stack — see
	// PLAN.md "Cross-cutting subsystems / Scene-snapshot stack".
	// _animations is saved as a list snapshot so the sub-scene's
	// loadActors-appended entries can be unwound on pop. The Program
	// SharedPtr preserves the old _code buffer; Animation::dropBaseIfIn
	// is skipped while a snapshot exists so the saved actors' _base
	// pointers remain valid.
	struct SceneFrame {
		Common::SharedPtr<Program> blockProgram;
		Common::SharedPtr<Interpreter> blockInterpreter;
		uint16 currentBlock;
		uint32 currentRoom;
		Common::SharedPtr<Room> room;
		CodePointer resumePC;
		Common::List<Animation *> savedAnimations;
		Common::Array<uint16> savedActiveActorIds; // DOS active actor table DS:0x25fb
		PostMoveCallback savedPostMoveCallback;    // DOS [0x65ab..] block snapshot
		Common::Array<CastEntry> savedCastTable;   // DOS [0x1977..] cast table snapshot (Op_38 SaveCastBackup)
	};
	bool hasSavedScene() const { return _savedScene; }
	void saveSceneFrame(const CodePointer &resumePC);
	CodePointer switchToSceneLikeDos(uint16 sceneId, const CodePointer &resumePC);
	CodePointer restoreSceneFrame();
	void backupRoomForStatusLikeDos();
	void restoreRoomFromBackupLikeDos();
	void enterStatusScreenLoopLikeDos();
	bool beginStatusSaveSnapshotLikeDos();
	void endStatusSaveSnapshotLikeDos();

	friend class Debugger;

private:
	void doChangeRoom();
	void clearRoomTransientAnimations();
	void refreshCurrentRoomActorFramesLikeDos();
	void registerCurrentRoomActorsLikeDos();
	void centerCameraOnProtagonistLikeDos();
	void queueDirtyObjectPlacementLikeDos(uint16 objId, int16 x, int16 y);
	void flushDirtyObjectPlacementsLikeDos(uint16 room);
	void refreshObjectSpriteAndExitInfoLikeDos(uint16 objId);
	void restartBlockAudioLikeDos();
	void updateScrollPosition();
	bool speechWouldConsumeRightClickLikeDos() const;
	bool hasQueuedRunMode(uint16 mode) const;
	bool dispatchReadyActorRoomScriptWaitMode(uint16 mode);
	void runItemRoomScriptSlotLikeDos();
	void runStatusScreenScriptsLikeDos();
	void resetQueuedRunMode(uint16 mode);
	void cancelDeferredScriptsForInterpreter(Interpreter *interpreter);
	void cancelSpeechSlotCallbacksForInterpreter(Interpreter *interpreter);
	bool redirectDeferredMode(uint16 mode, const CodePointer &target);
	void syncQueuedRunsLikeDos(Common::Serializer &s);
	void runQueued();
	uint16 cellGroupLikeDos() const {
		return _savedScene ? _savedScene->currentBlock : _currentBlock;
	}
	uint32 cellKey(uint16 id) const { return (uint32(cellGroupLikeDos()) << 16) | id; }
	void storeCellByte(uint16 id, uint8 value) {
		const uint32 key = cellKey(id);
		if (value == 1)
			_cellBits.erase(key);
		else
			_cellBits[key] = value;
	}
	struct SpeechSlotCallback {
		SpeechSlotCallback() : mode(0), hasMode(false) {}
		SpeechSlotCallback(const CodePointer &p, uint16 m, bool h)
			: callback(p), mode(m), hasMode(h) {}
		CodePointer callback;
		uint16 mode;
		bool hasMode;
	};
	struct SpeechSlot {
		SpeechSlot()
			: framesLeft(0), framesTotal(0), active(0), type(0), owner(0),
			  refX(0), refY(0), color(0xeb), maxLines(0), pageIndex(0) {}
		uint8 framesLeft;
		uint8 framesTotal;
		uint8 active;
		uint8 type;
		uint16 owner;
		uint16 refX;
		uint16 refY;
		uint8 color;
		uint16 maxLines;
		Common::String text;
		Common::Array<Common::String> pages;
		uint pageIndex;
		Common::Queue<SpeechSlotCallback> callbacks;
	};
	SpeechSlot *findFreeSpeechSlot();
	const SpeechSlot *findSpeechSlotForOwner(uint16 owner) const;
	SpeechSlot *findSpeechSlotForOwner(uint16 owner);
	void clearSpeechSlot(SpeechSlot &slot);
	void startSpeechSlotPage(SpeechSlot &slot, uint page);
	bool initSpeechSlot(SpeechSlot &slot, const Common::String &text, uint16 maxLines);
	void finishSpeechSlot(SpeechSlot &slot);

	Engine *_engine;
	Resources *_resources;
	Common::SharedPtr<Interpreter> _toplevelInterpreter, _blockInterpreter;
	Common::SharedPtr<Program> _sceneProgramKeepAlive;
	Common::SharedPtr<Interpreter> _sceneInterpreterKeepAlive;
	Actor *_protagonist;
	uint16 _protagonistId; // DOS CS:0x010f
	uint32 _nextRoom;
	uint32 _currentRoom;
	uint16 _currentBlock;
	Common::SharedPtr<Program> _blockProgram;
	uint16 _loadBlockOverrideId;
	Common::Array<byte> _loadBlockOverrideData;
	Common::List<Animation *> _animations;
	enum {
		kActiveActorTableSlots = 20
	};
	Common::Array<uint16> _activeActorIds; // DOS DS:0x25fb, 20 slots × 0x2e stride; only wId is modeled.
	Common::SharedPtr<CodePointer> _roomLoop;
	Common::SharedPtr<Room> _room;
	Music *_music;

	struct DelayedRun {
		enum WaitKind {
			kWaitNone,
			kWaitCastEntryInactive
		};
		DelayedRun(const CodePointer &c, uint16 d, uint16 tick, uint16 mode = 0,
				   bool hasMode = false, uint16 deferredSlotMode = 0,
				   WaitKind wait = kWaitNone, uint16 waitValue = 0)
			: code(c), delay(d), queuedTick(tick), runMode(mode), hasRunMode(hasMode),
			  deferredMode(deferredSlotMode), canceled(false), waitKind(wait),
			  waitParam(waitValue) {}
		CodePointer code;
		uint16 delay;
		uint16 queuedTick;
		uint16 runMode;
		bool hasRunMode;
		uint16 deferredMode;
		bool canceled;
		WaitKind waitKind;
		uint16 waitParam;
	};
	Common::List<DelayedRun> _queued;

	struct RoomBackup {
		RoomBackup() : valid(false), currentBlock(0), currentRoom(0), loadedBackdropId(0), cameraX(0), cameraY(0),
					   scrollChanged(false), cursorMode(0), fullscreen(false),
					   roomActive(true), noStep(false), actorFrameCount(0), drawCommandCount(0),
					   postMoveTargetFrameMirror(0), nextRoom(0), forceRoomRestart(false),
					   inStatusMode(false), enteringStatusScreen(false), stepPending(false),
					   logicDirty(false), autoCloseTimer(0) {}
		bool valid;
		uint16 currentBlock;
		uint32 currentRoom;
		uint16 loadedBackdropId;
		Common::SharedPtr<Program> blockProgram;
		Common::SharedPtr<Interpreter> blockInterpreter;
		Common::SharedPtr<Room> room;
		Common::List<Animation *> animations;
		Common::Array<uint16> activeActorIds;
		Common::Array<CastEntry> castTable;
		Common::List<DelayedRun> queued;
		int16 cameraX;
		int16 cameraY;
		bool scrollChanged;
		uint16 cursorMode;
		bool fullscreen;
		bool roomActive;
		bool noStep;
		Common::Array<Zone> zones;
		Common::Array<CollisionZone> collisionZones;
		Common::Array<ZoneB> zonesB;
		Common::Array<Zone> walkboxes;
		Actor::Frame actorFrameZero;
		Common::Array<Actor::Frame> actorFrameTable;
		uint16 actorFrameCount;
		Common::Array<OverlayEntry> overlayQueue;
		Common::Array<AnimListEntry> animList;
		Common::Array<DrawCommand> drawCommands;
		Common::Array<uint16> visibleNoSpriteExits;
		uint16 drawCommandCount;
		PostMoveCallback postMoveCallback;
		uint8 postMoveTargetFrameMirror;
		uint32 nextRoom;
		bool forceRoomRestart;
		bool inStatusMode;
		bool enteringStatusScreen;
		bool stepPending;
		bool logicDirty;
		int16 autoCloseTimer;
	};
	void captureRoomStateForStatusSaveLikeDos(RoomBackup &dst) const;
	void applyRoomStateForStatusSaveLikeDos(const RoomBackup &src);
	RoomBackup _roomBackup;
	RoomBackup _statusSaveShadow;
	bool _statusSaveOverrideActive;

	struct DirtyObjectPlacement {
		DirtyObjectPlacement() : objId(0), currentX(0), currentYMinusHeight(0), targetX(0), targetYMinusHeight(0) {}
		uint16 objId;
		int16 currentX;
		int16 currentYMinusHeight;
		int16 targetX;
		int16 targetYMinusHeight;
	};
	DirtyObjectPlacement _dirtyObjectPlacements[5]; // DS:0x1945, five 10-byte placement slots

	CodePointer _skipPoint;
	uint32 _frameCounter;
	uint16 _gameState;                            // DS:0x666e — current entity type (0 none, 1 exit, 2 object, 3 actor)
	bool _inStatusMode;                           // DS:0x676e — true while the room-999 status screen is active
	bool _fullscreenGateActive;                   // DS:0x673f — set by Op_cc, blocks system-menu hotkeys and restores default cursor on room change
	bool _fullscreenGateInitialized;              // DOS CS:[0x52a3] — Op_cc first-entry guard
	bool _enteringStatusScreen;                   // transient RunStatusScreenLoop room-999 transition
	bool _roomActive;                             // DS:0x6740 — gates room/entity interaction
	bool _logicDirty;                             // DS:0x673c — set by logic-mutating draw/cursor opcodes
	bool _forceRoomRestart;                       // DOS g_flag_restart_room even when the room id is unchanged
	bool _paused;                                 // DS:0x6743 — one-frame pause/repaint gate set by transition helpers
	uint16 _currentPlace;                         // DOS CS:[0x111] — savegame "place" id, set by Op_c9
	bool _stepPending;                            // DS:0x6748 — set by hotspot click, cleared on action
	int16 _autoCloseTimer;                        // DS:0x668a — status button one-shot/persistent overlay timer
	bool _noStep;                                 // DS:0x6747 — true while control is locked (Op_95/Op_96)
	bool _breakInner;                             // DS:0x672f — set by protagonist walk dispatch inside scripts
	Common::Array<Zone> _zones;                   // mirrors g_zone[8], cleared by Op_da
	Common::Array<CollisionZone> _collisionZones; // mirrors g_collision_zone[24], cleared by Op_dc
	Common::Array<ZoneB> _zonesB;                 // mirrors g_zone_b[30], cleared by Op_de
	Common::Array<Zone> _walkboxes;               // mirrors g_walkbox[*], cleared by Op_e2
	Actor::Frame _actorFrameZero;                 // DOS backing record at DS:0x0791, before Op_df appends start
	Common::Array<Actor::Frame> _actorFrameTable; // DOS frame backing table from frame id 1; active count is separate
	uint16 _actorFrameCount;
	Common::HashMap<uint16, uint16> _objectRoom; // sparse object-id → room map
	Common::HashMap<uint16, int16> _objectPosX;
	Common::HashMap<uint16, int16> _objectPosY;
	Common::HashMap<uint32, uint8> _objectFields; // (objId<<8)|fieldOffset → byte, for Op_67 unknown offsets
	Common::HashMap<uint32, uint8> _exitFields;   // (exitId<<8)|fieldOffset → byte, for Op_66 unknown offsets
	Common::Array<uint16> _objectExitList;        // dynamic object exits registered through AddExitToList-style paths
	Common::Array<DrawCommand> _drawCommands;
	Common::Array<uint16> _visibleNoSpriteExits; // DOS DS:0x04fb no-sprite exit side list, count at DS:0x661f
	Common::HashMap<uint32, uint8> _cellBits;    // (room<<16)|entity -> DOS cell byte
	Common::HashMap<uint16, uint8> _actorFlag70; // Actor.field_0x70 (Op_49)
	uint16 _menuStashA, _menuStashB;             // pbRam00023206/8 (Op_4d)
	bool _menuStashConsumed;                     // uRam00023291 stash flag
	uint16 _defaultCursorMode;                   // DS:0x667a — restored by ApplyChangeRoomTransition after Op_cc fullscreen gate
	uint16 _cursorMode;                          // DS:0x6678 — g_cursor_mode: 0x04=walk, 0x20=drag, 0x40/0x80=verb-style
	uint16 _cursorStepIndex;                     // DS:0x6680 — g_drag_step_idx, also used by the software cursor sequence
	uint16 _dragTarget;                          // current drag-source object id
	uint16 _dragTargetMode40;                    // DS:0x667e — written by Op_76, read by Op_0b
	Actor *_implicitActor;                       // SI register's last-resolved actor
	uint16 _hitTarget;                           // DOS g_current_hit_region (DS:0x6672), not entity id
	uint16 _switchValue;                         // last value pushed for case dispatch (sign of active switch)
	uint16 _switchTarget;                        // bytecode offset to jump to on case match
	uint16 _branchState;                         // DS:0x671c — saved PC for switch-loop reentry
	uint8 _pendingError;                         // 1000:0003 — DOS pending-error code (0 = none)
	uint8 _lastErrorCode;                        // CS:0x5 — DOS g_lastErrorCode one-shot dedup
	uint16 _gameScore;                           // DS:0x6670
	uint16 _maxGameScore;                        // CS:[0x91]
	Common::HashMap<uint16, bool> _scoreEventClaimed;
	uint16 _currentEntityId;  // DAT_1cb5_666c
	uint16 _drawCommandCount; // DS:0x661b
	Common::Array<OverlayEntry> _overlayQueue;
	uint16 _graphicSlots[7]; // DS:0x676f..0x677b
	uint8 _walkSpeedFlag;    // DS:0x674d
	int16 _cameraX, _cameraY;
	uint16 _cameraTargetX, _cameraTargetY;
	int16 _scrollDx, _scrollDy; // DS:0x662b/0x662d, persistent UpdateScrollPosition deltas
	bool _scrollChanged;        // DS:0x662f, set by UpdateScrollPosition
	bool _inputEnabled;
	Common::Point _cursorLockedPos; // DOS DS:0x6752/0x6754
	uint8 _buttonsLocked;           // DOS DS:0x665f
	uint8 _rightClickCycleCooldown; // DS:0x674a — CheckDoubleClickReset four-tick right-button cycle guard
	bool _speechSkipInput;          // DOS g_buttons_locked == 2 path in UpdateSpeechBubbles
	uint16 _loadedBackdropId;       // DS:0x666a — written by SetBackdropImage
	Common::Array<AnimListEntry> _animList;
	uint16 _dialogCursor0, _dialogCursor1, _dialogClickGate; // DOS DS:0x6662, 0x6664, 0x6660
	uint8 _postMoveTargetFrameMirror;                        // DOS DS:0x6609
	PostMoveCallback _postMoveCallback;                      // DOS DS:0x65ab register-state slot
	CutsceneBackup _cutsceneBackup;                          // DOS Op_97/Op_98 backup slot
	Common::Array<CastEntry> _castTable;                     // DOS DS:0x1977 cast registry (18 slots)
	Common::Array<SpeechSlot> _speechSlots;                  // DOS DS:0x4e63, 6 entries
	uint16 _uiTextSpeechSlot;                                // DOS DS:0x669a pointer, modeled as a speech-slot index
	ModalState _modalState;                                  // DOS DS:0x66ae..0x66c6 modal regs + 0x6741 stash
	Common::Array<uint16> _menuItemIndices;                  // DOS DS:0x4f1b — Op_54 lookup for selected idx
	uint16 _opcodeMode;                                      // DS:0x670e
	uint16 _escBreakProc;                                    // DS:0x6726 (g_break_target_proc)
	uint16 _escBreakSrcPC;                                   // legacy debug/save word; target segment lives in _skipPoint.interpreter()
	bool _escBreakPending;                                   // ESC latched by fade/video code until HandleEscDuringScript.
	uint16 _bubbleLineHeight;                                // DOS DAT_1000_885e, written by Op_fd and read by FormatBubbleText_Inner.
	Common::String _parserBuffer;
	uint8 _parserBufferCapacity;
	uint8 _callDepth;                  // DOS call-stack depth (max 8)
	const CodePointer *_runningQueued; // currently running queued entry (nullable)
	uint16 _runningQueuedMode;         // DOS deferred mode tag (0x0b..0x12), or 0 for generic queued code
	uint16 _motionTextTicks;           // DOS g_unknown_66d6 countdown used by Op_56 text motion
	Common::Array<byte> _motionText;
	bool _slowCpu;                             // DS:0x67b5 — always false on modern hosts
	Common::SharedPtr<SceneFrame> _savedScene; // null = empty (matches DOS sentinel)
};

#define Log Logic::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_LOGIC_H
