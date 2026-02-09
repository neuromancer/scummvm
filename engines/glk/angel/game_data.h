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

#ifndef GLK_ANGEL_GAME_DATA_H
#define GLK_ANGEL_GAME_DATA_H

#include "glk/angel/types.h"
#include "common/stream.h"
#include "common/file.h"

namespace Glk {
namespace Angel {

/**
 * Handles loading and managing the game's data files:
 *   - tables: game world state (locations, objects, characters, vehicles)
 *   - vocab: vocabulary entries (nip-encoded words)
 *   - message: bytecode procedure strings (virtual memory paged)
 */
class GameData {
public:
	// ---- World data (from tables file) ----
	Place _map[kMaxNbrLocations + 1];
	Object _props[kMaxNbrObjects + 1];
	Person _cast[kMaxCastSize + 1];
	Vehicle _fleet[kMaxNbrVehicles + 1];
	int _robot[kMaxNbrProperties + 1];

	int _castSize;
	int _nbrObjects;
	int _nbrLocations;
	int _nbrVehicles;
	int _nbrVWords;
	int _nbrProperties;

	// Vocab data
	VEntry _vocab[kMaxNbrVWords + 1];
	Common::String _vText[kNbrVBlocks + 1];  // Blocks 1..10

	// Initial state (from tables)
	Determiner _initDeterminer;
	IntTimeRecord _initTime;
	GeneralInfo _initGeneral;
	ASuggestion _initSuggestion;
	ComRecord _initCom;

	// ---- Message file (virtual memory) ----
	struct VMPage {
		byte data[kPageSize];
		bool loaded;
		int pageNum;

		VMPage() : loaded(false), pageNum(-1) { memset(data, 0, kPageSize); }
	};

	VMPage _vmPages[kVMPCapacity];
	Common::SeekableReadStream *_messageFile;

	// Translation tables (nip cipher)
	char _yTable[64];     // nip -> char
	byte _xTable[128];    // char -> nip

	GameData();
	~GameData();

	/** Load all game data from the three data files */
	bool load(Common::SeekableReadStream *tablesFile,
	          Common::SeekableReadStream *vocabFile,
	          Common::SeekableReadStream *messageFile);

	// ---- Message VM access ----

	/** Seek to a specific chunk index in the message file */
	void vmSeek(int recordIndex);

	/** Read the current chunk at the VM cursor */
	Chunk readChunk(int recordIndex);

	/** Get a page from the VM cache, loading if necessary */
	VMPage &getPage(int pageNum);

private:
	bool loadTables(Common::SeekableReadStream *stream);
	bool loadVocab(Common::SeekableReadStream *stream);
	bool initMessageVM(Common::SeekableReadStream *stream);
	void initCipherTables();
};

} // End of namespace Angel
} // End of namespace Glk

#endif
