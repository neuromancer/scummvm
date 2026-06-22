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

#include "common/span.h"
#include "common/textconsole.h"
#include "common/util.h"

using namespace Common;

namespace Interspective {

namespace {

class MapEntryTable {
public:
	MapEntryTable(byte *data, uint32 size) : _data(data), _size(size) {}

	uint32 offsetOfEntry(uint16 index, const char *filename) const {
		const int32 offset = entryOffset(index);
		if (!contains(offset, 4)) {
			warning("MapFile::offsetOfEntry: id %u resolves outside %s (entryOff=%d)",
					index, filename, offset);
			return 0;
		}

		return entrySpan(uint32(offset), 4).getUint32LEAt(0);
	}

	void patchLow16(uint16 index, uint16 value) const {
		const uint32 offset = uint32(index - 1) * 4;
		if (index == 0 || !contains(offset, 2))
			return;

		Common::Span<byte> entry = mutableEntrySpan(offset, 2);
		entry[0] = uint8(value & 0xff);
		entry[1] = uint8(value >> 8);
	}

private:
	static int32 entryOffset(uint16 index) {
		return int16(uint16(uint16(index - 1) * 4));
	}

	bool contains(int32 offset, uint32 size) const {
		return offset >= 0 && uint32(offset) <= _size && size <= _size - uint32(offset);
	}

	bool contains(uint32 offset, uint32 size) const {
		return offset <= _size && size <= _size - offset;
	}

	Common::Span<const byte> entrySpan(uint32 offset, uint32 size) const {
		return Common::Span<const byte>(_data + offset, size);
	}

	Common::Span<byte> mutableEntrySpan(uint32 offset, uint32 size) const {
		return Common::Span<byte>(_data + offset, size);
	}

	byte *_data;
	uint32 _size;
};

} // namespace

void MapFile::readFile(SeekableReadStream &stream) {
	/*uint32 actually_read = */ stream.read(_data, 1200);

	//	_entryCount = actually_read / 4;
}

uint32 MapFile::offsetOfEntry(uint16 index) {
	return MapEntryTable(_data, sizeof(_data)).offsetOfEntry(index, filename());
}

void MapFile::patchEntryLow16(uint16 index, uint16 value) {
	MapEntryTable(_data, sizeof(_data)).patchLow16(index, value);
}

} // End of namespace Interspective
