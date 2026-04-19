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
	};

	// Run opcodes until a thread yields or all are idle.
	TickResult tick();

	// Clear the pending action so the next tick can resume.
	void resume();

	const Bih &bih() const { return _bih; }

private:
	bool step(int slot);

	NeuromancerEngine *_engine;
	Bih _bih;

	VmThread _threads[4];
	int _currentThread;

	// Game-state byte array (x4bae[] in the DOS build). The original struct
	// is ~24 KB; fields are addressed by absolute byte offset inside that
	// region. Opcodes 0x05-0x08 read single bytes; 0x0E/0x0F/0x15 read/write
	// 16-bit LE pairs starting at the given byte index.
	static const uint32 kVarsSize = 0x4000; // 16 KB -- covers all known
	                                        // offsets used by the shipped
	                                        // BIH scripts up to the PAX area.
	uint8 _vars[kVarsSize];
	int   _updateHold;

	Action _pendingAction;
	uint16 _pendingString;
	uint8  _pendingLevel;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_NEURO_VM_H
