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

#ifndef NEUROMANCER_BIH_SCRIPT_H
#define NEUROMANCER_BIH_SCRIPT_H

#include "common/scummsys.h"

namespace Neuromancer {

class NeuromancerEngine;
class NeuroVM;

// Narrow bytecode interpreter for BIH-compiled per-level scripts.
//
// Each BIH file has a native-code region alongside its high-level
// bytecode; the high-level VM's opcode 0x16 invokes subroutines inside
// that region to perform effects the high-level opcodes can't (cash
// transfers, inventory tweaks, picking NPC sprite frames, etc.).
//
// The encoding is compact (~35 instruction types) and opaque to the
// engine: we just decode, dispatch, and exit at the end of the routine.
// The entire instruction set is strictly a subset of x86 real-mode --
// that's what the DOS build tools emitted -- but the interpreter is
// specialised to this one data format and does not attempt general
// x86 compatibility.
//
// Memory model: a flat 64 KB data segment shared with NeuroVM (via
// NeuroVM::vars()) which holds game state, the stack, and the loaded
// BIH bytes themselves (at NeuroVM::kBihBase). Instruction fetches and
// data loads both go through that one buffer.
class BihScript {
public:
	explicit BihScript(NeuromancerEngine *engine);

	// Run the subroutine starting at DSEG address `pc`. Returns the
	// number of instructions executed, or -1 on decode error. Stops
	// when the subroutine halts (RET-FAR in the DOS encoding, our
	// "end of routine" sentinel) or when a safety instruction-count
	// limit is reached.
	int run(uint16 pc);

	// Convenience: run the subroutine at `bihOffset` inside the currently
	// attached BIH, i.e. `NeuroVM::kBihBase + bihOffset`.
	int runBihOffset(uint16 bihOffset);

private:
	NeuromancerEngine *_engine;

	// Eight 16-bit scratch slots. The bytecode addresses byte halves
	// (low 0..3, high 4..7) using 8086-style register encoding; we
	// keep 16 bytes so both widths share the same storage without
	// endian fiddling. Slot 4 is the in-script stack pointer (SP).
	uint8 _r[16];

	// Program counter -- a DSEG address into NeuroVM::vars().
	uint16 _pc;

	// Comparison-result latch: last ALU op's carry / parity / aux /
	// zero / sign / overflow results, used by conditional-branch
	// opcodes. Encoded in the same bit positions as the x86 FLAGS
	// register so the single-byte opcode-70 conditional test table
	// stays a direct 4-bit lookup.
	uint16 _flags;
	enum {
		kFlagCarry    = 0x0001,
		kFlagParity   = 0x0004,
		kFlagAux      = 0x0010,
		kFlagZero     = 0x0040,
		kFlagSign     = 0x0080,
		kFlagOverflow = 0x0800
	};

	bool _halted;

	// Sanity-check bounds captured on attach: the BIH's DSEG footprint
	// (for PC validation) and the writable stack range (for overflow /
	// underflow detection on push/pop).
	uint16 _bihLo;
	uint16 _bihHi;
	uint16 _stackLo;
	uint16 _stackHi;

	// --- decode helpers ---
	uint8  fetch8();
	uint16 fetch16();
	struct OpAddress { uint8 mod, reg, rm; };
	OpAddress fetchOperand();
	uint16    resolveMemAddr(const OpAddress &oa);

	// --- register file access (index 0..7) ---
	uint16 readR16(uint8 idx) const;
	void   writeR16(uint8 idx, uint16 val);
	uint8  readR8(uint8 idx) const;
	void   writeR8(uint8 idx, uint8 val);

	// --- data memory (via NeuroVM DSEG) ---
	uint8  readMem8(uint16 addr) const;
	void   writeMem8(uint16 addr, uint8 val);
	uint16 readMem16(uint16 addr) const;
	void   writeMem16(uint16 addr, uint16 val);

	// --- stack ---
	void   pushW(uint16 val);
	uint16 popW();

	// --- ALU primitives; set flags as side-effect ---
	uint16 alu16(uint8 sub, uint16 dst, uint16 src);
	uint8  alu8 (uint8 sub, uint8  dst, uint8  src);
	void   setLogicFlags16(uint16 res);
	void   setLogicFlags8(uint8 res);
	void   setAddFlags16(uint16 dst, uint16 src, uint32 res);
	void   setAddFlags8(uint8 dst, uint8 src, uint16 res);
	void   setSubFlags16(uint16 dst, uint16 src, uint32 res);
	void   setSubFlags8(uint8 dst, uint8 src, uint16 res);

	// --- callback escape ---
	// The bytecode invokes engine services through a single "far-call
	// indirect" encoding that dispatches based on the first stack word
	// (cmd code) written by the caller before the call. Mirrors the
	// neuro_cb() dispatcher in scene_real_world.c.
	void dispatchCallback();

	// --- top-level dispatcher ---
	bool step();
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_BIH_SCRIPT_H
