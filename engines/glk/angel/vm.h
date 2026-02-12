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
	void displayMsg(int addr);

	/** Debug: dump raw nips starting at a given position within a message */
	void dumpNipsAt(int addr, int startPos, int count);

	void setSuppressText(bool suppress) { _suppressText = suppress; _baseSuppressText = suppress; }
	void setBaseSuppressText(bool suppress) { _baseSuppressText = suppress; }

	/** Get the next decoded character from the message stream */
	char getAChar();

	/** Get the next raw 6-bit nip from the message stream */
	int getNip();

	/** Read a multi-nip integer value (2 nips = 12 bits) */
	int getNumber();

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
	};
	static const int kMaxCallDepth = 32;
	CallFrame _callStack[kMaxCallDepth];
	int _callDepth;

	bool _capitalizeNext;    // Set by kCapOp via kFe, capitalizes next text char
	bool _suppressText;      // Text output suppression (active state)
	bool _baseSuppressText;  // Base suppress level set by angel.cpp (CXG 18,9 re-eval)
	int _cseContentDepth;    // >0 when inside CSE case content (EndSym = case end)

	// Entity context — set by resolveEntity() (NAT_F0 35 / proc 35 equivalent).
	// The kFtr/kFar handlers read a ref nip (getNip()+135), then call
	// resolveEntity() which sets these variables based on the ref operation.
	// testIs (proc 77) saves this context, does a second resolution, and
	// compares the two entity values.
	bool _entityFlag;        // intermediate[N][9]  — entity resolved successfully
	int _entityValue;        // intermediate[N][10] — resolved entity value
	int _entityOp;           // intermediate[N][11] — raw operation enum
	int _entityType;         // intermediate[N][12] — entity type (0-6)

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
	void executeTest(Operation op, int ref);

	/** Execute an edit opcode (via Fa/Far edit range) */
	void executeEdit(Operation op, int ref);

	/** Execute an Fe/Fer opcode (base 135, reference/display ops) */
	void executeFe(Operation op, int ref);

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
	void opRst(int ref);

	// Arithmetic edits
	void opIncr(int ref);
	void opDecr(int ref);
	void opAdd(int ref);
	void opSub(int ref);

	// ---- Test implementations ----
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
	 * Resolve a UCSD Pascal data-segment address to an entity field value.
	 * Used by readCompValue in kFt comparison handlers.
	 * The address encodes BASE + (entityIndex-1)*recordSize + fieldOffset.
	 */
	int getEntityFieldValue(int address);
};

} // End of namespace Angel
} // End of namespace Glk

#endif
