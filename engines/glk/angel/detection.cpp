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
	// AngelSoft games are identified by the presence of a "tables" file
	// alongside "message" and "vocab" companion files.
	// We key detection on the "tables" file.

	for (Common::FSList::const_iterator file = fslist.begin(); file != fslist.end(); ++file) {
		if (file->isDirectory())
			continue;

		Common::String filename = file->getName();

		// Look for the "tables" data file (case-insensitive)
		if (!filename.equalsIgnoreCase("tables"))
			continue;

		Common::File gameFile;
		if (!gameFile.open(*file))
			continue;

		gameFile.seek(0);
		Common::String md5 = Common::computeStreamMD5AsString(gameFile, 5000);
		uint32 filesize = gameFile.size();

		// Check if this matches any known game
		const GlkDetectionEntry *p = ANGEL_GAMES;
		while (p->_md5) {
			if (md5 == p->_md5 || (p->_filesize == filesize && !strcmp(p->_md5, "00000000000000000000000000000000")))
				break;
			++p;
		}

		if (!p->_gameId) {
			// Unknown AngelSoft game — use generic entry
			const PlainGameDescriptor &desc = ANGEL_GAME_LIST[0];
			gameList.push_back(GlkDetectedGame(desc.gameId, desc.description, filename, md5, filesize));
		} else {
			PlainGameDescriptor gameDesc = findGame(p->_gameId);
			gameList.push_back(GlkDetectedGame(p->_gameId, gameDesc.description, filename, md5, filesize));
		}
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
