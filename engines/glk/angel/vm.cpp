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
#include "glk/glk.h"
#include "common/debug.h"
#include "common/textconsole.h"
#include "common/random.h"

namespace Glk {
namespace Angel {

VM::VM(Angel *engine, GameData *data, GameState *state)
    : _engine(engine), _data(data), _state(state), _callDepth(0) {
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

	// Debug: dump nips at key positions for message 4546
	if (addr == 4546) {
		Common::String dump;
		for (int p = 95; p <= 120; p++) {
			int cursor = p % kChunkSize;
			int chunkAddr = addr + (p / kChunkSize);
			Chunk c = _data->readChunk(chunkAddr);
			int nip = c.getNip(cursor);
			char ch = _data->_yTable[nip];
			dump += Common::String::format(" %d:%d(%c)", p, nip, (ch >= 32 && ch < 127) ? ch : '?');
		}
		warning("Angel VM: msg4546 nips[95-120]:%s", dump.c_str());
		dump.clear();
		for (int p = 295; p <= 320; p++) {
			int cursor = p % kChunkSize;
			int chunkAddr = addr + (p / kChunkSize);
			Chunk c = _data->readChunk(chunkAddr);
			int nip = c.getNip(cursor);
			char ch = _data->_yTable[nip];
			dump += Common::String::format(" %d:%d(%c)", p, nip, (ch >= 32 && ch < 127) ? ch : '?');
		}
		warning("Angel VM: msg4546 nips[295-320]:%s", dump.c_str());
	}
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
	char ch = _data->_yTable[nip];
	// Debug output only for first 5 chars or control codes
	// debugC(kDebugScripts, "Angel VM: getAChar pos=%d nip=%d char=%d '%c'",
	// 	_state->_msgPos - 1, nip, (int)ch, (ch >= 32 && ch < 127) ? ch : '?');
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
	// "Jump ahead n places in the current message" (relative forward)
	int newPos = _state->_msgPos + n;
	if (newPos > _state->_msgLength && _state->_msgLength > 0) {
		_state->_eom = true;
		return;
	}
	_state->_msgPos = newPos;
	_state->_msgCursor = newPos % kChunkSize;
	int chunkAddr = _state->_msgBase + (newPos / kChunkSize);
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
			warning("Angel VM: EndSym at pos=%d base=%d depth=%d", _state->_msgPos, _state->_msgBase, _callDepth);
			if (_callDepth > 0) {
				// Return from FCall
				_callDepth--;
				CallFrame &frame = _callStack[_callDepth];
				_state->_msgBase = frame.base;
				_state->_msgPos = frame.pos;
				_state->_msgCursor = frame.cursor;
				_state->_msgLength = frame.length;
				_state->_vmCurRecord = _data->readChunk(
					_state->_msgBase + (_state->_msgPos / kChunkSize));
			} else {
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
			// Action without reference
			{
				int opNip = getNip();
				if (opNip < kNumOperations)
					executeAction((Operation)opNip, 0);
				else
					warning("Angel VM: Unknown action opcode %d", opNip);
			}
			break;

		case kFar:
			// Action with reference
			{
				int opNip = getNip();
				int ref = getNumber();
				if (opNip < kNumOperations)
					executeAction((Operation)opNip, ref);
				else
					warning("Angel VM: Unknown action+ref opcode %d", opNip);
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
			// Test with reference — nip + kTestOpcodeBase = Operation
			{
				int opNip = getNip();
				int ref = getNumber();
				int opVal = opNip + kTestOpcodeBase;
				if (opVal < kNumOperations)
					executeTest((Operation)opVal, ref);
				else
					warning("Angel VM: Unknown test+ref opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kFe:
			// Edit without reference — nip + kEditOpcodeBase = Operation
			{
				int opNip = getNip();
				int opVal = opNip + kEditOpcodeBase;
				if (opVal < kNumOperations)
					executeEdit((Operation)opVal, 0);
				else
					warning("Angel VM: Unknown edit opcode nip=%d op=%d", opNip, opVal);
			}
			break;

		case kFer:
			// Edit with reference — nip + kEditOpcodeBase = Operation
			{
				int opNip = getNip();
				int ref = getNumber();
				int opVal = opNip + kEditOpcodeBase;
				if (opVal < kNumOperations)
					executeEdit((Operation)opVal, ref);
				else
					warning("Angel VM: Unknown edit+ref opcode nip=%d op=%d", opNip, opVal);
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
			// Regular text character — queue for output
			charCount++;
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
	// Format A: CSE marker already consumed. Read type and count.
	// Then entries: each is (value: 1 nip, target: 2 nips via getNumber).
	// No extra nips are read for the match value — it comes from game state.
	// If no case matches, execution continues sequentially after the table.
	int caseType = getNip();
	int nbrCases = getNip();

	KindOfCase kind = (KindOfCase)caseType;
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
		// The match value comes from game state, not from the stream.
		// For event-driven messages, this is the event register's x value.
		matchValue = _state->_clock.xReg[kXWelcome].x;
		break;

	default:
		warning("Angel VM: Unknown case type %d", caseType);
		break;
	}

	debugC(kDebugScripts, "Angel VM: CSE type=%d count=%d matchValue=%d",
	       caseType, nbrCases, matchValue);

	// Read case table entries and find match
	for (int i = 0; i < nbrCases; i++) {
		int caseValue = getNip();
		int target = getNumber();

		debugC(kDebugScripts, "Angel VM: CSE entry[%d] value=%d target=%d",
		       i, caseValue, target);

		bool isMatch = (kind == kRandomCase)
			? (i == matchValue)
			: (caseValue == matchValue);

		if (isMatch) {
			debugC(kDebugScripts, "Angel VM: CSE matched, jumping to %d", target);
			jump(target);
			return;
		}
	}

	// No match — fall through: continue sequential execution after the table.
	debugC(kDebugScripts, "Angel VM: CSE no match, falling through at pos %d",
	       _state->_msgPos);
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
	case kPkUpOp:     opPkUp(); break;
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
	case kPtOnOp:     opPtOn(); break;
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
	// Generate article ("a", "an", "the") for the current object
	if (_state->_thing > 0 && _state->_thing <= _data->_nbrObjects) {
		const Object &obj = _data->_props[_state->_thing];
		if (obj.useThe) {
			_engine->putWord("the ");
		} else {
			// Check if name starts with vowel for a/an
			_engine->putWord("a ");
		}
	}
}

void VM::opInv() {
	// Show inventory
	_engine->print("You are carrying:");
	_engine->newLine();

	bool hasAnything = false;
	for (int i = 1; i <= _data->_nbrObjects; i++) {
		if (_state->_possessions.has(i)) {
			hasAnything = true;
			_engine->print("  ");
			// TODO: Extract and print object name from vocab
			_engine->print("object #");
			char buf[16];
			snprintf(buf, sizeof(buf), "%d", i);
			_engine->print(buf);
			if (_state->_wearing.has(i))
				_engine->print(" (being worn)");
			_engine->newLine();
		}
	}
	if (!hasAnything) {
		_engine->print("  Nothing.");
		_engine->newLine();
	}
}

void VM::opSpk(int ref) {
	// Speak a vocabulary word
	if (ref > 0 && ref <= _data->_nbrVWords) {
		// TODO: Extract word from vocab and print it
		debugC(kDebugScripts, "Angel VM: SPK word index %d", ref);
	}
}

void VM::opCap(int ref) {
	// Capitalize next output
	debugC(kDebugScripts, "Angel VM: CAP %d", ref);
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
	// Swap objects between player and person
	opGive();
	// TODO: Also grab the trade item
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
	// Board/ride a vehicle
	debugC(kDebugScripts, "Angel VM: RIDE vehicle %d", _state->_cur.rideWhat);
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
	debugC(kDebugScripts, "Angel VM: NXSTOP");
}

void VM::opOffer() {
	debugC(kDebugScripts, "Angel VM: OFFER");
}

void VM::opTour() {
	_state->_touring = true;
	_state->_tourPoint = _state->_location;
}

void VM::opRRide() {
	debugC(kDebugScripts, "Angel VM: RRIDE");
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
	debugC(kDebugScripts, "Angel VM: BLUFF");
}

void VM::opCurse() {
	_state->_nbrOffenses++;
	debugC(kDebugScripts, "Angel VM: CURSE (offenses: %d)", _state->_nbrOffenses);
}

void VM::opWelcome() {
	debugC(kDebugScripts, "Angel VM: WELCOME");
}

void VM::opMrdr() {
	// Murder (NPC kills NPC)
	debugC(kDebugScripts, "Angel VM: MURDER");
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
	debugC(kDebugScripts, "Angel VM: PUTIT");
}

void VM::opPourIt() {
	debugC(kDebugScripts, "Angel VM: POURIT");
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
	// Set a game variable
	debugC(kDebugScripts, "Angel VM: SET %d", ref);
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
	// Switch — toggle a boolean
	debugC(kDebugScripts, "Angel VM: SW %d", ref);
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
	// Set/modify object attribute
	debugC(kDebugScripts, "Angel VM: ATTR %d", ref);
}

void VM::opAsg(int ref) {
	// Assign value
	debugC(kDebugScripts, "Angel VM: ASG %d", ref);
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
	// Reset / restart something
	debugC(kDebugScripts, "Angel VM: RST %d", ref);
}

void VM::opIncr(int ref) {
	debugC(kDebugScripts, "Angel VM: INCR %d", ref);
}

void VM::opDecr(int ref) {
	debugC(kDebugScripts, "Angel VM: DECR %d", ref);
}

void VM::opAdd(int ref) {
	debugC(kDebugScripts, "Angel VM: ADD %d", ref);
}

void VM::opSub(int ref) {
	debugC(kDebugScripts, "Angel VM: SUB %d", ref);
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
	// Vehicle key — does player have access?
	debugC(kDebugScripts, "Angel VM: VKEY test %d", ref);
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
	debugC(kDebugScripts, "Angel VM: SYN test %d", ref);
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
	// General "is" test — context-dependent
	debugC(kDebugScripts, "Angel VM: IS test %d", ref);
	return false;
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
	debugC(kDebugScripts, "Angel VM: LESS test %d", ref);
	return false;
}

bool VM::testEq(int ref) {
	debugC(kDebugScripts, "Angel VM: EQ test %d", ref);
	return false;
}

bool VM::testLEq(int ref) {
	debugC(kDebugScripts, "Angel VM: LEQ test %d", ref);
	return false;
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
	case kXRegOp:    return 0;  // TODO: which register?
	default:         return 0;
	}
}

} // End of namespace Angel
} // End of namespace Glk
