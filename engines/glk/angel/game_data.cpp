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
#include "common/debug.h"
#include "common/textconsole.h"

namespace Glk {
namespace Angel {

GameData::GameData() : _castSize(0), _nbrObjects(0), _nbrLocations(0),
                       _nbrVehicles(0), _nbrVWords(0), _nbrProperties(0),
                       _messageFile(nullptr) {
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

	return true;
}

bool GameData::loadTables(Common::SeekableReadStream *stream) {
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
	debugC(1, 0, "Angel: tables file size=%d bytes, %d records", fileSize, totalRecords);

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

	debugC(1, 0, "Angel: tables: %d persons, %d objects, %d locations, %d vehicles",
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

			debugC(1, 0, "Angel: MscEntry: location=%d direction=%d nbrPoss=%d",
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
			debugC(1, 0, "Angel: ComEntry at rec %d (parsing deferred)", i);
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
				debugC(1, 0, "Angel: TimeEntry[%d]: clock day=%d %d:%02d %s",
				       timeIdx, _initTime.day, _initTime.hour,
				       _initTime.minute, _initTime.am ? "AM" : "PM");
			} else if (timeIdx == 2) {
				// rec 6: NtgrRegisters first half (xReg[0..4])
				// Contains WELCOME, CURSE, ENTRY, MOVE events
				for (int e = 0; e < 5; e++) {
					_initTime.xReg[e].x = (int)RW(off + 12 + e * 4);
					_initTime.xReg[e].proc = (int)RW(off + 12 + e * 4 + 2);
					if (_initTime.xReg[e].proc > 0) {
						debugC(1, 0, "Angel: xReg[%d] x=%d proc=%d",
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
				p.pName = (int)RW(off + 4);
				for (int s = 0; s < 4 && s < kSecretOp - kTradeOp + 1; s++)
					p.sFun[s] = (int)RW(off + 6 + s * 2);
				// Packed fields after carrying — parse located from bytes 22-23
				p.located = (int)RW(off + 22);
				p.unseen = (p.n > 0);  // Visible if has description
				p.resting = false;
				debugC(2, 0, "Angel: Person[%d]: n=%d pName=%d located=%d",
				       personIdx, p.n, p.pName, p.located);
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
				// Contents: 8 bytes as 2 x 32-bit words (big-endian)
				obj.contents.setWord(0, (uint32)RW(off + 2) << 16 | RW(off + 4));
				obj.contents.setWord(1, (uint32)RW(off + 6) << 16 | RW(off + 8));
				obj.n = (int)RW(off + 10);
				obj.oName = (int)RW(off + 12);
				obj.size = (int)RW(off + 14);
				obj.value = (int)RW(off + 16);
				// Properties: 8 bytes as 2 x 32-bit words (big-endian)
				obj.properties.setWord(0, (uint32)RW(off + 18) << 16 | RW(off + 20));
				obj.properties.setWord(1, (uint32)RW(off + 22) << 16 | RW(off + 24));
				obj.state = (int)RW(off + 26);
				obj.inOrOn = (int)RW(off + 28);
				obj.kindOfThing = (ObjType)buf[off + 30];
				obj.useThe = (buf[off + 31] != 0);
				obj.litUp = (buf[off + 32] != 0);
				obj.itsOpen = (buf[off + 33] != 0);
				obj.itsLocked = (buf[off + 34] != 0);
				obj.unseen = (buf[off + 35] != 0);
				debugC(2, 0, "Angel: Object[%d]: n=%d oName=%d size=%d val=%d state=%d kind=%d",
				       objIdx, obj.n, obj.oName, obj.size, obj.value, obj.state, obj.kindOfThing);
			}
			objIdx++;
			break;
		}

		case kMapEntry: {
			// Place layout (36 bytes):
			//   0-1:   disc (2)
			//   2-3:   n (description key, big-endian)
			//   4-5:   shortDscr (VWordIndex)
			//   6+:    packed navigation/flags (nextPlace, traffic, curb, etc.)
			//   34-35: packed flags including view/unseen
			if (locIdx < kMaxNbrLocations + 1) {
				Place &place = _map[locIdx];
				place.n = (int)RW(off + 2);
				place.shortDscr = (int)RW(off + 4);
				place.unseen = true;
				place.view = kSunlit;  // Default to sunlit; exact parsing TBD
				debugC(2, 0, "Angel: Map[%d]: n=%d shortDscr=%d",
				       locIdx, place.n, place.shortDscr);
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
				vcl.vName = (int)RW(off + 6);
				vcl.stopped = (int)RW(off + 16);
				vcl.useThe = (buf[off + 18] != 0);
				vcl.unseen = (buf[off + 19] != 0);
				debugC(2, 0, "Angel: Vehicle[%d]: n=%d vName=%d stopped=%d",
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

	warning("Angel: Tables loaded. Starting location=%d, WELCOME proc=%d",
	       _initGeneral.location, _initTime.xReg[kXWelcome].proc);
	return true;
}

bool GameData::loadVocab(Common::SeekableReadStream *stream) {
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

	int fileSize = stream->size();
	debugC(1, 0, "Angel: vocab file size = %d bytes", fileSize);

	byte *buf = new byte[fileSize];
	stream->read(buf, fileSize);

	static const int kRecordSize = 26;
	_nbrVWords = fileSize / kRecordSize;
	if (_nbrVWords > kMaxNbrVWords)
		_nbrVWords = kMaxNbrVWords;

	debugC(1, 0, "Angel: vocab loading %d entries", _nbrVWords);

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
		// byte b24 = rec[24]; // reserved/extra metadata
		byte b25 = rec[25];

		ve.ve.code = (VWords)(b23 & 0x3F);
		ve.ve.display = (DsplType)((b23 >> 6) & 0x3);
		ve.ve.vType = (KindOfWord)(b22 & 0x0F);

		// Ref field depends on vType: small-range types use byte22 upper bits,
		// larger-range types use byte25
		if (ve.ve.vType == kADirection || ve.ve.vType == kADay) {
			ve.ve.ref = (b22 >> 4) & 0x7;
		} else {
			ve.ve.ref = b25;
		}

		debugC(2, 0, "Angel: vocab[%d] = '%s' type=%d code=%d ref=%d",
		       i, decoded.c_str(), ve.ve.vType, ve.ve.code, ve.ve.ref);
	}

	delete[] buf;

	debugC(1, 0, "Angel: vocab loaded %d words into %d text blocks", _nbrVWords, curBlock);
	for (int i = 1; i <= kNbrVBlocks; i++) {
		if (!_vText[i].empty())
			debugC(1, 0, "Angel: VText[%d] = %u chars", i, _vText[i].size());
	}

	return true;
}

bool GameData::initMessageVM(Common::SeekableReadStream *stream) {
	_messageFile = stream;

	int fileSize = stream->size();
	debugC(1, 0, "Angel: message file size = %d bytes", fileSize);
	debugC(1, 0, "Angel: message file pages = %d (of max %d)",
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

} // End of namespace Angel
} // End of namespace Glk
