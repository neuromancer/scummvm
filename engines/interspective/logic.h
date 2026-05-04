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

namespace Interspective {
//

class Actor;
class Animation;
class Debugger;
class Engine;
class Music;
class Resources;

class Logic : public Common::Singleton<Logic> {
public:
	Logic()
		: _frameCounter(0),
		  _verbMode(0),
		  _gameState(0),
		  _inMapMode(false),
		  _stepPending(false),
		  _noStep(false),
		  _menuStashA(0),
		  _menuStashB(0),
		  _menuStashConsumed(false),
		  _cursorMode(0),
		  _dragTarget(0),
		  _dragTargetMode40(0),
		  _implicitActor(0),
		  _hitTarget(0),
		  _switchValue(0),
		  _switchTarget(0),
		  _branchState(0),
		  _pendingError(0),
		  _gameScore(0),
		  _maxGameScore(0),
		  _currentEntityId(0),
		  _drawCommandCount(0),
		  _opcodeMode(0),
		  _escBreakProc(0),
		  _escBreakSrcPC(0),
		  _parserBufferCapacity(60),
		  _callDepth(0),
		  _runningQueued(0),
		  _slowCpu(false) {}
	~Logic();

	// Monotonically increasing per-tick counter — wraps at uint16 to mirror the DOS
	// g_tick_counter at DS:0x6666. Used by Op_10 (timer-fire) and Op_ed (set-deadline).
	uint16 frameTicks() const { return uint16(_frameCounter); }

	// VM state (mirrors DS:0x6XXX globals from the binary).
	uint16 verbMode() const { return _verbMode; }
	void setVerbMode(uint16 v) { _verbMode = v; }
	uint16 gameState() const { return _gameState; }
	void setGameState(uint16 s) { _gameState = s; }
	bool inMapMode() const { return _inMapMode; }
	void setInMapMode(bool v) { _inMapMode = v; }
	bool stepPending() const { return _stepPending; }
	void setStepPending(bool v) { _stepPending = v; }
	// DOS g_flag_no_step (DS:0x6747). Set by Op_95 to lock player input during
	// cutscenes. Op_96 clears both _noStep and _stepPending.
	bool noStep() const { return _noStep; }
	void setNoStep(bool v) { _noStep = v; }

	// Per-room geometry tables, mirrors of the DOS g_zone / g_collision_zone /
	// g_zone_b / g_walkbox arrays. Populated by Op_d9 / Op_dd, cleared by
	// Op_da / Op_dc / Op_de / Op_e2. Pathfinding and hotspot dispatch will
	// read these once implemented.
	struct Zone {
		uint16 a, b, c, d;          // 4 raw uint16 args (typically a bbox)
	};
	struct ZoneB {
		uint16 a, b, c, d, var;     // Op_dd writes 5 fields per entry
	};
	void zonesClear() { _zones.clear(); }
	void zonesAdd(const Zone &z) { if (_zones.size() < 8) _zones.push_back(z); }
	const Common::Array<Zone> &zones() const { return _zones; }

	void collisionZonesClear() { _collisionZones.clear(); }
	const Common::Array<Zone> &collisionZones() const { return _collisionZones; }

	void zonesBClear() { _zonesB.clear(); }
	void zonesBAdd(const ZoneB &z) { if (_zonesB.size() < 30) _zonesB.push_back(z); }
	const Common::Array<ZoneB> &zonesB() const { return _zonesB; }

	void walkboxesClear() { _walkboxes.clear(); }
	const Common::Array<Zone> &walkboxes() const { return _walkboxes; }

	// Object-table runtime state. Mirrors the per-object 18-byte record
	// (DOS GetObjectOffset, stride 0x12) field at +0 (room id). Op_7f writes
	// it; Op_18 / Op_1b / Op_21 read it. Engine doesn't load the original
	// table from iuc_main.dat yet — entries default to "present" when absent.
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
	void clearObjectRooms() { _objectRoom.clear(); }
	uint16 objectRoomCount() const { return _objectRoom.size(); }

	// Per-entity cell-bit array. DOS keeps an 8-bit flag byte per entity id
	// (objects + exits share the id space, indexed against g_object_count_max).
	// Bit 0 is the "active in current room" visibility bit; bits 1..7 are
	// per-script flags. Op_7b sets bit 0, Op_7c clears it, Op_15 tests an
	// arbitrary bit. We store sparsely; absent ids read as 0.
	uint8 cellByte(uint16 id) const {
		Common::HashMap<uint16, uint8>::const_iterator it = _cellBits.find(id);
		return it == _cellBits.end() ? 0 : it->_value;
	}
	bool cellBit(uint16 id, uint8 bit) const {
		return ((cellByte(id) >> (bit & 7)) & 1) != 0;
	}
	void setCellBit(uint16 id, uint8 bit) {
		_cellBits[id] = uint8(cellByte(id) | (1 << (bit & 7)));
	}
	void clearCellBit(uint16 id, uint8 bit) {
		uint8 v = uint8(cellByte(id) & ~(1 << (bit & 7)));
		if (v == 0)
			_cellBits.erase(id);
		else
			_cellBits[id] = v;
	}
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
	uint16 menuStashA() const { return _menuStashA; }
	uint16 menuStashB() const { return _menuStashB; }
	bool menuStashConsumed() const { return _menuStashConsumed; }
	void setMenuStashConsumed(bool v) { _menuStashConsumed = v; }
	uint16 cursorMode() const { return _cursorMode; }
	void setCursorMode(uint16 v) { _cursorMode = v; }
	uint16 dragTarget() const { return _dragTarget; }
	void setDragTarget(uint16 v) { _dragTarget = v; }
	// Second drag-target slot (DS:0x667e — `g_drag_target_mode40`),
	// distinct from `_dragTarget` (DS:0x667c). Written by Op_76
	// alongside `_g_cursor_mode = 0x40`. Read by Op_0b only.
	uint16 dragTargetMode40() const { return _dragTargetMode40; }
	void setDragTargetMode40(uint16 v) { _dragTargetMode40 = v; }
	// "Implicit actor" — DOS opcodes that take an actor id call
	// GetActorOffset(id) which sets SI to the actor record. SI is
	// preserved across opcodes by the dispatcher; opcodes that take
	// no explicit actor arg (Op_1e/Op_20) read whatever SI was last
	// pointed at. C++ models this via an explicit `_implicitActor`
	// pointer updated by every opcode that resolves an actor by id.
	Actor *implicitActor() const { return _implicitActor; }
	void setImplicitActor(Actor *ac) { _implicitActor = ac; }
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
	// `g_game_score` (DS:0x6670) — running score (read by Op_5c).
	uint16 gameScore() const { return _gameScore; }
	void setGameScore(uint16 v) { _gameScore = v; }
	// Score-system divisor (CS:[0x91]). Used by Op_5d to compute
	// percent and tenths display.
	uint16 maxGameScore() const { return _maxGameScore; }
	void setMaxGameScore(uint16 v) { _maxGameScore = v; }

	// Current-entity id (DAT_1cb5_666c) — index of the entity whose
	// script is currently running. Set by RunEntityScript before
	// dispatching an entity script. Read by Op_5b. C++ updates this
	// at the dispatch sites that mirror RunEntityScript.
	uint16 currentEntityId() const { return _currentEntityId; }
	void setCurrentEntityId(uint16 id) { _currentEntityId = id; }

	// `g_draw_command_count` (DS:0x661b) — count of pending draw
	// commands queued for this frame. Incremented by AddDrawCommand,
	// reset per frame. Read by Op_58.
	uint16 drawCommandCount() const { return _drawCommandCount; }
	void setDrawCommandCount(uint16 c) { _drawCommandCount = c; }
	void incrementDrawCommandCount() { _drawCommandCount++; }
	void clearDrawCommandCount() { _drawCommandCount = 0; }

	// `g_opcode_mode` (DS:0x670e) — mode of the currently dispatching
	// script (entity type for entity scripts, slot index 0xb..0x12 for
	// deferred queue slots). Set by RunEntityScript and runQueued
	// before invoking the script. Read by Op_3a/Op_3d for deferred-mode
	// dispatch decisions.
	uint16 opcodeMode() const { return _opcodeMode; }
	void setOpcodeMode(uint16 m) { _opcodeMode = m; }

	// ESC-handler break point (DOS g_break_target_proc/di/es +
	// g_esc_during_script). Captures (mode, current PC, target) when
	// Op_3d sets it; used by HandleEscDuringScript to dispatch ESC.
	uint16 escBreakProc() const { return _escBreakProc; }
	uint16 escBreakSrcPC() const { return _escBreakSrcPC; }
	void setEscBreakPoint(uint16 proc, uint16 srcPC, const CodePointer &target) {
		_escBreakProc = proc;
		_escBreakSrcPC = srcPC;
		_skipPoint = target;
	}
	void clearEscBreakPoint() {
		_escBreakProc = 0;
		_escBreakSrcPC = 0;
		_skipPoint.reset();
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
	// sites. DOS's MainGameLoop reads it and halts. C++ equivalent:
	// the interpreter dispatch checks at the end of each opcode and
	// terminates with `error()` when set. 0 = no error.
	uint8 pendingError() const { return _pendingError; }
	void setPendingError(uint8 code) { _pendingError = code; }
	void clearPendingError() { _pendingError = 0; }
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

	void changeRoom(uint16);

	Engine *engine() { return _engine; }

	void tick();
	void callAnimations();

	void addAnimation(Animation *anim);
	void removeAnimation(Animation *anim);
	void setRoomLoop(const CodePointer &code);

	const Common::List<Animation *> animations() const { return _animations; }
	Room *room() const { return _room.get(); }
	uint16 roomNumber() const { return _currentRoom; }
	uint16 currentRoom() const { return _currentRoom; }
	Actor *getActor(uint16 id) const;

	Program *blockProgram() const { return _blockProgram.get(); }
	Interpreter *blockInterpreter() const { return _blockInterpreter.get(); }
	Interpreter *mainInterpreter() const { return _toplevelInterpreter.get(); }
	void runLater(const CodePointer &, uint16 delay = 0);
	uint16 queuedCount() const { return uint16(_queued.size()); }
	// Remove any queued (runLater) entry whose CodePointer matches `p`.
	// Used by Op_3a / Op_3c to cancel a previously-deferred script.
	// Returns true if the canceled entry matched the *currently
	// running* queued script (= DOS `g_break_loop = 1` self-cancel
	// case). Caller (Op_3a / Op_3c) returns kReturn in that case.
	bool cancelLater(const CodePointer &p);

	bool canSkipCutscene() const { return !_skipPoint.isEmpty(); }
	void setSkipPoint(const CodePointer &);
	void skipCutscene();

	// "Current place" id (DOS CS:[0x111], a savegame state identifier
	// — not the same as the room number). Set by Op_c9 / Op_fb.
	void setCurrentPlace(uint16 p) { _currentPlace = p; }
	uint16 currentPlace() const { return _currentPlace; }

	Animation *animation(uint16 offset) const;

	Music *music() const { return _music; }
	void setMusic(Music *m) { _music = m; }
	Resources *resources() const { return _resources; }

	// DOS scene-snapshot slot (`_g_block_pc_offset` @ 0x6718 et al.).
	// Op_38 (1000:3c58) saves the current scene; Op_01 (1000:59a3)
	// restores it. DOS uses a single slot, not a stack — see
	// PLAN.md "Cross-cutting subsystems / Scene-snapshot stack".
	struct SceneFrame {
		Common::SharedPtr<Program> blockProgram;
		Common::SharedPtr<Interpreter> blockInterpreter;
		uint16 currentBlock;
		uint32 currentRoom;
		Common::SharedPtr<Room> room;
		CodePointer resumePC;
	};
	bool hasSavedScene() const { return _savedScene; }
	void saveSceneFrame(const CodePointer &resumePC);
	bool restoreSceneFrame();

	friend class Debugger;
private:

	void doChangeRoom();
	void runQueued();


	Engine *_engine;
	Resources *_resources;
	Common::SharedPtr<Interpreter> _toplevelInterpreter, _blockInterpreter;
	Actor *_protagonist;
	uint32 _nextRoom;
	uint32 _currentRoom;
	uint16 _currentBlock;
	Common::SharedPtr<Program> _blockProgram;
	Common::List<Animation *> _animations;
	Common::SharedPtr<CodePointer> _roomLoop;
	Common::SharedPtr<Room> _room;
	Music *_music;

	struct DelayedRun {
		DelayedRun(const CodePointer &c, uint16 d) : code(c), delay(d) {}
		CodePointer code;
		uint16 delay;
	};
	Common::List<DelayedRun> _queued;

	CodePointer _skipPoint;
	uint32 _frameCounter;
	uint16 _verbMode;       // DS:0x6678 — current verb (LOOK/USE/etc), 0x80 = system
	uint16 _gameState;      // DS:0x666e — 0=fresh, 1=running, 2=in dialog
	bool _inMapMode;        // DS:0x676e — true while world map is shown
	uint16 _currentPlace;   // DOS CS:[0x111] — savegame "place" id, set by Op_c9
	bool _stepPending;      // DS:0x6748 — set by hotspot click, cleared on action
	bool _noStep;           // DS:0x6747 — true while control is locked (Op_95/Op_96)
	Common::Array<Zone> _zones;            // mirrors g_zone[8], cleared by Op_da
	Common::Array<Zone> _collisionZones;   // mirrors g_collision_zone[*], cleared by Op_dc
	Common::Array<ZoneB> _zonesB;          // mirrors g_zone_b[30], cleared by Op_de
	Common::Array<Zone> _walkboxes;        // mirrors g_walkbox[*], cleared by Op_e2
	Common::HashMap<uint16, uint16> _objectRoom; // sparse object-id → room map
	Common::HashMap<uint16, int16> _objectPosX;
	Common::HashMap<uint16, int16> _objectPosY;
	Common::HashMap<uint16, uint8> _cellBits;    // per-entity flag byte (Op_7b/7c/Op_15)
	Common::HashMap<uint16, uint8> _actorFlag70; // Actor.field_0x70 (Op_49)
	uint16 _menuStashA, _menuStashB;             // pbRam00023206/8 (Op_4d)
	bool _menuStashConsumed;                     // uRam00023291 stash flag
	uint16 _cursorMode;     // DS:0x665d — 0x04=walk, 0x20=drag, 0x40/0x80=verb-style
	uint16 _dragTarget;     // current drag-source object id
	uint16 _dragTargetMode40; // DS:0x667e — written by Op_76, read by Op_0b
	Actor *_implicitActor;  // SI register's last-resolved actor (Op_1e/Op_20)
	uint16 _hitTarget;      // last-hit hotspot id (mirrors g_current_hit_region)
	uint16 _switchValue;    // last value pushed for case dispatch (sign of active switch)
	uint16 _switchTarget;   // bytecode offset to jump to on case match
	uint16 _branchState;    // DS:0x671c — saved PC for switch-loop reentry
	uint8 _pendingError;    // 1000:0003 — DOS pending-error code (0 = none)
	uint16 _gameScore;      // DS:0x6670
	uint16 _maxGameScore;   // CS:[0x91]
	uint16 _currentEntityId; // DAT_1cb5_666c
	uint16 _drawCommandCount; // DS:0x661b
	uint16 _opcodeMode;       // DS:0x670e
	uint16 _escBreakProc;     // DS:0x6726 (g_break_target_proc)
	uint16 _escBreakSrcPC;    // DS:0x6728 (g_break_target_di)
	Common::String _parserBuffer;
	uint8 _parserBufferCapacity;
	uint8 _callDepth;       // DOS call-stack depth (max 8)
	const CodePointer *_runningQueued; // currently running queued entry (nullable)
	bool _slowCpu;          // DS:0x67b5 — always false on modern hosts
	Common::SharedPtr<SceneFrame> _savedScene; // null = empty (matches DOS sentinel)
};

#define Log Logic::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_LOGIC_H
