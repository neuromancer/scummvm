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

#include "neuromancer/neuro_vm.h"

#include "neuromancer/bih_script.h"
#include "neuromancer/detection.h"
#include "neuromancer/level_handlers.h"
#include "neuromancer/neuromancer.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/textconsole.h"

namespace Neuromancer {

// --------------------------------------------------------------------------
// Bih
// --------------------------------------------------------------------------

void Bih::attach(const byte *bytes, uint32 size) {
	_bytes = bytes;
	_size = size;
	memset(&_hdr, 0, sizeof(_hdr));
	if (!bytes || size < 0x28)
		return;
	_hdr.cbOffset        = READ_LE_UINT16(bytes + 0x00);
	_hdr.cbSegment       = READ_LE_UINT16(bytes + 0x02);
	_hdr.ctrlStructAddr  = READ_LE_UINT16(bytes + 0x04);
	_hdr.textOffset      = READ_LE_UINT16(bytes + 0x06);
	_hdr.bytecodeArrayOffset[0] = READ_LE_UINT16(bytes + 0x08);
	_hdr.bytecodeArrayOffset[1] = READ_LE_UINT16(bytes + 0x0A);
	_hdr.bytecodeArrayOffset[2] = READ_LE_UINT16(bytes + 0x0C);
	_hdr.initObjCodeOffset[0]   = READ_LE_UINT16(bytes + 0x0E);
	_hdr.initObjCodeOffset[1]   = READ_LE_UINT16(bytes + 0x10);
	_hdr.initObjCodeOffset[2]   = READ_LE_UINT16(bytes + 0x12);
	for (int i = 0; i < 10; i++)
		_hdr.unknown[i] = READ_LE_UINT16(bytes + 0x14 + i * 2);
}

const char *Bih::textString(uint16 n) const {
	if (!_bytes || _hdr.textOffset >= _size)
		return "";
	const byte *p   = _bytes + _hdr.textOffset;
	const byte *end = _bytes + _size;
	for (uint16 i = 0; i < n && p < end; i++) {
		while (p < end && *p != 0)
			p++;
		if (p < end)
			p++; // skip terminator
	}
	if (p >= end)
		return "";
	return (const char *)p;
}

uint16 Bih::programAddress(uint8 tableIdx, uint8 progIdx) const {
	if (!_bytes || tableIdx >= 3)
		return 0;
	uint32 tblOff = _hdr.bytecodeArrayOffset[tableIdx];
	if (tblOff + (uint32)(progIdx + 1) * 2 > _size)
		return 0;
	return READ_LE_UINT16(_bytes + tblOff + (uint32)progIdx * 2);
}

// --------------------------------------------------------------------------
// NeuroVM
// --------------------------------------------------------------------------

NeuroVM::NeuroVM(NeuromancerEngine *engine)
	: _engine(engine),
	  _currentThread(3),
	  _updateHold(0),
	  _pendingAction(Action::kIdle),
	  _pendingString(0),
	  _pendingLevel(0),
	  _pendingVar1(0),
	  _pendingVar2(0) {
	memset(_threads, 0, sizeof(_threads));
	_vars.resize(kVarsSize);
	memset(_levelDialog, 0, sizeof(_levelDialog));
}

void NeuroVM::attach(const byte *bihData, uint32 size) {
	_bih.attach(bihData, size);

	// Copy the BIH bytes into the DSEG mirror at 0xA8E8 so BihScript can
	// fetch instructions and BIH-local data in place, exactly as the DOS
	// engine does when it loads a BIH into g_a8e0.bih.bytes.
	if (bihData && (uint32)kBihBase + size <= _vars.size()) {
		memcpy(_vars.data() + kBihBase, bihData, size);
	} else if (bihData) {
		warning("NeuroVM::attach: BIH too large (%u bytes) for DSEG window", size);
	}

	resetThreads();
	debugC(1, kDebugScript,
	       "NeuroVM attached: text=0x%04X bytecode=[0x%04X,0x%04X,0x%04X] init=[0x%04X,0x%04X,0x%04X]",
	       _bih.header().textOffset,
	       _bih.header().bytecodeArrayOffset[0],
	       _bih.header().bytecodeArrayOffset[1],
	       _bih.header().bytecodeArrayOffset[2],
	       _bih.header().initObjCodeOffset[0],
	       _bih.header().initObjCodeOffset[1],
	       _bih.header().initObjCodeOffset[2]);
}

void NeuroVM::resetThreads() {
	for (int i = 0; i < 4; i++) {
		_threads[i].active     = false;
		_threads[i].nextOpAddr = 0;
		_threads[i].var1       = 0;
		_threads[i].var2       = 0;
		_threads[i].flag       = 0;
	}
	_currentThread   = 3;
	_pendingAction   = Action::kIdle;
	_pendingString   = 0;
	_pendingLevel    = 0;
	_updateHold      = 0;
}

void NeuroVM::startDefaultThread(int slot, uint8 flagTable) {
	if (slot < 0 || slot >= 4)
		return;
	VmThread &t = _threads[slot];
	t.active     = true;
	t.flag       = flagTable;
	t.var1       = 0;
	t.var2       = 0;
	t.nextOpAddr = _bih.programAddress(flagTable & 3, 0);
	debugC(1, kDebugScript, "NeuroVM: slot %d started at 0x%04X (table %u)",
	       slot, t.nextOpAddr, flagTable);
}

void NeuroVM::resume() {
	_pendingAction = Action::kIdle;
}

NeuroVM::TickResult NeuroVM::tick() {
	TickResult r = { Action::kIdle, 0, 0, 0, 0 };

	if (_pendingAction != Action::kIdle) {
		r.action    = _pendingAction;
		r.stringNum = _pendingString;
		r.levelN    = _pendingLevel;
		r.var1      = _pendingVar1;
		r.var2      = _pendingVar2;
		return r;
	}

	// Match DOS neuro_vm() (scene_real_world.c:128): run AT MOST one
	// opcode per active thread per tick, then return. This is critical for
	// scripts that busy-wait on a variable (e.g. Ratz's Pay-up nag in R1:
	// TOP -> jne var[16],255 -> TOP -> ...). With the DOS cadence the loop
	// advances one opcode per frame, which leaves the main loop responsive
	// to mouse / keyboard input. Our old "run until yield or idle" design
	// would execute thousands of cond-jumps in a single frame, flooding the
	// log and starving input.
	for (int slot = 3; slot >= 0; slot--) {
		if (!_threads[slot].active)
			continue;
		if (step(slot)) {
			r.action    = _pendingAction;
			r.stringNum = _pendingString;
			r.levelN    = _pendingLevel;
			r.var1      = _pendingVar1;
			r.var2      = _pendingVar2;
			return r;
		}
	}

	return r;
}

bool NeuroVM::step(int slot) {
	VmThread &t = _threads[slot];
	if (t.nextOpAddr == 0 || t.nextOpAddr >= _bih.size()) {
		warning("NeuroVM: slot %d PC 0x%04X out of BIH (size 0x%04X), deactivating",
		        slot, t.nextOpAddr, _bih.size());
		t.active = false;
		return false;
	}

	const byte *op = _bih.bytes() + t.nextOpAddr;
	byte code = op[0];

	switch (code) {
	case 0x00: {
		uint16 addr = _bih.programAddress(t.flag & 3, 0);
		debugC(2, kDebugScript, "VM: slot %d op 0x00 -> default prog 0x%04X", slot, addr);
		t.nextOpAddr = addr;
		break;
	}

	case 0x01: {
		_pendingAction = Action::kDialogReply;
		_pendingString = op[1];
		_pendingVar1 = t.var1;
		_pendingVar2 = t.var2;
		t.nextOpAddr += 2;
		// DOS scene_real_world.c:164: reset active_dialog_reply so the
		// idle script ping-pongs silently on `jne var[16], 255 -> TOP`
		// until the dialog picker writes a real reply. Relies on our
		// one-opcode-per-tick dispatch (matching DOS) so the spin does
		// not starve the main loop.
		writeVar8(kVarActiveDialogReply, 0xFF);
		debugC(2, kDebugScript,
		       "VM: slot %d op 0x01 dialog str=%u at (%u, %u); reset var[16]=0xFF",
		       slot, _pendingString, t.var1, t.var2);
		return true;
	}

	case 0x02: {
		_pendingAction = Action::kTextOutput;
		_pendingString = op[1];
		t.nextOpAddr += 2;
		debugC(2, kDebugScript, "VM: slot %d op 0x02 text str=%u", slot, _pendingString);
		return true;
	}

	case 0x03: {
		uint16 addr = _bih.programAddress(t.flag & 3, op[1]);
		debugC(2, kDebugScript, "VM: slot %d op 0x03 prog[%u] -> 0x%04X", slot, op[1], addr);
		t.nextOpAddr = addr;
		break;
	}

	case 0x04: {
		int16 offset = (int16)READ_LE_UINT16(op + 1);
		t.nextOpAddr = (uint16)(t.nextOpAddr + offset + 1);
		debugC(2, kDebugScript, "VM: slot %d op 0x04 goto %+d -> 0x%04X", slot, offset, t.nextOpAddr);
		break;
	}

	case 0x05: case 0x06: case 0x07: case 0x08: {
		// [op][idx][_][val][offLo][offHi] -- 6 bytes total.
		// Condition MET -> advance past the instruction (fallthrough).
		// Condition NOT MET -> take the alternate branch by `offset + 4`.
		// `idx` is a byte offset inside g_4bae.x4bae[]; absolute DSEG
		// address is 0x4BAE + idx. BIH native code stores the same field
		// at that DSEG location, so high-level and BihScript share one memory.
		uint8 idx = op[1];
		uint8 val = op[3];
		int16 offset = (int16)READ_LE_UINT16(op + 4);
		uint8 got = _vars[(uint32)kX4baeBase + idx];
		bool met = false;
		switch (code) {
		case 0x05: met = (got == val); break; // "equal"
		case 0x06: met = (got != val); break; // "not equal"
		case 0x07: met = (got <  val); break; // "less"
		case 0x08: met = (got >= val); break; // "greater-or-equal"
		}
		if (met)
			t.nextOpAddr += 6;
		else
			t.nextOpAddr = (uint16)(t.nextOpAddr + offset + 4);
		// Cond-jumps can fire thousands of times a second while a script
		// is busy-waiting on a variable. Suppress exact repeats so the
		// log stays useful.
		static int s_lastSlot = -1;
		static byte s_lastCode = 0, s_lastIdx = 0, s_lastGot = 0, s_lastVal = 0;
		static bool s_lastMet = false;
		static uint32 s_repeatCount = 0;
		if (slot == s_lastSlot && code == s_lastCode && idx == s_lastIdx &&
		    got == s_lastGot && val == s_lastVal && met == s_lastMet) {
			s_repeatCount++;
		} else {
			if (s_repeatCount > 0) {
				debugC(2, kDebugScript, "VM: ... (prev cond repeated %u times)", s_repeatCount);
				s_repeatCount = 0;
			}
			debugC(2, kDebugScript, "VM: slot %d cond 0x%02X var[%u]=%u vs %u -> %s",
			       slot, code, idx, got, val, met ? "fallthrough" : "jump");
			s_lastSlot = slot;
			s_lastCode = code;
			s_lastIdx = idx;
			s_lastGot = got;
			s_lastVal = val;
			s_lastMet = met;
		}
		break;
	}

	case 0x0E: case 0x0F: {
		// 16-bit variable write. `idx` is an x4bae-relative byte offset;
		// absolute DSEG target is 0x4BAE + idx.
		uint16 idx = READ_LE_UINT16(op + 1);
		uint16 val = READ_LE_UINT16(op + 3);
		uint32 addr = (uint32)kX4baeBase + idx;
		if (addr + 1 < kVarsSize) {
			_vars[addr]     = (uint8)(val & 0xFF);
			_vars[addr + 1] = (uint8)(val >> 8);
		} else {
			warning("NeuroVM: set-var idx 0x%04X out of range", idx);
		}
		t.nextOpAddr += 5;
		debugC(2, kDebugScript, "VM: slot %d op 0x%02X var[0x%04X]=%u", slot, code, idx, val);
		break;
	}

	case 0x10: {
		_pendingAction = Action::kChangeLevel;
		_pendingLevel = op[1];
		t.nextOpAddr += 2;
		debugC(2, kDebugScript, "VM: slot %d op 0x10 level=%u", slot, _pendingLevel);
		return true;
	}

	case 0x11:
		_updateHold++;
		t.nextOpAddr++;
		break;

	case 0x12:
		_updateHold = 0;
		t.nextOpAddr++;
		break;

	case 0x13: {
		// Set per-level dialog control: level index at op[1], first reply
		// id at op[2], total replies at op[3]. The dialog scene reads
		// these when the player later enters dialog mode (opcode 0x17).
		uint8 levelN   = op[1];
		uint8 firstRep = op[2];
		uint8 totalRep = op[3];
		if (levelN < 64) {
			_levelDialog[levelN].firstReply   = firstRep;
			_levelDialog[levelN].totalReplies = totalRep;
		}
		debugC(2, kDebugScript, "VM: slot %d op 0x13 lvl=%u first=%u total=%u",
		       slot, levelN, firstRep, totalRep);
		t.nextOpAddr += 4;
		break;
	}

	case 0x15: {
		// 16-bit add-to-variable (little-endian). x4bae-relative idx.
		uint16 idx = READ_LE_UINT16(op + 1);
		uint16 val = READ_LE_UINT16(op + 3);
		uint32 addr = (uint32)kX4baeBase + idx;
		if (addr + 1 < kVarsSize) {
			uint16 cur = _vars[addr] | ((uint16)_vars[addr + 1] << 8);
			uint16 sum = cur + val;
			_vars[addr]     = (uint8)(sum & 0xFF);
			_vars[addr + 1] = (uint8)(sum >> 8);
		} else {
			warning("NeuroVM: add-var idx 0x%04X out of range", idx);
		}
		t.nextOpAddr += 5;
		break;
	}

	case 0x16: {
		// Call into the BIH's per-level native-code routine at the
		// given offset. Runs synchronously via the BIH script
		// interpreter, which reads DSEG via _vars so the routine sees
		// the same game state the high-level VM is running against.
		uint16 offset = READ_LE_UINT16(op + 1);
		debugC(2, kDebugScript, "VM: slot %d op 0x16 exec bih+0x%04X", slot, offset);
		BihScript script(_engine);
		script.runBihOffset(offset);
		// Also let the C++ handler layer observe -- useful for hooking
		// in hand-ported overrides alongside the interpreter.
		if (LevelHandlers *h = _engine->levelHandlers())
			h->execTarget(_engine->currentLevel(), offset, _engine);
		t.nextOpAddr += 3;
		break;
	}

	case 0x17:
		_pendingAction = Action::kEnterDialog;
		t.nextOpAddr++;
		debugC(2, kDebugScript, "VM: slot %d op 0x17 enter dialog", slot);
		return true;

	case 0x18: {
		// Dynamic string output: the operand indexes a WORD array
		// x4c7c[] at DSEG 0x4C7C (= x4bae-relative 0xCE). op[1] is a
		// WORD index, so byte offset = 0x4C7C + op[1] * 2.
		uint32 varOff = (uint32)0x4C7C + (uint32)op[1] * 2;
		uint16 stringNum = 0;
		if (varOff + 1 < kVarsSize)
			stringNum = (uint16)_vars[varOff] | ((uint16)_vars[varOff + 1] << 8);
		_pendingAction = Action::kDialogReply;
		_pendingString = stringNum;
		_pendingVar1 = t.var1;
		_pendingVar2 = t.var2;
		t.nextOpAddr += 2;
		// Same rationale as op 0x01 (DOS scene_real_world.c:301).
		writeVar8(kVarActiveDialogReply, 0xFF);
		debugC(2, kDebugScript,
		       "VM: slot %d op 0x18 dynamic str var[0x%04X]=%u at (%u, %u); reset var[16]=0xFF",
		       slot, varOff, stringNum, t.var1, t.var2);
		return true;
	}

	default:
		warning("NeuroVM: slot %d unknown opcode 0x%02X at 0x%04X, deactivating",
		        slot, code, t.nextOpAddr);
		t.active = false;
		break;
	}

	return false;
}

} // End of namespace Neuromancer
