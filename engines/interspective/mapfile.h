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

#ifndef INTERSPECTIVE_MAPFILE_H
#define INTERSPECTIVE_MAPFILE_H

#include "common/str.h"
#include "common/stream.h"

#include "interspective/datafile.h"

namespace Interspective {

class Resources;

class MapFile : public Datafile {
public:
	MapFile(const char *name) : Datafile(0), _filename(name) {}
	const char *filename() const { return _filename.c_str(); }
	void readFile(Common::SeekableReadStream &stream);

	uint32 offsetOfEntry(uint16 index);
	// DOS Op_ca patches only the low 16 bits of an entry's offset
	// (`MOV word ptr ES:[DI], AX` @ 1000:5269). Index is 1-based.
	void patchEntryLow16(uint16 index, uint16 value);
	// Number of 4-byte entries (capacity = 1200/4 = 300).
	uint16 entryCapacity() const { return 1200 / 4; }

private:
	byte _data[1200];
	uint16 _imgCount;
	Common::String _filename;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_MAPFILE_H
