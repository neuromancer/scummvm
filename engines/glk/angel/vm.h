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

#ifndef GLK_ANGEL_VM_H
#define GLK_ANGEL_VM_H

#include "glk/angel/types.h"
#include "glk/angel/game_data.h"
#include "glk/angel/game_state.h"

namespace Glk {
namespace Angel {

class Angel;  // Forward declaration

/**
 * The AngelSoft message-file bytecode virtual machine.
 *
 * The message file contains "procedure strings" — sequences of 6-bit
 * nip values that encode both displayable text and control instructions.
 * This VM interprets those procedure strings.
 *
 * Control codes:
 *   ^ (JU)     — Unconditional jump
 *   | (JF)     — Jump if FALSE (skip if _tfIndicator is false)
 *   * (CSE)    — CASE dispatch (RandomCase, WordCase, SynCase, RefCase)
 *   ( (Fa)     — Execute action opcode
 *   + (Far)    — Execute action opcode with reference argument
 *   $ (Ft)     — Test opcode (sets _tfIndicator)
 *   & (Ftr)    — Test opcode with reference argument
 *   % (Fe)     — Edit/state-change opcode
 *   = (Fer)    — Edit opcode with reference argument
 *   \ (FCall)  — Call subroutine (push return address)
 *   @ (EndSym) — End of message / return from call
 */
class VM {
public:
	VM(Angel *engine, GameData *data, GameState *state);

	/**
	 * Open and begin interpreting a message procedure at the given address.
	 * This is the main entry point for running game scripts.
	 */
	void openMsg(int addr, const char *caller = "unknown");

	/**
	 * Execute the current message procedure until EndSym or EOM.
	 * Text output is queued; control codes are dispatched.
	 */
	void executeMsg();

	/**
	 * Display a message (open + execute + close).
	 * Convenience wrapper.
	 */
	void displayMsg(int addr, bool descriptionOnly = false);

	/** Debug: dump raw nips starting at a given position within a message */
	void dumpNipsAt(int addr, int startPos, int count);

	void setSuppressText(bool suppress) { _suppressText = suppress; _baseSuppressText = suppress; }
	void setBaseSuppressText(bool suppress) { _baseSuppressText = suppress; }
	void setRespondMode(bool respond) { _respondMode = respond; }
	void resetEntityContext() { _entityType = -1; _entityValue = 0; _entityFlag = false; }
	void populateEntitySlot(int vocabIdx, byte kindOfWord, byte ref);
	void resetRuntimeState();

	/** Get the next decoded character from the message stream */
	char getAChar();

	/** Get the next raw 6-bit nip from the message stream */
	int getNip();

	/** Read a multi-nip integer value (2 nips = 12 bits) */
	int getNumber();

	/** Read an 18-bit value from 3 nips (used by RESPOND proc 6/33) */
	int getNumber18();

	/** Jump forward by a relative offset within the current message (for JU/JF) */
	void jump(int offset);

	/** Jump to an absolute position within the current message (for CSE targets) */
	void jumpTo(int pos);

private:
	Angel *_engine;
	GameData *_data;
	GameState *_state;

	// Call stack for FCall/EndSym
	struct CallFrame {
		int base;
		int pos;
		int cursor;
		int length;
		bool suppressText;
		int cseContentDepth;
	};
	static const int kMaxCallDepth = 32;
	CallFrame _callStack[kMaxCallDepth];
	int _callDepth;

	bool _capitalizeNext;    // Set by kCapOp via kFe, capitalizes next text char
	int _lastRawNip;         // Last raw nip value before yTable translation
	bool _suppressText;      // Text output suppression (active state)
	bool _baseSuppressText;  // Base suppress level set by angel.cpp (CXG 18,9 re-eval)
	bool _descriptionOnly;   // Stop at first EndSym section break (for entity descriptions)
	bool _respondMode;       // In respond mode: kRoleOp skips description → response section
	int _cseContentDepth;    // >0 when inside CSE case content (EndSym = case end)
	bool _lastTestResult;    // Result of last test operation (for JF branching).
	                         // Separate from _tfIndicator: kNewOp sets _lastTestResult
	                         // but NOT _tfIndicator (P-code proc 76 doesn't write global[5]).

	// Comparison field address — P-code RESPOND global[8].
	// Set by proc 58 (readCompValue) when getNip() != 0: stores field ref.
	// Used as a side-effect address reference for entity field comparisons.
	int _compFieldAddr;

	// Entity context — set by resolveEntity() (NAT_F0 35 / proc 35 equivalent).
	// The kFtr/kFar handlers read a ref nip (getNip()+135), then call
	// resolveEntity() which sets these variables based on the ref operation.
	// testIs (proc 77) saves this context, does a second resolution, and
	// compares the two entity values.
	bool _entityFlag;        // intermediate[N][9]  — entity resolved successfully
	int _entityValue;        // intermediate[N][10] — resolved entity value
	int _entityOp;           // intermediate[N][11] — raw operation enum
	int _entityType;         // intermediate[N][12] — entity type (0-6)
	bool _entityContextFresh; // True when kFar/kFtr just set entity context

	// Nested kDscOp context. DOS kDscOp(entity) evaluates the described
	// message against that entity, not the player's live current room/object.
	bool _describedEntityActive;
	Operation _describedEntityOp;
	int _describedEntityType;
	int _describedEntityValue;

	/** Advance to the next nip, handling chunk boundaries */
	void bumpMsg();

	/** Execute a single control code and its operands */
	void executeControl(char code);

	/** CASE dispatch */
	void executeCase();

	// ---- Opcode dispatch ----

	/** Execute an action opcode (Fa/Far) */
	void executeAction(Operation op, int ref);

	/** Execute a test opcode (Ft/Ftr), sets _state->_tfIndicator */
	void executeTest(Operation op, int ref, bool isKFtr = false);

	/** Execute an edit opcode (via Fa/Far edit range) */
	void executeEdit(Operation op, int ref);

	/** Execute an Fe/Fer opcode (base 135, reference/display ops).
	 *  fromFe=true means called from kFe (may read inline data from stream).
	 *  fromFe=false means called from kFer (ref is fully resolved, no stream reads). */
	void executeFe(Operation op, int ref, bool fromFe);

	// ---- Action implementations ----
	void opPrint(int ref);
	void opDsc(int ref);
	void opAOp();
	void opInv();
	void opSpk(int ref);
	void opCap(int ref);
	void opForce(int ref);

	void opTake();
	void opDrop();
	void opWear();
	void opShed();
	void opPkUp();
	void opDrp();
	void opThrow();
	void opPour();
	void opPutOp();
	void opOpen();
	void opClose();
	void opLock();
	void opUnlock();
	void opKill();
	void opGive();
	void opGrab();
	void opSwap();
	void opGrant();
	void opTkOff();
	void opPtOn();
	void opToss();
	void opTrash();

	void opMove();
	void opEntry();
	void opRide();
	void opRLoc(int ref);
	void opNxStop();
	void opOffer();
	void opTour();
	void opRRide();
	void opTrade();
	void opGreet();
	void opGift();
	void opSecret();
	void opBluff();
	void opCurse();
	void opWelcome();
	void opMrdr();

	void opOpnIt();
	void opClsIt();
	void opLkIt();
	void opUnLkIt();
	void opPutIt();
	void opPourIt();

	void opSave();
	void opQuit();
	void opRestart();

	// ---- Edit implementations ----
	void opTick(int ref);
	void opEvent(int ref);
	void opSet(int ref);
	void opSsp(int ref);
	void opRsm(int ref);
	void opSw(int ref);
	void opAdv();
	void opRecede();
	void opChz(int ref);
	void opAttr(int ref);
	void opAsg(int ref);
	void opMov(int ref);
	void opRst(int refOp);

	// Arithmetic edits
	void opIncr(int ref);
	void opDecr(int ref);
	void opAdd(int ref);
	void opSub(int ref);

	// ---- Entity table helpers ----
	struct EntitySlotInfo {
		bool valid;
		KindOfWord type;
		int ref;

		EntitySlotInfo() : valid(false), type(kAnObject), ref(0) {}
	};

	/**
	 * Convert an entity table index (from message stream getNumber()) to a
	 * vocab file index.  The ASG runtime's entity table includes library
	 * words (standard verbs, prepositions, etc.) before game-specific vocab.
	 * Entity numbers 1..kLibraryWordCount are library words (no vocab entry).
	 * Entity numbers kLibraryWordCount+1..N map to vocab[entityNum - kLibraryWordCount - 1].
	 * Returns -1 if the entity number is out of game vocab range.
	 */
	int entityToVocabIdx(int entityNum) const;
	EntitySlotInfo resolveEntitySlotInfo(int entityNum) const;

	// ---- Test implementations ----
	/** Check if location is adjacent to current location (FUNCTION Local) */
	bool isLocal(int locRef);

	bool testHere(int ref);
	bool testOwns(int ref);
	bool testWears(int ref);
	bool testHas(int ref);
	bool testOn(int ref);
	bool testIn(int ref);
	bool testFull(int ref);
	bool testLocked(int ref);
	bool testOpened(int ref);
	bool testClosed(int ref);
	bool testCvrd(int ref);
	bool testDark();
	bool testLit();
	bool testFog();
	bool testDoor(int ref);
	bool testBox(int ref);
	bool testVsl(int ref);
	bool testSup(int ref);
	bool testLamp(int ref);
	bool testCorpse(int ref);
	bool testLqd(int ref);
	bool testHidden(int ref);
	bool testStuff(int ref);
	bool testDEnd();
	bool testKey(int ref);
	bool testHPass(int ref);
	bool testVKey(int ref);
	bool testCan(int ref);
	bool testCant(int ref);
	bool testRand(int ref);
	bool testAsk();
	bool testAny(int ref);
	bool testWord(int ref);
	bool testSyn(int ref);
	bool testNew(int ref);
	bool testHolds(int ref);
	bool testIs(int ref);
	bool testFair(int ref);
	bool testCarry(int ref);
	bool testTail();
	bool testOnTour();
	bool testLess(int ref);
	bool testEq(int ref);
	bool testLEq(int ref);

	// ---- Reference/value ops ----
	int getRefValue(Operation op);

	/** Resolve entity context (proc 35 equivalent) for kFtr/kFar ref operations */
	void resolveEntity(int op);

	/**
	 * Look up a game state field value by entity reference number.
	 * Implements P-code proc 55: classifies ref via proc 53, then
	 * dispatches to read from _cmdEntry, xReg countdown, or
	 * object/person struct fields.
	 * Returns abs(result). Out-of-range refs return 0.
	 */
	int lookupFieldValue(int entityRef);

	/**
	 * Store a value to a game state field identified by entity reference.
	 * Implements P-code proc 54: classifies ref via proc 53, then
	 * dispatches to write to _cmdEntry (X), xReg countdown (T), or
	 * object/person struct fields (A).
	 */
	void storeFieldValue(int entityRef, int value);

	/**
	 * Read a value from the message stream using the proc 58 pattern.
	 * Reads 1 nip: if 0, reads getNumber18() literal (3 more nips = 18-bit).
	 * If non-zero, calls lookupFieldValue to get the current field value.
	 * Also stores the field ref nip in _lastFieldRef for storeFieldValue.
	 */
	int readCompValueFromStream();

	/** The field ref nip from the last readCompValueFromStream() call.
	 *  0 if the value was a literal (no field to write back to). */
	int _lastFieldRef;

	/**
	 * Resolve a UCSD Pascal data-segment address to an entity field value.
	 * Used by readCompValue in kFt comparison handlers.
	 * The address encodes BASE + (entityIndex-1)*recordSize + fieldOffset.
	 */
	int getEntityFieldValue(int address);

	/**
	 * Store a value to a UCSD Pascal data-segment entity field.
	 * Counterpart to getEntityFieldValue for write operations.
	 */
	void storeEntityFieldValue(int address, int value);
};

} // End of namespace Angel
} // End of namespace Glk

#endif
