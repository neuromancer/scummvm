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
      _capitalizeNext(false), _suppressText(true),
      _entityFlag(false), _entityValue(0), _entityOp(0), _entityType(-1) {
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

	// Read the first chunk at the message address
	_state->_vmCurRecord = _data->readChunk(addr);

	// Messages have a 2-nip header: (nip0 << 6) | nip1 = length/marker.
	// The original ForceMsg "sets the first two nips of Msg to zero".
	// Content starts at nip position 2. Read and skip the header.
	int hdr0 = getNip();
	int hdr1 = getNip();
	_state->_msgLength = (hdr0 << 6) | hdr1;

	warning("Angel VM: openMsg addr=%d hdrLen=%d caller=%s", addr, _state->_msgLength, caller);
}

int VM::getNip() {
	if (_state->_eom)
		return 0;

	int pos = _state->_msgPos;
	int nip = _state->_vmCurRecord.getNip(_state->_msgCursor);
	bumpMsg();
	// Verbose tracing for WELCOME message (addr 4098)
	if (_state->_msgBase == 4098 && pos >= 92) {
		warning("Angel VM: getNip pos=%d nip=%d", pos, nip);
	}
	return nip;
}

char VM::getAChar() {
	int pos = _state->_msgPos;
	int nip = getNip();
	if (_state->_eom)
		return kEndSym;
	char ch = _data->_yTable[nip];
	// Verbose tracing for WELCOME message (addr 4098)
	if (_state->_msgBase == 4098 && pos >= 92) {
		warning("Angel VM: getAChar pos=%d nip=%d char=%d '%c'",
			pos, nip, (int)ch, (ch >= 32 && ch < 127) ? ch : '?');
	}
	return ch;
}

int VM::getNumber() {
	// Read a 12-bit number from 2 nips (big-endian)
	int hi = getNip();
	int lo = getNip();
	return (hi << 6) | lo;
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

void VM::displayMsg(int addr) {
	warning("Angel VM: displayMsg(%d) called, callDepth=%d", addr, _callDepth);
	openMsg(addr, "displayMsg");
	executeMsg();
	warning("Angel VM: displayMsg(%d) returned", addr);
}

void VM::executeMsg() {
	debugC(kDebugScripts, "Angel VM: executeMsg starting, msgBase=%d msgLength=%d", _state->_msgBase, _state->_msgLength);
	int charCount = 0;
	int iterCount = 0;
	Common::String textOutput;  // Accumulate text for debug
	while (!_state->_eom && _state->_stillPlaying) {
		if (++iterCount > 5000) {
			warning("Angel VM: executeMsg runaway loop after %d iterations at pos=%d base=%d",
			        iterCount, _state->_msgPos, _state->_msgBase);
			_state->_eom = true;
			break;
		}
		char ch = getAChar();

		if (_state->_eom)
			break;

		// Check for control codes
		switch (ch) {
		case kEndSym:
			// End of message / return from call
			if (_callDepth > 0) {
				_callDepth--;
				CallFrame &frame = _callStack[_callDepth];
				warning("Angel VM: EndSym at pos=%d base=%d depth=%d → restore base=%d pos=%d cursor=%d",
				        _state->_msgPos, _state->_msgBase, _callDepth + 1,
				        frame.base, frame.pos, frame.cursor);
				_state->_msgBase = frame.base;
				_state->_msgPos = frame.pos;
				_state->_msgCursor = frame.cursor;
				_state->_msgLength = frame.length;
				_state->_vmCurRecord = _data->readChunk(
					_state->_msgBase + (_state->_msgPos / kChunkSize));
			} else {
				warning("Angel VM: EndSym at pos=%d base=%d depth=0 → EOM", _state->_msgPos, _state->_msgBase);
				_state->_eom = true;
			}
			break;

		case kJU:
			// Unconditional jump: next 2 nips = forward offset
			{
				int target = getNumber();
				warning("Angel VM: JU target=%d from pos=%d -> pos=%d", target, _state->_msgPos, _state->_msgPos + target);
				jump(target);
			}
			break;

		case kJF:
			// Jump if FALSE: next 2 nips = forward offset
			{
				int target = getNumber();
				if (!_state->_tfIndicator) {
					warning("Angel VM: JF target=%d tf=F from pos=%d -> pos=%d", target, _state->_msgPos, _state->_msgPos + target);
				}
				if (!_state->_tfIndicator)
					jump(target);
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
			// Action with reference — p-code (proc 93):
			//   NAT_F0 32(0, 50)  → getNip + kActionOpcodeBase (1 nip for action opcode)
			//   NAT_F0 32(0, 135) → getNip + kFeOpcodeBase (1 nip for ref operation)
			//   NAT_F0 35         → entity resolution (sets context)
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
				warning("Angel VM: kFar opNip=%d op=%d refNip=%d refOp=%d ref=%d entityFlag=%d entityValue=%d",
				        opNip, opVal, refNip, refOp, ref, _entityFlag ? 1 : 0, _entityValue);
				if (opVal >= kNumOperations) {
					warning("Angel VM: Unknown action+ref opcode nip=%d op=%d", opNip, opVal);
				} else if (opVal >= kEditOpcodeBase) {
					executeEdit((Operation)opVal, ref);
				} else {
					executeAction((Operation)opVal, ref);
				}
			}
			break;

		case kFt:
			// Test without reference — nip + kTestOpcodeBase = Operation
			{
				int opNip = getNip();
				int opVal = opNip + kTestOpcodeBase;
				if (opVal < kNumOperations)
					executeTest((Operation)opVal, 0);
				else
					warning("Angel VM: Unknown test opcode nip=%d op=%d", opNip, opVal);
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
				warning("Angel VM: kFtr opNip=%d op=%d refNip=%d refOp=%d ref=%d entityFlag=%d entityValue=%d entityType=%d",
				        opNip, opVal, refNip, refOp, ref, _entityFlag ? 1 : 0, _entityValue, _entityType);
				if (opVal < kNumOperations)
					executeTest((Operation)opVal, ref);
				else
					warning("Angel VM: Unknown test+ref opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kFe:
			// Fe (display/reference op) — nip + kFeOpcodeBase(135) = Operation
			// From proc 96 disassembly: NAT_F0 32(0, 135) reads nip + 135.
			// XJP dispatch covers cases 136..164 (kXRegOp..kSpkOp).
			{
				int opNip = getNip();
				int opVal = opNip + kFeOpcodeBase;
				if (opVal < kNumOperations)
					executeFe((Operation)opVal, 0);
				else
					warning("Angel VM: Unknown Fe opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kFer:
			// Fer (display/reference op with ref) — nip + kFeOpcodeBase = Operation
			// From proc 101 disassembly: reads op nip + 135, then ref nip + 135
			// (resolved via getRefValue). kInvOp skips the ref read.
			{
				int opNip = getNip();
				int opVal = opNip + kFeOpcodeBase;
				int ref = 0;
				if (opVal != kInvOp) {
					// Read reference as getNip() + kFeOpcodeBase → resolve via getRefValue
					int refNip = getNip();
					int refOp = refNip + kFeOpcodeBase;
					if (refOp < kNumOperations)
						ref = getRefValue((Operation)refOp);
					else
						warning("Angel VM: Fer ref out of range nip=%d refOp=%d", refNip, refOp);
				}
				if (opVal < kNumOperations)
					executeFe((Operation)opVal, ref);
				else
					warning("Angel VM: Unknown Fer opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kFCall:
			// Procedure call
			{
				int addr = getNumber();
				warning("Angel VM: FCall addr=%d depth=%d returnBase=%d returnPos=%d",
				        addr, _callDepth, _state->_msgBase, _state->_msgPos);
				if (_callDepth < kMaxCallDepth) {
					CallFrame &frame = _callStack[_callDepth++];
					frame.base = _state->_msgBase;
					frame.pos = _state->_msgPos;
					frame.cursor = _state->_msgCursor;
					frame.length = _state->_msgLength;
					openMsg(addr, "FCall");
				} else {
					warning("Angel VM: Call stack overflow");
				}
			}
			break;

		default:
			// Regular text character — output unless suppressed
			if (_suppressText) {
				debugC(kDebugScripts, "Angel VM: suppressed text char '%c' at pos=%d", ch, _state->_msgPos - 1);
				break;
			}
			charCount++;
			if (_capitalizeNext) {
				if (ch >= 'a' && ch <= 'z')
					ch = ch - 'a' + 'A';
				_capitalizeNext = false;
			}
			textOutput += ch;
			if (textOutput.size() >= 80) {
				warning("Angel VM text: %s", textOutput.c_str());
				textOutput.clear();
			}
			_engine->putChar(ch);
			break;
		}
	}
	if (!textOutput.empty()) {
		warning("Angel VM text: %s", textOutput.c_str());
	}
	debugC(kDebugScripts, "Angel VM: executeMsg ending, charCount=%d eom=%d", charCount, _state->_eom);
}

void VM::executeCase() {
	// CSE interleaved content format (from proc 103 / proc 5 disassembly):
	//   type: getNip (KindOfCase: 0=Random, 1=Word, 2=Syn, 3=Ref)
	//   [matchRef: getNumber — only for RefCase]
	//   nbrCases: getNip
	//   totalSize: getNumber (nips from here to end of CSE block)
	//   entries: nbrCases × [val: getNip, skip: getNumber, content...]
	//
	// Interleaved model: each entry's content is inline after its header.
	// When NOT matched: jump(skip) to skip over the content to next entry.
	// When matched: don't jump — return and let executeMsg process the content.
	// val == 0 is a special "default" that always matches.

	int caseType = getNip();
	KindOfCase kind = (KindOfCase)caseType;

	// For RefCase, read the match reference from the stream.
	// proc 103 p-code: NAT_F0 32(0, 135) + NAT_F0 35 reads 2 nips (getNumber).
	// Empirically verified: getNumber() gives matchRef=131, nbrCases=11, totalSize=2434
	// which align with 11 EndSym boundaries in the raw nip data.
	int matchRef = 0;
	if (kind == kRefCase) {
		matchRef = getNumber();
	}

	int nbrCases = getNip();
	int totalSize = getNumber();
	int endPos = _state->_msgPos + totalSize;

	// Determine match value based on case type
	int matchValue = 0;
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
		if (matchRef < kNumOperations)
			matchValue = getRefValue((Operation)matchRef);
		break;

	default:
		warning("Angel VM: Unknown case type %d", caseType);
		break;
	}

	warning("Angel VM: CSE type=%d nbrCases=%d matchValue=%d matchRef=%d totalSize=%d endPos=%d pos=%d",
	        caseType, nbrCases, matchValue, matchRef, totalSize, endPos, _state->_msgPos);

	// Iterate through entries with interleaved content.
	// Entry format: val=getNip (1 nip), skip=getNumber (2 nips), then inline content.
	// Empirically verified against raw game data: val=getNip produces coherent case
	// values (0-54) and readable text content. val=getNumber produces garbage (3456+).
	// Note: p-code shows CPI 3,5(0,1) for val which nominally means getNumber, but
	// the game data is authoritative — likely a platform-specific parameter convention.
	bool matched = false;
	for (int i = 0; i < nbrCases && !matched; i++) {
		int caseValue = getNip();     // val is getNip (1 nip) — empirically verified
		int skip = getNumber();       // skip is getNumber (2 nips)

		warning("Angel VM: CSE entry[%d] val=%d skip=%d pos=%d", i, caseValue, skip, _state->_msgPos);

		bool isMatch;
		if (kind == kRandomCase) {
			isMatch = (i == matchValue);
		} else if (caseValue == 0) {
			isMatch = true;  // val=0 is unconditional default (proc 103 p-code)
		} else {
			isMatch = (caseValue == matchValue);
		}

		if (isMatch) {
			matched = true;
			warning("Angel VM: CSE MATCHED entry %d (val=%d), content at pos=%d", i, caseValue, _state->_msgPos);
			// Push a return frame so that EndSym within the CSE content
			// returns to endPos (past the entire CSE block) instead of
			// terminating the entire displayMsg call. This matches the
			// original p-code epilogue: NAT_F0 66 + Jump(endPos-currentPos).
			if (_callDepth < kMaxCallDepth) {
				CallFrame &frame = _callStack[_callDepth++];
				frame.base = _state->_msgBase;
				frame.pos = endPos;
				frame.cursor = endPos % kChunkSize;
				frame.length = _state->_msgLength;
			}
			// Stream is at content start — return and let executeMsg process it.
			// When content hits EndSym, the frame will pop and resume at endPos.
		} else {
			// Skip over this entry's inline content
			warning("Angel VM: CSE skip entry %d, jump(%d) from pos=%d", i, skip, _state->_msgPos);
			jump(skip);
		}
	}

	if (!matched) {
		// No match found — skip to end of CSE block
		warning("Angel VM: CSE no match, jumping to endPos=%d from pos=%d", endPos, _state->_msgPos);
		if (_state->_msgPos < endPos) {
			jump(endPos - _state->_msgPos);
		}
	}

	// If matched, the stream is positioned at the start of the matched
	// entry's inline content. executeMsg() will process it naturally
	// (text output, opcodes, JU/JF flow control, etc.).
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
		// In VM bytecode context (kFa dispatch), kPkUpOp controls text suppression
		// (proc 12 in RESPOND segment). Sets seg[20].global[5] := 0 (suppress text),
		// then conditionally enables based on game state check. During WELCOME init
		// the condition fails, so text stays suppressed.
		_suppressText = true;
		debugC(kDebugScripts, "Angel VM: kPkUpOp → suppress text");
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
		// In VM bytecode context (kFa dispatch), kPtOnOp controls text suppression
		// (proc 15 in RESPOND segment). Sets seg[20].global[5] := 0 first, then
		// conditionally enables if game state check passes. During WELCOME init
		// the condition typically fails, keeping text suppressed.
		// TODO: implement conditional enable based on CXG 20,10 check
		_suppressText = true;
		debugC(kDebugScripts, "Angel VM: kPtOnOp → suppress text (conditional enable TODO)");
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

void VM::executeFe(Operation op, int ref) {
	switch (op) {
	case kCapOp:
		// Set capitalize-next-character flag and clear text suppression
		_capitalizeNext = true;
		_suppressText = false;
		debugC(kDebugScripts, "Angel VM: Fe kCapOp → capitalize next char, unsuppress");
		break;

	case kForceOp:
		// Force output flush + clear flags
		_engine->forceOutput();
		_engine->outLn();
		_capitalizeNext = false;
		_suppressText = false;
		debugC(kDebugScripts, "Angel VM: Fe kForceOp → flush output, unsuppress");
		break;

	case kSpkOp:
		// Like kForceOp but without the newline
		_capitalizeNext = false;
		debugC(kDebugScripts, "Angel VM: Fe kSpkOp ref=%d", ref);
		break;

	case kDscOp:
		// Display a description message. Via kFe: reads getNumber() for address.
		// Via kFer: uses ref as address.
		{
			int addr = ref;
			if (ref == 0) {
				// kFe path: read address from stream (CPI 3,5(0,1) = getNumber)
				addr = getNumber();
			}
			warning("Angel VM: Fe kDscOp addr=%d ref=%d", addr, ref);
			if (addr > 0)
				displayMsg(addr);
		}
		break;

	case kAOp:
		// Print article "a"/"an" (or "the"). Via kFe: reads getNumber.
		// Via kFer: uses ref.
		{
			int target = ref;
			if (ref == 0) {
				// kFe path: read from stream
				target = getNumber();
			}
			// TODO: implement proper a/an/the logic based on target entity
			warning("Angel VM: Fe kAOp target=%d ref=%d (article stub)", target, ref);
		}
		break;

	case kPrintOp:
		// Print a numbered message (same as action kPrintOp)
		{
			int addr = ref;
			if (ref == 0) {
				addr = getNumber();
			}
			warning("Angel VM: Fe kPrintOp addr=%d", addr);
			if (addr > 0)
				displayMsg(addr);
		}
		break;

	case kInvOp:
		// Inventory display — no extra nips via kFe
		warning("Angel VM: Fe kInvOp (stub)");
		break;

	case kTimeOp:
		// Display current time
		{
			char timeBuf[32];
			snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s",
			         _state->_clock.hour, _state->_clock.minute,
			         _state->_clock.am ? "AM" : "PM");
			_engine->putWord(timeBuf);
		}
		break;

	case kXRegOp:
		// xReg manipulation — no extra nips via kFe
		warning("Angel VM: Fe kXRegOp ref=%d (stub)", ref);
		break;

	case kCtntsOp:
		// Display container contents
		warning("Angel VM: Fe kCtntsOp ref=%d (stub)", ref);
		break;

	case kCtnrOp:
		// Display container name
		warning("Angel VM: Fe kCtnrOp ref=%d (stub)", ref);
		break;

	case kFleetOp:
		warning("Angel VM: Fe kFleetOp ref=%d (stub)", ref);
		break;

	case kRoleOp:
		warning("Angel VM: Fe kRoleOp ref=%d (stub)", ref);
		break;

	case kCntrOp:
		// Counter display
		warning("Angel VM: Fe kCntrOp ref=%d (stub)", ref);
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
				default:
					break; // types 5-10 (verb, day, etc.): no display
				}
				if (!name.empty()) {
					if (useThe)
						_engine->putWord("the ");
					for (uint i = 0; i < name.size(); i++)
						_engine->putChar(name[i]);
					warning("Angel VM: Fe ref-value op %d → displayed '%s%s'",
					        (int)op, useThe ? "the " : "", name.c_str());
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
					warning("Angel VM: Fe ref-value op %d type=%d val=%d nameIdx=%d nbrVWords=%d → no name",
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
	warning("Angel VM: opPrint(%d)", ref);
	if (ref > 0)
		displayMsg(ref);
}

void VM::opDsc(int ref) {
	// Describe a location or object
	// ref = description key (n field of Place or Object)
	warning("Angel VM: opDsc(%d)", ref);
	if (ref > 0)
		displayMsg(ref);
}

void VM::opAOp() {
	warning("Angel VM: opAOp not correctly implemented - missing vowel check for a/an");
}

void VM::opInv() {
	warning("Angel VM: opInv not correctly implemented - needs vocab name lookup");
}

void VM::opSpk(int ref) {
	warning("Angel VM: opSpk(%d) not implemented", ref);
}

void VM::opCap(int ref) {
	warning("Angel VM: opCap(%d) not implemented (via action dispatch)", ref);
}

void VM::opForce(int ref) {
	// Force output queue flush
	_engine->forceOutput();
}

void VM::opTake() {
	// Player takes an object
	int obj = _state->_cur.doItToWhat;
	if (obj > 0 && obj <= _data->_nbrObjects) {
		Place &loc = _data->_map[_state->_location];
		if (loc.objects.has(obj)) {
			loc.objects.unset(obj);
			_state->_possessions.set(obj);
			_state->_nbrPossessions++;
		}
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
	warning("Angel VM: opSwap not correctly implemented - only gives, doesn't grab back");
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
	warning("Angel VM: opRide not implemented");
}

void VM::opRLoc(int ref) {
	// Set player location to ref
	if (ref > 0 && ref <= _data->_nbrLocations) {
		_state->_pprvLocation = _state->_prvLocation;
		_state->_prvLocation = _state->_location;
		_state->_location = ref;
		_state->_trail.set(ref);
	}
}

void VM::opNxStop() {
	warning("Angel VM: opNxStop not implemented");
}

void VM::opOffer() {
	warning("Angel VM: opOffer not implemented");
}

void VM::opTour() {
	_state->_touring = true;
	_state->_tourPoint = _state->_location;
}

void VM::opRRide() {
	warning("Angel VM: opRRide not implemented");
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
	warning("Angel VM: opBluff not implemented");
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
	warning("Angel VM: opMrdr not implemented");
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
	warning("Angel VM: opPutIt not implemented");
}

void VM::opPourIt() {
	warning("Angel VM: opPourIt not implemented");
}

void VM::opSave() {
	_engine->saveGame();
}

void VM::opQuit() {
	_state->_stillPlaying = false;
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
	// Set event register: ref encodes register index and procedure addr
	// Fe EVENT <reg> <addr>
	int reg = ref;
	int addr = getNumber();
	if (reg >= 0 && reg < 10) {
		_state->_clock.xReg[reg].proc = addr;
	}
}

void VM::opSet(int ref) {
	// Set object attribute (case 75 in kFa XJP -> L_28f3)
	// p-code: CXG 18,15 reads 1 nip (attribute index),
	// then NAT_F0 53 resolves it (reads 2 more getNumber values from stream),
	// LEQS set operations, NAT_F0 58/54 apply changes and enable text display.
	//
	// Stream consumption: 1 nip (attrNip) + 2 getNumber (4 nips) = 5 nips total
	int attrNip = getNip();
	int setVal1 = getNumber();  // NAT_F0 53 reads these from stream
	int setVal2 = getNumber();

	// NAT_F0 58/54 apply set operations and enable text display
	// (sets seg[20].global[5] := 1, enabling text output after init)
	_suppressText = false;

	warning("Angel VM: opSet(ref=%d, attrNip=%d, val1=%d, val2=%d) stub - text enabled",
	        ref, attrNip, setVal1, setVal2);
}

void VM::opSsp(int ref) {
	// Suspend a person (make them rest)
	if (ref > 0 && ref <= _data->_castSize) {
		_data->_cast[ref].resting = true;
	}
}

void VM::opRsm(int ref) {
	// Resume a person (make them active)
	if (ref > 0 && ref <= _data->_castSize) {
		_data->_cast[ref].resting = false;
	}
}

void VM::opSw(int ref) {
	warning("Angel VM: opSw(%d) not implemented", ref);
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
	warning("Angel VM: opAttr(%d) not implemented", ref);
}

void VM::opAsg(int ref) {
	// Assign xReg event register (case 83 in kFa XJP → L_2949)
	// p-code: CPI 3,5(0,1) → flag=1 means getNumber (2 nips)
	// UCSD p-machine param order: local[2]=last pushed=1 (the flag)
	// Then dispatches on xReg[index] sub-field via XJP cases 0-3:
	//   case 0: set xReg[i].proc to resolved set value
	//   case 1: set global[3008] (vocabulary set)
	//   case 2: set global[3010] (entity set) + derived lookups
	//   case 3: set global[3009] (attribute set)
	int xRegValue = getNumber();  // 2 nips: CPI 3,5 with flag=1
	warning("Angel VM: opAsg(ref=%d, value=%d) stub - nips consumed but logic not implemented", ref, xRegValue);
}

void VM::opMov(int ref) {
	// Move an object to a location
	int obj = ref;
	int dest = getNumber();
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
	}
}

void VM::opRst(int ref) {
	warning("Angel VM: opRst(%d) not implemented", ref);
}

void VM::opIncr(int ref) {
	warning("Angel VM: opIncr(%d) not implemented", ref);
}

void VM::opDecr(int ref) {
	warning("Angel VM: opDecr(%d) not implemented", ref);
}

void VM::opAdd(int ref) {
	warning("Angel VM: opAdd(%d) not implemented", ref);
}

void VM::opSub(int ref) {
	warning("Angel VM: opSub(%d) not implemented", ref);
}

// ============================================================
// Test implementations
// ============================================================

bool VM::testHere(int ref) {
	Place &loc = _state->map(_state->_location);
	warning("Angel VM: testHere ref=%d loc=%d people.empty=%d objects.empty=%d",
	        ref, _state->_location, loc.people.isEmpty() ? 1 : 0, loc.objects.isEmpty() ? 1 : 0);
	if (ref > 0) {
		// Specific ref: check objects then people
		if (ref <= _data->_nbrObjects && loc.objects.has(ref))
			return true;
		if (ref <= _data->_castSize && loc.people.has(ref))
			return true;
		return false;
	}
	// No ref: is any person present at this location?
	return !loc.people.isEmpty();
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
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].itsLocked;
	return false;
}

bool VM::testOpened(int ref) {
	if (ref > 0 && ref <= _data->_nbrObjects)
		return _data->_props[ref].itsOpen;
	return false;
}

bool VM::testClosed(int ref) {
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
	// Dead end: no path from current location in current direction
	Place &loc = _state->map(_state->_location);
	for (int d = 0; d < kNumDirections; d++) {
		if (loc.nextPlace[d] > kNowhere)
			return false;
	}
	return true;
}

bool VM::testKey(int ref) {
	// Does player have the key (access right) for ref?
	return _state->_capabilities.has(ref);
}

bool VM::testHPass(int ref) {
	// Has player visited location ref?
	return _state->_trail.has(ref);
}

bool VM::testVKey(int ref) {
	warning("Angel VM: testVKey(%d) not implemented", ref);
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
	warning("Angel VM: testSyn(%d) not implemented", ref);
	return false;
}

bool VM::testNew(int ref) {
	// Is the location ref unseen (new)?
	if (ref > 0 && ref <= _data->_nbrLocations)
		return _data->_map[ref].unseen;
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
		// $ path: inline entity specification.
		// Reads a getNumber() value and compares _entityOp against it.
		// This tests "Is the entity reference type X?"
		int entityNum = getNumber();
		bool result = (_entityOp == entityNum);
		warning("Angel VM: testIs(ref=%d) $ path entityOp=%d entityNum=%d result=%s",
		        ref, _entityOp, entityNum, result ? "TRUE" : "FALSE");
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
			warning("Angel VM: testIs(ref=%d) non-$ flags failed: oldFlag=%d newFlag=%d -> FALSE",
			        ref, oldEntityFlag ? 1 : 0, _entityFlag ? 1 : 0);
			return false;
		}

		// Compare entity values based on entity type.
		// For objects (0), persons (1), and most types: simple equality.
		bool result = (_entityValue == oldEntityValue);
		warning("Angel VM: testIs(ref=%d) non-$ ch='%c'(%d) newOp=%d newValue=%d oldValue=%d type=%d result=%s",
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
	switch (op) {
	case kItOp:      return _state->_thing;
	case kTargOp:    return _state->_cur.doItToWhat;
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
	case kXRegOp:    warning("Angel VM: getRefValue(kXRegOp) not implemented"); return 0;
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
		_entityType = 6;  // kAVerb
		_entityValue = _state->_verb;
		_entityFlag = (_entityValue != 0);
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
		_entityValue = _state->_placeNamed;
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
		// P-code overwrites entityValue=1 after CPL 41 — the flag
		// still reflects whether location is valid.
		_entityType = 2;
		_entityValue = _state->_location;
		_entityFlag = (_entityValue != kNowhere);
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
		// Operations above 155 (kDscOp, kAOp, etc.) are outside
		// proc 35's XJP range — entity stays unresolved (flag=false).
		warning("Angel VM: resolveEntity op=%d outside range 135-155", op);
		break;
	}

	warning("Angel VM: resolveEntity op=%d → flag=%d value=%d type=%d",
	        op, _entityFlag ? 1 : 0, _entityValue, _entityType);
}

} // End of namespace Angel
} // End of namespace Glk
