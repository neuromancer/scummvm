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

#include "interspective/program.h"

#include "common/endian.h"
#include "common/util.h"

#include "interspective/actor.h"
#include "interspective/exit.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/resources.h"
#include "interspective/util.h"
#include "interspective/value.h"

namespace Interspective {

enum FooterOffsets {
	kExitsCount = 2,
	kActorsCount = 4,
	kExits = 8,
	kActors = 0xA,
	kSpriteMap = 0x0C,
	kEntryPointOffset = 0x0E
};

enum {
	kDosResourceSegmentSize = 0x8000
};

namespace {

class ProgramFooter {
public:
	ProgramFooter(const byte *footer) : _footer(footer) {}

	uint16 exitsCount() const { return wordAt(kExitsCount); }
	uint16 actorsCount() const { return wordAt(kActorsCount); }
	uint16 exitsOffset() const { return wordAt(kExits); }
	uint16 actorsOffset() const { return wordAt(kActors); }
	uint16 spriteMapOffset() const { return wordAt(kSpriteMap); }
	uint16 entryPointOffset() const { return wordAt(kEntryPointOffset); }

private:
	uint16 wordAt(uint16 offset) const { return READ_LE_UINT16(_footer + offset); }

	const byte *_footer;
};

class ProgramCodeSegment {
public:
	ProgramCodeSegment(byte *data, uint32 size) : _readData(data), _writeData(data), _size(size) {}
	ProgramCodeSegment(const byte *data, uint32 size) : _readData(data), _writeData(0), _size(size) {}

	bool contains(uint32 offset, uint32 size) const {
		return offset <= _size && size <= _size - offset;
	}

	const byte *ptr(uint32 offset) const { return _readData + offset; }

	byte *mutablePtr(uint32 offset) const {
		assert(_writeData);
		return _writeData + offset;
	}

	uint8 byteAt(uint32 offset) const { return *ptr(offset); }
	uint16 wordAt(uint32 offset) const { return READ_LE_UINT16(ptr(offset)); }

private:
	const byte *_readData;
	byte *_writeData;
	uint32 _size;
};

} // namespace

Program::Program(Common::ReadStream &file, uint16 id)
	: _code(kDosResourceSegmentSize, byte(0)), _codeSize(0), _exits(0), _exitsCount(0) {
	uint16 length = file.readUint16LE(); // for this length
	if (length > 25000)
		error("too large a program (%d)", length);

	_codeSize = length;

	ProgramCodeSegment code(base(), kDosResourceSegmentSize);
	file.read(code.mutablePtr(2), length - 2);
	Resources::descramble(code.mutablePtr(2), length - 2);

	file.read(_footer, 0x10);
	snprintf(_debugInfo, 50, "block %d", id);
}

void Program::loadActors(Interpreter *in) {
	const ProgramFooter footer(_footer);
	uint16 nactors = footer.actorsCount();
	debugC(3, kDebugLevelFiles, "loading %d actors from the program file", nactors);
	uint16 actors = footer.actorsOffset();
	for (int i = 0; i < nactors; ++i) {
		Actor *ac = new Actor(CodePointer(actors, in));
		ac->setId(uint16(i + 1)); // DOS uses 1-based ids
		_actors.push_back(ac);
		actors += Actor::Size;
	}
}

Program::~Program() {
	// _actors are cleaned up by the block Interpreter's destructor — see Interpreter::~Interpreter.
	clearExits();
}

uint16 Program::begin() {
	return entryPointOffset();
}

uint16 Program::entryPointOffset() {
	return ProgramFooter(_footer).entryPointOffset();
}

byte *Program::localVariable(uint16 offset) {
	return ProgramCodeSegment(base(), kDosResourceSegmentSize).mutablePtr(offset);
}

uint16 Program::roomHandler(uint16 room) {
	const ProgramCodeSegment code(base(), _codeSize);
	uint32 indexOffset = 2;

	uint16 r;
	while (code.contains(indexOffset, 2) && (r = code.wordAt(indexOffset)) != 0xffff) {
		if (!code.contains(indexOffset + 2, 2))
			return 0;

		const uint16 handler = code.wordAt(indexOffset + 2);
		if (r == room) {
			return handler;
		}
		indexOffset += 4;
	}

	return 0;
}

SpriteInfo Program::getSpriteInfo(uint16 index) const {
	const ProgramFooter footer(_footer);
	const ProgramCodeSegment code(base(), _codeSize);
	const uint16 spritemapOffset = footer.spriteMapOffset();
	const byte *spritemap = code.ptr(spritemapOffset);

	// Bound check matching MainDat::getSpriteInfo. The footer doesn't
	// store a per-block sprite count, so derive the upper bound from
	// the buffer size: at most (codeSize - spritemapOffset) /
	// SpriteInfo::kSpriteMapRecordSize. Without this an out-of-range
	// id (e.g. uninitialised script field, or main vs block id mismatch)
	// runs straight off the end of _code.
	const uint16 maxEntries = (spritemapOffset < _codeSize)
								  ? (_codeSize - spritemapOffset) / SpriteInfo::kSpriteMapRecordSize
								  : 0;
	if (index >= maxEntries) {
		warning("Program::getSpriteInfo: index 0x%04x out of range "
				"(spritemap@0x%04x, codeSize=0x%04x, max=%u) — returning empty",
				(uint)index, (uint)spritemapOffset, (uint)_codeSize, (uint)maxEntries);
		return SpriteInfo();
	}

	return SpriteInfo(spritemap, index);
}

void Program::clearExits() {
	if (_exits) {
		for (int i = 0; i < _exitsCount; i++)
			delete _exits[i];
		delete[] _exits;
		_exits = 0;
	}
}

void Program::loadExits(Interpreter *in) {
	const ProgramFooter footer(_footer);
	uint16 nexits = footer.exitsCount();
	debugC(3, kDebugLevelFiles, "loading %d exits from the program file", nexits);
	uint16 exits = footer.exitsOffset();

	clearExits();

	_exits = new Exit *[nexits];

	for (int i = 0; i < nexits; ++i) {
		_exits[i] = new Exit(CodePointer(exits, in), uint16(i + 1));
		exits += Exit::Size;
	}

	_exitsCount = nexits;
}

Exit *Program::getExit(uint16 index) const {
	if (!_exits || index == 0 || index > _exitsCount)
		return nullptr;
	return _exits[index - 1];
}

bool Program::getExitRecordField(uint16 index, uint8 off, uint8 size, uint16 &value) const {
	value = 0;
	if (index == 0 || index > _exitsCount || size == 0 || size > 2 || off + size > Exit::Size)
		return false;

	const ProgramFooter footer(_footer);
	const ProgramCodeSegment code(base(), _codeSize);
	const uint16 exits = footer.exitsOffset();
	const uint32 fieldOffset = uint32(exits) + uint32(index - 1) * Exit::Size + off;
	if (fieldOffset + size > _codeSize)
		return false;

	value = size == 1 ? code.byteAt(fieldOffset) : code.wordAt(fieldOffset);
	return true;
}

bool Program::getExitRoomWord(uint16 index, uint16 &room) const {
	if (index == 0)
		return false;

	const ProgramFooter footer(_footer);
	const ProgramCodeSegment code(base(), kDosResourceSegmentSize);
	const uint16 exits = footer.exitsOffset();
	const uint16 off = uint16(exits + uint16(index - 1) * Exit::Size);
	if (off > kDosResourceSegmentSize - 2)
		return false;

	room = code.wordAt(off);
	return true;
}

Common::List<Exit *> Program::exitsForRoom(uint16 room) const {
	Common::List<Exit *> room_exits;
	for (int i = 0; i < _exitsCount; i++)
		if (_exits[i]->room() == room)
			room_exits.push_back(_exits[i]);

	return room_exits;
}

Actor *Program::actor(uint16 index) const {
	Common::List<Actor *>::const_iterator it = _actors.begin();

	while (index) {
		if (it == _actors.end())
			return nullptr;
		it++;
		index--;
	}

	if (it == _actors.end())
		return nullptr;
	return *it;
}

} // End of namespace Interspective
