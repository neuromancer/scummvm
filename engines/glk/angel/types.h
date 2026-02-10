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

#ifndef GLK_ANGEL_TYPES_H
#define GLK_ANGEL_TYPES_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/array.h"

namespace Glk {
namespace Angel {

/**
 * @file types.h
 * @brief Data types for the AngelSoft Game System (ASG) engine.
 *
 * Translated from the Apple Pascal / Mac Pascal source code of the
 * AngelSoft adventure game system, as used by "Indiana Jones in
 * Revenge of the Ancients" (1987).
 */

// ============================================================
// Constants
// ============================================================

static const int kMaxCastSize = 31;         // Max characters on stage
static const int kMaxNbrObjects = 63;       // Max objects available
static const int kMaxNbrLocations = 119;    // Max locations in game map
static const int kMaxNbrAnOther = 119;      // Max "other" words
static const int kMaxNbrVehicles = 15;      // Max vehicles in fleet
static const int kMaxNbrVWords = 319;       // Max entries in vocabulary table
static const int kMaxNbrProperties = 63;    // Max properties (verbs)
static const int kNbrLocked = 6;            // Locked locations + 1
static const int kNbrStops = 5;             // Stops on a bus route
static const int kNbrVBlocks = 10;          // Vocabulary text blocks
static const int kNobody = 1;              // Dummy person reference
static const int kNowhere = 1;             // Dummy location reference
static const int kNonthing = 1;            // Dummy object reference
static const int kATreasure = 2;           // Treasure object reference
static const int kNameSize = 21;           // Person or object name size
static const int kEmptyWord = 0;           // Non-word in vocabulary table
static const int kFleetDelta = 5;          // Fleet advances 1 stop every 5 moves

// NtgrRegisters indices for AnOther vocab word proc addresses
static const int kXNothing = 0;
static const int kXWelcome = 1;
static const int kXCurse = 2;
static const int kXEntry = 3;
static const int kXMove = 4;

// Display field coordinates (Mac-specific, adapted for GLK)
static const int kXDMove = 6;
static const int kXDDay = 7;
static const int kXDTime = 8;
static const int kXDScore = 9;

// Message/IO constants
static const int kDepth = 500;             // Depth of msg queue
static const int kVMBFactor = 84;          // Chunks per 512-byte page (2 bytes unused)
static const int kVMPCapacity = 200;       // Max 512-byte pages in VM
static const int kVMRCapacity = 17000;     // (VMBFactor+1)*VMPCapacity
static const int kChunkSize = 8;           // Nips per message file record
static const int kChunkWidth = 6;          // Bytes per message file record
static const int kPageSize = 512;          // Size of a VM page in bytes
static const int kPageHeader = 2;          // Bytes of header at start of each page

// Bytecode opcode offsets: nips are 6-bit (0-63), but the Operation enum
// spans 0-165+. Each prefix (Fa/Ft/Fe) has its own nip-to-Operation mapping:
//   Fa/Far (actions):  Operation = nip + 50 (TkOffOp base)
//   Fe/Fer (edits):    Operation = nip + 73 (TickOp base)
//   Ft/Ftr (tests):    Operation = nip + 87 (LessOp base)
static const int kActionOpcodeBase = 50;  // = kTkOffOp ordinal
static const int kEditOpcodeBase = 73;    // = kTickOp ordinal
static const int kTestOpcodeBase = 87;    // = kLessOp ordinal
static const int kFeOpcodeBase = 135;     // = kPassOp ordinal (for kFe/kFer handlers)

// Special codes in proc strings (message VM bytecodes)
static const char kJU = '^';              // Unconditional jump
static const char kJF = '|';              // Jump if FALSE
static const char kCSE = '*';             // CASE operation
static const char kFa = '(';              // Action
static const char kFar = '+';             // Action with reference code
static const char kFt = '$';              // Test
static const char kFtr = '&';             // Test with reference code
static const char kFe = '%';              // Editing function
static const char kFer = '=';             // Edit with reference code
static const char kFCall = '\\';          // Procedure Call
static const char kEndSym = '@';          // Message terminator

// ============================================================
// Enumerations
// ============================================================

/**
 * Bytecode operation codes (~140 opcodes).
 * Ordinal values must match the original Pascal enum for correct
 * dispatch from the message file bytecode.
 */
enum Operation {
	// Data/definition ops (0-25)
	kSizeOp = 0, kValueOp, kPropsOp, kMapOp, kCastOp,
	kEndOp, kPListOp, kStateOp, kDescrOp, kALockOp, kMHaveOp,
	kMoodOp, kChgOp, kFbdOp, kProbOp, kCCarryOp, kDirOp,
	kRouteOp, kVocabOp, kTransOp, kRestrOp, kPDropOp,
	kCmdOp, kEOFOp, kUnlkdOp, kCurbOp,

	// Character/action response ops (26-49)
	kCurseOp, kTakeOp, kDropOp, kWearOp, kShedOp, kTossOp, kTrashOp,
	kWelcomeOp, kMoveOp, kEntryOp, kOpnItOp, kClsItOp, kLkItOp,
	kUnLkItOp, kMrdrOp, kPutItOp, kPourItOp, kBluffOp, kTourOp,
	kRRideOp, kTradeOp, kGreetOp, kGiftOp, kSecretOp,

	// Player verb ops (50-72)
	kTkOffOp, kDrpOp, kPkUpOp, kPtOnOp, kThrowOp, kPourOp, kRLocOp,
	kLockOp, kUnlockOp, kNxStopOp, kOfferOp, kSaveOp, kQuitOp, kRestartOp, kRideOp,
	kGiveOp, kGrabOp, kSwapOp, kOpenOp, kCloseOp, kPutOp, kKillOp, kGrantOp,

	// Editing/state-change ops (73-86)
	kTickOp, kEventOp, kSetOp, kSspOp, kRsmOp, kSwOp, kAdvOp,
	kRecedeOp, kChzOp, kAttrOp, kAsgOp, kMovOp, kPrintOp, kRstOp,

	// Comparison/arithmetic ops (87-98)
	kLessOp, kEqOp, kLEqOp, kIncrOp, kDecrOp, kAddOp, kSubOp,
	kDarkOp, kLitOp, kFogOp, kOwnsOp, kCanOp,

	// Boolean test ops (99-115)
	kOnOp, kInOp, kFullOp, kLockedOp, kOpenedOp, kCvrdOp, kClosedOp,
	kSupOp, kBoxOp, kVslOp, kLampOp, kCorpseOp, kDoorOp, kLqdOp,
	kHiddenOp, kStuffOp,

	// More test/query ops (115-134)
	kDEndOp, kHereOp, kWearsOp, kHasOp, kKeyOp, kHPassOp, kVKeyOp, kCantOp,
	kRandOp, kAskOp, kAnyOp, kWordOp, kSynOp, kNewOp, kHoldsOp, kIsOp, kFairOp,
	kCarryOp, kTailOp, kOnTourOp,

	// Reference/access ops (135+)
	kPassOp, kXRegOp,
	kVerbOp, kItOp, kTargOp, kVclOp, kPersonOp, kObjOp, kSunOp,
	kCtntsOp, kCtnrOp, kLocOp, kPlaceOp, kThingOp, kOtherOp, kCabOp,
	kPrvOp, kVLocOp, kPPrvOp,
	kTimeOp, kDayOp, kDscOp, kAOp, kInvOp, kFleetOp, kRoleOp,
	kCapOp, kCntrOp, kForceOp, kSpkOp, kNoOp,

	kNumOperations  // Sentinel for count
};

/** Directions for movement */
enum MotionSpec {
	kNorth = 0, kSouth, kEast, kWest, kUp, kDown,
	kNumDirections
};

/** Command classification */
enum ThingToDo {
	kGiveUp = 0, kSaveGame, kATrip, kAMove, kAnAction, kAQuery,
	kAnOffer, kANod, kInventory, kATour, kARubout, kNothing
};

/** Fixed vocabulary word codes */
enum VWords {
	kVNorth = 0, kVSouth, kVEast, kVWest, kVUp, kVDown,
	kVTo, kVWith, kVOn, kVFor, kVBy, kVIt, kVIn,
	kVEnter, kVOpen, kVClose, kVExit, kVTrade, kVRide, kVGo,
	kVTake, kVRead, kVGive, kVWhere, kVLook, kVHave,
	kVQuit, kVSave, kVHello,
	kVThrow, kVDrop, kVWear, kVRemove, kVBreak, kVDestroy, kVExamine,
	kVPutIn, kVPutOn, kVTakeOut, kVTakeOff,
	kVLock, kVUnlock, kVKill, kVPour, kVRestore, kVInventory,
	kVCurse, kVBluff, kVArrival, kVHow, kVWhy,
	kNotAVWord,
	kNumVWords
};

/** Days of the week */
enum DayOfWeek {
	kSunday = 0, kMonday, kTuesday, kWednesday, kThursday, kFriday, kSaturday,
	kNumDays
};

/** Word display type */
enum DsplType {
	kAllCaps = 0, kInitCap, kNoCaps
};

/** Word classification */
enum KindOfWord {
	kAnObject = 0, kAPerson, kALocation, kAVehicle, kABuilding,
	kADirection, kAVerb, kAPronoun, kAPreposition, kADay, kAnOther
};

/** Object types */
enum ObjType {
	kABox = 0, kAVessel, kASupport, kALiquid, kALamp, kAStiff, kAProp
};

/** Vehicle types */
enum VType {
	kAnExcursion = 0, kABus, kACar
};

/** Location lighting */
enum Aspect {
	kLitUp = 0, kDark, kSunlit
};

/** Fog list indices */
enum FogRef {
	kFogStart = 0, kFogLimit, kFogBank
};

/** Table record entry types */
enum KindOfEntry {
	kPersonEntry = 0, kObjEntry, kMapEntry, kVclEntry,
	kDtrEntry, kRobotEntry, kTimeEntry, kMscEntry,
	kSugEntry, kComEntry
};

/** Case types in message VM */
enum KindOfCase {
	kRandomCase = 0, kWordCase, kSynCase, kRefCase
};

// ============================================================
// Bitset types (Pascal SET OF 0..N)
// ============================================================

/**
 * Simple fixed-size bitset class to emulate Pascal SET types.
 * Pascal sets are bit-packed; element 0 is the LSB of the first byte.
 */
template<int N>
class BitSet {
	uint32 _bits[(N + 31) / 32];
public:
	BitSet() { clear(); }

	void clear() {
		for (int i = 0; i < (N + 31) / 32; i++)
			_bits[i] = 0;
	}

	bool has(int i) const {
		if (i < 0 || i >= N) return false;
		return (_bits[i / 32] >> (i % 32)) & 1;
	}

	void set(int i) {
		if (i >= 0 && i < N)
			_bits[i / 32] |= (1u << (i % 32));
	}

	void unset(int i) {
		if (i >= 0 && i < N)
			_bits[i / 32] &= ~(1u << (i % 32));
	}

	void toggle(int i) {
		if (i >= 0 && i < N)
			_bits[i / 32] ^= (1u << (i % 32));
	}

	bool isEmpty() const {
		for (int i = 0; i < (N + 31) / 32; i++)
			if (_bits[i]) return false;
		return true;
	}

	int count() const {
		int c = 0;
		for (int i = 0; i < N; i++)
			if (has(i)) c++;
		return c;
	}

	/** Union: this |= other */
	BitSet<N> &operator|=(const BitSet<N> &other) {
		for (int i = 0; i < (N + 31) / 32; i++)
			_bits[i] |= other._bits[i];
		return *this;
	}

	/** Intersection: this &= other */
	BitSet<N> &operator&=(const BitSet<N> &other) {
		for (int i = 0; i < (N + 31) / 32; i++)
			_bits[i] &= other._bits[i];
		return *this;
	}

	/** Difference: this &= ~other */
	BitSet<N> &operator-=(const BitSet<N> &other) {
		for (int i = 0; i < (N + 31) / 32; i++)
			_bits[i] &= ~other._bits[i];
		return *this;
	}

	bool operator==(const BitSet<N> &other) const {
		for (int i = 0; i < (N + 31) / 32; i++)
			if (_bits[i] != other._bits[i]) return false;
		return true;
	}

	bool operator!=(const BitSet<N> &other) const {
		return !(*this == other);
	}

	/** Get raw word for serialization */
	uint32 getWord(int idx) const { return _bits[idx]; }
	void setWord(int idx, uint32 val) { _bits[idx] = val; }
	static int numWords() { return (N + 31) / 32; }
};

// Pascal SET OF types
typedef BitSet<kMaxNbrObjects + 1>    ObjSet;       // SET OF 0..63
typedef BitSet<kMaxCastSize + 1>      PersonSet;    // SET OF 0..31
typedef BitSet<kMaxNbrLocations + 1>  LocSet;       // SET OF 0..119
typedef BitSet<kMaxNbrAnOther + 1>    OtherSet;     // SET OF 0..119
typedef BitSet<kMaxNbrVehicles + 1>   VehicleSet;   // SET OF 0..15
typedef BitSet<kNbrLocked + 1>        AccessSet;    // SET OF 0..6
typedef BitSet<kMaxNbrProperties + 1> PropSet;      // SET OF 0..63
typedef BitSet<kMaxNbrVWords + 1>     VSet;         // SET OF VWordIndex
typedef BitSet<kNumDirections>        DirSet;       // SET OF MotionSpec
typedef BitSet<kNumDays>              DaySet;       // SET OF DayOfWeek
typedef BitSet<kNumVWords>            VWordSet;     // SET OF VWords

// ============================================================
// Data Structures
// ============================================================

/** Fog route: 3 location references */
struct FogList {
	int loc[3];   // Indexed by FogRef: FogStart, FogLimit, FogBank

	FogList() { loc[0] = loc[1] = loc[2] = kNowhere; }
};

/** Vocabulary entry core data */
struct VECore {
	VWords code;          // The word encoded (VWords enum)
	DsplType display;     // Display type
	KindOfWord vType;     // Word type (variant discriminant)

	// Variant fields (union)
	int ref;              // ObjX / PersonX / LocX / VclX / DirX / OtherX / VerbX / DayX

	VECore() : code(kNotAVWord), display(kNoCaps), vType(kAnObject), ref(0) {}
};

/** Vocabulary table entry (in-memory) */
struct VEntry {
	int vbi;          // Vocab block index (1..10)
	int len;          // Word length (1..21)
	int dsp;          // Displacement within VText[vbi] (1..255)
	VECore ve;        // The core

	VEntry() : vbi(0), len(0), dsp(0) {}
};

/** External vocabulary entry (on-disk) */
struct XVEntry {
	byte wd[kNameSize + 1];  // Compressed word (22 nips packed)
	VECore xv;               // The core
};

/** Event record */
struct Event {
	int x;            // Register counter
	int proc;         // Associated procedure address

	Event() : x(0), proc(0) {}
};

/** Time record */
struct TimeRecord {
	DayOfWeek day;
	int hour;         // 1..12
	int minute;       // 0..59
	bool am;
	int tickNumber;
	Event xReg[10];   // Event registers 0..9

	TimeRecord() : day(kSunday), hour(12), minute(0), am(true), tickNumber(0) {}
};

/** Internal time record (for file I/O — only 5 event registers) */
struct IntTimeRecord {
	DayOfWeek day;
	int hour;
	int minute;
	bool am;
	int tickNumber;
	Event xReg[5];

	IntTimeRecord() : day(kSunday), hour(12), minute(0), am(true), tickNumber(0) {}
};

/** Location / place record */
struct Place {
	int n;                                  // Place description key
	int shortDscr;                          // Place name (VWordIndex)
	int nextPlace[kNumDirections];          // Transition table (LocRef per direction)
	bool traffic[kNumDirections];           // 50% conditional passage
	DirSet curb;                            // Curb connector set
	int accessLock;                         // AccessRight (0..NbrLocked)
	int mustHave;                           // ObjRef required for entry
	int fogPath;                            // Next location in fog path
	PersonSet people;                       // People here
	ObjSet objects;                         // Objects here
	Aspect view;                            // LitUp / Dark / Sunlit
	bool useThe;
	bool foggy;
	bool itsADoor;
	bool itsOpen;
	bool itsLocked;
	bool unseen;

	Place() : n(0), shortDscr(0), accessLock(0), mustHave(0), fogPath(0),
	          view(kDark), useThe(false), foggy(false), itsADoor(false),
	          itsOpen(false), itsLocked(false), unseen(true) {
		for (int i = 0; i < kNumDirections; i++) {
			nextPlace[i] = kNowhere;
			traffic[i] = false;
		}
	}
};

/** Object record */
struct Object {
	ObjSet contents;                        // Objects contained within
	int n;                                  // Description key
	int oName;                              // Name (VWordIndex)
	int size;                               // Measure 0..9
	int value;                              // Measure 0..9
	PropSet properties;                     // SET OF OProp
	int state;                              // Measure 0..9
	int inOrOn;                             // Container/support ObjRef
	ObjType kindOfThing;
	bool useThe;
	bool litUp;
	bool itsOpen;
	bool itsLocked;
	bool unseen;

	Object() : n(0), oName(0), size(0), value(0), state(0), inOrOn(0),
	           kindOfThing(kAProp), useThe(false), litUp(false),
	           itsOpen(false), itsLocked(false), unseen(true) {}
};

/** Person / character record */
struct Person {
	int n;                                  // Description key
	int pName;                              // Name (VWordIndex)
	int sFun[kSecretOp - kTradeOp + 1];    // Script procedures (TradeOp..SecretOp)
	ObjSet carrying;                        // Possessions
	int located;                            // LocRef
	int mood;                               // Measure 0..9
	MotionSpec direction;
	int dropping;                           // ProbRange 0..100
	int change;                             // ProbRange 0..100
	int corpse;                             // ObjRef of dead body
	bool useThe;
	bool resting;
	bool unseen;

	Person() : n(0), pName(0), located(kNowhere), mood(0),
	           direction(kNorth), dropping(0), change(0), corpse(0),
	           useThe(false), resting(true), unseen(true) {
		for (int i = 0; i < kSecretOp - kTradeOp + 1; i++)
			sFun[i] = 0;
	}
};

/** Vehicle record */
struct Vehicle {
	int n;                                  // Description key
	int rideProc;                           // Ride procedure
	int vName;                              // VWordIndex
	ObjSet cantCarry;                       // Objects too large
	int stopped;                            // Current location (LocRef)
	bool useThe;
	bool unseen;
	VType vclType;
	LocSet route;                           // For bus/excursion
	int inside;                             // For car: interior LocRef

	Vehicle() : n(0), rideProc(0), vName(0), stopped(kNowhere),
	            useThe(false), unseen(true), vclType(kAnExcursion), inside(0) {}
};

/** Parser result — what the player wants to do */
struct Determiner {
	int doItToWhat;                         // ObjRef
	int withWhat;                           // ObjRef (tool required)
	int tradeWhat;                          // ObjRef (what player trades)
	int forWhat;                            // ObjRef (what player wants)
	int personNamed;                        // PersonRef
	int rideWhat;                           // VehicleRef
	int whereTo;                            // LocRef
	bool driving;

	Determiner() : doItToWhat(0), withWhat(0), tradeWhat(0), forWhat(0),
	               personNamed(0), rideWhat(0), whereTo(0), driving(false) {}
};

/** General player info (saved/loaded from tables) */
struct GeneralInfo {
	AccessSet capabilities;
	ObjSet possessions;
	ObjSet wearing;
	int location;                           // LocRef
	MotionSpec direction;
	int nbrPossessions;
	FogList fogRoute;
	int robotAddr;
	bool civilianTime;
	bool completeGame;

	GeneralInfo() : location(kNowhere), direction(kNorth),
	                nbrPossessions(0), robotAddr(0),
	                civilianTime(true), completeGame(false) {}
};

/** Robot suggestion */
struct ASuggestion {
	int m;                                  // Move number
	KindOfWord kind;
	int ref;                                // o / P / L / V / X depending on kind

	ASuggestion() : m(0), kind(kAnObject), ref(0) {}
};

/** Communication area record (saved/loaded from tables) */
struct ComRecord {
	int prvLocation;                        // LocRef
	int pprvLocation;                       // LocRef
	int vLocation;                          // LocRef
	MotionSpec prvDirection;
	int nbrOffenses;
	int lastPerson;                         // PersonRef
	int pursuer;                            // PersonRef
	bool gotHim;
	bool dspTime;
	bool dspDay;
	bool dspMove;
	bool dspScore;
	int probPickUp;                         // ProbRange

	ComRecord() : prvLocation(kNowhere), pprvLocation(kNowhere), vLocation(kNowhere),
	              prvDirection(kNorth), nbrOffenses(0), lastPerson(0), pursuer(0),
	              gotHim(false), dspTime(false), dspDay(false), dspMove(false),
	              dspScore(false), probPickUp(0) {}
};

// ============================================================
// Message VM chunk
// ============================================================

/** A 6-byte chunk holding 8 six-bit nips */
struct Chunk {
	byte data[kChunkWidth];

	Chunk() { memset(data, 0, kChunkWidth); }

	/**
	 * Extract nip at position i (0..7) from the chunk.
	 * Each nip is 6 bits. Chunk is 48 bits = 6 bytes.
	 * Nip 0 starts at bit 42 (big-endian, MSB first).
	 */
	int getNip(int i) const {
		// Big-endian bit layout: nip 0 at bits 47..42, nip 1 at 41..36, etc.
		int bitPos = 42 - (i * 6);
		int byteIdx = (47 - bitPos) / 8;        // Actually let's compute properly

		// Build a 48-bit value from the 6 bytes (big-endian)
		uint64 val = 0;
		for (int b = 0; b < 6; b++)
			val = (val << 8) | data[b];

		// Extract 6 bits starting at bit position (from MSB)
		int shift = 42 - (i * 6);
		return (int)((val >> shift) & 0x3F);
	}

	/** Insert nip value n at position i (0..7) */
	void setNip(int i, int n) {
		uint64 val = 0;
		for (int b = 0; b < 6; b++)
			val = (val << 8) | data[b];

		int shift = 42 - (i * 6);
		uint64 mask = ~((uint64)0x3F << shift);
		val = (val & mask) | ((uint64)(n & 0x3F) << shift);

		for (int b = 5; b >= 0; b--) {
			data[b] = val & 0xFF;
			val >>= 8;
		}
	}
};

} // End of namespace Angel
} // End of namespace Glk

#endif
