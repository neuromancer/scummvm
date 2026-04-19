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
	memset(_vars, 0, sizeof(_vars));
	memset(_levelDialog, 0, sizeof(_levelDialog));
}

void NeuroVM::attach(const byte *bihData, uint32 size) {
	_bih.attach(bihData, size);
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

	// Bound total opcodes per tick so a self-jump or similar bug can't hang
	// the main loop.
	const int kOpcodeBudget = 1024;
	int budget = kOpcodeBudget;

	while (budget-- > 0) {
		bool anyActive = false;
		for (int slot = 3; slot >= 0; slot--) {
			if (!_threads[slot].active)
				continue;
			anyActive = true;

			if (step(slot)) {
				r.action    = _pendingAction;
				r.stringNum = _pendingString;
				r.levelN    = _pendingLevel;
				r.var1      = _pendingVar1;
				r.var2      = _pendingVar2;
				return r;
			}
		}
		if (!anyActive)
			break;
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
		// DOS scene_real_world.c:164 resets active_dialog_reply to 0xFF
		// when yielding on op 0x01. Without this, a script that loops
		// while checking var[16] re-observes the previous reply and
		// re-enters the same branch forever.
		writeVar8(kVarActiveDialogReply, 0xFF);
		debugC(2, kDebugScript,
		       "VM: slot %d op 0x01 dialog str=%u at (%u, %u); reset var[%u]=0xFF",
		       slot, _pendingString, t.var1, t.var2, kVarActiveDialogReply);
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
		// This is inverted from a typical assembler JE; the DOS scripts use
		// these as "if X == Y then continue-here else goto else-branch".
		uint8 idx = op[1];
		uint8 val = op[3];
		int16 offset = (int16)READ_LE_UINT16(op + 4);
		uint8 got = _vars[idx];
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
		debugC(2, kDebugScript, "VM: slot %d cond 0x%02X var[%u]=%u vs %u -> %s",
		       slot, code, idx, got, val, met ? "fallthrough" : "jump");
		break;
	}

	case 0x0E: case 0x0F: {
		// 16-bit variable write. idx is a byte offset into _vars; value is
		// stored little-endian across [idx, idx+1].
		uint16 idx = READ_LE_UINT16(op + 1);
		uint16 val = READ_LE_UINT16(op + 3);
		if (idx + 1 < kVarsSize) {
			_vars[idx]     = (uint8)(val & 0xFF);
			_vars[idx + 1] = (uint8)(val >> 8);
		} else {
			warning("NeuroVM: set-var idx 0x%04X out of range (max 0x%04X)",
			        idx, (uint32)kVarsSize);
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
		// 16-bit add-to-variable (little-endian).
		uint16 idx = READ_LE_UINT16(op + 1);
		uint16 val = READ_LE_UINT16(op + 3);
		if (idx + 1 < kVarsSize) {
			uint16 cur = _vars[idx] | ((uint16)_vars[idx + 1] << 8);
			uint16 sum = cur + val;
			_vars[idx]     = (uint8)(sum & 0xFF);
			_vars[idx + 1] = (uint8)(sum >> 8);
		} else {
			warning("NeuroVM: add-var idx 0x%04X out of range", idx);
		}
		t.nextOpAddr += 5;
		break;
	}

	case 0x16: {
		uint16 offset = READ_LE_UINT16(op + 1);
		debugC(2, kDebugScript, "VM: slot %d op 0x16 exec offset=0x%04X", slot, offset);
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
		// Dynamic string output: the operand indexes an array of 16-bit
		// string IDs that lives in game state. In the DOS build the
		// pointer is `&g_4bae.x4c7c`, at DSEG 0x4C7C -- offset 0xCE
		// inside the x4bae_t struct; op[1] is a WORD index, so the byte
		// offset into our _vars[] is 0xCE + op[1] * 2.
		uint16 baseOff = 0xCE;
		uint16 varOff  = (uint16)(baseOff + (uint16)op[1] * 2);
		uint16 stringNum = 0;
		if ((uint32)varOff + 1 < kVarsSize)
			stringNum = (uint16)_vars[varOff] | ((uint16)_vars[varOff + 1] << 8);
		_pendingAction = Action::kDialogReply;
		_pendingString = stringNum;
		_pendingVar1 = t.var1;
		_pendingVar2 = t.var2;
		t.nextOpAddr += 2;
		// Reset active_dialog_reply matching the DOS op 0x18 behaviour,
		// same reason as op 0x01.
		writeVar8(kVarActiveDialogReply, 0xFF);
		debugC(2, kDebugScript,
		       "VM: slot %d op 0x18 dynamic str var[0x%04X]=%u at (%u, %u); reset var[%u]=0xFF",
		       slot, varOff, stringNum, t.var1, t.var2, kVarActiveDialogReply);
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
