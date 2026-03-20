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

#include "glk/angel/game_data.h"
#include "glk/glk.h"
#include "common/debug.h"
#include "common/textconsole.h"

namespace Glk {
namespace Angel {

namespace {

static uint16 readUint16LE(const byte *ptr) {
	return (uint16)ptr[0] | ((uint16)ptr[1] << 8);
}

static uint32 readUint32LE(const byte *ptr) {
	return (uint32)ptr[0] |
	       ((uint32)ptr[1] << 8) |
	       ((uint32)ptr[2] << 16) |
	       ((uint32)ptr[3] << 24);
}

static int decodeVocabIndexLE(const byte *ptr) {
	uint16 raw = readUint16LE(ptr);
	return raw ? (int)raw - 1 : -1;
}

} // namespace

GameData::GameData() : _castSize(0), _nbrObjects(0), _nbrLocations(0),
                       _nbrVehicles(0), _nbrVWords(0), _nbrProperties(0),
                       _isDosData(false), _messageFile(nullptr) {
	memset(_robot, 0, sizeof(_robot));
	memset(_yTable, 0, sizeof(_yTable));
	memset(_xTable, 0, sizeof(_xTable));
	initCipherTables();
}

GameData::~GameData() {
	// messageFile is not owned by us — it's the game file from GlkEngine
}

void GameData::initCipherTables() {
	// The YTable/XTable cipher was cracked from proc 27 of the INITIALI segment.
	// The algorithm generates letter mappings via: nip = (letter_index * 7) mod 26
	// where letter_index = 0 for 'a', 1 for 'b', etc.
	//
	// Both uppercase and lowercase map to the same nip value.

	// Initialize all entries to '@' (EndSym / unused)
	for (int i = 0; i < 64; i++)
		_yTable[i] = '@';
	for (int i = 0; i < 128; i++)
		_xTable[i] = 0;

	// ---- Letters (nips 0–25) ----
	// Formula: YTable[(j * 7) mod 26] = 'a' + j
	// nip  0 = 'a',  1 = 'p',  2 = 'e',  3 = 't',  4 = 'i',  5 = 'x'
	// nip  6 = 'm',  7 = 'b',  8 = 'q',  9 = 'f', 10 = 'u', 11 = 'j'
	// nip 12 = 'y', 13 = 'n', 14 = 'c', 15 = 'r', 16 = 'g', 17 = 'v'
	// nip 18 = 'k', 19 = 'z', 20 = 'o', 21 = 'd', 22 = 's', 23 = 'h'
	// nip 24 = 'w', 25 = 'l'
	for (int j = 0; j < 26; j++) {
		int nip = (j * 7) % 26;
		_yTable[nip] = (char)('a' + j);
	}

	// ---- Special characters (nips 26–47) ----
	_yTable[26] = ' ';    // space
	_yTable[27] = '^';    // JU — unconditional jump
	_yTable[28] = '|';    // JF — jump if false
	_yTable[29] = '*';    // CSE — case
	_yTable[30] = '(';    // Fa — action
	_yTable[31] = '$';    // Ft — test
	_yTable[32] = '%';    // Fe — edit
	_yTable[33] = '@';    // EndSym (message terminator)
	_yTable[34] = '#';    // display text delimiter
	_yTable[35] = '.';    // period
	_yTable[36] = ',';    // comma
	_yTable[37] = '-';    // hyphen
	_yTable[38] = '?';    // question mark
	_yTable[39] = '"';    // double quote
	_yTable[40] = ';';    // semicolon
	_yTable[41] = '\'';   // apostrophe
	_yTable[42] = '!';    // exclamation
	_yTable[43] = ':';    // colon
	_yTable[44] = '&';    // Ftr — test with reference code
	_yTable[45] = '=';    // Fer — edit with reference code
	_yTable[46] = '+';    // Far — action with reference code
	_yTable[47] = '\\';   // FCall — procedure call

	// ---- Unused (nips 48–51) ----
	// In the original UCSD Pascal, uninitialized table entries were NUL (CHR(0)).
	// They must NOT map to '@' (EndSym), otherwise the VM prematurely terminates
	// messages whenever these nip values appear (375+ occurrences in game data).
	// Map to NUL so they fall through to default text output harmlessly.
	for (int i = 48; i < 52; i++)
		_yTable[i] = '\0';

	// ---- Digits (nips 52–61) ----
	for (int d = 0; d < 10; d++)
		_yTable[52 + d] = (char)('0' + d);

	// ---- Unused (nips 62–63) ----
	_yTable[62] = '\0';
	_yTable[63] = '\0';

	// Build reverse table (XTable: ASCII → nip)
	for (int i = 0; i < 128; i++)
		_xTable[i] = 33;  // default: EndSym nip for unmapped chars

	for (int i = 0; i < 64; i++) {
		byte ch = (byte)_yTable[i];
		if (ch < 128)
			_xTable[ch] = (byte)i;
	}

	// Map uppercase letters to the same nips as lowercase
	for (int j = 0; j < 26; j++) {
		int nip = (j * 7) % 26;
		_xTable[(byte)('A' + j)] = (byte)nip;
	}
}

bool GameData::load(Common::SeekableReadStream *tablesFile,
                    Common::SeekableReadStream *vocabFile,
                    Common::SeekableReadStream *messageFile) {
	if (!tablesFile || !vocabFile || !messageFile)
		return false;

	if (!loadTables(tablesFile)) {
		warning("Angel: Failed to load tables file");
		return false;
	}

	if (!loadVocab(vocabFile)) {
		warning("Angel: Failed to load vocab file");
		return false;
	}

	if (!initMessageVM(messageFile)) {
		warning("Angel: Failed to initialize message VM");
		return false;
	}

	// Load the response address table from the message file.
	// Must be called after both loadTables() and initMessageVM().
	loadResponseTable();

	return true;
}

bool GameData::loadTables(Common::SeekableReadStream *stream) {
	if (stream->size() == 4280) {
		static const int kRecordSize = 40;

		const int fileSize = stream->size();
		const int totalRecords = fileSize / kRecordSize;
		debugC(1, kDebugScripts, "Angel: tables file size=%d bytes, %d DOS records", fileSize, totalRecords);

		byte *buf = new byte[fileSize];
		stream->read(buf, fileSize);

		int nPerson = 0, nObj = 0, nLoc = 0, nVcl = 0;
		for (int i = 0; i < totalRecords; i++) {
			switch (buf[i * kRecordSize]) {
			case kPersonEntry: nPerson++; break;
			case kObjEntry:    nObj++; break;
			case kMapEntry:    nLoc++; break;
			case kVclEntry:    nVcl++; break;
			default: break;
			}
		}

		_castSize = nPerson;
		_nbrObjects = nObj;
		_nbrLocations = nLoc;
		_nbrVehicles = nVcl;
		_nbrProperties = kMaxNbrProperties;

		debugC(1, kDebugScripts, "Angel: DOS tables: %d persons, %d objects, %d locations, %d vehicles",
		       _castSize, _nbrObjects, _nbrLocations, _nbrVehicles);

		int personIdx = 1, objIdx = 1, locIdx = 1, vclIdx = 1, timeIdx = 0;

		for (int i = 0; i < totalRecords; i++) {
			const int off = i * kRecordSize;
			const int disc = buf[off];

			switch (disc) {
			case kMscEntry: {
				_initGeneral.capabilities.setWord(0, buf[off + 1]);
				_initGeneral.possessions.setWord(0, readUint32LE(&buf[off + 2]));
				_initGeneral.possessions.setWord(1, readUint32LE(&buf[off + 6]));
				_initGeneral.wearing.setWord(0, readUint32LE(&buf[off + 10]));
				_initGeneral.wearing.setWord(1, readUint32LE(&buf[off + 14]));
				_initGeneral.location = (int)buf[off + 18];
				_initGeneral.direction = (MotionSpec)buf[off + 19];
				_initGeneral.nbrPossessions = (int)buf[off + 20];
				_initGeneral.fogRoute.loc[0] = (int)buf[off + 22];
				_initGeneral.fogRoute.loc[1] = (int)buf[off + 23];
				_initGeneral.fogRoute.loc[2] = (int)buf[off + 24];
				_initGeneral.robotAddr = (int)readUint16LE(&buf[off + 25]);
				_initGeneral.civilianTime = (buf[off + 27] != 0);
				_initGeneral.completeGame = (buf[off + 28] != 0);

				debugC(1, kDebugScripts, "Angel: DOS MscEntry: location=%d direction=%d nbrPoss=%d robotAddr=%d",
				       _initGeneral.location, _initGeneral.direction,
				       _initGeneral.nbrPossessions, _initGeneral.robotAddr);
				break;
			}

			case kDtrEntry: {
				_initDeterminer.doItToWhat = (int)buf[off + 1];
				_initDeterminer.withWhat = (int)buf[off + 2];
				_initDeterminer.tradeWhat = (int)buf[off + 3];
				_initDeterminer.forWhat = (int)buf[off + 4];
				_initDeterminer.personNamed = (int)buf[off + 5];
				_initDeterminer.rideWhat = (int)buf[off + 6];
				_initDeterminer.whereTo = (int)buf[off + 7];
				break;
			}

			case kSugEntry: {
				_initSuggestion.m = (int)buf[off + 1];
				_initSuggestion.kind = (KindOfWord)buf[off + 3];
				_initSuggestion.ref = (int)buf[off + 5];
				break;
			}

			case kComEntry: {
				_initCom.prvLocation = (int)buf[off + 1];
				_initCom.pprvLocation = (int)buf[off + 2];
				_initCom.vLocation = (int)buf[off + 3];
				_initCom.prvDirection = (MotionSpec)buf[off + 4];
				_initCom.nbrOffenses = (int)buf[off + 5];
				_initCom.lastPerson = (int)buf[off + 6];
				_initCom.pursuer = (int)buf[off + 7];
				_initCom.gotHim = (buf[off + 8] != 0);
				_initCom.dspTime = (buf[off + 9] != 0);
				_initCom.dspDay = (buf[off + 10] != 0);
				_initCom.dspMove = (buf[off + 11] != 0);
				_initCom.dspScore = (buf[off + 12] != 0);
				_initCom.probPickUp = (int)buf[off + 13];
				break;
			}

			case kTimeEntry: {
				if (timeIdx == 0) {
					_initTime.day = (DayOfWeek)buf[off + 1];
					_initTime.hour = (int)buf[off + 2];
					_initTime.minute = (int)buf[off + 3];
					_initTime.am = (buf[off + 4] != 0);
					_initTime.tickNumber = (int)buf[off + 5];
					debugC(1, kDebugScripts, "Angel: DOS TimeEntry[%d]: clock day=%d %d:%02d %s",
					       timeIdx, _initTime.day, _initTime.hour,
					       _initTime.minute, _initTime.am ? "AM" : "PM");
				} else if (timeIdx == 2) {
					_initTime.xReg[0].x = 0;
					_initTime.xReg[0].proc = 0;
					for (int e = 1; e < 5; e++) {
						const int xOff = off + 11 + (e - 1) * 4;
						_initTime.xReg[e].x = (int)readUint16LE(&buf[xOff]);
						_initTime.xReg[e].proc = (int)readUint16LE(&buf[xOff + 2]);
						if (_initTime.xReg[e].proc > 0) {
							debugC(1, kDebugScripts, "Angel: DOS xReg[%d] x=%d proc=%d",
							       e, _initTime.xReg[e].x,
							       _initTime.xReg[e].proc);
						}
					}
				}
				timeIdx++;
				break;
			}

			case kPersonEntry: {
				if (personIdx < kMaxCastSize + 1) {
					Person &p = _cast[personIdx];
					p.n = (int)readUint16LE(&buf[off + 1]);
					p.pName = decodeVocabIndexLE(&buf[off + 3]);
					for (int s = 0; s < 4 && s < kSecretOp - kTradeOp + 1; s++)
						p.sFun[s] = (int)readUint16LE(&buf[off + 5 + s * 2]);
					p.carrying.setWord(0, readUint32LE(&buf[off + 13]));
					p.carrying.setWord(1, readUint32LE(&buf[off + 17]));
					p.located = (int)buf[off + 21];
					p.mood = (int)buf[off + 22];
					p.direction = (MotionSpec)buf[off + 23];
					p.dropping = (int)buf[off + 24];
					p.change = (int)buf[off + 25];
					p.corpse = (int)buf[off + 26];
					p.useThe = (buf[off + 27] != 0);
					p.resting = (buf[off + 28] != 0);
					p.unseen = (buf[off + 29] != 0);

					debugC(2, kDebugScripts, "Angel: DOS Person[%d]: n=%d pName=%d located=%d mood=%d corpse=%d",
					       personIdx, p.n, p.pName, p.located, p.mood, p.corpse);
				}
				personIdx++;
				break;
			}

			case kObjEntry: {
				if (objIdx < kMaxNbrObjects + 1) {
					Object &obj = _props[objIdx];
					obj.contents.setWord(0, readUint32LE(&buf[off + 1]));
					obj.contents.setWord(1, readUint32LE(&buf[off + 5]));
					obj.n = (int)readUint16LE(&buf[off + 9]);
					obj.oName = decodeVocabIndexLE(&buf[off + 11]);
					obj.size = (int)buf[off + 13];
					obj.value = (int)buf[off + 14];
					obj.properties.setWord(0, readUint32LE(&buf[off + 15]));
					obj.properties.setWord(1, readUint32LE(&buf[off + 19]));
					obj.state = (int)buf[off + 23];
					obj.inOrOn = (int)buf[off + 24];
					obj.kindOfThing = (ObjType)buf[off + 25];
					obj.useThe = (buf[off + 26] != 0);
					obj.litUp = (buf[off + 27] != 0);
					obj.itsOpen = (buf[off + 28] != 0);
					obj.itsLocked = (buf[off + 29] != 0);
					obj.unseen = (buf[off + 30] != 0);

					debugC(2, kDebugScripts, "Angel: DOS Object[%d]: n=%d oName=%d size=%d val=%d state=%d kind=%d inOrOn=%d",
					       objIdx, obj.n, obj.oName, obj.size, obj.value,
					       obj.state, obj.kindOfThing, obj.inOrOn);
				}
				objIdx++;
				break;
			}

			case kMapEntry: {
				if (locIdx < kMaxNbrLocations + 1) {
					Place &place = _map[locIdx];
					place.n = (int)readUint16LE(&buf[off + 1]);
					place.shortDscr = decodeVocabIndexLE(&buf[off + 3]);
					for (int d = 0; d < kNumDirections; d++) {
						place.nextPlace[d] = (int)buf[off + 5 + d];
						place.traffic[d] = (buf[off + 11 + d] != 0);
					}
					place.accessLock = (int)buf[off + 17];
					place.mustHave = (int)buf[off + 18];
					place.fogPath = (int)buf[off + 19];
					// DOS field order: useThe at off+20, THEN people, objects, view, flags
					place.useThe = (buf[off + 20] != 0);
					// People set at off+21 (4 bytes PersonSet)
					place.people.clear();
					place.people.setWord(0, readUint32LE(&buf[off + 21]));
					// Objects set at off+25 (8 bytes ObjSet)
					place.objects.clear();
					place.objects.setWord(0, readUint32LE(&buf[off + 25]));
					place.objects.setWord(1, readUint32LE(&buf[off + 29]));
					place.view = (Aspect)buf[off + 33];
					place.foggy = (buf[off + 34] != 0);
					place.itsADoor = (buf[off + 35] != 0);
					place.itsOpen = (buf[off + 36] != 0);
					place.itsLocked = (buf[off + 37] != 0);
					place.unseen = (buf[off + 39] != 0);

					// Log objects at this location
					for (int o = 1; o <= _nbrObjects; o++) {
						if (place.objects.has(o))
							debugC(2, kDebugScripts, "Angel: DOS Map[%d] has object %d (n=%d oName=%d)", locIdx, o, _props[o].n, _props[o].oName);
					}
					debugC(2, kDebugScripts, "Angel: DOS Map[%d]: n=%d shortDscr=%d exits=[N=%d,S=%d,E=%d,W=%d,U=%d,D=%d] access=%d mustHave=%d fog=%d traffic=[%d,%d,%d,%d,%d,%d] door=%d open=%d",
					       locIdx, place.n, place.shortDscr,
					       place.nextPlace[0], place.nextPlace[1],
					       place.nextPlace[2], place.nextPlace[3],
					       place.nextPlace[4], place.nextPlace[5],
					       place.accessLock, place.mustHave, place.fogPath,
					       place.traffic[0], place.traffic[1],
					       place.traffic[2], place.traffic[3],
					       place.traffic[4], place.traffic[5],
					       place.itsADoor ? 1 : 0, place.itsOpen ? 1 : 0);
				}
				locIdx++;
				break;
			}

			case kVclEntry: {
				if (vclIdx < kMaxNbrVehicles + 1) {
					Vehicle &vcl = _fleet[vclIdx];
					vcl.n = (int)readUint16LE(&buf[off + 1]);
					vcl.rideProc = (int)readUint16LE(&buf[off + 3]);
					vcl.vName = decodeVocabIndexLE(&buf[off + 5]);
					vcl.cantCarry.setWord(0, readUint32LE(&buf[off + 7]));
					vcl.cantCarry.setWord(1, readUint32LE(&buf[off + 11]));
					vcl.stopped = (int)buf[off + 15];
					vcl.useThe = (buf[off + 16] != 0);
					vcl.unseen = (buf[off + 17] != 0);
					vcl.vclType = (VType)buf[off + 18];
					if (vcl.vclType == kACar) {
						vcl.inside = (int)buf[off + 19];
					} else {
						for (int s = 0; s < kNbrStops; s++) {
							int stop = (int)buf[off + 19 + s];
							if (stop >= 1 && stop <= kMaxNbrLocations)
								vcl.route.set(stop);
						}
					}

					debugC(2, kDebugScripts, "Angel: DOS Vehicle[%d]: n=%d vName=%d stopped=%d type=%d inside=%d",
					       vclIdx, vcl.n, vcl.vName, vcl.stopped, vcl.vclType, vcl.inside);
				}
				vclIdx++;
				break;
			}

			default:
				warning("Angel: Unknown DOS table record type %d at record %d", disc, i);
				break;
			}
		}

		for (int loc = 1; loc <= _nbrLocations; loc++)
			_map[loc].objects.clear();
		for (int obj = 1; obj <= _nbrObjects; obj++) {
			int holder = _props[obj].inOrOn;
			if (holder >= 1 && holder <= _nbrLocations)
				_map[holder].objects.set(obj);
		}

		delete[] buf;

		debugC(1, kDebugScripts, "Angel: DOS tables loaded. Starting location=%d, WELCOME proc=%d",
		       _initGeneral.location, _initTime.xReg[kXWelcome].proc);
		return true;
	}

	/**
	 * The tables file is a UCSD Pascal "FILE OF TableRecord".
	 * Each record is 36 bytes (18 big-endian 16-bit words).
	 * Bytes 0-1: discriminant (KindOfEntry enum, big-endian).
	 *
	 * Record order for Indiana Jones: Revenge of the Ancients:
	 *   rec 0: MscEntry (GeneralInfo)
	 *   rec 1: DtrEntry (Determiner)
	 *   rec 2: SugEntry (ASuggestion)
	 *   rec 3: ComEntry (ComRecord)
	 *   recs 4-7: TimeEntry × 4 (IntTimeRecord)
	 *   recs 8-13: PersonEntry × 6
	 *   recs 14-36: ObjEntry × 23
	 *   recs 37-104: MapEntry × 68
	 *   recs 105-106: VclEntry × 2
	 *   recs 107-112: PersonEntry × 6
	 */

	static const int kRecordSize = 36;

	int fileSize = stream->size();
	int totalRecords = fileSize / kRecordSize;
	debugC(1, kDebugScripts, "Angel: tables file size=%d bytes, %d records", fileSize, totalRecords);

	byte *buf = new byte[fileSize];
	stream->read(buf, fileSize);

	// Helper: read big-endian 16-bit word
	#define RW(off) (((uint16)buf[(off)] << 8) | buf[(off) + 1])

	// First pass: count record types
	int nPerson = 0, nObj = 0, nLoc = 0, nVcl = 0;
	for (int i = 0; i < totalRecords; i++) {
		int disc = RW(i * kRecordSize);
		switch (disc) {
		case kPersonEntry: nPerson++; break;
		case kObjEntry:    nObj++; break;
		case kMapEntry:    nLoc++; break;
		case kVclEntry:    nVcl++; break;
		default: break;
		}
	}
	_castSize = nPerson;
	_nbrObjects = nObj;
	_nbrLocations = nLoc;
	_nbrVehicles = nVcl;
	_nbrProperties = kMaxNbrProperties;

	debugC(1, kDebugScripts, "Angel: tables: %d persons, %d objects, %d locations, %d vehicles",
	       _castSize, _nbrObjects, _nbrLocations, _nbrVehicles);

	// Second pass: parse records
	// Entity indices are 1-based (Pascal convention: kNowhere=1, kNobody=1).
	// Index 0 in each array is unused; the first record from the file maps to index 1.
	int personIdx = 1, objIdx = 1, locIdx = 1, vclIdx = 1, timeIdx = 0;

	for (int i = 0; i < totalRecords; i++) {
		int off = i * kRecordSize;
		int disc = RW(off);

		switch (disc) {
		case kMscEntry: {
			// GeneralInfo — MscEntry layout (36 bytes):
			//   0-1:   disc (7)
			//   2-3:   capabilities (AccessSet, 2 bytes)
			//   4-11:  possessions (ObjSet, 8 bytes)
			//   12-19: wearing (ObjSet, 8 bytes)
			//   20-21: location (int)
			//   22-23: direction (MotionSpec enum)
			//   24-25: nbrPossessions (int)
			//   26-31: fogRoute (3 × int)
			//   32-33: robotAddr (int)
			//   34:    civilianTime (bool, 1 byte)
			//   35:    completeGame (bool, 1 byte)
			_initGeneral.location = (int)RW(off + 20);
			_initGeneral.direction = (MotionSpec)RW(off + 22);
			_initGeneral.nbrPossessions = (int)RW(off + 24);
			_initGeneral.robotAddr = (int)RW(off + 32);
			_initGeneral.civilianTime = (buf[off + 34] != 0);
			_initGeneral.completeGame = (buf[off + 35] != 0);

			debugC(1, kDebugScripts, "Angel: MscEntry: location=%d direction=%d nbrPoss=%d",
			       _initGeneral.location, _initGeneral.direction,
			       _initGeneral.nbrPossessions);
			break;
		}

		case kDtrEntry: {
			// Determiner — DtrEntry layout:
			//   2-3: doItToWhat, 4-5: withWhat, 6-7: tradeWhat,
			//   8-9: forWhat, 10-11: personNamed, 12-13: rideWhat,
			//   14-15: whereTo
			_initDeterminer.doItToWhat = (int)RW(off + 2);
			_initDeterminer.withWhat = (int)RW(off + 4);
			_initDeterminer.tradeWhat = (int)RW(off + 6);
			_initDeterminer.forWhat = (int)RW(off + 8);
			_initDeterminer.personNamed = (int)RW(off + 10);
			_initDeterminer.rideWhat = (int)RW(off + 12);
			_initDeterminer.whereTo = (int)RW(off + 14);
			break;
		}

		case kSugEntry: {
			// ASuggestion:  2-3: m, 4-5: kind, 6-7: ref
			_initSuggestion.m = (int)RW(off + 2);
			_initSuggestion.kind = (KindOfWord)RW(off + 4);
			_initSuggestion.ref = (int)RW(off + 6);
			break;
		}

		case kComEntry: {
			// ComRecord — layout is not fully determined yet.
			// Parse what we can.
			debugC(1, kDebugScripts, "Angel: ComEntry at rec %d (parsing deferred)", i);
			break;
		}

		case kTimeEntry: {
			// IntTimeRecord layout (36 bytes):
			//   0-1:   disc (6)
			//   2-3:   day (DayOfWeek)
			//   4-5:   hour
			//   6-7:   minute
			//   8-9:   am (bool as word)
			//   10-11: tickNumber
			//   12-31: xReg[0..4], each 4 bytes (x: 2, proc: 2)
			if (timeIdx == 0) {
				// rec 4: initial game clock
				_initTime.day = (DayOfWeek)RW(off + 2);
				_initTime.hour = (int)RW(off + 4);
				_initTime.minute = (int)RW(off + 6);
				_initTime.am = (RW(off + 8) != 0);
				_initTime.tickNumber = (int)RW(off + 10);
				debugC(1, kDebugScripts, "Angel: TimeEntry[%d]: clock day=%d %d:%02d %s",
				       timeIdx, _initTime.day, _initTime.hour,
				       _initTime.minute, _initTime.am ? "AM" : "PM");
			} else if (timeIdx == 2) {
				// rec 6: NtgrRegisters first half (xReg[0..4])
				// Contains WELCOME, CURSE, ENTRY, MOVE events
				for (int e = 0; e < 5; e++) {
					_initTime.xReg[e].x = (int)RW(off + 12 + e * 4);
					_initTime.xReg[e].proc = (int)RW(off + 12 + e * 4 + 2);
					if (_initTime.xReg[e].proc > 0) {
						debugC(1, kDebugScripts, "Angel: xReg[%d] x=%d proc=%d",
						       e, _initTime.xReg[e].x,
						       _initTime.xReg[e].proc);
					}
				}
			}
			// rec 5 and 7: secondary time data (display flags etc.), deferred
			timeIdx++;
			break;
		}

		case kPersonEntry: {
			// Person layout (36 bytes):
			//   0-1:   disc (0)
			//   2-3:   n (description key)
			//   4-5:   pName (VWordIndex)
			//   6-13:  sFun[0..3] (4 × 2 bytes: Trade, Greet, Gift, Secret)
			//   14-21: carrying (ObjSet, 8 bytes)
			//   22+:   packed fields (located, mood, direction, etc.)
			if (personIdx < kMaxCastSize + 1) {
				Person &p = _cast[personIdx];
				p.n = (int)RW(off + 2);
				p.pName = (int)RW(off + 4) - 1;  // VWordIndex: 1-based in file → 0-based
				for (int s = 0; s < 4 && s < kSecretOp - kTradeOp + 1; s++)
					p.sFun[s] = (int)RW(off + 6 + s * 2);
				// Packed fields after carrying (PACKED RECORD, offset 22):
				// Word at offset 22 contains packed subrange fields.
				// Bits 1-7 = Located (LocRef, 7 bits).
				// Bit 0 is the last bit of the previous packed field (Carrying ObjSet).
				// Verified by cross-referencing with Place.People sets:
				//   Person[6] (Marion) >> 1 = 7, Location 7 People bit 6 set ✓
				//   Person[2] (Benito) >> 1 = 10, Location 10 People bit 2 set ✓
				uint16 packedWord = RW(off + 22);
				p.located = (packedWord >> 1) & 0x7F;
				p.unseen = (p.n > 0);  // Entities with descriptions start unseen
				p.resting = false;
				debugC(2, kDebugScripts, "Angel: Person[%d]: n=%d pName=%d located=%d (raw=0x%04X)",
				       personIdx, p.n, p.pName, p.located, packedWord);
			}
			personIdx++;
			break;
		}

		case kObjEntry: {
			// Object layout (36 bytes):
			//   0-1:   disc (1)
			//   2-9:   contents (ObjSet, 8 bytes = 64 bits)
			//   10-11: n (description key)
			//   12-13: oName (VWordIndex)
			//   14-15: size (Measure 0..9)
			//   16-17: value (Measure 0..9)
			//   18-25: properties (PropSet, 8 bytes = 64 bits)
			//   26-27: state (Measure 0..9)
			//   28-29: inOrOn (ObjRef)
			//   30:    kindOfThing (ObjType enum)
			//   31:    useThe (boolean)
			//   32:    litUp (boolean)
			//   33:    itsOpen (boolean)
			//   34:    itsLocked (boolean)
			//   35:    unseen (boolean)
			if (objIdx < kMaxNbrObjects + 1) {
				Object &obj = _props[objIdx];
				// Object is a PACKED RECORD. Fields are bit-packed into words:
				//   w1-w4  (off+2):  Contents ObjSet (64 bits, 4 words)
				//   w5     (off+10): n Natural (full word)
				//   w6     (off+12): OName(9 bits) + Size(4 bits) + 3 unused
				//   w7     (off+14): Value(4 bits) + 12 unused
				//   w8-w11 (off+16): Properties SET OF OProp (64 bits, 4 words)
				//   w12    (off+24): State(4) + InOrOn(6) + KindOfThing(3) +
				//                    UseThe(1) + LitUp(1) + ItsOpen(1)
				//   w13    (off+26): ItsLocked(1) + Unseen(1) + padding
				obj.contents.setWord(0, (uint32)RW(off + 2) << 16 | RW(off + 4));
				obj.contents.setWord(1, (uint32)RW(off + 6) << 16 | RW(off + 8));
				obj.n = (int)RW(off + 10);

				uint16 w6 = RW(off + 12);
				obj.oName = (int)(w6 & 0x1FF) - 1;  // 9-bit VWordIndex, 1-based → 0-based
				obj.size = (int)((w6 >> 9) & 0xF);

				obj.value = (int)(RW(off + 14) & 0xF);

				obj.properties.setWord(0, (uint32)RW(off + 16) << 16 | RW(off + 18));
				obj.properties.setWord(1, (uint32)RW(off + 20) << 16 | RW(off + 22));

				uint16 w12 = RW(off + 24);
				obj.state = (int)(w12 & 0xF);
				obj.inOrOn = (int)((w12 >> 4) & 0x3F);
				obj.kindOfThing = (ObjType)((w12 >> 10) & 0x7);
				obj.useThe = ((w12 >> 13) & 1) != 0;
				obj.litUp = ((w12 >> 14) & 1) != 0;
				obj.itsOpen = ((w12 >> 15) & 1) != 0;

				uint16 w13 = RW(off + 26);
				obj.itsLocked = (w13 & 1) != 0;
				obj.unseen = ((w13 >> 1) & 1) != 0;

				debugC(2, kDebugScripts, "Angel: Object[%d]: n=%d oName=%d size=%d val=%d state=%d kind=%d useThe=%d",
				       objIdx, obj.n, obj.oName, obj.size, obj.value, obj.state, obj.kindOfThing, obj.useThe);
			}
			objIdx++;
			break;
		}

		case kMapEntry: {
			// Place PACKED RECORD layout (36 bytes):
			//   0-1:   disc (2)
			//   2-3:   n (description key, big-endian)
			//   4-5:   shortDscr (VWordIndex)
			//   6-11:  nextPlace[0..5] — PACKED ARRAY[MotionSpec] OF LocRef
			//          3 big-endian words, each packing 2 LocRefs (7 bits each),
			//          LSB-first: bits 0-6 = first dir, bits 7-13 = second dir.
			//   12+:   traffic, curb, accessLock, mustHave, fogPath, view, flags
			if (locIdx < kMaxNbrLocations + 1) {
				Place &place = _map[locIdx];
				place.n = (int)RW(off + 2);
				place.shortDscr = (int)RW(off + 4) - 1;  // VWordIndex: 1-based in file → 0-based

				// Decode nextPlace from 3 packed words (bytes 6-11).
				for (int d = 0; d < 3; d++) {
					uint16 w = RW(off + 6 + d * 2);
					place.nextPlace[d * 2]     = w & 0x7F;         // bits 0-6
					place.nextPlace[d * 2 + 1] = (w >> 7) & 0x7F;  // bits 7-13
				}

				place.unseen = true;
				place.useThe = true;   // All 68 locations use NoCaps display → "the [name]"
				place.view = kSunlit;  // Default to sunlit; exact parsing TBD

				debugC(2, kDebugScripts, "Angel: Map[%d]: n=%d shortDscr=%d exits=[N=%d,S=%d,E=%d,W=%d,U=%d,D=%d]",
				       locIdx, place.n, place.shortDscr,
				       place.nextPlace[0], place.nextPlace[1],
				       place.nextPlace[2], place.nextPlace[3],
				       place.nextPlace[4], place.nextPlace[5]);
			}
			locIdx++;
			break;
		}

		case kVclEntry: {
			// Vehicle layout (36 bytes):
			//   0-1:   disc (3)
			//   2-3:   n (description key)
			//   4-5:   rideProc
			//   6-7:   vName (VWordIndex)
			//   8-15:  cantCarry (ObjSet, 8 bytes)
			//   16-17: stopped (LocRef)
			//   18:    useThe (boolean)
			//   19:    unseen (boolean)
			//   20+:   vclType + variant (route/inside)
			if (vclIdx < kMaxNbrVehicles + 1) {
				Vehicle &vcl = _fleet[vclIdx];
				vcl.n = (int)RW(off + 2);
				vcl.rideProc = (int)RW(off + 4);
				vcl.vName = (int)RW(off + 6) - 1;  // VWordIndex: 1-based in file → 0-based
				vcl.stopped = (int)RW(off + 16);
				vcl.useThe = (buf[off + 18] != 0);
				vcl.unseen = (buf[off + 19] != 0);
				debugC(2, kDebugScripts, "Angel: Vehicle[%d]: n=%d vName=%d stopped=%d",
				       vclIdx, vcl.n, vcl.vName, vcl.stopped);
			}
			vclIdx++;
			break;
		}

		default:
			warning("Angel: Unknown table record type %d at record %d", disc, i);
			break;
		}
	}

	#undef RW

	delete[] buf;

	debugC(1, kDebugScripts, "Angel: Tables loaded. Starting location=%d, WELCOME proc=%d",
	       _initGeneral.location, _initTime.xReg[kXWelcome].proc);
	return true;
}

bool GameData::loadVocab(Common::SeekableReadStream *stream) {
	if (stream->size() == 6136) {
		_isDosData = true;
		const int fileSize = stream->size();
		debugC(1, kDebugScripts, "Angel: DOS vocab file size = %d bytes", fileSize);

		byte *buf = new byte[fileSize];
		stream->read(buf, fileSize);

		static const int kRecordSize = 26;
		_nbrVWords = fileSize / kRecordSize;
		if (_nbrVWords > kMaxNbrVWords)
			_nbrVWords = kMaxNbrVWords;

		debugC(1, kDebugScripts, "Angel: DOS vocab loading %d entries", _nbrVWords);

		int curBlock = 1;
		for (int i = 1; i <= kNbrVBlocks; i++)
			_vText[i].clear();

		for (int i = 0; i < _nbrVWords; i++) {
			const byte *rec = buf + i * kRecordSize;

			int wordLen = rec[0];
			if (wordLen < 1 || wordLen > kNameSize)
				wordLen = 0;

			Common::String decoded;
			for (int c = 1; c <= wordLen; c++) {
				int nip = rec[c];
				if (nip >= 0 && nip < 64)
					decoded += _yTable[nip];
			}

			if (curBlock <= kNbrVBlocks && _vText[curBlock].size() + decoded.size() > 255)
				curBlock++;

			VEntry &ve = _vocab[i];
			if (curBlock <= kNbrVBlocks && !decoded.empty()) {
				ve.dsp = _vText[curBlock].size();
				ve.len = decoded.size();
				ve.vbi = curBlock;
				_vText[curBlock] += decoded;
			} else {
				ve.dsp = 0;
				ve.len = 0;
				ve.vbi = 0;
			}

			ve.ve.raw0 = (rec[22] << 8) | rec[23];
			ve.ve.raw1 = (rec[24] << 8) | rec[25];
			ve.ve.code = (VWords)rec[22];
			ve.ve.display = (DsplType)rec[23];
			ve.ve.vType = (KindOfWord)rec[24];
			ve.ve.ref = (int)rec[25];

			debugC(2, kDebugScripts, "Angel: DOS vocab[%d] = '%s' type=%d code=%d ref=%d raw0=%d raw1=%d",
			       i, decoded.c_str(), ve.ve.vType, ve.ve.code,
			       ve.ve.ref, ve.ve.raw0, ve.ve.raw1);
		}

		delete[] buf;

		debugC(1, kDebugScripts, "Angel: DOS vocab loaded %d words into %d text blocks", _nbrVWords, curBlock);
		for (int i = 1; i <= kNbrVBlocks; i++) {
			if (!_vText[i].empty())
				debugC(1, kDebugScripts, "Angel: VText[%d] = %u chars", i, _vText[i].size());
		}

		return true;
	}

	/**
	 * The vocab file is a UCSD Pascal "FILE OF XVEntry" with 13-word (26-byte) records.
	 *
	 * XVEntry layout (26 bytes = 13 big-endian 16-bit words):
	 *   Bytes 0-21:  ComprWord (11 BE words, IXP 2,6 packed array of 22 6-bit nips)
	 *   Bytes 22-25: VECore (2 BE words, packed record)
	 *
	 * ComprWord IXP 2,6 decoding: each word holds 2 nips:
	 *   even nip = word & 0x3F, odd nip = (word >> 6) & 0x3F
	 *   nip[0] = word length, nips[1..length] = YTable-ciphered characters
	 *
	 * VECore byte layout:
	 *   byte22 bits 0-3: vType (KindOfWord)
	 *   byte22 bits 4-6: ref for directions/days
	 *   byte23 bits 0-5: code (VWords)
	 *   byte23 bits 6-7: display (DsplType)
	 *   byte25:          ref for persons/locations/objects/verbs/others
	 */

	_isDosData = false;
	int fileSize = stream->size();
	debugC(1, kDebugScripts, "Angel: vocab file size = %d bytes", fileSize);

	byte *buf = new byte[fileSize];
	stream->read(buf, fileSize);

	static const int kRecordSize = 26;
	_nbrVWords = fileSize / kRecordSize;
	if (_nbrVWords > kMaxNbrVWords)
		_nbrVWords = kMaxNbrVWords;

	debugC(1, kDebugScripts, "Angel: vocab loading %d entries", _nbrVWords);

	// Initialize VText blocks
	int curBlock = 1;
	for (int i = 1; i <= kNbrVBlocks; i++)
		_vText[i].clear();

	for (int i = 0; i < _nbrVWords; i++) {
		const byte *rec = buf + i * kRecordSize;

		// --- Decode ComprWord (IXP 2,6 big-endian words) ---
		int nips[22];
		for (int w = 0; w < 11; w++) {
			uint16 word = (rec[w * 2] << 8) | rec[w * 2 + 1];
			nips[w * 2]     = word & 0x3F;
			nips[w * 2 + 1] = (word >> 6) & 0x3F;
		}

		int wordLen = nips[0];
		if (wordLen < 1 || wordLen > kNameSize)
			wordLen = 0;

		// Decode nips to characters via YTable
		Common::String decoded;
		for (int c = 1; c <= wordLen; c++) {
			int nip = nips[c];
			decoded += _yTable[nip];
		}

		// --- Build VText blocks ---
		// Each block can hold up to 255 characters (Pascal STRING[255]).
		// When adding a word would exceed 255, advance to the next block.
		if (curBlock <= kNbrVBlocks && _vText[curBlock].size() + decoded.size() > 255) {
			curBlock++;
		}

		VEntry &ve = _vocab[i];
		if (curBlock <= kNbrVBlocks && !decoded.empty()) {
			ve.dsp = _vText[curBlock].size();
			ve.len = decoded.size();
			ve.vbi = curBlock;
			_vText[curBlock] += decoded;
		} else {
			ve.dsp = 0;
			ve.len = 0;
			ve.vbi = 0;
		}

		// --- Decode VECore (bytes 22-25) ---
		byte b22 = rec[22];
		byte b23 = rec[23];
		byte b24 = rec[24];
		byte b25 = rec[25];

		// Store raw packed words for testIs $ path (proc 77 uses DIVI on these)
		ve.ve.raw0 = (b22 << 8) | b23;
		ve.ve.raw1 = (b24 << 8) | b25;

		ve.ve.code = (VWords)(b23 & 0x3F);
		ve.ve.display = (DsplType)((b23 >> 6) & 0x3);
		ve.ve.vType = (KindOfWord)(b22 & 0x0F);

		// VECore is a PACKED RECORD with a variant field after VType.
		// VType ends at bit 11 of word 0, leaving 4 bits (12-15).
		// Small variants (≤4 bits) are packed in word 0 upper bits.
		// Large variants (>4 bits) start at bit 0 of word 1.
		uint16 word1_be = (b24 << 8) | b25;
		switch (ve.ve.vType) {
		case kAnObject:     // ObjRef 1..63 → 6 bits in word 1
			ve.ve.ref = word1_be & 0x3F;
			break;
		case kAPerson:      // PersonRef 1..31 → 5 bits in word 1
			ve.ve.ref = word1_be & 0x1F;
			break;
		case kALocation:    // LocRef 1..119 → 7 bits in word 1
		case kABuilding:    // Also LocRef
			ve.ve.ref = word1_be & 0x7F;
			break;
		case kAVehicle:     // VehicleRef 1..15 → 4 bits in word 0 upper
			ve.ve.ref = (b22 >> 4) & 0xF;
			break;
		case kADirection:   // MotionSpec 0..5 → 3 bits in word 0 upper
		case kADay:         // DayOfWeek 0..6 → 3 bits in word 0 upper
			ve.ve.ref = (b22 >> 4) & 0x7;
			break;
		case kAVerb:        // OProp 0..63 → 6 bits in word 1
			ve.ve.ref = word1_be & 0x3F;
			break;
		case kAnOther:      // OtherCount 1..119 → 7 bits in word 1
			ve.ve.ref = word1_be & 0x7F;
			break;
		default:            // APronoun, APreposition: no variant data
			ve.ve.ref = 0;
			break;
		}

		debugC(2, kDebugScripts, "Angel: vocab[%d] = '%s' type=%d code=%d ref=%d raw0=%d raw1=%d",
		       i, decoded.c_str(), ve.ve.vType, ve.ve.code, ve.ve.ref, ve.ve.raw0, ve.ve.raw1);

	}

	delete[] buf;

	debugC(1, kDebugScripts, "Angel: vocab loaded %d words into %d text blocks", _nbrVWords, curBlock);
	for (int i = 1; i <= kNbrVBlocks; i++) {
		if (!_vText[i].empty())
			debugC(1, kDebugScripts, "Angel: VText[%d] = %u chars", i, _vText[i].size());
	}

	return true;
}

bool GameData::initMessageVM(Common::SeekableReadStream *stream) {
	_messageFile = stream;

	int fileSize = stream->size();
	debugC(1, kDebugScripts, "Angel: message file size = %d bytes", fileSize);
	debugC(1, kDebugScripts, "Angel: message file pages = %d (of max %d)",
	       (fileSize + kPageSize - 1) / kPageSize, kVMPCapacity);

	// Initialize VM page cache
	for (int i = 0; i < kVMPCapacity; i++) {
		_vmPages[i].loaded = false;
		_vmPages[i].pageNum = -1;
	}

	return true;
}

GameData::VMPage &GameData::getPage(int pageNum) {
	// Simple direct-mapped cache: page N goes to slot N
	assert(pageNum >= 0 && pageNum < kVMPCapacity);

	VMPage &page = _vmPages[pageNum];
	if (!page.loaded || page.pageNum != pageNum) {
		_messageFile->seek(pageNum * kPageSize);
		int bytesRead = _messageFile->read(page.data, kPageSize);
		if (bytesRead < kPageSize)
			memset(page.data + bytesRead, 0, kPageSize - bytesRead);
		page.loaded = true;
		page.pageNum = pageNum;
	}
	return page;
}

Chunk GameData::readChunk(int recordIndex) {
	// Each page holds (VMBFactor+1) = 85 chunks of 6 bytes each
	// Page number = recordIndex / 85
	// Offset within page = 2 (header) + (recordIndex % 85) * 6
	// The first 2 bytes of each 512-byte page are padding/header (0xe5 0xe5)
	int chunksPerPage = kVMBFactor + 1;
	int pageNum = recordIndex / chunksPerPage;
	int chunkOffset = kPageHeader + (recordIndex % chunksPerPage) * kChunkWidth;

	VMPage &page = getPage(pageNum);

	Chunk chunk;
	memcpy(chunk.data, page.data + chunkOffset, kChunkWidth);
	return chunk;
}

void GameData::loadResponseTable() {
	// The response address table maps location indices to response handler
	// message addresses. In the original P-code, this is populated during
	// INITIALI by reading 64 entries (3 nips each, 18-bit addresses) from
	// a message in the MESSAGE file.
	//
	// Auto-detect the table by scanning for a message with length=192
	// (64 entries * 3 nips) whose entries are all valid chunk addresses.

	if (!_messageFile)
		return;

	int fileSize = _messageFile->size();
	int maxPages = (fileSize + kPageSize - 1) / kPageSize;
	int maxAddr = maxPages * (kVMBFactor + 1);

	int tableAddr = -1;

	// Scan from the end of the file backwards (the table is typically near the end)
	for (int addr = maxAddr - 1; addr >= 0 && tableAddr < 0; addr--) {
		Chunk hdrChunk = readChunk(addr);
		int h0 = hdrChunk.getNip(0);
		int h1 = hdrChunk.getNip(1);
		int length = (h0 << 6) | h1;

		if (length != 192)
			continue;

		// Check that all 64 entries decode to valid addresses (< maxAddr)
		bool allValid = true;
		for (int i = 0; i < 64 && allValid; i++) {
			int nipPos = 2 + i * 3;
			int chunkIdx = addr + nipPos / kChunkSize;
			int cursorIdx = nipPos % kChunkSize;
			Chunk c = readChunk(chunkIdx);
			int n1 = c.getNip(cursorIdx);

			nipPos++;
			chunkIdx = addr + nipPos / kChunkSize;
			cursorIdx = nipPos % kChunkSize;
			c = readChunk(chunkIdx);
			int n2 = c.getNip(cursorIdx);

			nipPos++;
			chunkIdx = addr + nipPos / kChunkSize;
			cursorIdx = nipPos % kChunkSize;
			c = readChunk(chunkIdx);
			int n3 = c.getNip(cursorIdx);

			int val = (n1 << 12) | (n2 << 6) | n3;
			if (val > 0 && val >= maxAddr)
				allValid = false;
		}

		if (allValid)
			tableAddr = addr;
	}

	if (tableAddr < 0) {
		debugC(1, kDebugScripts, "Angel: Response table not found in message file");
		return;
	}

	debugC(1, kDebugScripts, "Angel: Response table found at message address %d", tableAddr);

	// Read 64 raw entries into a temporary array (g[229+0..63] in P-code).
	int entries[64];
	memset(entries, 0, sizeof(entries));
	for (int i = 0; i < 64; i++) {
		int nipPos = 2 + i * 3;
		int chunkIdx = tableAddr + nipPos / kChunkSize;
		int cursorIdx = nipPos % kChunkSize;
		Chunk c = readChunk(chunkIdx);
		int n1 = c.getNip(cursorIdx);

		nipPos++;
		chunkIdx = tableAddr + nipPos / kChunkSize;
		cursorIdx = nipPos % kChunkSize;
		c = readChunk(chunkIdx);
		int n2 = c.getNip(cursorIdx);

		nipPos++;
		chunkIdx = tableAddr + nipPos / kChunkSize;
		cursorIdx = nipPos % kChunkSize;
		c = readChunk(chunkIdx);
		int n3 = c.getNip(cursorIdx);

		entries[i] = (n1 << 12) | (n2 << 6) | n3;
		if (entries[i] > 0) {
			debugC(2, kDebugScripts, "Angel: Response entry[%d] = %d", i, entries[i]);
		}
	}

	// Map locations to response addresses.
	// P-code formula: responseAddr = g[229 + g[326+entityIdx*4+3] / 80]
	//
	// The location table uses a dummy slot for kNowhere=1, and the response
	// table has the same dummy slot at entry[0]. The live places therefore
	// map to entries[loc - 1], not entries[loc].
	//
	// This matches the DOS startup behavior:
	//   loc 7 (central chamber) -> entry[6] = 2667
	//   loc 6 (tomb)            -> entry[5] = 86
	//
	// Using entries[loc] shifts all handlers by one location and causes the
	// early chamber/tomb scripts to fire on the wrong rooms.
	for (int loc = 1; loc <= _nbrLocations && loc < 64; loc++) {
		const int entryIdx = loc - 1;
		if (entries[entryIdx] > 0) {
			_map[loc].responseAddr = entries[entryIdx];
			debugC(2, kDebugScripts, "Angel: Location %d responseAddr = %d (entry[%d])",
			       loc, entries[entryIdx], entryIdx);
		}
	}
}

} // End of namespace Angel
} // End of namespace Glk
