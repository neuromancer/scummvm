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
		  _hitTarget(0),
		  _switchValue(0),
		  _switchTarget(0),
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
	uint16 hitTarget() const { return _hitTarget; }
	void setHitTarget(uint16 v) { _hitTarget = v; }
	uint16 switchValue() const { return _switchValue; }
	void setSwitchValue(uint16 v) { _switchValue = v; }
	uint16 switchTarget() const { return _switchTarget; }
	void setSwitchTarget(uint16 v) { _switchTarget = v; }
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
	// Remove any queued (runLater) entry whose CodePointer matches `p`.
	// Used by Op_3a / Op_3c to cancel a previously-deferred script.
	bool cancelLater(const CodePointer &p);

	bool canSkipCutscene() const { return !_skipPoint.isEmpty(); }
	void setSkipPoint(const CodePointer &);
	void skipCutscene();

	Animation *animation(uint16 offset) const;

	Music *music() const { return _music; }
	void setMusic(Music *m) { _music = m; }

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
	bool _stepPending;      // DS:0x6748 — set by hotspot click, cleared on action
	bool _noStep;           // DS:0x6747 — true while control is locked (Op_95/Op_96)
	Common::Array<Zone> _zones;            // mirrors g_zone[8], cleared by Op_da
	Common::Array<Zone> _collisionZones;   // mirrors g_collision_zone[*], cleared by Op_dc
	Common::Array<ZoneB> _zonesB;          // mirrors g_zone_b[30], cleared by Op_de
	Common::Array<Zone> _walkboxes;        // mirrors g_walkbox[*], cleared by Op_e2
	Common::HashMap<uint16, uint16> _objectRoom; // sparse object-id → room map
	Common::HashMap<uint16, uint8> _cellBits;    // per-entity flag byte (Op_7b/7c/Op_15)
	Common::HashMap<uint16, uint8> _actorFlag70; // Actor.field_0x70 (Op_49)
	uint16 _menuStashA, _menuStashB;             // pbRam00023206/8 (Op_4d)
	bool _menuStashConsumed;                     // uRam00023291 stash flag
	uint16 _cursorMode;     // DS:0x665d — 0x04=walk, 0x20=drag, 0x40/0x80=verb-style
	uint16 _dragTarget;     // current drag-source object id
	uint16 _hitTarget;      // last-hit hotspot id (mirrors g_current_hit_region)
	uint16 _switchValue;    // last value pushed for case dispatch (sign of active switch)
	uint16 _switchTarget;   // bytecode offset to jump to on case match
	bool _slowCpu;          // DS:0x67b5 — always false on modern hosts
};

#define Log Logic::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_LOGIC_H
