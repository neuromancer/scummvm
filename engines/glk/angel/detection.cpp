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

#include "glk/angel/detection.h"
#include "glk/angel/detection_tables.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/md5.h"
#include "engines/game.h"

namespace Glk {
namespace Angel {

void AngelMetaEngine::getSupportedGames(PlainGameList &games) {
	for (const PlainGameDescriptor *pd = ANGEL_GAME_LIST; pd->gameId; ++pd)
		games.push_back(*pd);
}

const GlkDetectionEntry *AngelMetaEngine::getDetectionEntries() {
	return ANGEL_GAMES;
}

GameDescriptor AngelMetaEngine::findGame(const char *gameId) {
	for (const PlainGameDescriptor *pd = ANGEL_GAME_LIST; pd->gameId; ++pd) {
		if (!strcmp(gameId, pd->gameId))
			return *pd;
	}
	return GameDescriptor::empty();
}

bool AngelMetaEngine::detectGames(const Common::FSList &fslist, DetectedGames &gameList) {
	// AngelSoft games require multiple data files. We check each known game
	// entry against the directory contents, requiring all files to be present.

	for (const AngelDetectionEntry *entry = ANGEL_GAME_ENTRIES; entry->_gameId; ++entry) {
		// Build a map of filename -> FSNode for all required files
		bool allFound = true;
		Common::HashMap<Common::String, Common::FSNode> foundFiles;

		for (const AngelGameFile *gf = entry->_files; gf->_filename; ++gf) {
			bool fileFound = false;
			for (Common::FSList::const_iterator file = fslist.begin(); file != fslist.end(); ++file) {
				if (file->isDirectory())
					continue;
				if (file->getName().equalsIgnoreCase(gf->_filename)) {
					foundFiles[gf->_filename] = *file;
					fileFound = true;
					break;
				}
			}
			if (!fileFound) {
				allFound = false;
				break;
			}
		}

		if (!allFound)
			continue;

		// All required files found — compute MD5s and verify all files
		PlainGameDescriptor gameDesc = findGame(entry->_gameId);
		Common::String primaryFile = entry->_files[0]._filename;

		bool allMd5sMatch = true;
		for (const AngelGameFile *gf = entry->_files; gf->_filename; ++gf) {
			Common::File df;
			if (!df.open(foundFiles[gf->_filename])) {
				allMd5sMatch = false;
				break;
			}
			Common::String fileMd5 = Common::computeStreamMD5AsString(df, 5000);
			df.close();
			if (gf->_md5 && fileMd5 != gf->_md5) {
				allMd5sMatch = false;
				break;
			}
		}

		if (!allMd5sMatch)
			continue;

		// Known game — use constructor that does NOT set hasUnknownFiles
		GlkDetectedGame gd(entry->_gameId, gameDesc.description, primaryFile,
		                    entry->_language, entry->_platform);

		gameList.push_back(gd);
	}

	return !gameList.empty();
}

void AngelMetaEngine::detectClashes(Common::StringMap &map) {
	for (const PlainGameDescriptor *pd = ANGEL_GAME_LIST; pd->gameId; ++pd) {
		if (map.contains(pd->gameId))
			error("Duplicate game Id found - %s", pd->gameId);
		map[pd->gameId] = "";
	}
}

} // End of namespace Angel
} // End of namespace Glk
