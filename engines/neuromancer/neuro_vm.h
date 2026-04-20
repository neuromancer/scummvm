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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#ifndef NEUROMANCER_NEURO_VM_H
#define NEUROMANCER_NEURO_VM_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/serializer.h"

namespace Neuromancer {

class NeuromancerEngine;

// BIH header layout. Mirrors bih_hdr_t in Reuromancer/NeuromancerWin64/data.h.
struct BihHeader {
	uint16 cbOffset;                 // 0x00
	uint16 cbSegment;                // 0x02
	uint16 ctrlStructAddr;           // 0x04
	uint16 textOffset;               // 0x06 -- start of the string table
	uint16 bytecodeArrayOffset[3];   // 0x08..0x0C -- 3 program-table offsets
	uint16 initObjCodeOffset[3];     // 0x0E..0x12 -- native init/update/deinit
	uint16 unknown[10];              // 0x14..0x27
};

// Non-owning view over a decompressed BIH buffer. Provides header access,
// null-terminated string lookup, and program-address lookup.
class Bih {
public:
	Bih() : _bytes(nullptr), _size(0) {}

	void attach(const byte *bytes, uint32 size);

	const BihHeader &header() const { return _hdr; }
	const byte *bytes() const { return _bytes; }
	uint32 size() const { return _size; }

	// Returns the null-terminated string at text-section index `n`, or ""
	// if the index walks past the end of the buffer.
	const char *textString(uint16 n) const;

	// BIH-relative offset of program `progIdx` inside table `tableIdx`
	// (0..2). Each table entry is a 16-bit little-endian offset.
	uint16 programAddress(uint8 tableIdx, uint8 progIdx) const;

	// Scan the capability list that sits right after the BIH header
	// (DOS walks bih.bytes + sizeof(bih_hdr_t) in scene_real_world.c:
	// is_pax_on_level and is_jack_on_level). Each byte is a capability
	// code; the list is terminated by 0. Returns true if `code` appears
	// before the terminator. `1` = PAX terminal, `2` = jack point.
	bool hasCapability(uint8 code) const;

private:
	const byte *_bytes;
	uint32 _size;
	BihHeader _hdr;
};

// One VM thread.
struct VmThread {
	bool   active;
	uint16 nextOpAddr;  // BIH-relative offset of the next opcode
	uint16 var1;
	uint16 var2;
	uint8  flag;        // low 2 bits select the program table
};

// Bytecode interpreter from scene_real_world.c:neuro_vm().
//
// Dispatch is cooperative: tick() runs opcodes round-robin (slot 3 down to
// 0) until a blocking opcode yields. Blocking opcodes are text output,
// dialog, and level change; the caller inspects TickResult and calls
// resume() once the user has acknowledged.
class NeuroVM {
public:
	explicit NeuroVM(NeuromancerEngine *engine);

	// Attach a BIH byte buffer (must outlive the VM or be replaced).
	// Clears all thread state.
	void attach(const byte *bihData, uint32 size);

	// Activate thread `slot` at the default program of table `flagTable`.
	// Equivalent to the engine issuing opcode 0x00 once on entry.
	void startDefaultThread(int slot, uint8 flagTable);

	void resetThreads();

	enum class Action {
		kIdle,
		kTextOutput,
		kDialogReply,
		kEnterDialog,
		kChangeLevel
	};

	struct TickResult {
		Action action;
		uint16 stringNum;
		uint8  levelN;
		// Thread-local variables carried by the yielding opcode; used by
		// dialog bubbles (op 0x01 / 0x18) for positioning near the speaker.
		uint16 var1;
		uint16 var2;
	};

	// Run opcodes until a thread yields or all are idle.
	TickResult tick();

	// Clear the pending action so the next tick can resume.
	void resume();

	// Save / load hook. Persists the 64 KB DSEG, thread slots, pending
	// action, and per-level dialog control. The caller must re-attach
	// the BIH (via attach()) BEFORE invoking this on load, so the VM
	// knows its bytecode layout; syncGame only touches mutable state.
	void syncGame(Common::Serializer &s);

	const Bih &bih() const { return _bih; }

	// DSEG-absolute data memory accessors. `addr` is a 16-bit DOS data
	// segment address, 0x0000..0xFFFF -- always in range of the 64 KB
	// _vars buffer. Shared by the high-level VM (which translates
	// x4bae-relative operands by adding kX4baeBase) and the BIH script
	// interpreter (which fetches & writes memory at the DSEG addresses
	// its instructions encode directly).
	uint8  readVar8(uint16 addr) const {
		assert((uint32)addr < _vars.size());
		return _vars[addr];
	}
	void   writeVar8(uint16 addr, uint8 val) {
		assert((uint32)addr < _vars.size());
		_vars[addr] = val;
	}
	uint16 readVar16(uint16 addr) const {
		return (uint16)readVar8(addr) | ((uint16)readVar8((uint16)(addr + 1)) << 8);
	}
	void   writeVar16(uint16 addr, uint16 val) {
		writeVar8(addr,               (uint8)(val & 0xFF));
		writeVar8((uint16)(addr + 1), (uint8)(val >> 8));
	}

	// Raw pointer to the DSEG buffer for save/load. Prefer read/write
	// helpers above for interactive access.
	uint8       *vars()       { return _vars.data(); }
	const uint8 *vars() const { return _vars.data(); }

	// Per-level dialog control (seeded by opcode 0x13).
	uint8 dialogFirstReply(uint8 level) const {
		return level < 64 ? _levelDialog[level].firstReply : 0;
	}
	uint8 dialogTotalReplies(uint8 level) const {
		return level < 64 ? _levelDialog[level].totalReplies : 0;
	}

	// DSEG-absolute address of active_dialog_reply in g_4bae. Note that
	// *nothing in our high-level VM actually uses this* -- the DOS BIH
	// scripts encode cond-jump operands using the low byte of the DSEG
	// address (0xBE = 190), but the 5-op-encoding only has an 8-bit
	// index byte, so for our purposes it's the same 0x10 offset within
	// x4bae_t. We keep both offsets here for BihScript and scene code.
	static const uint16 kVarActiveDialogReply = 0x4BBE;
	static const uint16 kVarDialogWaitFlag    = 0x4BAE;

	// Legacy byte-index aliases for the high-level VM's cond-jump
	// operand (which is only 8 bits wide). These are the low bytes of
	// the DSEG-absolute addresses above, i.e. the offsets the BIH
	// scripts use in their encoded immediates.
	static const uint8  kVarIdxActiveDialogReply = 0x10;
	static const uint8  kVarIdxDialogWaitFlag    = 0x00;

private:
	bool step(int slot);

	NeuromancerEngine *_engine;
	Bih _bih;

	VmThread _threads[4];
	int _currentThread;

	// DOS-compatible data memory (64 KB). Holds game state (x4bae,
	// x3f85, the other structs), the stack used by native BIH scripts
	// starting at 0xCC10, and the currently-loaded BIH at 0xA8E8. All
	// offsets are absolute within this region -- matching the DOS data
	// segment layout so the 8086-encoded BIH handlers read and write
	// the same addresses they always did.
	//
	// Backed by Common::Array so size() is always authoritative and so
	// future save/load can serialize with .data(), .size().
	static const uint32 kVarsSize = 0x10000; // 64 KB (full DOS DSEG)
	Common::Array<uint8> _vars;
	int   _updateHold;

public:
	// DSEG address of the first byte of the x4bae_t struct (where the
	// high-level VM's "var[idx]" operands index). Public so BihScript
	// and save/load can translate between x4bae-relative and DSEG-absolute.
	static const uint16 kX4baeBase = 0x4BAE;

	// BIH is loaded at this absolute DSEG offset (= DOS g_a8e0.bih).
	// BihScript uses this to fetch instructions and resolve op 0x16
	// targets (which are `bihOffset + kBihBase` in the DOS memory map).
	static const uint16 kBihBase = 0xA8E8;

private:

	Action _pendingAction;
	uint16 _pendingString;
	uint8  _pendingLevel;
	uint16 _pendingVar1;
	uint16 _pendingVar2;

	// Per-level dialog control set by opcode 0x13. Up to 64 levels; for each,
	// the first_dialog_reply and total_dialog_replies values that the dialog
	// scene will use when the player enters dialog mode (opcode 0x17).
	struct LevelDialogCtrl { uint8 firstReply; uint8 totalReplies; };
	LevelDialogCtrl _levelDialog[64];
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_NEURO_VM_H
