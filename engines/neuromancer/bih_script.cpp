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

#include "neuromancer/bih_script.h"

#include "neuromancer/detection.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/neuro_vm.h"

#include "common/debug.h"
#include "common/textconsole.h"

namespace Neuromancer {

// Slot indices inside _r[]. Encoding is fixed by the DOS-era assembler:
// 0=A, 1=C, 2=D, 3=B, 4=SP, 5=BP, 6=SI, 7=DI for words; byte accesses
// address low halves 0..3 and high halves 4..7 of those word slots.
enum { kSlotA = 0, kSlotC = 1, kSlotD = 2, kSlotB = 3, kSlotSP = 4,
       kSlotBP = 5, kSlotSI = 6, kSlotDI = 7 };

// Initial stack pointer. Matches DOS STACK_OFFT + STACK_SIZE.
static const uint16 kStackTop = 0xCC10 + 0x800;

// Safety limit: largest routine in the shipped BIHs is <100 instructions.
// 65536 gives generous headroom in case a routine has a large loop.
static const int kInstructionBudget = 65536;

// Even-parity table for the PF flag (bit set if popcount is even).
static const byte kParityTable[256] = {
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
};


// ---------------------------------------------------------------------------
// Construction / entry points
// ---------------------------------------------------------------------------

BihScript::BihScript(NeuromancerEngine *engine)
	: _engine(engine), _pc(0), _flags(0), _halted(false) {
	memset(_r, 0, sizeof(_r));
}

int BihScript::runBihOffset(uint16 bihOffset) {
	return run((uint16)(NeuroVM::kBihBase + bihOffset));
}

int BihScript::run(uint16 pc) {
	_pc = pc;
	_flags  = 0x0202;
	_halted = false;
	writeR16(kSlotSP, kStackTop);

	// Sanity-check bounds. BIH footprint runs from kBihBase to the end
	// of the attached BIH; stack grows downward from kStackTop through
	// kStackTop - 0x800. Any PC or stack pointer outside these ranges
	// indicates a corrupt script and will abort execution with a log.
	uint32 bihSize = _engine->vm()->bih().size();
	_bihLo   = NeuroVM::kBihBase;
	_bihHi   = (uint16)(NeuroVM::kBihBase + bihSize);
	_stackLo = (uint16)(kStackTop - 0x800);
	_stackHi = kStackTop;

	if (pc < _bihLo || pc >= _bihHi) {
		warning("BihScript: entry PC 0x%04X outside BIH [0x%04X..0x%04X)",
		        pc, _bihLo, _bihHi);
		return -1;
	}

	debugC(1, kDebugScript, "BihScript: run @ DSEG 0x%04X (bih+0x%04X)",
	       pc, (uint16)(pc - NeuroVM::kBihBase));

	int count = 0;
	while (!_halted && count < kInstructionBudget) {
		// Per-step PC sanity. The BIH's instruction stream is always
		// inside the loaded BIH byte range; a PC anywhere else means
		// a broken RET, runaway jump, or corrupt encoding.
		if (_pc < _bihLo || _pc >= _bihHi) {
			warning("BihScript: PC 0x%04X escaped BIH [0x%04X..0x%04X), aborting",
			        _pc, _bihLo, _bihHi);
			return -1;
		}
		if (!step())
			return -1;
		count++;
	}
	if (count >= kInstructionBudget)
		warning("BihScript: instruction budget exhausted at PC 0x%04X", _pc);
	debugC(2, kDebugScript, "BihScript: halted after %d instructions", count);
	return count;
}


// ---------------------------------------------------------------------------
// Fetch / operand decode
// ---------------------------------------------------------------------------

uint8 BihScript::fetch8() {
	uint8 b = _engine->vm()->readVar8(_pc);
	_pc = (uint16)(_pc + 1);
	return b;
}

uint16 BihScript::fetch16() {
	uint16 lo = fetch8();
	uint16 hi = fetch8();
	return lo | (hi << 8);
}

BihScript::OpAddress BihScript::fetchOperand() {
	uint8 b = fetch8();
	OpAddress oa;
	oa.mod = (b & 0xC0) >> 6;
	oa.reg = (b & 0x38) >> 3;
	oa.rm  = b & 0x07;
	return oa;
}

// Operand-addressing decode: resolves memory-mode forms only (mod != 3).
// Implements the classic "effective address" table with two variants:
// direct 16-bit immediate (mod=0 rm=6) and register-base + optional 8/16
// displacement. The four register-base pairs are mapped here from the
// encoding bits.
uint16 BihScript::resolveMemAddr(const OpAddress &oa) {
	uint16 addr;
	if (oa.rm >> 1 == 3) {
		// rm = 6 (direct or [BP]+disp) or rm = 7 ([BX]+disp)
		if (oa.rm & 1)
			addr = readR16(kSlotB);
		else
			addr = oa.mod ? readR16(kSlotBP) : fetch16();
	} else {
		addr = readR16(kSlotSI + (oa.rm & 1));
		if (!(oa.rm & 4))
			addr = (uint16)(addr + readR16(kSlotB + (oa.rm & 2)));
	}
	switch (oa.mod) {
	case 1: addr = (uint16)(addr + (int8)fetch8()); break;
	case 2: addr = (uint16)(addr + fetch16()); break;
	default: break;
	}
	return addr;
}


// ---------------------------------------------------------------------------
// Register / memory access
// ---------------------------------------------------------------------------

uint16 BihScript::readR16(uint8 idx) const {
	idx = (uint8)((idx & 7) << 1);           // defensive: mask to 0..7
	return (uint16)_r[idx] | ((uint16)_r[idx + 1] << 8);
}
void BihScript::writeR16(uint8 idx, uint16 v) {
	idx = (uint8)((idx & 7) << 1);
	_r[idx]     = (uint8)(v & 0xFF);
	_r[idx + 1] = (uint8)(v >> 8);
}
uint8 BihScript::readR8(uint8 idx) const {
	idx &= 7;
	return _r[((idx & 3) << 1) | ((idx & 4) >> 2)];
}
void BihScript::writeR8(uint8 idx, uint8 v) {
	idx &= 7;
	_r[((idx & 3) << 1) | ((idx & 4) >> 2)] = v;
}

uint8  BihScript::readMem8(uint16 addr) const { return _engine->vm()->readVar8(addr); }
void   BihScript::writeMem8(uint16 addr, uint8 v) { _engine->vm()->writeVar8(addr, v); }
uint16 BihScript::readMem16(uint16 addr) const { return _engine->vm()->readVar16(addr); }
void   BihScript::writeMem16(uint16 addr, uint16 v) { _engine->vm()->writeVar16(addr, v); }


// ---------------------------------------------------------------------------
// Stack
// ---------------------------------------------------------------------------

void BihScript::pushW(uint16 val) {
	uint16 sp = (uint16)(readR16(kSlotSP) - 2);
	if (sp < _stackLo) {
		warning("BihScript: stack overflow (SP=0x%04X below 0x%04X) at PC 0x%04X",
		        sp, _stackLo, _pc);
		_halted = true;
		return;
	}
	writeR16(kSlotSP, sp);
	writeMem16(sp, val);
}

uint16 BihScript::popW() {
	uint16 sp = readR16(kSlotSP);
	if (sp >= _stackHi) {
		warning("BihScript: stack underflow (SP=0x%04X past 0x%04X) at PC 0x%04X",
		        sp, _stackHi, _pc);
		_halted = true;
		return 0;
	}
	uint16 val = readMem16(sp);
	writeR16(kSlotSP, (uint16)(sp + 2));
	return val;
}


// ---------------------------------------------------------------------------
// ALU helpers -- shared across ADD/OR/ADC/SBB/AND/SUB/XOR/CMP
// ---------------------------------------------------------------------------

// Sub-operation identifier: 0=ADD, 1=OR, 2=ADC, 3=SBB, 4=AND, 5=SUB, 6=XOR, 7=CMP
// (CMP is SUB without writeback). Callers mask to 3 bits; we mask again
// here as defence in depth and halt on any wider value.
uint16 BihScript::alu16(uint8 sub, uint16 dst, uint16 src) {
	sub &= 7;
	uint32 res = 0;
	switch (sub) {
	case 0: // ADD
		res = (uint32)dst + src; setAddFlags16(dst, src, res); break;
	case 1: // OR
		res = dst | src; setLogicFlags16((uint16)res); break;
	case 2: // ADC
		res = (uint32)dst + src + ((_flags & kFlagCarry) ? 1 : 0);
		setAddFlags16(dst, src, res); break;
	case 3: // SBB
		res = (uint32)dst - src - ((_flags & kFlagCarry) ? 1 : 0);
		setSubFlags16(dst, src, res); break;
	case 4: // AND
		res = dst & src; setLogicFlags16((uint16)res); break;
	case 5: // SUB
		res = (uint32)dst - src; setSubFlags16(dst, src, res); break;
	case 6: // XOR
		res = dst ^ src; setLogicFlags16((uint16)res); break;
	case 7: // CMP
		res = (uint32)dst - src; setSubFlags16(dst, src, res);
		return dst; // no writeback
	default:
		warning("BihScript: alu16 invalid sub=%u at PC 0x%04X", sub, _pc);
		_halted = true; return dst;
	}
	return (uint16)res;
}

uint8 BihScript::alu8(uint8 sub, uint8 dst, uint8 src) {
	sub &= 7;
	uint16 res = 0;
	switch (sub) {
	case 0: res = (uint16)dst + src; setAddFlags8(dst, src, res); break;
	case 1: res = dst | src;         setLogicFlags8((uint8)res); break;
	case 2: res = (uint16)dst + src + ((_flags & kFlagCarry) ? 1 : 0);
	        setAddFlags8(dst, src, res); break;
	case 3: res = (uint16)dst - src - ((_flags & kFlagCarry) ? 1 : 0);
	        setSubFlags8(dst, src, res); break;
	case 4: res = dst & src;         setLogicFlags8((uint8)res); break;
	case 5: res = (uint16)dst - src; setSubFlags8(dst, src, res); break;
	case 6: res = dst ^ src;         setLogicFlags8((uint8)res); break;
	case 7: res = (uint16)dst - src; setSubFlags8(dst, src, res); return dst;
	default:
		warning("BihScript: alu8 invalid sub=%u at PC 0x%04X", sub, _pc);
		_halted = true; return dst;
	}
	return (uint8)res;
}

void BihScript::setLogicFlags16(uint16 r) {
	_flags = (uint16)(_flags & ~(kFlagCarry | kFlagOverflow | kFlagAux | kFlagZero | kFlagSign | kFlagParity));
	if (r == 0)         _flags |= kFlagZero;
	if (r & 0x8000)     _flags |= kFlagSign;
	if (kParityTable[r & 0xFF]) _flags |= kFlagParity;
}
void BihScript::setLogicFlags8(uint8 r) {
	_flags = (uint16)(_flags & ~(kFlagCarry | kFlagOverflow | kFlagAux | kFlagZero | kFlagSign | kFlagParity));
	if (r == 0)        _flags |= kFlagZero;
	if (r & 0x80)      _flags |= kFlagSign;
	if (kParityTable[r]) _flags |= kFlagParity;
}

void BihScript::setAddFlags16(uint16 dst, uint16 src, uint32 res) {
	_flags = (uint16)(_flags & ~(kFlagCarry | kFlagOverflow | kFlagAux | kFlagZero | kFlagSign | kFlagParity));
	if (res & 0x10000)                                           _flags |= kFlagCarry;
	if ((res & 0xFFFF) == 0)                                     _flags |= kFlagZero;
	if (res & 0x8000)                                            _flags |= kFlagSign;
	if (kParityTable[res & 0xFF])                                _flags |= kFlagParity;
	if ((res ^ src) & (res ^ dst) & 0x8000)                      _flags |= kFlagOverflow;
	if ((src ^ dst ^ res) & 0x10)                                _flags |= kFlagAux;
}
void BihScript::setAddFlags8(uint8 dst, uint8 src, uint16 res) {
	_flags = (uint16)(_flags & ~(kFlagCarry | kFlagOverflow | kFlagAux | kFlagZero | kFlagSign | kFlagParity));
	if (res & 0x100)                                             _flags |= kFlagCarry;
	if ((res & 0xFF) == 0)                                       _flags |= kFlagZero;
	if (res & 0x80)                                              _flags |= kFlagSign;
	if (kParityTable[res & 0xFF])                                _flags |= kFlagParity;
	if ((res ^ src) & (res ^ dst) & 0x80)                        _flags |= kFlagOverflow;
	if ((src ^ dst ^ res) & 0x10)                                _flags |= kFlagAux;
}
void BihScript::setSubFlags16(uint16 dst, uint16 src, uint32 res) {
	setAddFlags16(dst, src, res);
	// For subtraction, OF is set when sign of src differs from dst AND
	// sign of res matches src. Simpler: flip the ADD overflow logic.
	_flags &= ~kFlagOverflow;
	if (((dst ^ src) & (dst ^ res)) & 0x8000)
		_flags |= kFlagOverflow;
}
void BihScript::setSubFlags8(uint8 dst, uint8 src, uint16 res) {
	setAddFlags8(dst, src, res);
	_flags &= ~kFlagOverflow;
	if (((dst ^ src) & (dst ^ res)) & 0x80)
		_flags |= kFlagOverflow;
}


// ---------------------------------------------------------------------------
// Conditional-branch test (opcodes 0x70..0x7F). Uses the standard
// condition-code table indexed by the low 4 bits of the opcode.
// The BIH assembler emits the same bit positions as x86 FLAGS so a
// simple bitmask check works here.
// ---------------------------------------------------------------------------
static bool testCc(uint8 cc, uint16 f) {
	// Local copies of the flag masks, since this helper lives outside
	// the class and BihScript's enum is private.
	const uint16 kC = 0x0001, kP = 0x0004, kZ = 0x0040;
	const uint16 kS = 0x0080, kO = 0x0800;
	auto F = [f](uint16 m) { return (f & m) != 0; };
	bool result = false;
	switch (cc & 0x0E) {
	case 0x00: result =  F(kO);                       break; // JO
	case 0x02: result =  F(kC);                       break; // JB / JNAE
	case 0x04: result =  F(kZ);                       break; // JE / JZ
	case 0x06: result =  F(kC) || F(kZ);              break; // JBE
	case 0x08: result =  F(kS);                       break; // JS
	case 0x0A: result =  F(kP);                       break; // JP
	case 0x0C: result =  F(kS) != F(kO);              break; // JL
	case 0x0E: result =  F(kZ) || (F(kS) != F(kO));   break; // JLE
	}
	if (cc & 1) result = !result;
	return result;
}


// ---------------------------------------------------------------------------
// Top-level dispatcher. One pass per opcode byte. Returns false on
// truly-unknown opcodes so the caller can bail cleanly.
// ---------------------------------------------------------------------------
bool BihScript::step() {
	uint16 instrPc = _pc;
	uint8 op = fetch8();

	// --- 0x00..0x3D: ALU rm/r variants (grouped by low 3 bits) ----------
	// Layout: ((sub << 3) | form) where
	//   form 0 = rm8 op= r8
	//   form 1 = rm16 op= r16
	//   form 2 = r8 op= rm8
	//   form 3 = r16 op= rm16
	//   form 4 = AL op= imm8
	//   form 5 = AX op= imm16
	if (op < 0x40 && (op & 6) != 6) {
		uint8 sub = (op >> 3) & 7;
		uint8 form = op & 7;

		if (form == 4) {
			uint8 imm = fetch8();
			writeR8(kSlotA, alu8(sub, readR8(kSlotA), imm));
			return true;
		}
		if (form == 5) {
			uint16 imm = fetch16();
			writeR16(kSlotA, alu16(sub, readR16(kSlotA), imm));
			return true;
		}

		OpAddress oa = fetchOperand();
		bool wide    = (form & 1) != 0;
		bool srcIsRm = (form & 2) != 0;
		uint16 addr  = (oa.mod == 3) ? 0 : resolveMemAddr(oa);

		if (wide) {
			uint16 rmVal = (oa.mod == 3) ? readR16(oa.rm) : readMem16(addr);
			uint16 regVal = readR16(oa.reg);
			if (srcIsRm) {
				writeR16(oa.reg, alu16(sub, regVal, rmVal));
			} else {
				uint16 newVal = alu16(sub, rmVal, regVal);
				if (sub == 7)  { /* CMP: no writeback */ }
				else if (oa.mod == 3) writeR16(oa.rm, newVal);
				else                  writeMem16(addr, newVal);
			}
		} else {
			uint8 rmVal = (oa.mod == 3) ? readR8(oa.rm) : readMem8(addr);
			uint8 regVal = readR8(oa.reg);
			if (srcIsRm) {
				writeR8(oa.reg, alu8(sub, regVal, rmVal));
			} else {
				uint8 newVal = alu8(sub, rmVal, regVal);
				if (sub == 7)  { /* CMP */ }
				else if (oa.mod == 3) writeR8(oa.rm, newVal);
				else                  writeMem8(addr, newVal);
			}
		}
		return true;
	}

	// --- 0x40..0x47: INC r16 ---
	if ((op & 0xF8) == 0x40) {
		uint8 idx = op & 7;
		writeR16(idx, alu16(0, readR16(idx), 1)); // ADD reg, 1 (does set flags)
		return true;
	}
	// --- 0x48..0x4F: DEC r16 ---
	if ((op & 0xF8) == 0x48) {
		uint8 idx = op & 7;
		writeR16(idx, alu16(5, readR16(idx), 1));
		return true;
	}
	// --- 0x50..0x57: PUSH r16 ---
	if ((op & 0xF8) == 0x50) {
		pushW(readR16(op & 7));
		return true;
	}
	// --- 0x58..0x5F: POP r16 ---
	if ((op & 0xF8) == 0x58) {
		writeR16(op & 7, popW());
		return true;
	}

	// --- 0x70..0x7F: short conditional branch ---
	if ((op & 0xF0) == 0x70) {
		int8 disp = (int8)fetch8();
		if (testCc((uint8)(op & 0x0F), _flags))
			_pc = (uint16)(_pc + disp);
		return true;
	}

	switch (op) {
	// -- 0x81 / 0x83: ALU rm16, imm{16 | sign-ext-8} --
	case 0x81:
	case 0x83: {
		OpAddress oa = fetchOperand();
		uint16 addr = (oa.mod == 3) ? 0 : resolveMemAddr(oa);
		uint16 rmVal = (oa.mod == 3) ? readR16(oa.rm) : readMem16(addr);
		uint16 imm   = (op == 0x83) ? (uint16)(int16)(int8)fetch8() : fetch16();
		uint16 newVal = alu16(oa.reg, rmVal, imm);
		if (oa.reg == 7) { /* CMP */ }
		else if (oa.mod == 3) writeR16(oa.rm, newVal);
		else                  writeMem16(addr, newVal);
		return true;
	}

	// -- 0x84 / 0x85: TEST rm, r (bitwise AND, no writeback) --
	case 0x84: {
		OpAddress oa = fetchOperand();
		uint8 a = (oa.mod == 3) ? readR8(oa.rm) : readMem8(resolveMemAddr(oa));
		setLogicFlags8((uint8)(a & readR8(oa.reg)));
		return true;
	}
	case 0x85: {
		OpAddress oa = fetchOperand();
		uint16 a = (oa.mod == 3) ? readR16(oa.rm) : readMem16(resolveMemAddr(oa));
		setLogicFlags16((uint16)(a & readR16(oa.reg)));
		return true;
	}

	// -- 0x88..0x8B: MOV rm/r variants --
	case 0x88: { // MOV rm8, r8
		OpAddress oa = fetchOperand();
		uint8 v = readR8(oa.reg);
		if (oa.mod == 3) writeR8(oa.rm, v); else writeMem8(resolveMemAddr(oa), v);
		return true;
	}
	case 0x89: { // MOV rm16, r16
		OpAddress oa = fetchOperand();
		uint16 v = readR16(oa.reg);
		if (oa.mod == 3) writeR16(oa.rm, v); else writeMem16(resolveMemAddr(oa), v);
		return true;
	}
	case 0x8A: { // MOV r8, rm8
		OpAddress oa = fetchOperand();
		uint8 v = (oa.mod == 3) ? readR8(oa.rm) : readMem8(resolveMemAddr(oa));
		writeR8(oa.reg, v);
		return true;
	}
	case 0x8B: { // MOV r16, rm16
		OpAddress oa = fetchOperand();
		uint16 v = (oa.mod == 3) ? readR16(oa.rm) : readMem16(resolveMemAddr(oa));
		writeR16(oa.reg, v);
		return true;
	}

	// -- 0xB0..0xB7: MOV r8, imm8 / 0xB8..0xBF: MOV r16, imm16 --
	case 0xB0: case 0xB1: case 0xB2: case 0xB3:
	case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		writeR8(op & 7, fetch8());
		return true;
	case 0xB8: case 0xB9: case 0xBA: case 0xBB:
	case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		writeR16(op & 7, fetch16());
		return true;

	// -- 0xC3: RET near --
	case 0xC3:
		_pc = popW();
		return true;

	// -- 0xC6 / 0xC7: MOV rm, imm --
	case 0xC6: {
		OpAddress oa = fetchOperand();
		uint16 addr = (oa.mod == 3) ? 0 : resolveMemAddr(oa);
		uint8 imm = fetch8();
		if (oa.mod == 3) writeR8(oa.rm, imm); else writeMem8(addr, imm);
		return true;
	}
	case 0xC7: {
		OpAddress oa = fetchOperand();
		uint16 addr = (oa.mod == 3) ? 0 : resolveMemAddr(oa);
		uint16 imm = fetch16();
		if (oa.mod == 3) writeR16(oa.rm, imm); else writeMem16(addr, imm);
		return true;
	}

	// -- 0xCB: RET-far / halt sentinel in BIH encoding --
	case 0xCB:
		_halted = true;
		return true;

	// -- 0xD7: XLAT (AL = [BX + AL]) --
	case 0xD7: {
		uint16 addr = (uint16)(readR16(kSlotB) + readR8(kSlotA));
		writeR8(kSlotA, readMem8(addr));
		return true;
	}

	// -- 0xE2: LOOP (decrement CX, branch if non-zero) --
	case 0xE2: {
		int8 disp = (int8)fetch8();
		uint16 cx = (uint16)(readR16(kSlotC) - 1);
		writeR16(kSlotC, cx);
		if (cx != 0)
			_pc = (uint16)(_pc + disp);
		return true;
	}

	// -- 0xE8: CALL near --
	case 0xE8: {
		int16 disp = (int16)fetch16();
		pushW(_pc);
		_pc = (uint16)(_pc + disp);
		return true;
	}

	// -- 0xEB: JMP short --
	case 0xEB: {
		int8 disp = (int8)fetch8();
		_pc = (uint16)(_pc + disp);
		return true;
	}

	// -- 0xFE / 0xFF: unary rm8 / rm16 (INC / DEC / CALL indirect / PUSH) --
	case 0xFE: {
		OpAddress oa = fetchOperand();
		uint16 addr = (oa.mod == 3) ? 0 : resolveMemAddr(oa);
		uint8 v     = (oa.mod == 3) ? readR8(oa.rm) : readMem8(addr);
		uint8 nv    = alu8(oa.reg ? 5 : 0, v, 1);
		if (oa.mod == 3) writeR8(oa.rm, nv); else writeMem8(addr, nv);
		return true;
	}
	case 0xFF: {
		OpAddress oa = fetchOperand();
		uint16 addr = (oa.mod == 3) ? 0 : resolveMemAddr(oa);
		switch (oa.reg) {
		case 0: case 1: {
			uint16 v  = (oa.mod == 3) ? readR16(oa.rm) : readMem16(addr);
			uint16 nv = alu16(oa.reg ? 5 : 0, v, 1);
			if (oa.mod == 3) writeR16(oa.rm, nv); else writeMem16(addr, nv);
			return true;
		}
		case 3:
			// "Far call through memory" — the callback escape. The
			// caller has pushed its cmd code and args on the stack;
			// we just dispatch and discard the two-word return
			// address the DOS version pushed before calling.
			pushW(0);     // placeholder far seg (unused)
			pushW(_pc);   // far offset return
			dispatchCallback();
			popW(); popW(); // clean up
			return true;
		case 6: {
			uint16 v = (oa.mod == 3) ? readR16(oa.rm) : readMem16(addr);
			pushW(v);
			return true;
		}
		}
		warning("BihScript: unknown 0xFF /%u at 0x%04X", oa.reg, instrPc);
		return false;
	}
	}

	warning("BihScript: unknown opcode 0x%02X at 0x%04X", op, instrPc);
	return false;
}


// ---------------------------------------------------------------------------
// Engine callback. Mirrors scene_real_world.c::neuro_cb: the script pushes
// a command-code word (and optional args) before the indirect far call.
// For now we just log the command; real dispatches will land as the
// level handlers surface.
// ---------------------------------------------------------------------------
void BihScript::dispatchCallback() {
	uint16 sp = readR16(kSlotSP);
	// DOS neuro_cb reads `cmd = *(ss16 + 2)` because at this point SP
	// has been adjusted: 2 words of far-return address sit below it.
	uint16 cmd = readMem16((uint16)(sp + 4));
	uint16 arg1 = readMem16((uint16)(sp + 6));
	uint16 arg2 = readMem16((uint16)(sp + 8));
	debugC(1, kDebugScript, "BihScript: callback cmd=%u arg1=0x%04X arg2=0x%04X",
	       cmd, arg1, arg2);
	(void)cmd; (void)arg1; (void)arg2; // TODO: route to engine services
}

} // End of namespace Neuromancer
