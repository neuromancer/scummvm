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

#include "engines/game.h"

namespace Glk {
namespace Angel {

const PlainGameDescriptor ANGEL_GAME_LIST[] = {
	{ "angelsoft", "AngelSoft Game" },

	{ "indianajonesancients", "Indiana Jones in Revenge of the Ancients" },

	{ nullptr, nullptr }
};

/**
 * AngelSoft games require multiple data files. Each entry specifies
 * MD5 and filesize for all required files.
 */
struct AngelGameFile {
	const char *_filename;
	const char *_md5;
	size_t _filesize;
};

struct AngelDetectionEntry {
	const char *_gameId;
	Common::Language _language;
	Common::Platform _platform;
	AngelGameFile _files[6]; // Up to 5 files + null terminator
};

const AngelDetectionEntry ANGEL_GAME_ENTRIES[] = {
	// Indiana Jones in Revenge of the Ancients
	{
		"indianajonesancients",
		Common::EN_ANY,
		Common::kPlatformMacintosh,
		{
			{ "tables",        "aca35ed9c97255d68e5b0ec623a95eab", 4096 },
			{ "vocab",         "a35077d350e0b1bbcc6c9cd18f7e458a", 6144 },
			{ "message",       "e4326535ee63849c94c8ea29b533d47f", 48128 },
			{ "BOOTUP",        "89912f74d8af56c81b6b90901a018665", 15360 },
			{ "StartupScreen", "b5e9322d3d4e2fa75a69498cf1ca6d00", 21888 },
			{ nullptr, nullptr, 0 }
		}
	},
	{
		"indianajonesancients",
		Common::EN_ANY,
		Common::kPlatformDOS,
		{
			{ "tables",        "aca35ed9c97255d68e5b0ec623a95eab", 4096 },
			{ "vocab",         "a35077d350e0b1bbcc6c9cd18f7e458a", 6144 },
			{ "message",       "e4326535ee63849c94c8ea29b533d47f", 48128 },
			{ "game.com",       "e4326535ee63849c94c8ea29b533d47f", 48128 },
			{ nullptr, nullptr, 0 }
		}
	},
	{ nullptr, Common::EN_ANY, Common::kPlatformUnknown, {{ nullptr, nullptr, 0 }} }
};

// Keep the single-file entry table for getDetectionEntries() compatibility
const GlkDetectionEntry ANGEL_GAMES[] = {
	DT_ENTRY0("indianajonesancients", "aca35ed9c97255d68e5b0ec623a95eab", 4096),
	DT_END_MARKER
};

} // End of namespace Angel
} // End of namespace Glk
