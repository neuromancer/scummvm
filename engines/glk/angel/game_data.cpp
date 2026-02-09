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

	// ---- Unused (nips 48–51) → '@' ----
	// Already set to '@' by initialization

	// ---- Digits (nips 52–61) ----
	for (int d = 0; d < 10; d++)
		_yTable[52 + d] = (char)('0' + d);

	// ---- Unused (nips 62–63) → '@' ----

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
	 * The tables file is a Pascal "FILE OF TableRecord".
	 * In UCSD Pascal / Mac Pascal, a typed file stores records sequentially
	 * with a 2-byte block header. Each record is the maximum size of the
	 * variant record (TableRecord), prefixed by the variant tag.
	 *
	 * The loading order depends on the game compiler's output.
	 * Typically the order is:
	 *   1. GeneralInfo (MscEntry)
	 *   2. Map entries (MapEntry × NbrLocations)
	 *   3. Object entries (ObjEntry × NbrObjects)
	 *   4. Person entries (PersonEntry × CastSize)
	 *   5. Vehicle entries (VclEntry × NbrVehicles)
	 *   6. Determiner (DtrEntry)
	 *   7. TimeRecord (TimeEntry)
	 *   8. Suggestion (SugEntry)
	 *   9. ComRecord (ComEntry)
	 *
	 * TODO: The exact binary layout of TableRecord needs to be reverse-engineered
	 * from the actual tables file. The record size and field offsets depend on
	 * how Mac Pascal packs PACKED RECORD fields.
	 *
	 * For now, we analyze the file to determine the record structure.
	 */

	int fileSize = stream->size();
	debugC(1, 0, "Angel: tables file size = %d bytes", fileSize);

	// Read the entire file into a buffer for analysis
	byte *buf = new byte[fileSize];
	stream->read(buf, fileSize);

	// TODO: Implement actual parsing once binary layout is determined.
	// For now, count what we expect to find based on comments in code.txt:
	// The file should contain entries for all locations, objects, persons, etc.
	//
	// Initial analysis step: dump the first few bytes to debug output
	debugC(1, 0, "Angel: tables file header bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

	// Provisional: set counts based on game constants
	// These will be overridden once we properly parse the tables file
	_nbrLocations = 3;  // At least 3 locations for testing
	_nbrObjects = 0;
	_castSize = 0;
	_nbrVehicles = 0;

	// TEMPORARY HARDCODED VALUES FOR TESTING
	// Based on reverse engineering of the 'message' file:
	// - Tables address 4100 × 8 nips/chunk = nip 32800
	// - This is the intro message starting with "let's continue..."
	// - With the page header fix, chunk 4100 should now map correctly
	_initGeneral.location = 2;
	_map[2].n = 4100;
	_map[2].shortDscr = 0;
	_map[2].unseen = true;
	_map[2].view = kSunlit;  // Not dark

	debugC(1, 0, "Angel: TEMP hardcoded _map[2].n = 4100 for intro testing");

	delete[] buf;
	return true;
}

bool GameData::loadVocab(Common::SeekableReadStream *stream) {
	/**
	 * The vocab file is a Pascal "FILE OF XVEntry".
	 * Each XVEntry contains:
	 *   - ComprWord Wd: PACKED ARRAY[0..21] OF 0..63 (22 * 6 bits = 132 bits ≈ 17 bytes)
	 *   - VECore XV: the vocabulary core data
	 *
	 * TODO: Determine exact byte size of XVEntry from the binary.
	 * Estimated: ~20 bytes per entry.
	 */

	int fileSize = stream->size();
	debugC(1, 0, "Angel: vocab file size = %d bytes", fileSize);

	// Read entire vocab file
	byte *buf = new byte[fileSize];
	stream->read(buf, fileSize);

	// TODO: Parse XVEntry records and build _vocab[] and _vText[] arrays
	// For now, log the file info
	debugC(1, 0, "Angel: vocab file header bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

	_nbrVWords = 0;

	delete[] buf;
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
