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

#include "interspective/mapfile.h"

#include "common/endian.h"
#include "common/textconsole.h"
#include "common/util.h"

using namespace Common;

namespace Interspective {

static int32 entryOffsetLikeDos(uint16 index) {
	return int16(uint16(uint16(index - 1) * 4));
}

void MapFile::readFile(SeekableReadStream &stream) {
	/*uint32 actually_read = */ stream.read(_data, 1200);

	//	_entryCount = actually_read / 4;
}

uint32 MapFile::offsetOfEntry(uint16 index) {
	const int32 offset = entryOffsetLikeDos(index);
	if (offset < 0 || offset + 3 >= int32(sizeof(_data))) {
		warning("MapFile::offsetOfEntry: id %u resolves outside %s (entryOff=%d)",
				index, filename(), offset);
		return 0;
	}
	return READ_LE_UINT32(_data + offset);
}

void MapFile::patchEntryLow16(uint16 index, uint16 value) {
	if (index == 0 || index * 4 > 1200)
		return;
	WRITE_LE_UINT16(_data + (index - 1) * 4, value);
}

} // End of namespace Interspective
