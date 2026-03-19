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

#include "glk/angel/vm.h"
#include "glk/angel/angel.h"
#include "glk/angel/parser.h"
#include "glk/glk.h"
#include "common/debug.h"
#include "common/textconsole.h"
#include "common/random.h"

namespace Glk {
namespace Angel {

VM::VM(Angel *engine, GameData *data, GameState *state)
    : _engine(engine), _data(data), _state(state), _callDepth(0),
      _capitalizeNext(false), _lastRawNip(0), _suppressText(false), _baseSuppressText(false),
      _descriptionOnly(false), _respondMode(false), _cseContentDepth(0),
      _lastTestResult(true), _compFieldAddr(0),
      _entityFlag(false), _entityValue(0), _entityOp(0), _entityType(-1),
      _entityContextFresh(false), _lastFieldRef(0),
      _describedEntityActive(false), _describedEntityOp(kNoOp),
      _describedEntityType(-1), _describedEntityValue(0) {
	memset(_callStack, 0, sizeof(_callStack));
}

void VM::openMsg(int addr, const char *caller) {
	if (addr <= 0) {
		_state->_eom = true;
		return;
	}

	_state->_msgBase = addr;
	_state->_msgPos = 0;
	_state->_msgCursor = 0;
	_state->_eom = false;
	_capitalizeNext = true;  // Auto-capitalize first letter of each message

	// Read the first chunk at the message address
	_state->_vmCurRecord = _data->readChunk(addr);

	// Messages have a 2-nip header: (nip0 << 6) | nip1 = length/marker.
	// The original ForceMsg "sets the first two nips of Msg to zero".
	// Content starts at nip position 2. Read and skip the header.
	int hdr0 = getNip();
	int hdr1 = getNip();
	_state->_msgLength = (hdr0 << 6) | hdr1;

	debugC(kDebugScripts, "Angel VM: openMsg addr=%d hdrLen=%d caller=%s", addr, _state->_msgLength, caller);
}

int VM::getNip() {
	if (_state->_eom)
		return 0;

	int nip = _state->_vmCurRecord.getNip(_state->_msgCursor);
	bumpMsg();
	return nip;
}

char VM::getAChar() {
	int nip = getNip();
	if (_state->_eom)
		return kEndSym;
	_lastRawNip = nip;
	return _data->_yTable[nip];
}

int VM::getNumber() {
	// Read a 12-bit number from 2 nips (big-endian)
	// This is CXS 10 = IOHANDLER seg 18 proc 10.
	int hi = getNip();
	int lo = getNip();
	return (hi << 6) | lo;
}

int VM::getNumber18() {
	// Read an 18-bit number from 3 nips (big-endian).
	// This is RESPOND proc 6 (via proc 33): proc5(flag=1)*64 + getNip.
	// Used by kEventOp and kSetOp (proc 58 literal path).
	// Different from getNumber() which only reads 2 nips (12-bit).
	int n1 = getNip();
	int n2 = getNip();
	int n3 = getNip();
	return (n1 << 12) | (n2 << 6) | n3;
}

void VM::bumpMsg() {
	_state->_msgPos++;
	_state->_msgCursor++;

	if (_state->_msgCursor >= kChunkSize) {
		// Move to the next chunk
		_state->_msgCursor = 0;
		int nextChunkAddr = _state->_msgBase + (_state->_msgPos / kChunkSize);
		_state->_vmCurRecord = _data->readChunk(nextChunkAddr);
	}
}

void VM::jump(int n) {
	// "Jump ahead n places in the current message" (relative forward).
	// Messages terminate via kEndSym in the stream, not via _msgLength.
	int newPos = _state->_msgPos + n;
	if (newPos < 0) {
		warning("Angel VM: jump(%d) from pos %d would go negative, clamping to 0", n, _state->_msgPos);
		newPos = 0;
	}
	_state->_msgPos = newPos;
	_state->_msgCursor = newPos % kChunkSize;
	int chunkAddr = _state->_msgBase + (newPos / kChunkSize);
	_state->_vmCurRecord = _data->readChunk(chunkAddr);
}

void VM::jumpTo(int pos) {
	// Jump to absolute position within the current message.
	// Messages terminate via kEndSym in the stream, not via _msgLength.
	if (pos < 0) {
		warning("Angel VM: jumpTo(%d) negative position, clamping to 0", pos);
		pos = 0;
	}
	_state->_msgPos = pos;
	_state->_msgCursor = pos % kChunkSize;
	int chunkAddr = _state->_msgBase + (pos / kChunkSize);
	_state->_vmCurRecord = _data->readChunk(chunkAddr);
}

void VM::displayMsg(int addr, bool descriptionOnly) {
	debugC(kDebugScripts, "Angel VM: displayMsg(%d) called, callDepth=%d descOnly=%d", addr, _callDepth, descriptionOnly ? 1 : 0);

	// Save current message state for re-entrant calls.
	// displayMsg can be called recursively from opcodes like kDscOp during
	// another displayMsg's executeMsg. Without save/restore, the nested call
	// corrupts the outer message's position and call stack depth.
	int savedBase = _state->_msgBase;
	int savedPos = _state->_msgPos;
	int savedCursor = _state->_msgCursor;
	int savedLength = _state->_msgLength;
	bool savedEom = _state->_eom;
	int savedCallDepth = _callDepth;
	bool savedDescOnly = _descriptionOnly;
	bool savedRespondMode = _respondMode;
	bool savedSuppressText = _suppressText;
	bool savedBaseSuppressText = _baseSuppressText;
	bool savedTfIndicator = _state->_tfIndicator;
	bool savedLastTestResult = _lastTestResult;
	Chunk savedRecord = _state->_vmCurRecord;
	CallFrame savedCallStack[kMaxCallDepth];
	if (savedCallDepth > 0)
		memcpy(savedCallStack, _callStack, savedCallDepth * sizeof(CallFrame));

	_callDepth = 0;
	_descriptionOnly = descriptionOnly;
	_state->_tfIndicator = true;  // No test has run yet; text should be visible
	_lastTestResult = true;

	openMsg(addr, "displayMsg");

	executeMsg();

	// Restore outer message state
	_state->_msgBase = savedBase;
	_state->_msgPos = savedPos;
	_state->_msgCursor = savedCursor;
	_state->_msgLength = savedLength;
	_state->_eom = savedEom;
	_callDepth = savedCallDepth;
	_descriptionOnly = savedDescOnly;
	_respondMode = savedRespondMode;
	_suppressText = savedSuppressText;
	_baseSuppressText = savedBaseSuppressText;
	_state->_tfIndicator = savedTfIndicator;
	_lastTestResult = savedLastTestResult;
	_state->_vmCurRecord = savedRecord;
	if (savedCallDepth > 0)
		memcpy(_callStack, savedCallStack, savedCallDepth * sizeof(CallFrame));

	debugC(kDebugScripts, "Angel VM: displayMsg(%d) returned, callDepth restored to %d", addr, _callDepth);
}

void VM::dumpNipsAt(int addr, int startPos, int count) {
	// Debug: dump nips at a given position within a message
	openMsg(addr, "dumpNips");
	jumpTo(startPos);
	Common::String nips, chars;
	for (int i = 0; i < count && !_state->_eom; i++) {
		int nip = getNip();
		char ch = _data->_yTable[nip];
		nips += Common::String::format("%d ", nip);
		if (ch >= 32 && ch < 127)
			chars += ch;
		else
			chars += Common::String::format("[%d]", (int)(unsigned char)ch);
	}
	debugC(kDebugScripts, "Angel VM: dumpNips addr=%d pos=%d: nips=%s", addr, startPos, nips.c_str());
	debugC(kDebugScripts, "Angel VM: dumpNips chars=%s", chars.c_str());
	_state->_eom = true;
}

void VM::executeMsg() {
	debugC(kDebugScripts, "Angel VM: executeMsg starting, msgBase=%d msgLength=%d eom=%d playing=%d",
	       _state->_msgBase, _state->_msgLength, _state->_eom, _state->_stillPlaying);
	int charCount = 0;
	int iterCount = 0;
	Common::String textOutput;  // Accumulate text for debug
	// P-code EXECMSG loop runs until EndSym or EOM. It does NOT check
	// the quit flag (global[2]). The quit flag is only checked in the
	// main game loop after the message finishes executing.
	while (!_state->_eom) {
		if (++iterCount > 5000) {
			warning("Angel VM: executeMsg runaway loop after %d iterations at pos=%d base=%d",
			        iterCount, _state->_msgPos, _state->_msgBase);
			_state->_eom = true;
			break;
		}
		int prePos = _state->_msgPos;
		char ch = getAChar();

		if (_state->_eom)
			break;

		// P-code proc 66 loop condition: pos <= len+2. When a JU jumps
		// to the end of the message, pos advances past len+2 after reading
		// one more nip, and the loop exits. Without this check, execution
		// continues reading nips from the next message in the file.
		if (_state->_msgPos > _state->_msgLength + 2) {
			if (_cseContentDepth > 0) {
				// Past CSE content boundary — signal CSE loop to exit.
				debugC(kDebugScripts, "Angel VM: boundary exit pos=%d > len+2=%d → CSE content end (cseDepth=%d)",
				        _state->_msgPos, _state->_msgLength + 2, _cseContentDepth);
				_state->_eom = true;
				break;
			} else if (_callDepth > 0) {
				// Return from FCall (same as EndSym at message end)
				_callDepth--;
				CallFrame &frame = _callStack[_callDepth];
				debugC(kDebugScripts, "Angel VM: boundary exit pos=%d > len+2=%d depth=%d → restore base=%d pos=%d",
				        _state->_msgPos, _state->_msgLength + 2, _callDepth + 1,
				        frame.base, frame.pos);
				_state->_msgBase = frame.base;
				_state->_msgPos = frame.pos;
				_state->_msgCursor = frame.cursor;
				_state->_msgLength = frame.length;
				_suppressText = frame.suppressText;
				_cseContentDepth = frame.cseContentDepth;
				_state->_vmCurRecord = _data->readChunk(
					_state->_msgBase + (_state->_msgPos / kChunkSize));
				// Discard the stale character read from the child message
				// and re-read from the restored parent context.
				continue;
			} else {
				debugC(kDebugScripts, "Angel VM: executeMsg pos=%d > len+2=%d, ending message",
				        _state->_msgPos, _state->_msgLength + 2);
				_state->_eom = true;
				break;
			}
		}

		// Trace every character position for alignment debugging (MSG30 only)
		if (_state->_msgBase == 30 || _state->_msgBase == 29 || _state->_msgBase == 3836 || _state->_msgBase == 3499 || _state->_msgBase == 7 || _state->_msgBase == 25) {
			debugC(kDebugScripts, "Angel VM: TRACE @pos=%d nip=%d ch='%c'(%d) base=%d tf=%d suppress=%d",
			        prePos, _lastRawNip, (ch >= 32 && ch < 127) ? ch : '?', (int)(unsigned char)ch,
			        _state->_msgBase, _state->_tfIndicator ? 1 : 0, _suppressText ? 1 : 0);
		}

		// Check for control codes
		switch (ch) {
		case kEndSym:
			// End of message / section break / return from call.
			//
			// In the original VM, the message loop runs until pos >= len+2.
			// EndSym within a message (pos < len+2) is a section break.
			// EndSym at the end of a message (pos >= len+2) terminates it.
			// FCall return happens when the called message reaches its end,
			// NOT on the first EndSym encountered.
			//
			// Priority:
			// 1. CSE content boundary → end CSE case (set _eom for loop exit)
			// 2. True end of FCall'd message → return from FCall
			// 3. True end of top-level message → EOM
			// 4. Section break within message → continue
			//
			// CSE content must be checked FIRST: the CSE handler temporarily
			// adjusts _msgLength to the content boundary, which would otherwise
			// be misinterpreted as the FCall'd message end. FCall saves/restores
			// _cseContentDepth (resetting to 0 for the callee), so this check
			// is safe even with nested FCall within CSE content.
			if (_cseContentDepth > 0 && _state->_msgPos >= _state->_msgLength + 2) {
				// End of CSE case content — signal CSE loop to exit.
				// The CSE handler will restore _msgLength and jump to endPos.
				debugC(kDebugScripts, "Angel VM: EndSym at pos=%d base=%d → CSE content end (cseDepth=%d len+2=%d)",
				        _state->_msgPos, _state->_msgBase, _cseContentDepth, _state->_msgLength + 2);
				_state->_eom = true;
			} else if (_callDepth > 0 && _state->_msgPos >= _state->_msgLength + 2) {
				// True end of FCall'd message → return from FCall
				_callDepth--;
				CallFrame &frame = _callStack[_callDepth];
				debugC(kDebugScripts, "Angel VM: EndSym at pos=%d base=%d depth=%d → restore base=%d pos=%d cursor=%d suppress=%d",
				        _state->_msgPos, _state->_msgBase, _callDepth + 1,
				        frame.base, frame.pos, frame.cursor, frame.suppressText ? 1 : 0);
				_state->_msgBase = frame.base;
				_state->_msgPos = frame.pos;
				_state->_msgCursor = frame.cursor;
				_state->_msgLength = frame.length;
				_suppressText = frame.suppressText;
				_cseContentDepth = frame.cseContentDepth;
				_state->_vmCurRecord = _data->readChunk(
					_state->_msgBase + (_state->_msgPos / kChunkSize));
			} else if (_state->_msgPos >= _state->_msgLength + 2) {
				// True end of top-level message
				debugC(kDebugScripts, "Angel VM: EndSym at pos=%d base=%d depth=0 → EOM (len+2=%d)",
				        _state->_msgPos, _state->_msgBase, _state->_msgLength + 2);
				_state->_eom = true;
			} else if (_descriptionOnly && _callDepth == 0) {
				// Description-only mode: stop at first EndSym section break.
				// Entity messages have [description] @ [response logic] structure.
				// Original proc 97 (kRoleOp) via CPI 4,9 only runs the first section.
				debugC(kDebugScripts, "Angel VM: EndSym section break at pos=%d → EOM (descriptionOnly)",
				        _state->_msgPos);
				_state->_eom = true;
			} else {
				// Section break within message — EndSym within content bounds
				// is a paragraph/section separator, not a message terminator.
				debugC(kDebugScripts, "Angel VM: EndSym section break at pos=%d base=%d depth=%d (len+2=%d)",
				        _state->_msgPos, _state->_msgBase, _callDepth, _state->_msgLength + 2);
				_engine->sectionBreak();
			}
			break;

		case kJU:
			// Unconditional jump: next 2 nips = forward offset.
			// Jump targets are encoded relative to the last nip of getNumber
			// (i.e. one position before _msgPos after getNumber), so subtract 1.
			// P-code L_3387: CLP2 5 (getNumber) then CXG 18,16 (jump).
			// CXG 18,16 does NOT modify seg[20].global[5] (text flag).
			// Text flag persists across jumps.
			{
				int target = getNumber();
				debugC(kDebugScripts, "Angel VM: JU target=%d from pos=%d -> pos=%d", target, _state->_msgPos, _state->_msgPos + target - 1);
				jump(target - 1);
			}
			break;

		case kJF:
			// Jump if FALSE: next 2 nips = forward offset.
			// Same encoding convention as kJU — subtract 1 from target.
			//
			// P-code JF (L_3390): loads seg[20].global[5] via LDE 20,5.
			// If TRUE: reads 2 nips (CXG 18,12 x2) and discards (no jump).
			// If FALSE: reads 2 nips (CLP2 5) and jumps (CXG 18,16).
			// NEITHER path modifies seg[20].global[5]. Text flag persists,
			// so text after JF is gated by the preceding test's result.
			{
				int target = getNumber();
				debugC(kDebugScripts, "Angel VM: JF target=%d testResult=%s tf=%s from pos=%d -> pos=%d",
				        target, _lastTestResult ? "T" : "F",
				        _state->_tfIndicator ? "T" : "F",
				        _state->_msgPos, _state->_msgPos + target - 1);
				if (!_lastTestResult) {
					jump(target - 1);
				}
			}
			break;

		case kCSE:
			executeCase();
			break;

		case kFa:
			// Action without reference — nip + kActionOpcodeBase = Operation
			// kFa XJP covers cases 50-93: player verb ops (50-72),
			// edit ops (73-86), and arithmetic edits (90-93).
			// Cases 87-89 (kLessOp-kLEqOp) are no-ops via kFa.
			{
				int opNip = getNip();
				int opVal = opNip + kActionOpcodeBase;
				debugC(kDebugScripts, "Angel VM: kFa opNip=%d op=%d base=%d pos=%d", opNip, opVal, _state->_msgBase, _state->_msgPos);
				if (opVal >= kNumOperations) {
					warning("Angel VM: Unknown action opcode nip=%d op=%d", opNip, opVal);
				} else if (opVal >= kEditOpcodeBase) {
					if (opVal >= kTestOpcodeBase && opVal < (int)kIncrOp) {
						// kLessOp/kEqOp/kLEqOp: no-op via kFa (XJP cases 87-89 → return)
						debugC(kDebugScripts, "Angel VM: kFa test op %d (no-op)", opVal);
					} else {
						executeEdit((Operation)opVal, 0);
					}
				} else {
					executeAction((Operation)opVal, 0);
				}
			}
			break;

		case kFar:
			// Action with reference — p-code proc 93.
			// Stream: opNip(1) + refNip(1), then:
			//   For edit ops 76-86 (kSspOp..kRstOp): XJP dispatch, each case reads
			//     its own inline data.
			//   For action ops 50-72: CXS 12 discriminator (1 nip), then
			//     if discr != 0: CXS 10 (2 nips). Then CLP1 81/83 executes action.
			//   For edit ops 73-75: XJP default = no-op (0 extra nips).
			{
				int opNip = getNip();
				int refNip = getNip();
				int refOp = refNip + kFeOpcodeBase;
				// Entity resolution (proc 35): sets _entityFlag/Value/Op/Type
				resolveEntity(refOp);
				int ref = 0;
				if (refOp < kNumOperations)
					ref = getRefValue((Operation)refOp);
				int opVal = opNip + kActionOpcodeBase;
				debugC(kDebugScripts, "Angel VM: kFar opNip=%d op=%d refNip=%d refOp=%d ref=%d entityFlag=%d entityValue=%d",
				        opNip, opVal, refNip, refOp, ref, _entityFlag ? 1 : 0, _entityValue);

				if (opVal >= (int)kSspOp && opVal <= (int)kRstOp) {
					// Opcodes 76-86: handled by kFar's own XJP dispatch (L_2b3e).
					// Each case reads its own inline data — NOT a uniform discriminator.
					switch ((Operation)opVal) {
					case kSspOp:
						opSsp(ref);
						break;
					case kRsmOp:
						opRsm(ref);
						break;
					case kSwOp: {
						// L_2aa5: CXS 11 = _entityOp, compare with 143.
						// If match: toggle global[3043]. If not: CXS 10 (_entityValue) + NAT_F0 47.
						if (_entityOp == 143) {
							debugC(kDebugScripts, "Angel VM: kFar kSwOp toggle (stub)");
						} else {
							debugC(kDebugScripts, "Angel VM: kFar kSwOp entityOp=%d entityVal=%d (stub)", _entityOp, _entityValue);
						}
						break;
					}
					case kAttrOp: {
						// L_2abd: CXS 12 = _entityType, CXS 10 = _entityValue.
						// Dispatch: NAT_F0 51/52 based on entityType.
						debugC(kDebugScripts, "Angel VM: kFar kAttrOp type=%d val=%d (stub)", _entityType, _entityValue);
						break;
					}
					case kAsgOp: {
						// L_2ace: CXS 12 = _entityType (discriminator), CXS 10 = _entityValue.
						// XJP case 0-3: store _entityValue in the appropriate state var.
						if (_entityFlag) {
							debugC(kDebugScripts, "Angel VM: kFar kAsgOp type=%d val=%d", _entityType, _entityValue);
							switch (_entityType) {
							case 0: _state->_cur.doItToWhat = _entityValue; break;
							case 1: _state->_cur.personNamed = _entityValue; break;
							case 2: _state->_placeNamed = _entityValue; break;
							case 3: _state->_cab = _entityValue; break;
							}
						} else {
							debugC(kDebugScripts, "Angel VM: kFar kAsgOp entityFlag=false (no assign)");
						}
						break;
					}
					case kMovOp: {
						// L_2b34: CXS 10 = _entityValue. NAT_F0 48 = opMove.
						debugC(kDebugScripts, "Angel VM: kFar kMovOp entityVal=%d", _entityValue);
						opMove();
						break;
					}
					case kRstOp:
						// L_2b3a: CPL 94 (proc 94). No stream reads at all.
						opRst(refOp);
						break;
					default:
						// Cases 79 (kAdvOp), 80 (kRecedeOp), 81 (kChzOp),
						// 85 (kPrintOp) → L_2b41 → return (no-op, 0 stream reads).
						debugC(kDebugScripts, "Angel VM: kFar edit op=%d (no-op)", opVal);
						break;
					}
				} else if (opVal >= (int)kTkOffOp && opVal <= (int)kGrantOp) {
					// Action opcodes 50-72 via kFar: P-code proc 93 L_2b43.
					// When CXI 6,6,218 returns FALSE (opcode is NOT an edit op),
					// the handler reads a discriminator nip (CXS 12) to decide
					// the dispatch path:
					//   discr == 0 → CLP1 81 (object action handler)
					//   discr != 0 → CXS 10 (getNumber) + CLP1 83 (person handler)
					int discr = getNip();
					int val = 0;
					if (discr != 0) {
						val = getNumber();
					}
					debugC(kDebugScripts, "Angel VM: kFar action op=%d discr=%d val=%d ref=%d entityType=%d entityValue=%d",
					       opVal, discr, val, ref, _entityType, _entityValue);
					// Execute the action with the already-resolved entity context.
					// opGrant (72) normally reads getNip() for the access right;
					// via kFar the right comes from the discriminator nip instead.
					if (opVal == (int)kGrantOp) {
						_state->_capabilities.set(discr);
						debugC(kDebugScripts, "Angel VM: kFar kGrantOp right=%d", discr);
					} else {
						executeAction((Operation)opVal, ref);
					}
				} else {
					// Edit ops 73-75 via kFar: P-code XJP default = L_2b41
					// (UJP → return). No extra nips consumed beyond the header.
					debugC(kDebugScripts, "Angel VM: kFar edit op=%d (no extra nips)", opVal);
				}
			}
			break;

		case kFt:
			// Test without reference — nip + kTestOpcodeBase = Operation
			// p-code: proc 72. For comparison tests (Less/Eq/LEq), proc 73
			// reads two inline values via proc 58 pattern (each reads 1 nip;
			// if 0, reads getNumber for literal; if non-zero, field ref lookup).
			// Total: 2-6 nips consumed.
			{
				int opNip = getNip();
				int opVal = opNip + kTestOpcodeBase;
				if (opVal == kLessOp || opVal == kEqOp || opVal == kLEqOp) {
					// Comparison tests via proc 73 -> proc 58 pattern.
					// Use the shared helper so comparison decoding matches
					// arithmetic edits (opIncr/opDecr/opAdd/opSub).
					int val1 = readCompValueFromStream();
					int val2 = readCompValueFromStream();
					bool result = false;
					if (opVal == kLessOp)
						result = (val1 < val2);
					else if (opVal == kEqOp)
						result = (val1 == val2);
					else
						result = (val1 <= val2);
					_lastTestResult = result;
					_state->_tfIndicator = result;
					debugC(kDebugScripts, "Angel VM: kFt comparison op=%d val1=%d val2=%d result=%s",
					        opVal, val1, val2, result ? "T" : "F");
				} else {
					// Proc 72 preamble: some tests read getNumber (2 nips)
					// into local[1] before dispatch (set membership check).
					// Others read getNip via their own handler code.
					// kFt path resolves references from the inline stream parameter.
					// NOTE: Do NOT reset _entityType here — P-code proc 72
					// does NOT reset entity type. Tests inherit entity context
					// from preceding kFar/kFtr (OP.md §14.9.2).
					int ref = 0;
					switch (opVal) {
					// Arithmetic edit ops — no preamble data consumed
					case kIncrOp: case kDecrOp: case kAddOp: case kSubOp:
					// No-op tests in kFt context (handler = RPU in P-code)
					case kSupOp: case kVslOp: case kLampOp:
					case kDoorOp: case kLqdOp: case kAnyOp: case kIsOp:
					// Tests using game-state locals (not preamble local[1])
					case kFullOp: case kStuffOp: case kFairOp:
					// Tests with no inline data
					case kDEndOp: case kAskOp: case kCantOp:
					case kTailOp: case kOnTourOp:
					// Tests where handler reads its own nips (not preamble):
					case kHereOp:  // proc 75: getNip(1) + getNumber(2) internally
						break;
					// Tests that read getNip (1 nip) via handler code
					case kWearsOp: case kRandOp: case kCarryOp:
					case kHiddenOp: case kHasOp: case kVKeyOp: case kHoldsOp:
						ref = getNip();
						break;
					// Tests that read getNumber (2 nips) — either in proc 72
					// preamble (stored in local[1]) or in handler (proc 74/75):
					// Preamble: kDarkOp, kLitOp, kFogOp, kOwnsOp, kCanOp,
					//   kOnOp, kInOp, kCvrdOp, kCorpseOp, kKeyOp, kHPassOp,
					//   kWordOp, kSynOp, kNewOp, kBoxOp
					// Handler: kLockedOp, kOpenedOp, kClosedOp (proc 74),
					//   kHereOp (proc 75)
					default:
						ref = getNumber();
						break;
					}
					debugC(kDebugScripts, "Angel VM: kFt test op=%d ref=%d", opVal, ref);
					if (opVal < kNumOperations) {
						executeTest((Operation)opVal, ref);
					} else {
						warning("Angel VM: Unknown test opcode nip=%d op=%d", opNip, opVal);
					}
				}
			}
			break;

		case kFtr:
			// Test with reference — p-code (proc 76):
			//   NAT_F0 32(0, 87)  → getNip + kTestOpcodeBase (1 nip for test opcode)
			//   NAT_F0 32(0, 135) → getNip + kFeOpcodeBase (1 nip for ref operation)
			//   NAT_F0 35         → entity resolution (sets context)
			{
				int opNip = getNip();
				int refNip = getNip();
				int refOp = refNip + kFeOpcodeBase;
				// Entity resolution (proc 35): sets _entityFlag/Value/Op/Type
				resolveEntity(refOp);
				// Also get simple ref value for tests other than testIs
				int ref = 0;
				if (refOp < kNumOperations)
					ref = getRefValue((Operation)refOp);
				int opVal = opNip + kTestOpcodeBase;
				debugC(kDebugScripts, "Angel VM: kFtr opNip=%d op=%d refNip=%d refOp=%d ref=%d entityFlag=%d entityValue=%d entityType=%d",
				        opNip, opVal, refNip, refOp, ref, _entityFlag ? 1 : 0, _entityValue, _entityType);
				if (opVal == kSynOp) {
					// P-code proc 76 case 127: RPU 5 (immediate return, no test).
					// testSyn is a no-op in kFtr context — no nips consumed, result=false.
					_lastTestResult = false;
					_state->_tfIndicator = false;
					debugC(kDebugScripts, "Angel VM: kFtr testSyn no-op (proc 76 RPU 5)");
				} else if (opVal < kNumOperations) {
					executeTest((Operation)opVal, ref);
				} else {
					warning("Angel VM: Unknown test+ref opcode nip=%d op=%d", opNip, opVal);
				}
			}
			break;

		case kFe:
			// Fe (display/reference op) — nip + kFeOpcodeBase(135) = Operation
			// From proc 96 disassembly: NAT_F0 32(0, 135) reads nip + 135.
			// XJP dispatch covers cases 136..164 (kXRegOp..kSpkOp).
			{
				int opNip = getNip();
				int opVal = opNip + kFeOpcodeBase;
				debugC(kDebugScripts, "Angel VM: kFe opNip=%d op=%d base=%d pos=%d", opNip, opVal, _state->_msgBase, _state->_msgPos);
				if (opVal < kNumOperations)
					executeFe((Operation)opVal, 0, true);
				else
					warning("Angel VM: Unknown Fe opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kFer:
			// Fer (display/reference op with ref) — nip + kFeOpcodeBase = Operation
			// From proc 101 disassembly: reads op nip + 135, then ref nip + 135
			// (resolved via getRefValue). Always reads both nips.
			{
				int opNip = getNip();
				int opVal = opNip + kFeOpcodeBase;
				// Always read the reference nip — proc 101 reads both nips
				// unconditionally. Some ops (like kInvOp) ignore the ref value,
				// but the nip must still be consumed to keep stream aligned.
				int refNip = getNip();
				int refOp = refNip + kFeOpcodeBase;
				int ref = 0;
				if (opVal != kInvOp) {
					if (refOp < kNumOperations)
						ref = getRefValue((Operation)refOp);
					else
						warning("Angel VM: Fer ref out of range nip=%d refOp=%d", refNip, refOp);
				} else {
					// For kInvOp, proc 101 doesn't resolve a ref — proc 102
					// reads the next nip as the inventory set type. Since our
					// kFer handler already consumed it as refNip, pass it through.
					ref = refNip;
				}
				debugC(kDebugScripts, "Angel VM: kFer opNip=%d op=%d refNip=%d refOp=%d ref=%d",
				        opNip, opVal, refNip, refOp, ref);
				if (opVal < kNumOperations)
					executeFe((Operation)opVal, ref, false);
				else
					warning("Angel VM: Unknown Fer opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kDisplayDelim:
			// Display boundary marker (nip 34, '#'): no-op control code.
			// Appears as a section/display boundary in the message stream.
			// The original P-code treats this as a control character (nip 34
			// is between EndSym=33 and the punctuation range 35+).
			debugC(kDebugScripts, "Angel VM: kDisplayDelim at pos=%d (no-op)", _state->_msgPos - 1);
			break;

		case kFCall:
			// Procedure call — called message inherits current suppress state.
			// The p-code treats FCall as a subroutine call within the same
			// execution context: suppress is NOT cleared, so text suppression
			// (e.g., from kForceOp during WELCOME) carries into the called msg.
			// CSE content depth is saved and reset: the callee has its own
			// CSE nesting independent of the caller's CSE context.
			//
			// Address encoding: FCall uses 3-nip (18-bit) addressing, NOT the
			// usual 2-nip getNumber(). NAT_F0 6 in the original P-code reads
			// 3 nips to form an 18-bit chunk address (max 262143).
			// Evidence: 2-nip gives invalid chunk addresses for most FCall
			// targets, while 3-nip gives valid message starts consistently.
			{
				int n1 = getNip();
				int n2 = getNip();
				int n3 = getNip();
				int addr = (n1 << 12) | (n2 << 6) | n3;
				debugC(kDebugScripts, "Angel VM: FCall addr=%d depth=%d returnBase=%d returnPos=%d suppress=%d",
				        addr, _callDepth, _state->_msgBase, _state->_msgPos, _suppressText ? 1 : 0);
				if (_callDepth < kMaxCallDepth) {
					CallFrame &frame = _callStack[_callDepth++];
					frame.base = _state->_msgBase;
					frame.pos = _state->_msgPos;
					frame.cursor = _state->_msgCursor;
					frame.length = _state->_msgLength;
					frame.suppressText = _suppressText;
					frame.cseContentDepth = _cseContentDepth;
					_cseContentDepth = 0;
					// P-code FCall handler (L_33c2): CLP2 6 reads 3-nip addr,
					// CLP2 9 pushes call frame (CPG 7, sets global[1509]=3).
					// NEITHER modifies seg[20].global[5] (the text flag).
					// Text flag persists: FCall'd text is gated by the
					// caller's test results (e.g., testSyn FALSE suppresses
					// text in the called message).
					_lastTestResult = true;
					openMsg(addr, "FCall");
				} else {
					warning("Angel VM: Call stack overflow");
				}
			}
			break;

		default:
			// Regular text character — output unless suppressed
			if (ch == '\0') {
				// Unmapped nip value (48-51, 62-63) — skip silently
				break;
			}
			if (_suppressText || !_state->_tfIndicator) {
				// Two-level text suppression:
				// 1. _suppressText: high-level (angel.cpp for ENTRY/WELCOME)
				// 2. _tfIndicator: test-level (seg[20].global[5]). In the
				//    P-code, putChar (CXG 18,5) gates on the text flag.
				//    Opcodes like kIsOp set tf=FALSE → text suppressed.
				//    Opcodes like kNewOp do NOT set tf → text flows.
				break;
			}
			charCount++;

			// kCapOp-based capitalization (for proper nouns like Tepotzteco).
			// Period-based auto-capitalize and auto-spacing after punctuation
			// are handled by the PutChar state machine in angel.cpp.
			if (_capitalizeNext && ch >= 'a' && ch <= 'z') {
				ch = ch - 'a' + 'A';
				_capitalizeNext = false;
			}
			textOutput += ch;
			if (textOutput.size() >= 80) {
				debugC(kDebugScripts, "Angel VM text: %s", textOutput.c_str());
				textOutput.clear();
			}
			_engine->putChar(ch);
			break;
		}
	}
	if (!textOutput.empty()) {
		debugC(kDebugScripts, "Angel VM text: %s", textOutput.c_str());
	}
	debugC(kDebugScripts, "Angel VM: executeMsg ending, charCount=%d eom=%d", charCount, _state->_eom);
}

void VM::executeCase() {
	// CSE interleaved content format (verified from proc 103 p-code + empirical data):
	//
	// Header:
	//   type: getNip (KindOfCase: 0=Random, 1=Word, 2=Syn, 3=Ref)
	//   [matchRef: getNip + 135 — only for RefCase]
	//   nbrCases: getNip
	//   totalSize: getNumber (nips from here to end of CSE block)
	//
	// Entries (non-random): nbrCases × [val: getNumber, skip: getNumber, content...]
	// Entries (random):     nbrCases × [skip: getNumber, content...]
	//
	// Content is skip-1 nips, ending with EndSym. Offsets are 1-based.
	// For non-random: val=0 is unconditional default (always matches).
	//
	// Match flow (proc 103 p-code at L_331f):
	//   Matched: NAT_F1 8 skips past getNumber+jump → skip NOT consumed in loop.
	//   Epilogue (L_332d): 2×CXG 18,12 consume skip nips, NAT_F0 66 executes content.
	//   Unmatched: skip=getNumber, CXG 18,16(skip) jumps past content (1-based).

	// P-code proc 103 at 0x3148: XJP dispatches on case 0..3 only.
	// Values outside that range fall through to L_314b with local[6]=0
	// (initial value = type 0 / kRandomCase / direct-index).
	// The header (nbrCases + totalSize) is still consumed.
	// In practice, invalid caseType values appear when the stream
	// position reaches overlapping message data (e.g., another message's
	// header). The type 0 handling with large skip values eventually
	// jumps past the message end, causing normal EOM termination.
	int caseType = getNip();
	bool validCaseType = (caseType >= 0 && caseType <= kRefCase);

	if (!validCaseType) {
		// Invalid caseType: consume header nips (nbrCases + totalSize)
		// and jump to endPos (clamped to message bounds).
		// P-code type 0 loop would iterate with garbage skip values
		// that exceed message length, so this is equivalent.
		int nbrCases = getNip();
		int totalSize = getNumber();
		// totalSize is counted from the first nip after the size field to the
		// first nip *after* the CSE block. Landing on the last nib inside the
		// block leaks one stray character/control nib into the parent stream.
		int endPos = _state->_msgPos + totalSize;
		debugC(kDebugScripts, "Angel VM: CSE unknown caseType %d (nbrCases=%d totalSize=%d endPos=%d) base=%d pos=%d — skipping",
		        caseType, nbrCases, totalSize, endPos, _state->_msgBase, _state->_msgPos);
		// Clamp endPos to message bounds to prevent reading past the file
		if (endPos > _state->_msgLength + 2)
			endPos = _state->_msgLength + 2;
		jumpTo(endPos);
		return;
	}

	KindOfCase kind = (KindOfCase)caseType;

	// RefCase: matchRef = getNip + 135 (proc 103 at L_31BF: NAT_F0 32(0,135))
	int matchRef = 0;
	if (kind == kRefCase) {
		matchRef = getNip() + kFeOpcodeBase;
	}

	int nbrCases = getNip();
	int totalSize = getNumber();
	// totalSize is the distance to the first nip after the CSE block, not to
	// the last nip inside it. Using -1 leaves the final block nib live and
	// causes garbage like the trailing "A U tg" seen in DOS msg 74 paths.
	int endPos = _state->_msgPos + totalSize;

	// Determine match value based on case type
	int matchValue = 0;
	{
		switch (kind) {
		case kRandomCase:
			matchValue = _engine->getRandom(nbrCases);
			break;

		case kWordCase:
			matchValue = _state->_verb;
			break;

		case kSynCase:
			matchValue = _state->_verb;
			break;

		case kRefCase:
			// P-code proc 103 at L_3156: calls resolveEntity(matchRef) to set
			// entity context, then uses _entityValue (via CXS 10) for matching.
			resolveEntity(matchRef);
			matchValue = _entityValue;
			break;

		default:
			break;
		}
	}

	debugC(kDebugScripts, "Angel VM: CSE type=%d nbrCases=%d matchValue=%d matchRef=%d totalSize=%d pos=%d base=%d",
	       caseType, nbrCases, matchValue, matchRef, totalSize, _state->_msgPos, _state->_msgBase);

	// CSE entry debug for broken messages
	if (_state->_msgBase == 30 || _state->_msgBase == 29 || _state->_msgBase == 3836) {
		int savedPos = _state->_msgPos;
		int savedCursor = _state->_msgCursor;
		Common::String nipStr;
		for (int d = 0; d < 20 && !_state->_eom; d++) {
			int rawNip = getNip();
			nipStr += Common::String::format("%d ", rawNip);
		}
		debugC(kDebugScripts, "Angel VM: CSE@%d entry nips: %s", savedPos, nipStr.c_str());
		_state->_msgPos = savedPos;
		_state->_msgCursor = savedCursor;
		_state->_vmCurRecord = _data->readChunk(
			_state->_msgBase + (_state->_msgPos / kChunkSize));
	}

	bool matched = false;
	for (int i = 0; i < nbrCases && !matched; i++) {
		// RandomCase entries: no val, just skip + content.
		// Non-random entries: val=getNumber, skip + content.
		int caseValue = 0;
		if (kind != kRandomCase) {
			caseValue = getNumber();
		}

		bool isMatch;
		if (kind == kRandomCase) {
			isMatch = (i == matchValue);
		} else if (caseValue == 0) {
			isMatch = true;  // val=0 is unconditional default (proc 103 at L_31B2)
		} else if (kind == kRefCase) {
			// RefCase: caseValue is a vocab index. Look up the entity reference
			// from the vocab table and compare with matchValue (proc 103 at L_31B9:
			// CXS 12 resolves Vocab[caseValue].ve.ref).
			if (caseValue > 0 && caseValue <= _data->_nbrVWords) {
				isMatch = (_data->_vocab[caseValue].ve.ref == matchValue);
				debugC(kDebugScripts, "Angel VM: RefCase entry[%d] vocabIdx=%d ref=%d vs matchValue=%d → %s",
				       i, caseValue, _data->_vocab[caseValue].ve.ref, matchValue, isMatch ? "MATCH" : "no");
			} else {
				isMatch = false;
			}
		} else if (kind == kSynCase) {
			// SynCase: caseValue is a vocab index. The p-code tests caseValue IN
			// synonymSet (proc 103 at L_3294). Match by checking if caseValue's
			// canonical verb reference AND word type match the player verb's.
			// Type check prevents cross-type false matches (e.g. "cylinder" ref=2
			// vs "east" ref=2 where one is an object and the other a direction).
			if (caseValue > 0 && caseValue <= _data->_nbrVWords &&
			    matchValue > 0 && matchValue <= _data->_nbrVWords) {
				isMatch = (_data->_vocab[caseValue].ve.ref == _data->_vocab[matchValue].ve.ref &&
				           _data->_vocab[caseValue].ve.vType == _data->_vocab[matchValue].ve.vType);
				debugC(kDebugScripts, "Angel VM: SynCase entry[%d] vocabIdx=%d ref=%d type=%d vs verb=%d ref=%d type=%d → %s",
				       i, caseValue, _data->_vocab[caseValue].ve.ref, _data->_vocab[caseValue].ve.vType,
				       matchValue, _data->_vocab[matchValue].ve.ref, _data->_vocab[matchValue].ve.vType,
				       isMatch ? "MATCH" : "no");
			} else {
				isMatch = (caseValue == matchValue);
			}
		} else {
			// WordCase or unknown: direct equality for now.
			isMatch = (caseValue == matchValue);
		}

		if (isMatch) {
			matched = true;

			// Matched: consume skip to determine content boundary.
			// Proc 103 epilogue (L_332d): 2×CXG 18,12 consume skip before content.
			int caseSkip = getNumber();
			int contentStart = _state->_msgPos;

			debugC(kDebugScripts, "Angel VM: CSE matched entry[%d] val=%d at pos=%d skip=%d",
			       i, caseValue, _state->_msgPos, caseSkip);

			// Execute content until EndSym at content boundary.
			// Temporarily adjust _msgLength so the standard EndSym check
			// (pos >= len+2) terminates at the case-ending EndSym, while
			// internal EndSym (section breaks) continue normally.
			// Content spans skip-1 nips: contentStart to contentStart+skip-2.
			// Case-ending EndSym is at contentStart+skip-2.
			// After reading it, pos = contentStart+skip-1.
			// Set len+2 = contentStart+skip-1 → len = contentStart+skip-3.
			int savedLength = _state->_msgLength;
			_state->_msgLength = contentStart + caseSkip - 3;
			_cseContentDepth++;
			int safety = 0;
			while (!_state->_eom && _state->_stillPlaying && safety < 500) {
				executeMsg();
				safety++;
			}
			_cseContentDepth--;
			_state->_msgLength = savedLength;

			// Jump to endPos to skip remaining entries (proc 103 at L_3338)
			if (_state->_msgPos < endPos)
				jumpTo(endPos);
			_state->_eom = false;
		} else {
			// Unmatched: read skip, jump past content (1-based offset).
			// Proc 103 at L_3324: skip=getNumber, CXG 18,16(skip) = jump(skip-1).
			int skip = getNumber();
			debugC(kDebugScripts, "Angel VM: CSE entry[%d] val=%d != matchValue=%d, skip=%d",
			       i, caseValue, matchValue, skip);
			jump(skip - 1);
		}
	}

	// P-code proc 103 L_3338: after both match and no-match, the CSE
	// resets the putChar state (intermediate[1][1] := 32 = space) and
	// jumps to endPos if not already past it.
	if (_state->_msgPos < endPos) {
		if (!matched) {
			debugC(kDebugScripts, "Angel VM: CSE no match, pos=%d → endPos=%d",
			       _state->_msgPos, endPos);
		}
		jumpTo(endPos);
	}
}

// ============================================================
// Action dispatch
// ============================================================

void VM::executeAction(Operation op, int ref) {
	switch (op) {
	// Data/definition ops (0-25) are no-ops at runtime — used during game initialization
	case kSizeOp:
	case kValueOp:
	case kPropsOp:
	case kMapOp:
	case kCastOp:
	case kEndOp:
	case kPListOp:
	case kStateOp:
	case kDescrOp:
	case kALockOp:
	case kMHaveOp:
	case kMoodOp:
	case kChgOp:
	case kFbdOp:
	case kProbOp:
	case kCCarryOp:
	case kDirOp:
	case kRouteOp:
	case kVocabOp:
	case kTransOp:
	case kRestrOp:
	case kPDropOp:
	case kCmdOp:
	case kEOFOp:
	case kUnlkdOp:
	case kCurbOp:
		// These are data definition opcodes, no action needed at runtime
		debugC(kDebugScripts, "Angel VM: Data definition opcode %d (no-op)", (int)op);
		break;

	case kPrintOp:    opPrint(ref); break;
	case kDscOp:      opDsc(ref); break;
	case kAOp:        opAOp(); break;
	case kInvOp:      opInv(); break;
	case kSpkOp:      opSpk(ref); break;
	case kCapOp:      opCap(ref); break;
	case kForceOp:    opForce(ref); break;

	case kTakeOp:     opTake(); break;
	case kDropOp:     opDrop(); break;
	case kWearOp:     opWear(); break;
	case kShedOp:     opShed(); break;
	case kPkUpOp:
		// Physical pickup action. In the original p-code (proc 12), this also
		// manages text suppression via CXS 9, but our action implementations
		// don't produce text output, so no suppression is needed.
		opTake();
		break;
	case kDrpOp:      opDrp(); break;
	case kThrowOp:    opThrow(); break;
	case kPourOp:     opPour(); break;
	case kPutOp:      opPutOp(); break;
	case kOpenOp:     opOpen(); break;
	case kCloseOp:    opClose(); break;
	case kLockOp:     opLock(); break;
	case kUnlockOp:   opUnlock(); break;
	case kKillOp:     opKill(); break;
	case kGiveOp:     opGive(); break;
	case kGrabOp:     opGrab(); break;
	case kSwapOp:     opSwap(); break;
	case kGrantOp:    opGrant(); break;
	case kTkOffOp:    opTkOff(); break;
	case kPtOnOp:
		// Physical wear/put-on action. In the original p-code (proc 15), this
		// also manages text suppression via CXS 9, but our action implementations
		// don't produce text output, so no suppression is needed.
		opWear();
		break;
	case kTossOp:     opToss(); break;
	case kTrashOp:    opTrash(); break;

	case kMoveOp:     opMove(); break;
	case kEntryOp:    opEntry(); break;
	case kRideOp:     opRide(); break;
	case kRLocOp:     opRLoc(ref); break;
	case kNxStopOp:   opNxStop(); break;
	case kOfferOp:    opOffer(); break;
	case kTourOp:     opTour(); break;
	case kRRideOp:    opRRide(); break;
	case kTradeOp:    opTrade(); break;
	case kGreetOp:    opGreet(); break;
	case kGiftOp:     opGift(); break;
	case kSecretOp:   opSecret(); break;
	case kBluffOp:    opBluff(); break;
	case kCurseOp:    opCurse(); break;
	case kWelcomeOp:  opWelcome(); break;
	case kMrdrOp:     opMrdr(); break;

	case kOpnItOp:    opOpnIt(); break;
	case kClsItOp:    opClsIt(); break;
	case kLkItOp:     opLkIt(); break;
	case kUnLkItOp:   opUnLkIt(); break;
	case kPutItOp:    opPutIt(); break;
	case kPourItOp:   opPourIt(); break;

	case kSaveOp:     opSave(); break;
	case kQuitOp:     opQuit(); break;
	case kRestartOp:  opRestart(); break;

	case kNoOp:       break;  // Do nothing

	default:
		warning("Angel VM: Unimplemented action opcode %d", (int)op);
		break;
	}
}

// ============================================================
// Test dispatch
// ============================================================

void VM::executeTest(Operation op, int ref) {
	bool result = false;

	switch (op) {
	case kHereOp:     result = testHere(ref); break;
	case kOwnsOp:     result = testOwns(ref); break;
	case kWearsOp:    result = testWears(ref); break;
	case kHasOp:      result = testHas(ref); break;
	case kOnOp:       result = testOn(ref); break;
	case kInOp:       result = testIn(ref); break;
	case kFullOp:     result = testFull(ref); break;
	case kLockedOp:   result = testLocked(ref); break;
	case kOpenedOp:   result = testOpened(ref); break;
	case kClosedOp:   result = testClosed(ref); break;
	case kCvrdOp:     result = testCvrd(ref); break;
	case kDarkOp:     result = testDark(); break;
	case kLitOp:      result = testLit(); break;
	case kFogOp:      result = testFog(); break;
	case kDoorOp:     result = testDoor(ref); break;
	case kBoxOp:      result = testBox(ref); break;
	case kVslOp:      result = testVsl(ref); break;
	case kSupOp:      result = testSup(ref); break;
	case kLampOp:     result = testLamp(ref); break;
	case kCorpseOp:   result = testCorpse(ref); break;
	case kLqdOp:      result = testLqd(ref); break;
	case kHiddenOp:   result = testHidden(ref); break;
	case kStuffOp:    result = testStuff(ref); break;
	case kDEndOp:     result = testDEnd(); break;
	case kKeyOp:      result = testKey(ref); break;
	case kHPassOp:    result = testHPass(ref); break;
	case kVKeyOp:     result = testVKey(ref); break;
	case kCanOp:      result = testCan(ref); break;
	case kCantOp:     result = testCant(ref); break;
	case kRandOp:     result = testRand(ref); break;
	case kAskOp:      result = testAsk(); break;
	case kAnyOp:      result = testAny(ref); break;
	case kWordOp:     result = testWord(ref); break;
	case kSynOp:      result = testSyn(ref); break;
	case kNewOp:      result = testNew(ref); break;
	case kHoldsOp:    result = testHolds(ref); break;
	case kIsOp:       result = testIs(ref); break;
	case kFairOp:     result = testFair(ref); break;
	case kCarryOp:    result = testCarry(ref); break;
	case kTailOp:     result = testTail(); break;
	case kOnTourOp:   result = testOnTour(); break;
	case kLessOp:     result = testLess(ref); break;
	case kEqOp:       result = testEq(ref); break;
	case kLEqOp:      result = testLEq(ref); break;

	default:
		warning("Angel VM: Unimplemented test opcode %d", (int)op);
		break;
	}

	// All tests set _lastTestResult for JF branching (UCSD boolean stack).
	_lastTestResult = result;

	// kNewOp does NOT set the text flag (seg[20].global[5]) — P-code proc 76
	// only stores the result to global[5] for kIsOp (case 130), not kNewOp (128).
	// All other tests DO set the text flag.
	if (op != kNewOp)
		_state->_tfIndicator = result;
}

// ============================================================
// Edit dispatch
// ============================================================

void VM::executeEdit(Operation op, int ref) {
	switch (op) {
	case kTickOp:     opTick(ref); break;
	case kEventOp:    opEvent(ref); break;
	case kSetOp:      opSet(ref); break;
	case kSspOp:      opSsp(ref); break;
	case kRsmOp:      opRsm(ref); break;
	case kSwOp:       opSw(ref); break;
	case kAdvOp:      opAdv(); break;
	case kRecedeOp:   opRecede(); break;
	case kChzOp:      opChz(ref); break;
	case kAttrOp:     opAttr(ref); break;
	case kAsgOp:      opAsg(ref); break;
	case kMovOp:      opMov(ref); break;
	case kPrintOp:    opPrint(ref); break;
	case kRstOp:      opRst(ref); break;
	case kIncrOp:     opIncr(ref); break;
	case kDecrOp:     opDecr(ref); break;
	case kAddOp:      opAdd(ref); break;
	case kSubOp:      opSub(ref); break;

	default:
		warning("Angel VM: Unimplemented edit opcode %d", (int)op);
		break;
	}
}

// ============================================================
// Fe/Fer dispatch (base 135 — reference/display ops)
// ============================================================
//
// From proc 96 (kFe handler) disassembly, XJP case 136..164:
//   136 kXRegOp  → NAT_F0 56, NAT_F0 42  (xReg manipulation)
//   137-143      → no-op (kVerbOp..kSunOp: reference values, no action)
//   144 kCtntsOp → L_2e98  (contents display, complex)
//   145 kCtnrOp  → CPL 99  (container display)
//   146-153      → no-op (kLocOp..kPPrvOp: reference values)
//   154 kTimeOp  → NAT_F0 43  (display time)
//   155 kDayOp   → no-op
//   156 kDscOp   → CPI 3,5(0,1) + CPL 100 = getNumber + displayMsg
//   157 kAOp     → CPI 3,5(0,1) + display article
//   158 kInvOp   → no-op via kFe (kFer handles it differently)
//   159 kFleetOp → CPL 98 (fleet display)
//   160 kRoleOp  → CPL 97 (role display)
//   161 kCapOp   → CXG 18,18(1) = set capitalize flag
//   162 kCntrOp  → CXG 18,7 + push 64 + CXG 18,5
//   163 kForceOp → CXG 18,9 + CXG 18,7 + CXG 18,8 + CXG 18,18(0)
//   164 kSpkOp   → same as kForceOp but no CXG 18,8

void VM::executeFe(Operation op, int ref, bool fromFe) {
	switch (op) {
	case kCapOp:
		// P-code: CXG 18,18(1) = PutItem(TRUE).
		// In the original, PutItem(TRUE) reads characters and capitalizes
		// ALL lowercase letters until EndSym. In our architecture, we set
		// _capitalizeNext for the first letter (period-based auto-capitalize
		// handles subsequent ones like in "D.C.").
		// Only unsuppress if _baseSuppressText is false — during WELCOME init,
		// _baseSuppressText=true and kCapOp should NOT unsuppress.
		_capitalizeNext = true;
		if (!_baseSuppressText)
			_suppressText = false;
		debugC(kDebugScripts, "Angel VM: Fe kCapOp → capitalize next char, unsuppress=%d",
		       !_baseSuppressText ? 1 : 0);
		break;

	case kForceOp: {
		// P-code: CXG 18,9 (EndSpeak) + CXG 18,7 (ForceQ) + CXG 18,8 (blank line) + CXG 18,18(0).
		// EndSpeak: double ForceQ + set PutChar state=3 (capitalize first letter).
		// CXG 18,8 outputs a blank line (only kForceOp, not kSpkOp).
		// CXG 18,9 also re-evaluates the text flag.
		//
		// Only produce visible output (endSpeak, outLn) when text was NOT
		// already suppressed at entry. During WELCOME init, kForceOp fires
		// in suppressed code sections — those should not produce blank lines.
		bool wasSuppressed = _suppressText;
		if (!wasSuppressed) {
			_engine->endSpeak();
			_engine->forceOutput();
			_engine->outLn();    // CXG 18,8: blank line separator
		}
		_capitalizeNext = false;  // PutChar state 3 handles capitalization now
		_suppressText = _baseSuppressText;  // Re-evaluate text flag (CXG 18,9)
		debugC(kDebugScripts, "Angel VM: Fe kForceOp → wasSuppressed=%d, suppress=%d (base)", wasSuppressed, _baseSuppressText);
		break;
	}

	case kSpkOp: {
		// P-code: CXG 18,9 (EndSpeak) + CXG 18,7 (ForceQ) + CXG 18,18(0).
		// Same as kForceOp but WITHOUT CXG 18,8 (no blank line).
		bool wasSuppressed = _suppressText;
		if (!wasSuppressed) {
			_engine->endSpeak();
			_engine->forceOutput();
		}
		_capitalizeNext = false;  // PutChar state 3 handles capitalization now
		_suppressText = _baseSuppressText;
		debugC(kDebugScripts, "Angel VM: Fe kSpkOp ref=%d → wasSuppressed=%d, suppress=%d", ref, wasSuppressed, _baseSuppressText);
		break;
	}

	case kDscOp:
		// Display a description message. Via kFe: reads getNumber() for address.
		// Via kFer: ref is an entity VALUE (location/object/person number),
		// NOT a message address. Convert to description address via .n field.
		// Uses descriptionOnly mode: stops at first EndSym section break,
		// displaying only the description section (not command response scripts).
		// Original P-code CPL 100 likewise only runs the description section.
		{
			int addr = ref;
			if (fromFe) {
				// kFe path: read address from stream (CPI 3,5(0,1) = getNumber)
				addr = getNumber();
			} else {
				// kFer path: ref is entity value. Look up message address
				// from the entity's .n field based on entity type context.
				if (_entityType == 2 && ref > 0 && ref <= _data->_nbrLocations)
					addr = _data->_map[ref].n;
				else if (_entityType == 0 && ref > 0 && ref <= _data->_nbrObjects)
					addr = _data->_props[ref].n;
				else if (_entityType == 1 && ref > 0 && ref <= _data->_castSize)
					addr = _data->_cast[ref].n;
			}
			debugC(kDebugScripts, "Angel VM: Fe kDscOp addr=%d ref=%d entityType=%d", addr, ref, _entityType);
			if (addr > 0) {
				const bool savedDescribedActive = _describedEntityActive;
				const Operation savedDescribedOp = _describedEntityOp;
				const int savedDescribedType = _describedEntityType;
				const int savedDescribedValue = _describedEntityValue;

				if (!fromFe) {
					switch (_entityType) {
					case 0:
						_describedEntityActive = true;
						_describedEntityOp = kObjOp;
						_describedEntityType = 0;
						_describedEntityValue = ref;
						break;
					case 1:
						_describedEntityActive = true;
						_describedEntityOp = kPersonOp;
						_describedEntityType = 1;
						_describedEntityValue = ref;
						break;
					case 2:
						_describedEntityActive = true;
						_describedEntityOp = kLocOp;
						_describedEntityType = 2;
						_describedEntityValue = ref;
						break;
					case 3:
						_describedEntityActive = true;
						_describedEntityOp = kVclOp;
						_describedEntityType = 3;
						_describedEntityValue = ref;
						break;
					default:
						break;
					}
				}

				displayMsg(addr, true);
				_describedEntityActive = savedDescribedActive;
				_describedEntityOp = savedDescribedOp;
				_describedEntityType = savedDescribedType;
				_describedEntityValue = savedDescribedValue;
			}
		}
		break;

	case kAOp:
		// Print article "a"/"an" (or "the"). Via kFe: reads getNumber.
		// Via kFer: uses ref.
		{
			int target = ref;
			if (fromFe) {
				// kFe path: read from stream
				target = getNumber();
			}
			// TODO: implement proper a/an/the logic based on target entity
			debugC(kDebugScripts, "Angel VM: Fe kAOp target=%d ref=%d (article stub)", target, ref);
		}
		break;

	case kPrintOp:
		// Print a numbered message (same as action kPrintOp)
		{
			int addr = ref;
			if (fromFe) {
				addr = getNumber();
			}
			debugC(kDebugScripts, "Angel VM: Fe kPrintOp addr=%d", addr);
			if (addr > 0)
				displayMsg(addr);
		}
		break;

	case kInvOp: {
		// Inventory item listing — P-code proc 102.
		// ref = set type nip (consumed by kFer handler as refNip).
		// P-code: CPI 3,32(0,87) computes 87 + nip for XJP dispatch:
		//   case 129 (nip 42): carried in hands = possessions \ wearing
		//   case 117 (nip 30): worn on person = wearing set
		int setCase = 87 + ref;
		debugC(kDebugScripts, "Angel VM: Fe kInvOp ref=%d setCase=%d", ref, setCase);

		Common::Array<int> items;
		for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
			if (setCase == 129) {
				// Carried in hands: possessions but NOT wearing
				if (_state->_possessions.has(obj) && !_state->_wearing.has(obj))
					items.push_back(obj);
			} else if (setCase == 117) {
				// Worn on person: wearing set
				if (_state->_wearing.has(obj))
					items.push_back(obj);
			}
		}

		// Set truth flag for JF branching: true if any items found.
		_state->_tfIndicator = !items.empty();
		debugC(kDebugScripts, "Angel VM: Fe kInvOp setCase=%d found %u items, tf=%d",
		       setCase, items.size(), _state->_tfIndicator ? 1 : 0);

		// Format and display item list (proc 63 equivalent).
		// Output: "the X, the Y and the Z"
		if (!_suppressText && !items.empty()) {
			for (uint i = 0; i < items.size(); i++) {
				Common::String name = _engine->parser()->getWordName(
					_data->_props[items[i]].oName);
				if (name.empty())
					continue;

				if (i == 0) {
					_engine->putWord("the ");
				} else if (i == items.size() - 1) {
					_engine->putWord(" and the ");
				} else {
					_engine->putWord(", the ");
				}
				_engine->putWord(name.c_str());
			}
		}
		break;
	}

	case kTimeOp:
		// Display current time
		if (!_suppressText) {
			char timeBuf[32];
			snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s",
			         _state->_clock.hour, _state->_clock.minute,
			         _state->_clock.am ? "AM" : "PM");
			_engine->putWord(timeBuf);
		}
		break;

	case kXRegOp: {
		// P-code: CLP2 56 (read field ref nip + lookup value) then CLP2 42 (intToASCII).
		// Stream: 1 nip (field reference via proc 56's CXG 18,15).
		int fieldRef = getNip();
		int value = lookupFieldValue(fieldRef);
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", value);
		_engine->putWord(buf);
		debugC(kDebugScripts, "Angel VM: Fe kXRegOp fieldRef=%d value=%d", fieldRef, value);
		break;
	}

	case kCtntsOp:
		// Display container contents
		debugC(kDebugScripts, "Angel VM: Fe kCtntsOp ref=%d (stub)", ref);
		break;

	case kCtnrOp:
		// Display container name
		debugC(kDebugScripts, "Angel VM: Fe kCtnrOp ref=%d (stub)", ref);
		break;

	case kFleetOp:
		debugC(kDebugScripts, "Angel VM: Fe kFleetOp ref=%d (stub)", ref);
		break;

	case kRoleOp: {
		// Role display (proc 97): describes entities at current location.
		// P-code: CXG 18,9 (endSpeak) + CXG 18,7 (forceQ) + CPI 4,3 (read param).
		// The inline parameter is an entity type/index consumed from the stream.
		//
		// Entity messages have two sections: [description] @ [response logic].
		// In describe mode: execute description, EndSym stops at section break.
		// In respond mode: skip description section, execute response logic.
		int roleParam = getNip();
		_engine->endSpeak();
		_engine->forceQ();

		if (_respondMode) {
			// Skip forward past the next EndSym to reach the response section.
			// Scan nips until we find EndSym (kEndSym = '@' = 64 in yTable).
			debugC(kDebugScripts, "Angel VM: Fe kRoleOp ref=%d param=%d RESPOND → skipping to response section", ref, roleParam);
			int safety = 0;
			while (!_state->_eom && safety < 5000) {
				char ch = getAChar();
				if (ch == kEndSym)
					break;
				safety++;
			}
			// Now positioned after the EndSym — response logic follows.
		} else {
			debugC(kDebugScripts, "Angel VM: Fe kRoleOp ref=%d param=%d (endSpeak+forceQ)", ref, roleParam);
		}
		break;
	}

	case kCntrOp:
		// Counter display
		debugC(kDebugScripts, "Angel VM: Fe kCntrOp ref=%d (stub)", ref);
		break;

	case kNoOp:
		break;

	default:
		// Reference value ops (kVerbOp..kSunOp, kLocOp..kPPrvOp, kDayOp).
		// When used via kFe, these resolve the entity AND display its name.
		// P-code: proc 96 L_2edd → NAT_F0 35 (resolveEntity) + CXG 17,2 +
		// XJP on entityType (0=object, 1=person, 2=location, 3=vehicle) +
		// L_2f41: if name found, display via CXG 18,6 (putWord).
		if (op >= kPassOp && op < kNumOperations) {
			resolveEntity(op);
			if (_entityFlag) {
				Common::String name;
				bool useThe = false;
				switch (_entityType) {
				case 0: // object
					if (_entityValue > 0 && _entityValue <= _data->_nbrObjects) {
						name = _engine->parser()->getWordName(_data->_props[_entityValue].oName);
						useThe = _data->_props[_entityValue].useThe;
					}
					break;
				case 1: // person
					if (_entityValue > 0 && _entityValue <= _data->_castSize) {
						name = _engine->parser()->getWordName(_data->_cast[_entityValue].pName);
						useThe = _data->_cast[_entityValue].useThe;
					}
					break;
				case 2: // location
					if (_entityValue > 0 && _entityValue <= _data->_nbrLocations) {
						name = _engine->parser()->getWordName(_data->_map[_entityValue].shortDscr);
						useThe = _data->_map[_entityValue].useThe;
					}
					break;
				case 3: // vehicle
					if (_entityValue > 0 && _entityValue <= _data->_nbrVehicles) {
						name = _engine->parser()->getWordName(_data->_fleet[_entityValue].vName);
						useThe = _data->_fleet[_entityValue].useThe;
					}
					break;
				case 6: // verb — use _state->_verb (vocab index) for name lookup
					if (_state->_verb > 0 && _state->_verb < _data->_nbrVWords)
						name = _engine->parser()->getWordName(_state->_verb);
					break;
				default:
					break;
				}
				if (!name.empty()) {
					if (!_suppressText) {
						// Consume _capitalizeNext here so that Fe entity
						// names get capitalized at message start. Without
						// this, _capitalizeNext leaks past the entity name
						// and capitalises the wrong character.
						if (useThe) {
							if (_capitalizeNext) {
								_engine->putWord("The ");
								_capitalizeNext = false;
							} else {
								_engine->putWord("the ");
							}
						}
						for (uint i = 0; i < name.size(); i++) {
							char ch = name[i];
							if (i == 0 && !useThe && _capitalizeNext
							    && ch >= 'a' && ch <= 'z') {
								ch = ch - 'a' + 'A';
								_capitalizeNext = false;
							}
							_engine->putChar(ch);
						}
					}
					debugC(kDebugScripts, "Angel VM: Fe ref-value op %d → %s '%s%s'",
					        (int)op, _suppressText ? "suppressed" : "displayed",
					        useThe ? "the " : "", name.c_str());
				} else {
					int nameIdx = 0;
					if (_entityType == 0 && _entityValue > 0 && _entityValue <= _data->_nbrObjects)
						nameIdx = _data->_props[_entityValue].oName;
					else if (_entityType == 1 && _entityValue > 0 && _entityValue <= _data->_castSize)
						nameIdx = _data->_cast[_entityValue].pName;
					else if (_entityType == 2 && _entityValue > 0 && _entityValue <= _data->_nbrLocations)
						nameIdx = _data->_map[_entityValue].shortDscr;
					else if (_entityType == 3 && _entityValue > 0 && _entityValue <= _data->_nbrVehicles)
						nameIdx = _data->_fleet[_entityValue].vName;
					debugC(kDebugScripts, "Angel VM: Fe ref-value op %d type=%d val=%d nameIdx=%d nbrVWords=%d → no name",
					        (int)op, _entityType, _entityValue, nameIdx, _data->_nbrVWords);
				}
			} else {
				debugC(kDebugScripts, "Angel VM: Fe ref-value op %d → entity not resolved", (int)op);
			}
		} else {
			warning("Angel VM: Unknown Fe opcode %d ref=%d", (int)op, ref);
		}
		break;
	}
}

// ============================================================
// Action implementations
// ============================================================

void VM::opPrint(int ref) {
	// Display a numbered message
	debugC(kDebugScripts, "Angel VM: opPrint(%d)", ref);
	if (ref > 0)
		displayMsg(ref);
}

void VM::opDsc(int ref) {
	// Describe a location or object
	// ref = description key (n field of Place or Object)
	debugC(kDebugScripts, "Angel VM: opDsc(%d)", ref);
	if (ref > 0)
		displayMsg(ref);
}

void VM::opAOp() {
	debugC(kDebugScripts, "Angel VM: opAOp not correctly implemented - missing vowel check for a/an");
}

void VM::opInv() {
	debugC(kDebugScripts, "Angel VM: opInv not correctly implemented - needs vocab name lookup");
}

void VM::opSpk(int ref) {
	debugC(kDebugScripts, "Angel VM: opSpk(%d) not implemented", ref);
}

void VM::opCap(int ref) {
	debugC(kDebugScripts, "Angel VM: opCap(%d) not implemented (via action dispatch)", ref);
}

void VM::opForce(int ref) {
	// Force output queue flush
	_engine->forceOutput();
}

void VM::opTake() {
	// Player takes an object — adds to possessions.
	// The script is responsible for testing preconditions (kHereOp etc.)
	// via kFt/JF before calling this action. We just execute the state change.
	int obj = _state->_cur.doItToWhat;
	debugC(kDebugScripts, "Angel VM: opTake/opPkUp obj=%d loc=%d", obj, _state->_location);
	if (obj > 0 && obj <= _data->_nbrObjects) {
		// Remove from location if present
		_data->_map[_state->_location].objects.unset(obj);
		_state->_possessions.set(obj);
		_state->_nbrPossessions++;
		debugC(kDebugScripts, "Angel VM: opTake SUCCESS - player now has obj %d", obj);
	}
}

void VM::opDrop() {
	// Player drops an object
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && obj <= _data->_nbrObjects) {
		if (_state->_possessions.has(obj)) {
			_state->_possessions.unset(obj);
			_data->_map[_state->_location].objects.set(obj);
			_state->_nbrPossessions--;
		}
	}
}

void VM::opWear() {
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && _state->_possessions.has(obj)) {
		_state->_wearing.set(obj);
	}
}

void VM::opShed() {
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && _state->_wearing.has(obj)) {
		_state->_wearing.unset(obj);
	}
}

void VM::opPkUp() { opTake(); }       // alias
void VM::opDrp() { opDrop(); }        // alias

void VM::opThrow() {
	// Throw = drop + extra effect
	opDrop();
}

void VM::opPour() {
	// Pour = drop liquid
	opDrop();
}

void VM::opPutOp() {
	// Put object in/on container
	int obj = _state->_cur.doItToWhat;
	int container = _state->_cur.withWhat;
	if (obj > 0 && container > 0) {
		if (_state->_possessions.has(obj)) {
			_state->_possessions.unset(obj);
			_state->_nbrPossessions--;
			_data->_props[obj].inOrOn = container;
			_data->_props[container].contents.set(obj);
		}
	}
}

void VM::opOpen() {
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && obj <= _data->_nbrObjects) {
		_data->_props[obj].itsOpen = true;
		_data->_props[obj].itsLocked = false;
		_state->recomputeSets();
	}
}

void VM::opClose() {
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && obj <= _data->_nbrObjects) {
		_data->_props[obj].itsOpen = false;
		_state->recomputeSets();
	}
}

void VM::opLock() {
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && obj <= _data->_nbrObjects) {
		_data->_props[obj].itsLocked = true;
		_data->_props[obj].itsOpen = false;
	}
}

void VM::opUnlock() {
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && obj <= _data->_nbrObjects) {
		_data->_props[obj].itsLocked = false;
	}
}

void VM::opKill() {
	int person = _state->_cur.personNamed;
	if (person > 0 && person <= _data->_castSize) {
		Person &p = _data->_cast[person];
		if (p.corpse > 0) {
			// Replace person with their corpse object
			Place &loc = _data->_map[p.located];
			loc.people.unset(person);
			loc.objects.set(p.corpse);
			_data->_props[p.corpse].unseen = false;
		}
		p.located = kNowhere;
	}
}

void VM::opGive() {
	int obj = _state->_cur.doItToWhat;
	int person = _state->_cur.personNamed;
	if (obj > 0 && person > 0 && _state->_possessions.has(obj)) {
		_state->_possessions.unset(obj);
		_state->_nbrPossessions--;
		_data->_cast[person].carrying.set(obj);
	}
}

void VM::opGrab() {
	int obj = _state->_cur.doItToWhat;
	int person = _state->_cur.personNamed;
	if (obj > 0 && person > 0 && _data->_cast[person].carrying.has(obj)) {
		_data->_cast[person].carrying.unset(obj);
		_state->_possessions.set(obj);
		_state->_nbrPossessions++;
	}
}

void VM::opSwap() {
	debugC(kDebugScripts, "Angel VM: opSwap not correctly implemented - only gives, doesn't grab back");
}

void VM::opGrant() {
	// Grant access right
	int right = getNip();
	_state->_capabilities.set(right);
}

void VM::opTkOff() { opShed(); }      // Take off = shed
void VM::opPtOn() { opWear(); }       // Put on = wear
void VM::opToss() { opThrow(); }      // Toss = throw
void VM::opTrash() { opDrop(); }      // Trash = destroy (drop + flag)

void VM::opMove() {
	// Execute the move procedure for current direction
	int dir = (int)_state->_direction;
	int dest = _state->map(_state->_location).nextPlace[dir];
	if (dest > kNowhere && dest <= _data->_nbrLocations) {
		_state->_pprvLocation = _state->_prvLocation;
		_state->_prvLocation = _state->_location;
		_state->_prvDirection = _state->_direction;
		_state->_location = dest;
		_state->_trail.set(dest);
		_state->map(dest).unseen = true;  // Will be described
		_state->_moveNumber++;
		_state->_lastMove = _state->_moveNumber;
	}
}

void VM::opEntry() {
	// Entry procedure for new location — describe it
	int loc = _state->_location;
	if (loc > 0 && loc <= _data->_nbrLocations) {
		Place &place = _data->_map[loc];
		if (place.n > 0) {
			displayMsg(place.n);
		}
		place.unseen = false;
	}
}

void VM::opRide() {
	debugC(kDebugScripts, "Angel VM: opRide not implemented");
}

void VM::opRLoc(int ref) {
	// P-code: proc 84 case 56 → CPI 3,10 (proc 10).
	// Proc 10 RESOLVES the destination from game state but does NOT
	// change the player's location. It computes:
	//   dest = place[placeNamed-1].nextPlace[direction]
	// and stores the result in intermediate state (global[3010] →
	// _placeNamed) and sets _tfIndicator to indicate validity.
	// The actual location change is done later by opMove (kMovOp)
	// or the angel.cpp fallback movement handler.
	int dest = ref;
	if (dest == 0) {
		// kFa path: compute destination from game state
		int loc = _state->_placeNamed;
		int dir = (int)_state->_direction;
		if (loc > 0 && loc <= _data->_nbrLocations && dir >= 0 && dir < kNumDirections) {
			dest = _data->_map[loc].nextPlace[dir];
		}
	}
	bool valid = (dest > kNowhere && dest <= _data->_nbrLocations);
	debugC(kDebugScripts, "Angel VM: opRLoc(ref=%d) placeNamed=%d dir=%d dest=%d valid=%d (loc stays %d)",
	       ref, _state->_placeNamed, (int)_state->_direction, dest, valid ? 1 : 0, _state->_location);
	if (valid) {
		_state->_placeNamed = dest;
	}
	_state->_tfIndicator = valid;
	_lastTestResult = valid;
}

void VM::opNxStop() {
	debugC(kDebugScripts, "Angel VM: opNxStop not implemented");
}

void VM::opOffer() {
	debugC(kDebugScripts, "Angel VM: opOffer not implemented");
}

void VM::opTour() {
	_state->_touring = true;
	_state->_tourPoint = _state->_location;
}

void VM::opRRide() {
	debugC(kDebugScripts, "Angel VM: opRRide not implemented");
}

void VM::opTrade() {
	int person = _state->_cur.personNamed;
	if (person > 0 && person <= _data->_castSize) {
		int proc = _data->_cast[person].sFun[kTradeOp - kTradeOp];
		if (proc > 0)
			displayMsg(proc);
	}
}

void VM::opGreet() {
	int person = _state->_cur.personNamed;
	if (person > 0 && person <= _data->_castSize) {
		int proc = _data->_cast[person].sFun[kGreetOp - kTradeOp];
		if (proc > 0)
			displayMsg(proc);
	}
}

void VM::opGift() {
	int person = _state->_cur.personNamed;
	if (person > 0 && person <= _data->_castSize) {
		int proc = _data->_cast[person].sFun[kGiftOp - kTradeOp];
		if (proc > 0)
			displayMsg(proc);
	}
}

void VM::opSecret() {
	int person = _state->_cur.personNamed;
	if (person > 0 && person <= _data->_castSize) {
		int proc = _data->_cast[person].sFun[kSecretOp - kTradeOp];
		if (proc > 0)
			displayMsg(proc);
	}
}

void VM::opBluff() {
	debugC(kDebugScripts, "Angel VM: opBluff not implemented");
}

void VM::opCurse() {
	_state->_nbrOffenses++;
	debugC(kDebugScripts, "Angel VM: CURSE (offenses: %d)", _state->_nbrOffenses);
}

void VM::opWelcome() {
	// kWelcomeOp (enum 33) is in the "Character/action response ops" range
	// (26-49). In the original p-code, the kFa handler adds base 50 to the
	// raw nip (NAT_F0 32(0,50)), making these ops unreachable via bytecode:
	// raw nip 33 → XJP case 83 = kAsgOp, not kWelcomeOp.
	//
	// This is only reached due to the base-offset bug (PLAN.md Priority 1).
	// The welcome event is triggered from angel.cpp via displayMsg(4098).
	// Calling displayMsg here would recurse since we're inside msg 4098.
	debugC(kDebugScripts, "Angel VM: opWelcome (no-op, base-offset misdispatch)");
}

void VM::opMrdr() {
	debugC(kDebugScripts, "Angel VM: opMrdr not implemented");
}

void VM::opOpnIt() {
	// Open a door/location
	int loc = _state->_location;
	if (loc > 0) {
		_data->_map[loc].itsOpen = true;
		_data->_map[loc].itsLocked = false;
	}
}

void VM::opClsIt() {
	int loc = _state->_location;
	if (loc > 0) {
		_data->_map[loc].itsOpen = false;
	}
}

void VM::opLkIt() {
	int loc = _state->_location;
	if (loc > 0) {
		_data->_map[loc].itsLocked = true;
		_data->_map[loc].itsOpen = false;
	}
}

void VM::opUnLkIt() {
	int loc = _state->_location;
	if (loc > 0) {
		_data->_map[loc].itsLocked = false;
	}
}

void VM::opPutIt() {
	debugC(kDebugScripts, "Angel VM: opPutIt not implemented");
}

void VM::opPourIt() {
	debugC(kDebugScripts, "Angel VM: opPourIt not implemented");
}

void VM::opSave() {
	// In the P-code, kSaveOp sets seg[19].g[161] = 1 (death flag).
	// This signals the game loop that the player has died.
	// It does NOT perform a save operation.
	debugC(kDebugScripts, "Angel VM: opSave → death flag set");
	_state->_stillPlaying = false;
}

void VM::opQuit() {
	// P-code QUIT handler: CXG 18,8 (outLn) + SRO 2 (set RESPOND segment
	// global[2] = 1). This sets a RESPOND-level flag, NOT the GAME segment's
	// quit flag. The main game loop checks GAME's global[2], which is separate.
	// During ENTRY (text suppressed), kQuitOp fires but should NOT terminate
	// the game. The flag is checked within the RESPOND segment's control flow.
	debugC(kDebugScripts, "Angel VM: opQuit (respond-level quit flag set)");
	_engine->outLn();
	_state->_respondQuit = true;
}

void VM::opRestart() {
	_state->reset();
}

// ============================================================
// Edit implementations
// ============================================================

void VM::opTick(int ref) {
	// Advance clock by ref ticks
	_state->_clock.tickNumber += (ref > 0 ? ref : 1);
	_state->_clock.minute += (ref > 0 ? ref : 1);
	while (_state->_clock.minute >= 60) {
		_state->_clock.minute -= 60;
		_state->_clock.hour++;
		if (_state->_clock.hour > 12) {
			_state->_clock.hour = 1;
		}
		// Handle AM/PM toggle at 12
	}
}

void VM::opEvent(int ref) {
	// Set event register (case 74 in kFa XJP → L_2935)
	// P-code: CLP2 57 reads getNip - 16 for register index (1 nip),
	//         CLP2 33 reads getNumber18 (procedure address, 3 nips).
	//         Stores addr to xReg[regIndex].proc (word[1]).
	// Stream consumption: getNip(1) + getNumber18(3) = 4 nips total
	int rawNip = getNip();
	int reg = rawNip - 16;  // proc 57: getNip - 16
	int addr = getNumber18();  // proc 33 → proc 6: 3-nip 18-bit address
	debugC(kDebugScripts, "Angel VM: opEvent(rawNip=%d, reg=%d, addr=%d)", rawNip, reg, addr);
	if (reg >= 0 && reg < 32) {
		_state->_clock.xReg[reg].proc = addr;
	} else {
		debugC(kDebugScripts, "Angel VM: opEvent reg=%d out of range (rawNip=%d)", reg, rawNip);
	}
}

void VM::opSet(int ref) {
	// Generic DOS write opcode (case 75 in kFa XJP → L_28f3).
	// P-code flow:
	//   1. CXG 18,15 → getNip (attrNip)                    (1 nip)
	//   2. CLP2 53   → categorize(attrNip)                  (0 nips)
	//   3. CXS 15    → getNip (skip value)                   (1 nip)
	//   4. CXS 16    → CXG 18,16(skip) = jump(skip-1)       (0 nips from stream)
	//   5. CLP2 58   → proc 58 readCompValue                (1 or 4 nips)
	//   6. CLP2 54   → dispatch stores result into the decoded target
	//
	// When skip=0, CXG 18,16(0) = jump(-1) backs up 1 position.
	// This causes proc 58 to re-read the same 0 nip as its literal flag,
	// saving 1 nip in the common case (the 0 serves as both skip AND flag).
	//
	// Proc 53 categorizes attrNip:
	//   nip  0..5  → entity field write (case 'A'), index = nip
	//   nip  6..15 → CmdEntry[nip-6] (case 'X')
	//   nip 16+    → xReg[nip-16] (case 'T')
	int attrNip = getNip();

	// CXS 15 + CXS 16: read skip value, then adjust position
	int cxs15val = getNip();
	jump(cxs15val - 1);  // CXG 18,16(skip): when 0, backs up to re-read as p58 flag

	int storeValue = readCompValueFromStream();
	int p58nip = _lastFieldRef;

	debugC(kDebugScripts, "Angel VM: opSet attrNip=%d cxs15=%d p58=%d val=%d",
	       attrNip, cxs15val, p58nip, storeValue);

	if (attrNip >= 6 && attrNip < 16) {
		// CmdEntry: dispatch addresses for command responses
		int idx = attrNip - 6;
		if (idx < GameState::kMaxCmdEntries) {
			_state->_cmdEntry[idx] = storeValue;
			debugC(kDebugScripts, "Angel VM: opSet CmdEntry[%d] = %d",
			       idx, storeValue);
		}
	} else if (attrNip < 6) {
		storeFieldValue(attrNip, storeValue);
		debugC(kDebugScripts, "Angel VM: opSet entity field[%d] = %d",
		       attrNip, storeValue);
	} else {
		int reg = attrNip - 16;  // proc 53: nip - 16 for xReg index
		if (reg >= 0 && reg < 32) {
			_state->_clock.xReg[reg].x = storeValue;
			debugC(kDebugScripts, "Angel VM: opSet xReg[%d].x = %d (nip=%d)",
			       reg, storeValue, attrNip);
		} else {
			debugC(kDebugScripts, "Angel VM: opSet xReg reg=%d out of range (nip=%d)",
			       reg, attrNip);
		}
	}
}

void VM::opSsp(int ref) {
	// Suspend event timer (case 76 in kFa XJP → L_2905)
	// P-code: CLP2 57 reads getNip - 16 for register index (1 nip),
	//         if xReg[idx].x > 0, negates it (makes negative = suspended).
	// Stream consumption: getNip = 1 nip
	int rawNip = getNip();
	int reg = rawNip - 16;  // proc 57: getNip - 16
	debugC(kDebugScripts, "Angel VM: opSsp(rawNip=%d, reg=%d)", rawNip, reg);
	if (reg >= 0 && reg < 32) {
		if (_state->_clock.xReg[reg].x > 0)
			_state->_clock.xReg[reg].x = -_state->_clock.xReg[reg].x;
	}
}

void VM::opRsm(int ref) {
	// Resume event timer (case 77 in kFa XJP → L_291d)
	// P-code: CLP2 57 reads getNip - 16 for register index (1 nip),
	//         if xReg[idx].x < 0, negates it (makes positive = active).
	// Stream consumption: getNip = 1 nip
	int rawNip = getNip();
	int reg = rawNip - 16;  // proc 57: getNip - 16
	debugC(kDebugScripts, "Angel VM: opRsm(rawNip=%d, reg=%d)", rawNip, reg);
	if (reg >= 0 && reg < 32) {
		if (_state->_clock.xReg[reg].x < 0)
			_state->_clock.xReg[reg].x = -_state->_clock.xReg[reg].x;
	}
}

void VM::opSw(int ref) {
	debugC(kDebugScripts, "Angel VM: opSw(%d) not implemented", ref);
}

void VM::opAdv() {
	// Advance fog one step
	int fogBank = _state->_fogRoute.loc[kFogBank];
	if (fogBank > 0 && fogBank <= _data->_nbrLocations) {
		int next = _data->_map[fogBank].fogPath;
		if (next > 0 && next != _state->_fogRoute.loc[kFogLimit]) {
			_data->_map[next].foggy = true;
			_state->_fogRoute.loc[kFogBank] = next;
		}
	}
}

void VM::opRecede() {
	// Recede fog one step
	int fogBank = _state->_fogRoute.loc[kFogBank];
	int fogStart = _state->_fogRoute.loc[kFogStart];
	if (fogBank > 0 && fogBank != fogStart) {
		_data->_map[fogBank].foggy = false;
		// Scan forward from start to find the previous fog location
		int prev = fogStart;
		int cur = fogStart;
		while (cur != fogBank && cur > 0) {
			prev = cur;
			cur = _data->_map[cur].fogPath;
		}
		_state->_fogRoute.loc[kFogBank] = prev;
	}
}

void VM::opChz(int ref) {
	// kFa path uses ref=0 and does not consume inline stream args.
	if (ref == 0) {
		debugC(kDebugScripts, "Angel VM: opChz kFa ref=0 (no-op)");
		return;
	}

	// Change location of a person
	int person = ref;
	int dest = getNumber();
	if (person > 0 && person <= _data->_castSize && dest > 0) {
		Person &p = _data->_cast[person];
		if (p.located > 0)
			_data->_map[p.located].people.unset(person);
		p.located = dest;
		if (dest > 0 && dest <= _data->_nbrLocations)
			_data->_map[dest].people.set(person);
	}
}

void VM::opAttr(int ref) {
	// kFa path uses ref=0 and does not consume inline stream args.
	if (ref == 0) {
		debugC(kDebugScripts, "Angel VM: opAttr kFa ref=0 (no-op)");
		return;
	}

	// Set entity attribute (case 82 in kFa XJP).
	// P-code: reads getNumber (2 nips) for attribute parameter.
	// TODO: implement actual attribute setting (e.g., person.unseen = false).
	int param = getNumber();
	debugC(kDebugScripts, "Angel VM: opAttr(%d) param=%d (stub — nips consumed)", ref, param);
}

void VM::opAsg(int ref) {
	// Assign entity reference.
	// TWO different code paths in the p-code:
	//
	// kFar path (proc 93, L_2ace): entity already resolved by resolveEntity.
	//   CXS 12 → entity type (0-3), CXS 10 → entity value.
	//   Dispatch on type:
	//     0 (object)   → doItToWhat = value
	//     1 (person)   → personNamed = value
	//     2 (vehicle)  → vehicleRef = value
	//     3 (location) → placeNamed = value
	//   Stream consumption: NONE (no nips read).
	//
	// kFa path (proc 84, L_2949): reads vocab index from stream.
	//   getNumber → vocab index, Vocab[index].VECore → type + value.
	//   Same dispatch as above.
	//   Stream consumption: getNumber = 2 nips.

	if (ref != 0 && _entityFlag) {
		// kFar path — entity context already set by resolveEntity.
		// Don't read from stream!
		int value = _entityValue;
		switch (_entityType) {
		case 0:  // object
			_state->_cur.doItToWhat = value;
			break;
		case 1:  // person
			_state->_cur.personNamed = value;
			break;
		case 2:  // location (our type 2 = p-code category 3)
			_state->_placeNamed = value;
			break;
		case 3:  // vehicle (our type 3 = p-code category 2)
			_state->_cab = value;
			break;
		default:
			warning("Angel VM: opAsg kFar unknown entityType=%d", _entityType);
			break;
		}
		debugC(kDebugScripts, "Angel VM: opAsg kFar ref=%d type=%d value=%d", ref, _entityType, value);
	} else {
		// kFa path — read 1-based VWordIndex from stream, resolve via VECore.
		// P-code proc 84: getNumber → vocab index, Vocab[index].VECore → type + value.
		int rawIdx = getNumber();
		int vocabIdx = rawIdx - 1;  // 1-based VWordIndex → 0-based
		if (vocabIdx >= 0 && vocabIdx <= _data->_nbrVWords) {
			const VECore &ve = _data->_vocab[vocabIdx].ve;
			int entityRef = ve.ref;
			int entityType = (int)ve.vType;
			switch (entityType) {
			case 0:  // AnObject
				_state->_cur.doItToWhat = entityRef;
				break;
			case 1:  // APerson
				_state->_cur.personNamed = entityRef;
				break;
			case 2:  // ALocation
			case 4:  // ABuilding (also LocRef)
				_state->_placeNamed = entityRef;
				break;
			case 3:  // AVehicle
				_state->_cab = entityRef;
				break;
			default:
				break;
			}
			_entityValue = entityRef;
			_entityFlag = (entityRef > 0);
			_entityType = entityType;
			debugC(kDebugScripts, "Angel VM: opAsg kFa vocabIdx=%d type=%d ref=%d",
			       vocabIdx, entityType, entityRef);
		} else {
			warning("Angel VM: opAsg kFa invalid vocabIdx=%d (raw=%d)", vocabIdx, rawIdx);
		}
	}
}

void VM::opMov(int ref) {
	// kFa path uses ref=0 and does not consume inline stream args.
	if (ref == 0) {
		debugC(kDebugScripts, "Angel VM: opMov kFa ref=0 (no-op)");
		return;
	}

	// Move entity to a location. Dispatches on _entityType:
	//   0 = object, 1 = person, 2 = location (no-op), 3 = vehicle
	int dest = getNumber();
	debugC(kDebugScripts, "Angel VM: opMov ref=%d entityType=%d dest=%d", ref, _entityType, dest);

	if (_entityFlag && _entityType == 1) {
		// Person movement
		if (ref > 0 && ref <= _data->_castSize && dest > 0) {
			_engine->utils()->moveHim(ref, dest);
			debugC(kDebugScripts, "Angel VM: opMov person %d → location %d", ref, dest);
		}
	} else {
		// Object movement (default)
		int obj = ref;
		if (obj > 0 && obj <= _data->_nbrObjects && dest > 0) {
			// Remove from current location
			for (int i = 1; i <= _data->_nbrLocations; i++) {
				_data->_map[i].objects.unset(obj);
			}
			_state->_possessions.unset(obj);
			// Place at new location
			if (dest <= _data->_nbrLocations) {
				_data->_map[dest].objects.set(obj);
			}
			debugC(kDebugScripts, "Angel VM: opMov object %d → location %d", obj, dest);
		}
	}
}

void VM::opRst(int refOp) {
	// Restore game state variable from its recall register.
	// P-code proc 94: XJP on refOp (138-148), restores from seg[19] recall
	// registers that were set once at game start and never updated.
	// Cases 143-147 (kSunOp..kPlaceOp) are no-ops in the original.
	// Case 148 (kThingOp) is special: copies _location to a recall slot.
	debugC(kDebugScripts, "Angel VM: opRst refOp=%d", refOp);
	switch (refOp) {
	case kItOp:
		debugC(kDebugScripts, "Angel VM: opRst kItOp: doItToWhat %d → %d",
		       _state->_cur.doItToWhat, _state->_recallDoItToWhat);
		_state->_cur.doItToWhat = _state->_recallDoItToWhat;
		break;
	case kTargOp:
		debugC(kDebugScripts, "Angel VM: opRst kTargOp: target %d → %d",
		       _state->_target, _state->_recallTarget);
		_state->_target = _state->_recallTarget;
		break;
	case kVclOp:
		debugC(kDebugScripts, "Angel VM: opRst kVclOp: cab %d → %d",
		       _state->_cab, _state->_recallCab);
		_state->_cab = _state->_recallCab;
		break;
	case kPersonOp:
		debugC(kDebugScripts, "Angel VM: opRst kPersonOp: personNamed %d → %d",
		       _state->_cur.personNamed, _state->_recallPersonNamed);
		_state->_cur.personNamed = _state->_recallPersonNamed;
		break;
	case kObjOp:
		debugC(kDebugScripts, "Angel VM: opRst kObjOp: thing %d → %d",
		       _state->_thing, _state->_recallThing);
		_state->_thing = _state->_recallThing;
		break;
	case kThingOp:
		// Case 148: seg[21].g[1] → seg[19].g[130].
		// Copies current location to a recall register field.
		debugC(kDebugScripts, "Angel VM: opRst kThingOp: store location %d to recallThing",
		       _state->_location);
		_state->_recallThing = _state->_location;
		break;
	default:
		// Cases 143-147 (kSunOp, kCtntsOp, kCtnrOp, kLocOp, kPlaceOp)
		// are no-ops in the original P-code.
		break;
	}
}

void VM::opIncr(int ref) {
	// P-code: proc 59 with amount=1, opcode=92 (add).
	// Proc 59 reads field ref via proc 58 (1 nip), looks up current value,
	// adds amount, stores back via proc 54.
	// Stream: 1 nip (field ref via readCompValue pattern)
	int curVal = readCompValueFromStream();
	int newVal = curVal + 1;
	if (_lastFieldRef != 0) {
		storeFieldValue(_lastFieldRef, newVal);
		debugC(kDebugScripts, "Angel VM: opIncr field[%d]: %d → %d", _lastFieldRef, curVal, newVal);
	} else {
		debugC(kDebugScripts, "Angel VM: opIncr literal=%d (no field to store)", curVal);
	}
}

void VM::opDecr(int ref) {
	// P-code: proc 59 with amount=1, opcode=93 (sub).
	// Clamped: max(0, current - 1).
	// Stream: 1 nip (field ref)
	int curVal = readCompValueFromStream();
	int newVal = (curVal > 1) ? curVal - 1 : 0;
	if (_lastFieldRef != 0) {
		storeFieldValue(_lastFieldRef, newVal);
		debugC(kDebugScripts, "Angel VM: opDecr field[%d]: %d → %d", _lastFieldRef, curVal, newVal);
	} else {
		debugC(kDebugScripts, "Angel VM: opDecr literal=%d (no field to store)", curVal);
	}
}

void VM::opAdd(int ref) {
	// P-code: CLP2 58 reads amount, then proc 59 reads field ref,
	// adds amount + current_value, stores back.
	// Stream: proc58(amount) + proc58(fieldRef) nips
	int amount = readCompValueFromStream();
	(void)_lastFieldRef; // amount field ref not needed
	int curVal = readCompValueFromStream();
	int targetFieldRef = _lastFieldRef;
	int newVal = amount + curVal;
	if (targetFieldRef != 0) {
		storeFieldValue(targetFieldRef, newVal);
		debugC(kDebugScripts, "Angel VM: opAdd field[%d] += %d: %d → %d",
		       targetFieldRef, amount, curVal, newVal);
	} else {
		debugC(kDebugScripts, "Angel VM: opAdd amount=%d curVal=%d (no target field)", amount, curVal);
	}
}

void VM::opSub(int ref) {
	// P-code: CLP2 58 reads amount, then proc 59 reads field ref,
	// computes max(0, current_value - amount), stores back.
	// Stream: proc58(amount) + proc58(fieldRef) nips
	int amount = readCompValueFromStream();
	(void)_lastFieldRef; // amount field ref not needed
	int curVal = readCompValueFromStream();
	int targetFieldRef = _lastFieldRef;
	int newVal = (curVal > amount) ? curVal - amount : 0;
	if (targetFieldRef != 0) {
		storeFieldValue(targetFieldRef, newVal);
		debugC(kDebugScripts, "Angel VM: opSub field[%d] -= %d: %d → %d",
		       targetFieldRef, amount, curVal, newVal);
	} else {
		debugC(kDebugScripts, "Angel VM: opSub amount=%d curVal=%d (no target field)", amount, curVal);
	}
}

// ============================================================
// Test implementations
// ============================================================

bool VM::isLocal(int locRef) {
	// FUNCTION Local(Loc: LocRef; VAR dir: MotionSpec): BOOLEAN
	// Returns true if locRef is the current location OR adjacent
	// (reachable in one step via any direction).
	if (locRef < 1 || locRef > _data->_nbrLocations)
		return false;
	if (locRef == _state->_location)
		return true;
	const Place &here = _state->map(_state->_location);
	for (int d = 0; d < kNumDirections; d++) {
		if (here.nextPlace[d] == locRef)
			return true;
	}
	return false;
}

/**
 * P-code entity table packing divisor for a given word type.
 * Entity table word[3] = ref * divisor + code. The P-code testIs
 * (proc 77) uses DIVI with the _entityType divisor to extract the ref.
 * Returns 0 for types without a testIs XJP case.
 */
static int entityTypeDivisor(int vType) {
	switch (vType) {
	case kAnObject:  return 80;
	case kAPerson:   return 64;
	case kALocation:
	case kABuilding: return 96;
	case kAVehicle:  return 60;
	case kAVerb:     return 80;
	case kAnOther:   return 64;
	default:         return 0;
	}
}

int VM::entityToVocabIdx(int entityNum) const {
	// Entity table indices include library words (1..kLibraryWordCount)
	// followed by game vocab entries (kLibraryWordCount+1..N).
	int vocabIdx = entityNum - kLibraryWordCount - 1;
	if (vocabIdx >= 0 && vocabIdx < _data->_nbrVWords)
		return vocabIdx;
	return -1;
}

VM::EntitySlotInfo VM::resolveEntitySlotInfo(int entityNum) const {
	EntitySlotInfo info;

	// DOS GAME.000 uses a large runtime slot table at DS:06b2 with 7-byte
	// records. The kFt handlers for Here/Syn/Is read slot +2 as KindOfWord
	// and slot +3 as the direct ref byte. We do not have that table loaded
	// yet, so only keep the handful of chamber-south overrides that were
	// needed to confirm the trapdoor path. Unverified high-entropy slots are
	// intentionally left unresolved rather than guessed from prior notes.
	if (_data->_isDosData) {
		switch (entityNum) {
		case 38:
			info.valid = true;
			info.type = kALocation;
			info.ref = 7;   // Current chamber slot used by msg 1322 in the south death path
			return info;
		case 142:
			info.valid = true;
			info.type = kADirection;
			info.ref = 1;   // South
			return info;
		case 208:
			info.valid = true;
			info.type = kALocation;
			info.ref = 7;   // Central chamber
			return info;
		default:
			break;
		}
	}

	int vocabIdx = entityToVocabIdx(entityNum);
	if (vocabIdx >= 0) {
		info.valid = true;
		info.type = _data->_vocab[vocabIdx].ve.vType;
		info.ref = _data->_vocab[vocabIdx].ve.ref;
	}

	return info;
}

bool VM::testHere(int ref) {
	Place &loc = _state->map(_state->_location);
	int vType, entityRef;

	if (_entityType >= 0) {
		// kFtr path (proc 78): reads 1 getNip, uses _entityType/_entityValue.
		int entityIdx = getNip();
		(void)entityIdx;
		vType = _entityType;
		entityRef = _entityValue;
		debugC(kDebugScripts, "Angel VM: testHere(kFtr) ref=%d loc=%d entityType=%d entityValue=%d entityIdx=%d",
		        ref, _state->_location, _entityType, _entityValue, entityIdx);
	} else {
		// kFt path (proc 75): reads getNip(1) + getNumber(2) = 3 nips.
		// CPI 4,3 = getNip (entity nip, consumed for alignment).
		// CPI 4,5 with flag=1 = getNumber (12-bit entity table index).
		// DOS GAME.000 resolves this through the DS:06b2 slot table and reads
		// slot+2 (KindOfWord) and slot+3 (ref).
		int entityNip = getNip();
		(void)entityNip;
		int entityTableIdx = getNumber();
		EntitySlotInfo slot = resolveEntitySlotInfo(entityTableIdx);
		if (!slot.valid) {
			debugC(kDebugScripts, "Angel VM: testHere(kFt) entityTableIdx=%d unresolved slot -> FALSE", entityTableIdx);
			return false;
		}
		vType = slot.type;
		entityRef = slot.ref;
		debugC(kDebugScripts, "Angel VM: testHere(kFt) entityTableIdx=%d vType=%d entityRef=%d loc=%d",
		        entityTableIdx, vType, entityRef, _state->_location);
	}

	debugC(kDebugScripts, "Angel VM: testHere vType=%d entityRef=%d loc=%d",
	        vType, entityRef, _state->_location);

	switch (vType) {
	case kAnObject:  // 0 — object: check if at current location
		return loc.objects.has(entityRef);
	case kAPerson:   // 1 — person: check if at current location
		return loc.people.has(entityRef);
	case kALocation: // 2 — location: adjacency check
	case kABuilding: // 4 — building: same as location
		return isLocal(entityRef);
	case kAVehicle:  // 3 — vehicle: check cab
		return (_state->_cab == entityRef);
	default:         // 5 (direction), 6 (verb): false
		return false;
	}
}

bool VM::testOwns(int ref) {
	// Does player own object ref?
	return _state->_possessions.has(ref);
}

bool VM::testWears(int ref) {
	return _state->_wearing.has(ref);
}

bool VM::testHas(int ref) {
	// Player has = owns or wears
	return _state->_possessions.has(ref) || _state->_wearing.has(ref);
}

bool VM::testOn(int ref) {
	// Is object ref on a support?
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].inOrOn > 0 &&
		       _data->_props[_data->_props[ref].inOrOn].kindOfThing == kASupport;
	return false;
}

bool VM::testIn(int ref) {
	// Is object ref in a container?
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].inOrOn > 0 &&
		       (_data->_props[_data->_props[ref].inOrOn].kindOfThing == kABox ||
		        _data->_props[_data->_props[ref].inOrOn].kindOfThing == kAVessel);
	return false;
}

bool VM::testFull(int ref) {
	// Is container ref full?
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].contents.count() > 0;
	return false;
}

bool VM::testLocked(int ref) {
	// kFtr path with location entity: check location's itsLocked
	if (_entityType == kALocation || _entityType == kABuilding) {
		if (ref > 0 && ref <= _data->_nbrLocations)
			return _data->_map[ref].itsLocked;
		return false;
	}
	// kFt path or object entity
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].itsLocked;
	return false;
}

bool VM::testOpened(int ref) {
	// kFtr path with location entity: check if door and open
	if (_entityType == kALocation || _entityType == kABuilding) {
		if (ref > 0 && ref <= _data->_nbrLocations)
			return _data->_map[ref].itsADoor && _data->_map[ref].itsOpen;
		return false;
	}
	// kFt path or object entity
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].itsOpen;
	return false;
}

bool VM::testClosed(int ref) {
	// kFtr path with location entity: check if door and closed
	if (_entityType == kALocation || _entityType == kABuilding) {
		if (ref > 0 && ref <= _data->_nbrLocations)
			return _data->_map[ref].itsADoor && !_data->_map[ref].itsOpen;
		return false;
	}
	// kFt path or object entity
	if (ref > 0 && ref <= _data->_nbrObjects)
		return !_data->_props[ref].itsOpen;
	return false;
}

bool VM::testCvrd(int ref) {
	return _state->_concealed.has(ref);
}

bool VM::testDark() {
	Place &loc = _state->map(_state->_location);
	if (loc.view == kDark) {
		// Check if player has a lit lamp
		for (int i = 1; i <= _data->_nbrObjects; i++) {
			if (_state->_possessions.has(i) && _data->_props[i].kindOfThing == kALamp && _data->_props[i].litUp)
				return false;
		}
		return true;
	}
	return false;
}

bool VM::testLit() {
	return !testDark();
}

bool VM::testFog() {
	return _state->map(_state->_location).foggy;
}

bool VM::testDoor(int ref) {
	if (ref > 0 && ref <= _data->_nbrLocations)
		return _data->_map[ref].itsADoor;
	return _state->map(_state->_location).itsADoor;
}

bool VM::testBox(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].kindOfThing == kABox;
	return false;
}

bool VM::testVsl(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].kindOfThing == kAVessel;
	return false;
}

bool VM::testSup(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].kindOfThing == kASupport;
	return false;
}

bool VM::testLamp(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].kindOfThing == kALamp;
	return false;
}

bool VM::testCorpse(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].kindOfThing == kAStiff;
	return false;
}

bool VM::testLqd(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].kindOfThing == kALiquid;
	return false;
}

bool VM::testHidden(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].unseen;
	return false;
}

bool VM::testStuff(int ref) {
	// Are there objects at location ref?
	if (ref > 0 && ref <= _data->_nbrLocations)
		return !_data->_map[ref].objects.isEmpty();
	return !_state->map(_state->_location).objects.isEmpty();
}

bool VM::testDEnd() {
	// Dead end: no path from current location in current direction.
	// Original Pascal: "FUNCTION DeadEnd: BOOLEAN" — checks the
	// current direction only, not all directions.
	int dir = (int)_state->_direction;
	if (dir < 0 || dir >= kNumDirections)
		return true;
	Place &loc = _state->map(_state->_location);
	return loc.nextPlace[dir] <= kNowhere;
}

bool VM::testKey(int ref) {
	// Does player have the key (access right) for ref?
	return _state->_capabilities.has(ref);
}

bool VM::testHPass(int ref) {
	// Has player visited location ref?
	// kFtr path: ref is direct location ref, _entityType is set.
	// kFt path: ref is entity table index from getNumber().
	int locRef;
	if (_entityType >= 0) {
		locRef = _entityValue;
	} else {
		EntitySlotInfo slot = resolveEntitySlotInfo(ref);
		if (slot.valid) {
			locRef = slot.ref;
		} else {
			locRef = ref;  // fallback
		}
	}
	debugC(kDebugScripts, "Angel VM: testHPass ref=%d locRef=%d entityType=%d", ref, locRef, _entityType);
	return _state->_trail.has(locRef);
}

bool VM::testVKey(int ref) {
	debugC(kDebugScripts, "Angel VM: testVKey(%d) not implemented", ref);
	return false;
}

bool VM::testCan(int ref) {
	// Can player perform verb/property ref?
	if (ref > 0 && ref <= _data->_nbrProperties) {
		int obj = _state->_cur.doItToWhat;
		if (obj > 0 && obj <= _data->_nbrObjects)
			return _data->_props[obj].properties.has(ref);
	}
	return false;
}

bool VM::testCant(int ref) {
	return !testCan(ref);
}

bool VM::testRand(int ref) {
	// Random test with probability ref (0-100)
	return _engine->getRandom(100) < ref;
}

bool VM::testAsk() {
	return _state->_aQuestion;
}

bool VM::testAny(int ref) {
	// Is there any object with property ref here?
	Place &loc = _state->map(_state->_location);
	for (int i = 1; i <= _data->_nbrObjects; i++) {
		if (loc.objects.has(i) && _data->_props[i].properties.has(ref))
			return true;
	}
	return false;
}

bool VM::testWord(int ref) {
	// Is VWord ref in the current input?
	return _state->_codeSet.has(ref);
}

bool VM::testSyn(int ref) {
	// DOS kSynOp resolves through the 7-byte slot table at DS:06b2.
	// The late-test dispatcher can reach the same slot-based logic while a
	// prior kFtr entity context is still live, so do NOT treat _entityType
	// as a discriminator here.
	debugC(kDebugScripts, "Angel VM: testSyn ref=%d entityType=%d entityValue=%d verb=%d direction=%d",
	       ref, _entityType, _entityValue, _state->_verb, _state->_direction);

	EntitySlotInfo slot = resolveEntitySlotInfo(ref);
	if (slot.valid) {
		// _state->_verb is already a vocab index (set in parser.cpp from token).
		int verbVocabIdx = _state->_verb;
		if (verbVocabIdx < 0 || verbVocabIdx >= _data->_nbrVWords)
			verbVocabIdx = -1;
		debugC(kDebugScripts, "Angel VM: testSyn(kFt) entityIdx=%d slotType=%d slotRef=%d direction=%d verb=%d verbVocabIdx=%d",
		       ref, slot.type, slot.ref,
		       _state->_direction, _state->_verb, verbVocabIdx);
		// Direction words: check if ref word's direction matches player direction
		if (slot.type == kADirection)
			return slot.ref == _state->_direction;
		// Verb words: check if ref is in same synonym group as current verb
		if (verbVocabIdx >= 0)
			return (slot.ref == _data->_vocab[verbVocabIdx].ve.ref &&
			        slot.type == _data->_vocab[verbVocabIdx].ve.vType);
	}

	if (_entityType >= 0) {
		debugC(kDebugScripts, "Angel VM: testSyn ref=%d unresolved slot with live entityType=%d -> FALSE",
		       ref, _entityType);
	}
	return false;
}

bool VM::testNew(int ref) {
	// Is the location/entity ref unseen (new)?
	// kFtr path: ref is direct entity ref, _entityType is set.
	// kFt path: ref is entity table index from getNumber().
	debugC(kDebugScripts, "Angel VM: testNew ref=%d nbrLoc=%d entityType=%d loc=%d locUnseen=%d",
	       ref, _data->_nbrLocations, _entityType, _state->_location,
	       _state->map(_state->_location).unseen ? 1 : 0);

	int vType, entityRef;
	if (_entityType >= 0) {
		// kFtr path: entity already resolved.
		vType = _entityType;
		entityRef = _entityValue;
	} else {
		// kFt path: read the DOS entity slot kind/ref pair.
		EntitySlotInfo slot = resolveEntitySlotInfo(ref);
		if (slot.valid) {
			vType = slot.type;
			entityRef = slot.ref;
		} else {
			// Fallback: check current location
			return _state->map(_state->_location).unseen;
		}
	}

	debugC(kDebugScripts, "Angel VM: testNew vType=%d entityRef=%d", vType, entityRef);

	switch (vType) {
	case kALocation:
	case kABuilding:
		if (entityRef > 0 && entityRef <= _data->_nbrLocations)
			return _data->_map[entityRef].unseen;
		break;
	case kAnObject:
		if (entityRef > 0 && entityRef <= _data->_nbrObjects)
			return _data->_props[entityRef].unseen;
		break;
	case kAPerson:
		if (entityRef > 0 && entityRef <= _data->_castSize)
			return _data->_cast[entityRef].unseen;
		break;
	default:
		break;
	}

	// Fallback: check current location
	return _state->map(_state->_location).unseen;
}

bool VM::testHolds(int ref) {
	// Does the referenced person/container hold anything?
	if (ref > 0 && ref <= _data->_castSize)
		return !_data->_cast[ref].carrying.isEmpty();
	return false;
}

bool VM::testIs(int ref) {
	// "Is" test â compound test opcode (proc 77 in RESPOND segment).
	//
	// P-code flow (proc 77):
	//   getAChar() â if '$' (kFt): $ path, else: non-$ path
	//
	//   $ path:
	//     local[1] = getNumber() (inline vocab/entity index)
	//     local[4] = 1 (mark as valid)
	//     If entityFlag set: lookup local[1] in vocab table, extract property
	//     set based on entity type, store comparison result in local[3].
	//     Falls to merge.
	//
	//   non-$ path:
	//     local[3] = old _entityValue (save from kFtr resolution)
	//     local[4] = old _entityFlag
	//     Read getNip()+135 â new ref operation
	//     Call resolveEntity() with new operation (overwrites entity context)
	//     Falls to merge.
	//
	//   Merge at L_1b68:
	//     if !(new _entityFlag AND local[4]) â return FALSE
	//     XJP on _entityType:
	//       case 0 (object): tfIndicator = (_entityValue == local[3])
	//       case 1 (person): tfIndicator = (_entityValue == local[3])
	//       case 3 (vehicle): set comparison
	//       case 2,6 (location): set/value comparison

	char ch = getAChar();

	if (ch == kFt) {
		// $ path: inline entity specification (proc 77).
		// P-code reads getNumber() as a vocab index, looks up the vocab
		// entry's VECore, and extracts an entity reference via MODI/DIVI
		// on the packed word (the result equals ve.ref since all entity
		// refs are below their type's modulus). Then compares against
		// _entityValue from the kFtr resolution.
		int entityNum = getNumber();

		if (!_entityFlag) {
			// When entityFlag is false (e.g., no verb entered during WELCOME),
			// the P-code resolveEntity returned no valid entity. Fall back to
			// comparing the raw operation code against the stream value.
			// This handles WELCOME's default case where kVerbOp (137) matches
			// entityNum=137, gating the intro text display.
			bool result = (_entityOp == entityNum);
			debugC(kDebugScripts, "Angel VM: testIs(ref=%d) $ path entityNum=%d entityFlag=false entityOp=%d -> %s",
			        ref, entityNum, _entityOp, result ? "TRUE" : "FALSE");
			return result;
		}

		EntitySlotInfo slot = resolveEntitySlotInfo(entityNum);
		if (!slot.valid) {
			debugC(kDebugScripts, "Angel VM: testIs(ref=%d) $ path entityNum=%d unresolved slot -> FALSE",
			        ref, entityNum);
			return false;
		}

		// DOS truth from HandleTestOpcode_130_Is:
		// the '$' path compares against the compared entity record's direct
		// ref byte (local_17 + 3).
		int extractedRef = slot.ref;
		int vocabVType = slot.type;

		bool result = (_entityValue == extractedRef);
		debugC(kDebugScripts, "Angel VM: testIs(ref=%d) $ path entityValue=%d extractedRef=%d (entityNum=%d slotType=%d entityType=%d) -> %s",
		        ref, _entityValue, extractedRef, entityNum,
		        vocabVType, _entityType,
		        result ? "TRUE" : "FALSE");

		// Do not apply the old isLocal() fallback here.
		// DOS HandleTestOpcode_130_Is dispatches through response-state helpers
		// on the location/building case; the previous adjacency shortcut was an
		// approximation and is what caused the chamber/tomb scripts to fire on
		// the wrong movement commands.

		return result;
	} else {
		// Non-$ path: save old entity context, resolve new, compare values.
		// P-code: local[3] = intermediate[3][10], local[4] = intermediate[3][9]
		int oldEntityValue = _entityValue;
		bool oldEntityFlag = _entityFlag;

		// Read new ref operation: getNip() + 135
		int refNip = getNip();
		int newRefOp = refNip + kFeOpcodeBase;

		// Entity resolution with new operation (proc 35)
		resolveEntity(newRefOp);

		// Merge: if !(newFlag AND oldFlag) -> FALSE
		if (!_entityFlag || !oldEntityFlag) {
			debugC(kDebugScripts, "Angel VM: testIs(ref=%d) non-$ flags failed: oldFlag=%d newFlag=%d -> FALSE",
			        ref, oldEntityFlag ? 1 : 0, _entityFlag ? 1 : 0);
			return false;
		}

		// Compare entity values based on entity type.
		// For objects (0), persons (1), and most types: simple equality.
		bool result = (_entityValue == oldEntityValue);
		debugC(kDebugScripts, "Angel VM: testIs(ref=%d) non-$ ch='%c'(%d) newOp=%d newValue=%d oldValue=%d type=%d result=%s",
		        ref, (ch >= 32 && ch < 127) ? ch : '?', (int)ch,
		        newRefOp, _entityValue, oldEntityValue, _entityType,
		        result ? "TRUE" : "FALSE");
		return result;
	}
}

bool VM::testFair(int ref) {
	// 50% probability test (fair coin)
	return _engine->getRandom(2) == 0;
}

bool VM::testCarry(int ref) {
	// Is person ref carrying anything?
	if (ref > 0 && ref <= _data->_castSize)
		return !_data->_cast[ref].carrying.isEmpty();
	return false;
}

bool VM::testTail() {
	// Is there someone following/pursuing the player?
	return _state->_pursuer > 0;
}

bool VM::testOnTour() {
	return _state->_touring;
}

bool VM::testLess(int ref) {
	debugC(kDebugScripts, "Angel VM: testLess asgV=%d < ref=%d -> %d",
	       _state->_asgV, ref, _state->_asgV < ref);
	return _state->_asgV < ref;
}

bool VM::testEq(int ref) {
	debugC(kDebugScripts, "Angel VM: testEq asgV=%d == ref=%d -> %d",
	       _state->_asgV, ref, _state->_asgV == ref);
	return _state->_asgV == ref;
}

bool VM::testLEq(int ref) {
	debugC(kDebugScripts, "Angel VM: testLEq asgV=%d <= ref=%d -> %d",
	       _state->_asgV, ref, _state->_asgV <= ref);
	return _state->_asgV <= ref;
}

int VM::getRefValue(Operation op) {
	if (_describedEntityActive && op == _describedEntityOp)
		return _describedEntityValue;

	switch (op) {
	case kItOp:      return _state->_thing;
	case kTargOp:    return _state->_target;
	case kVclOp:     return _state->_cab;
	case kPersonOp:  return _state->_cur.personNamed;
	case kObjOp:     return _state->_cur.doItToWhat;
	case kLocOp:     return _state->_location;
	case kPlaceOp:   return _state->_placeNamed;
	case kThingOp:   return _state->_thing;
	case kOtherOp:   return _state->_otherPerson;
	case kCabOp:     return _state->_cab;
	case kPrvOp:     return _state->_prvLocation;
	case kVLocOp:    return _state->_vLocation;
	case kPPrvOp:    return _state->_pprvLocation;
	case kTimeOp:    return _state->_clock.hour;
	case kDayOp:     return (int)_state->_clock.day;
	case kVerbOp:    return _state->_verb;
	case kXRegOp:    debugC(kDebugScripts, "Angel VM: getRefValue(kXRegOp) not implemented"); return 0;
	default:         return 0;
	}
}

// ============================================================
// Entity resolution (proc 35 equivalent)
// ============================================================
//
// Sets _entityFlag, _entityValue, _entityOp, _entityType based on
// the ref operation (135-155 range). Called from kFtr/kFar handlers.
//
// Entity types match KindOfWord:
//   0 = object (proc 36), 1 = person (proc 40),
//   2 = location (proc 41), 3 = vehicle (proc 39)
//
// _entityFlag = (value != 1), since 1 is the dummy ref for all types
// (kNobody=1, kNowhere=1, kNonthing=1).

void VM::resolveEntity(int op) {
	// Reset entity context (proc 35 prologue)
	_entityFlag = false;
	_entityValue = 0;
	_entityOp = op;
	_entityType = -1;

	if (_describedEntityActive && op == _describedEntityOp) {
		_entityType = _describedEntityType;
		_entityValue = _describedEntityValue;
		switch (_entityType) {
		case 0:
			_entityFlag = (_entityValue != kNonthing);
			break;
		case 1:
			_entityFlag = (_entityValue != kNobody);
			break;
		case 2:
			_entityFlag = (_entityValue != kNowhere);
			break;
		case 3:
			_entityFlag = (_entityValue != 1);
			break;
		default:
			_entityFlag = (_entityValue != 0);
			break;
		}
		return;
	}

	switch ((Operation)op) {
	case kPassOp:
		// Case 135: object resolution via current context object
		_entityType = 0;
		_entityValue = _state->_cur.doItToWhat;
		_entityFlag = (_entityValue != kNonthing);
		break;

	case kXRegOp:
		// Case 136: no-op (falls through in p-code)
		break;

	case kVerbOp:
		// Case 137: verb resolution (CPL 37)
		// P-code proc 37: looks up Vocab[verb_idx], checks VECore[0]/56 == AVerb(6),
		// if yes: entityValue = VECore[1]/80 (verb ref). If no: entityValue = 0.
		// entityFlag = (verb_idx != 0).
		// In our C++: the DIVI extraction equals the parsed .ref field.
		_entityType = 6;  // kAVerb
		_entityFlag = (_state->_verb != 0);
		if (_entityFlag && _state->_verb >= 0 && _state->_verb < _data->_nbrVWords &&
		    _data->_vocab[_state->_verb].ve.vType == kAVerb) {
			_entityValue = _data->_vocab[_state->_verb].ve.ref;
		} else {
			_entityValue = 0;
		}
		break;

	case kItOp:
		// Case 138: "it" → object (CPL 36 with _thing)
		_entityType = 0;
		_entityValue = _state->_thing;
		_entityFlag = (_entityValue != kNonthing);
		break;

	case kTargOp:
		// Case 139: target → location (CPL 41 with global[3010])
		_entityType = 2;
		_entityValue = _state->_target;
		_entityFlag = (_entityValue != kNowhere);
		break;

	case kVclOp:
		// Case 140: vehicle (CPL 39 with _cab)
		_entityType = 3;
		_entityValue = _state->_cab;
		_entityFlag = (_entityValue != 1);
		break;

	case kPersonOp:
		// Case 141: person (CPL 40 with _cur.personNamed)
		_entityType = 1;
		_entityValue = _state->_cur.personNamed;
		_entityFlag = (_entityValue != kNobody);
		break;

	case kObjOp:
		// Case 142: direct object (CPL 36 with _cur.doItToWhat)
		_entityType = 0;
		_entityValue = _state->_cur.doItToWhat;
		_entityFlag = (_entityValue != kNonthing);
		break;

	case kSunOp:
	case kCtntsOp:
	case kCtnrOp:
		// Cases 143-145: no-op in entity resolution
		break;

	case kLocOp:
		// Case 146: current location (CPL 41 with _location)
		// P-code proc 41 resolves through seg[17].global[1] table.
		// For Fe display, we need the raw location number to look up
		// the location name via _map[location].shortDscr.
		_entityType = 2;
		_entityFlag = (_state->_location != kNowhere);
		_entityValue = _state->_location;
		break;

	case kPlaceOp:
		// Case 147: named place (CPL 41 with _placeNamed)
		_entityType = 2;
		_entityValue = _state->_placeNamed;
		_entityFlag = (_entityValue != kNowhere);
		break;

	case kThingOp:
		// Case 148: thing → object (CPL 36 with _thing)
		_entityType = 0;
		_entityValue = _state->_thing;
		_entityFlag = (_entityValue != kNonthing);
		break;

	case kOtherOp:
		// Case 149: other person (CPL 40 with _otherPerson)
		_entityType = 1;
		_entityValue = _state->_otherPerson;
		_entityFlag = (_entityValue != kNobody);
		break;

	case kCabOp:
		// Case 150: cab → vehicle (CPL 39 with _cab)
		_entityType = 3;
		_entityValue = _state->_cab;
		_entityFlag = (_entityValue != 1);
		break;

	case kPrvOp:
		// Case 151: previous location (CPL 41 with _prvLocation)
		// P-code overwrites entityValue=1 after CPL 41.
		_entityType = 2;
		_entityValue = _state->_prvLocation;
		_entityFlag = (_entityValue != kNowhere);
		break;

	case kVLocOp:
		// Case 152: vehicle location (CPL 41 with _vLocation)
		_entityType = 2;
		_entityValue = _state->_vLocation;
		_entityFlag = (_entityValue != kNowhere);
		break;

	case kPPrvOp:
		// Case 153: pre-previous location (CPL 41 with _pprvLocation)
		_entityType = 2;
		_entityValue = _state->_pprvLocation;
		_entityFlag = (_entityValue != kNowhere);
		break;

	case kTimeOp:
		// Case 154: no-op in entity resolution
		break;

	case kDayOp:
		// Case 155: day resolution (CPL 38)
		_entityType = 9;  // kADay
		_entityValue = (int)_state->_clock.day;
		_entityFlag = true;
		break;

	default:
		// Operations above 155 (kDscOp=156 through kNoOp=165) are outside
		// proc 35's XJP range — entity stays unresolved (flag=false).
		// kNoOp(165) specifically means "no entity reference" and is used
		// by Far/Ftr/Fer when the reference operand indicates no entity.
		debugC(kDebugScripts, "Angel VM: resolveEntity op=%d outside XJP range 135-155 (no resolution)", op);
		break;
	}

	debugC(kDebugScripts, "Angel VM: resolveEntity op=%d → flag=%d value=%d type=%d",
	        op, _entityFlag ? 1 : 0, _entityValue, _entityType);
}

int VM::lookupFieldValue(int entityRef) {
	// P-code proc 55: classify entity ref via proc 53, then look up field value.
	//
	// Proc 53 classification:
	//   ref NOT in 480-bit INN set → type='A', adjusted=0 → return 0
	//   ref < 6                    → type='A', adjusted=ref
	//   6 ≤ ref < 16              → type='X', adjusted=ref-6
	//   ref ≥ 16                  → type='T', adjusted=ref-16
	//
	// We approximate the INN set with bounds checking: refs outside valid
	// ranges return 0 (same as the INN set filtering in the original P-code).

	int result = 0;

	if (entityRef < 0) {
		// Invalid
		result = 0;
	} else if (entityRef < 6) {
		// Type 'A' (object/person properties) — subcases 0-5
		int adjusted = entityRef;
		switch (adjusted) {
		case 0:
			// P-code case 0: return 0
			result = 0;
			break;
		case 1: {
			// P-code case 1: cast[personNamed-1].word[10] / 55
			// Word[10] of Person packed record = first packed word after carrying.
			// Contains located(7 bits) and other packed fields.
			// DIVI 55 extracts a field from this packed word.
			int pIdx = _state->_cur.personNamed;
			if (pIdx >= 1 && pIdx <= _data->_castSize) {
				const Person &p = _data->_cast[pIdx];
				// Packed word reconstruction: located is in bits 1-7 of word[10]
				// DIVI 55 likely extracts the located field
				// 55 ≈ 2^1 * ~27... not a clean extraction. Use mood instead?
				// For now, return located (most commonly compared person property)
				result = p.located;
				debugC(kDebugScripts, "Angel VM: lookupFieldValue A.1 person[%d].located=%d", pIdx, result);
			}
			break;
		}
		case 2: {
			// P-code case 2: props[doItToWhat-1].word[5] / 57
			// Word[5] of Object packed record (after Contents[4] + n[1]) = OName+Size
			// DIVI 57 extracts OName from packed OName(9)+Size(4) word
			int oIdx = _state->_cur.doItToWhat;
			if (oIdx >= 1 && oIdx <= _data->_nbrObjects) {
				const Object &o = _data->_props[oIdx];
				result = o.oName;
				debugC(kDebugScripts, "Angel VM: lookupFieldValue A.2 obj[%d].oName=%d", oIdx, result);
			}
			break;
		}
		case 3: {
			// P-code case 3: props[doItToWhat-1].word[11] / 48
			// Word[11] of Object packed record = State+InOrOn+KindOfThing+flags
			// DIVI 48 extracts a field (likely state or kindOfThing)
			int oIdx = _state->_cur.doItToWhat;
			if (oIdx >= 1 && oIdx <= _data->_nbrObjects) {
				const Object &o = _data->_props[oIdx];
				result = o.state;
				debugC(kDebugScripts, "Angel VM: lookupFieldValue A.3 obj[%d].state=%d", oIdx, result);
			}
			break;
		}
		case 4: {
			// P-code case 4: props[doItToWhat-1].word[6] (raw, no division)
			// Word[6] of Object packed record = Value field
			int oIdx = _state->_cur.doItToWhat;
			if (oIdx >= 1 && oIdx <= _data->_nbrObjects) {
				const Object &o = _data->_props[oIdx];
				result = o.value;
				debugC(kDebugScripts, "Angel VM: lookupFieldValue A.4 obj[%d].value=%d", oIdx, result);
			}
			break;
		}
		case 5:
			// P-code case 5: g[3097] — game-specific global variable
			// TODO: Map g[3097] to appropriate GameState field
			debugC(kDebugScripts, "Angel VM: lookupFieldValue A.5 g[3097] (TODO, returning 0)");
			result = 0;
			break;
		}
	} else if (entityRef < 16) {
		// Type 'X' — g[3045 + adjusted * 2], SIND 0 reads word 0
		// g[3045] is the CmdEntry array base (separate from xReg at g[3020]).
		// Each entry is 2 words: {flag (word 0), addr (word 1)}.
		// Dispatch (SIND 1) reads word 1 = _cmdEntry[adj] (the message address).
		// LookupFieldValue (SIND 0) reads word 0 = _cmdFlag[adj] (state/type flag).
		// For ref=8 (adjusted=2): _cmdFlag[2] = 1 for movement commands.
		int adjusted = entityRef - 6;
		if (adjusted >= 0 && adjusted < GameState::kMaxCmdEntries) {
			result = _state->_cmdFlag[adjusted];
			debugC(kDebugScripts, "Angel VM: lookupFieldValue X cmdFlag[%d]=%d (ref=%d)",
			       adjusted, result, entityRef);
		}
	} else if (entityRef < 48) {
		// Type 'T' (xReg countdown) — direct DOS access to .x.
		int adjusted = entityRef - 16;
		if (adjusted >= 0 && adjusted < 32) {
			result = _state->_clock.xReg[adjusted].x;
			debugC(kDebugScripts, "Angel VM: lookupFieldValue T xReg[%d].x=%d (ref=%d)",
			       adjusted, result, entityRef);
		} else {
			debugC(kDebugScripts, "Angel VM: lookupFieldValue T ref=%d out of range, returning 0",
			       entityRef);
		}
	} else {
		// Large refs: P-code data segment addresses for entity fields.
		// The 'T' path accesses g[3020 + (ref-16)*2], which maps to
		// location/person/object record fields in the game state.
		// Delegate to getEntityFieldValue which handles the address mapping.
		result = getEntityFieldValue(entityRef);
	}

	// P-code proc 55 returns abs(result)
	return (result >= 0) ? result : -result;
}

void VM::storeFieldValue(int entityRef, int value) {
	// P-code proc 54: classify entity ref, dispatch to write.
	// Classification matches lookupFieldValue (proc 53):
	//   ref < 6     → 'A' (object/person properties)
	//   6 ≤ ref < 16 → 'X' (cmdEntry at g[3045+adj*2])
	//   ref ≥ 16    → 'T' (xReg at g[3020+adj*2])

	if (entityRef < 0) {
		return;
	} else if (entityRef < 6) {
		// Type 'A' — object/person fields
		int subcase = entityRef;
		switch (subcase) {
		case 0:
			break;
		case 1: {
			// Person[personNamed].located
			int pIdx = _state->_cur.personNamed;
			if (pIdx >= 1 && pIdx <= _data->_castSize) {
				// Clamp to valid range
				if (value > 9) value = 9;
				_data->_cast[pIdx].located = value;
				debugC(kDebugScripts, "Angel VM: storeFieldValue A.1 person[%d].located=%d", pIdx, value);
			}
			break;
		}
		case 2: {
			// Object[doItToWhat].oName
			int oIdx = _state->_cur.doItToWhat;
			if (oIdx >= 1 && oIdx <= _data->_nbrObjects) {
				_data->_props[oIdx].oName = value;
				debugC(kDebugScripts, "Angel VM: storeFieldValue A.2 obj[%d].oName=%d", oIdx, value);
			}
			break;
		}
		case 3: {
			// Object[doItToWhat].state
			int oIdx = _state->_cur.doItToWhat;
			if (oIdx >= 1 && oIdx <= _data->_nbrObjects) {
				_data->_props[oIdx].state = value;
				debugC(kDebugScripts, "Angel VM: storeFieldValue A.3 obj[%d].state=%d", oIdx, value);
			}
			break;
		}
		case 4: {
			// Object[doItToWhat].value
			int oIdx = _state->_cur.doItToWhat;
			if (oIdx >= 1 && oIdx <= _data->_nbrObjects) {
				_data->_props[oIdx].value = value;
				debugC(kDebugScripts, "Angel VM: storeFieldValue A.4 obj[%d].value=%d", oIdx, value);
			}
			break;
		}
		case 5:
			// g[3097] — game-specific global
			debugC(kDebugScripts, "Angel VM: storeFieldValue A.5 g[3097]=%d (TODO)", value);
			break;
		}
	} else if (entityRef < 16) {
		// Type 'X' — cmdFlag[entityRef-6] (word 0 of CmdEntry records at g[3045])
		int adjusted = entityRef - 6;
		if (adjusted >= 0 && adjusted < GameState::kMaxCmdEntries) {
			_state->_cmdFlag[adjusted] = value;
			debugC(kDebugScripts, "Angel VM: storeFieldValue X cmdFlag[%d]=%d (ref=%d)",
			       adjusted, value, entityRef);
		}
	} else if (entityRef < 48) {
		// Type 'T' — xReg[entityRef-16].x
		int adjusted = entityRef - 16;
		if (adjusted >= 0 && adjusted < 32) {
			_state->_clock.xReg[adjusted].x = value;
			debugC(kDebugScripts, "Angel VM: storeFieldValue T xReg[%d].x=%d (ref=%d)",
			       adjusted, value, entityRef);
		} else {
			debugC(kDebugScripts, "Angel VM: storeFieldValue T ref=%d out of range, skipping",
			       entityRef);
		}
	} else {
		// Large refs: P-code data segment addresses for entity fields.
		storeEntityFieldValue(entityRef, value);
	}
}

int VM::readCompValueFromStream() {
	// P-code proc 58: reads getNip (1 nip) from the message stream.
	// If result == 0: reads getNumber18() (3 more nips) as a literal value.
	// If result != 0: calls lookupFieldValue(result) to get current field value.
	// Stores the field ref in _lastFieldRef for subsequent storeFieldValue.
	//
	// Total nip consumption: 1 nip (literal=0: +3 = 4 total; field ref: 1 total)
	int startPos = _state->_msgPos;
	int num = getNip();
	_lastFieldRef = num;  // Remember which field this refers to (0 = literal)

	if (num == 0) {
		// Literal path: proc 6 reads 3 nips (18-bit value)
		int val = getNumber18();
		debugC(kDebugScripts, "Angel VM: readCompValueFromStream: literal=%d @pos=%d",
		       val, startPos);
		return val;
	}
	// Field reference path: look up current value (0 more nips)
	int val = lookupFieldValue(num);
	debugC(kDebugScripts, "Angel VM: readCompValueFromStream: fieldRef=%d → value=%d @pos=%d",
	       num, val, startPos);
	return val;
}

int VM::getEntityFieldValue(int address) {
	// UCSD Pascal data-segment address layout:
	// Each entity array has a base address and fixed record size (in words).
	// address = BASE + (entityIndex - 1) * recordSize + fieldOffset
	//
	// Place record: 31 words (BASE_MAP = 296)
	//   0:n  1:shortDscr  2-7:nextPlace[0..5]  8-13:traffic[0..5]
	//   14:curb  15:accessLock  16:mustHave  17:fogPath
	//   18-19:people(2w)  20-23:objects(4w)  24:view
	//   25:useThe  26:foggy  27:itsADoor  28:itsOpen  29:itsLocked  30:unseen

	static const int BASE_MAP = 296;
	static const int RSIZE_PLACE = 31;
	int mapEnd = BASE_MAP + _data->_nbrLocations * RSIZE_PLACE;

	if (address >= BASE_MAP && address < mapEnd) {
		int rel = address - BASE_MAP;
		int idx = rel / RSIZE_PLACE + 1;  // 1-based entity index
		int fld = rel % RSIZE_PLACE;

		if (idx < 1 || idx > _data->_nbrLocations) {
			warning("Angel VM: getEntityFieldValue: Place[%d] out of range", idx);
			return 0;
		}

		const Place &p = _data->_map[idx];
		switch (fld) {
		case 0:  return p.n;
		case 1:  return p.shortDscr;
		case 2: case 3: case 4: case 5: case 6: case 7:
			return p.nextPlace[fld - 2];
		case 8: case 9: case 10: case 11: case 12: case 13:
			return p.traffic[fld - 8] ? 1 : 0;
		case 14: return 0;  // curb (DirSet) — TODO
		case 15: return p.accessLock;
		case 16: return p.mustHave;
		case 17: return p.fogPath;
		case 18: case 19: {
			// PersonSet: 2 x 16-bit words from the people bit set.
			// In P-code, stored as SET OF 0..31 (2 words).
			uint32 bits = p.people.getWord(0);
			return (fld == 18) ? (int)(bits & 0xFFFF) : (int)((bits >> 16) & 0xFFFF);
		}
		case 20: case 21: case 22: case 23: {
			// ObjSet: 4 x 16-bit words from the objects bit set.
			int wordIdx = (fld - 20) / 2;
			uint32 bits = p.objects.getWord(wordIdx);
			return ((fld - 20) % 2 == 0) ? (int)(bits & 0xFFFF) : (int)((bits >> 16) & 0xFFFF);
		}
		case 24: return (int)p.view;
		case 25: return p.useThe ? 1 : 0;
		case 26: return p.foggy ? 1 : 0;
		case 27: return p.itsADoor ? 1 : 0;
		case 28: return p.itsOpen ? 1 : 0;
		case 29: return p.itsLocked ? 1 : 0;
		case 30: return p.unseen ? 1 : 0;
		default:
			warning("Angel VM: getEntityFieldValue: Place field %d unknown", fld);
			return 0;
		}
	}

	// TODO: Person, Object, Vehicle entity ranges (come after locations)
	debugC(kDebugScripts, "Angel VM: getEntityFieldValue: address %d not in known range (mapRange=%d-%d)",
	        address, BASE_MAP, mapEnd - 1);
	return 0;
}

void VM::storeEntityFieldValue(int address, int value) {
	static const int BASE_MAP = 296;
	static const int RSIZE_PLACE = 31;
	int mapEnd = BASE_MAP + _data->_nbrLocations * RSIZE_PLACE;

	if (address >= BASE_MAP && address < mapEnd) {
		int rel = address - BASE_MAP;
		int idx = rel / RSIZE_PLACE + 1;
		int fld = rel % RSIZE_PLACE;

		if (idx < 1 || idx > _data->_nbrLocations) {
			warning("Angel VM: storeEntityFieldValue: Place[%d] out of range", idx);
			return;
		}

		Place &p = _data->_map[idx];
		debugC(kDebugScripts, "Angel VM: storeEntityFieldValue Place[%d].field[%d] = %d", idx, fld, value);
		switch (fld) {
		case 0:  p.n = value; break;
		case 1:  p.shortDscr = value; break;
		case 2: case 3: case 4: case 5: case 6: case 7:
			p.nextPlace[fld - 2] = value; break;
		case 8: case 9: case 10: case 11: case 12: case 13:
			p.traffic[fld - 8] = (value != 0); break;
		case 15: p.accessLock = value; break;
		case 16: p.mustHave = value; break;
		case 17: p.fogPath = value; break;
		case 24: p.view = (Aspect)value; break;
		case 25: p.useThe = (value != 0); break;
		case 26: p.foggy = (value != 0); break;
		case 27: p.itsADoor = (value != 0); break;
		case 28: p.itsOpen = (value != 0); break;
		case 29: p.itsLocked = (value != 0); break;
		case 30: p.unseen = (value != 0); break;
		default:
			debugC(kDebugScripts, "Angel VM: storeEntityFieldValue: Place field %d unhandled", fld);
			break;
		}
		return;
	}

	debugC(kDebugScripts, "Angel VM: storeEntityFieldValue: address %d not in known range", address);
}

} // End of namespace Angel
} // End of namespace Glk
