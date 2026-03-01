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

#include "hypno/teacher/StringTable.h"
#include "common/file.h"

namespace Hypno {

StringTable::StringTable(const Common::String &filename, int loadNow) : _filename(filename) {
	if (loadNow)
		load();
}

StringTable::~StringTable() {}

void StringTable::load() {
	Common::File file;
	if (!file.open(Common::Path(_filename))) return;
	
	while (!file.eos()) {
		Common::String line = file.readLine();
		line.trim();
		if (line.empty()) continue;

		// Simple parsing: ID STRING
		size_t firstSpace = line.find(' ');
		if (firstSpace != Common::String::npos) {
			uint32 id = atoi(line.substr(0, firstSpace).c_str());
			Common::String text = line.substr(firstSpace + 1);
			text.trim();
			_strings[id] = text;
		}
	}
}

void StringTable::unload() {
	_strings.clear();
}

Common::String StringTable::getString(uint32 id) {
	if (_strings.contains(id))
		return _strings[id];
	return "";
}

} // End of namespace Hypno
