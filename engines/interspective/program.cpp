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

#include "common/span.h"
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
	ProgramFooter(const byte *footer) : _footer(footer, 0x10) {}

	uint16 exitsCount() const { return wordAt(kExitsCount); }
	uint16 actorsCount() const { return wordAt(kActorsCount); }
	uint16 exitsOffset() const { return wordAt(kExits); }
	uint16 actorsOffset() const { return wordAt(kActors); }
	uint16 spriteMapOffset() const { return wordAt(kSpriteMap); }
	uint16 entryPointOffset() const { return wordAt(kEntryPointOffset); }

private:
	uint16 wordAt(uint16 offset) const { return _footer.getUint16LEAt(offset); }

	Common::Span<const byte> _footer;
};

class ProgramCodeSegment {
public:
	ProgramCodeSegment(Common::Span<byte> data) : _readData(data.data()), _writeData(data.data()), _size(data.size()) {}
	ProgramCodeSegment(Common::Span<const byte> data) : _readData(data.data()), _writeData(0), _size(data.size()) {}

	bool contains(uint32 offset, uint32 size) const {
		return offset <= _size && size <= _size - offset;
	}

	Common::Span<const byte> span(uint32 offset) const {
		assert(offset <= _size);
		return Common::Span<const byte>(_readData + offset, _size - offset);
	}

	Common::Span<const byte> span(uint32 offset, uint32 size) const {
		assert(contains(offset, size));
		return Common::Span<const byte>(_readData + offset, size);
	}

	Common::Span<byte> mutableSpan(uint32 offset, uint32 size) const {
		assert(_writeData);
		assert(contains(offset, size));
		return Common::Span<byte>(_writeData + offset, size);
	}

	byte *mutablePtr(uint32 offset) const {
		return mutableSpan(offset, _size - offset).data();
	}

	uint8 byteAt(uint32 offset) const { return span(offset, 1).getUint8At(0); }
	uint16 wordAt(uint32 offset) const { return span(offset, 2).getUint16LEAt(0); }

private:
	const byte *_readData;
	byte *_writeData;
	uint32 _size;
};

} // namespace

Program::Program(Common::ReadStream &file, uint16 id)
	: _code(kDosResourceSegmentSize, byte(0)), _codeSize(0) {
	uint16 length = file.readUint16LE(); // for this length
	if (length > 25000)
		error("too large a program (%d)", length);

	_codeSize = length;

	ProgramCodeSegment code(mutableDosSegment());
	Common::Span<byte> payload = code.mutableSpan(2, length - 2);
	file.read(payload.data(), payload.size());
	Resources::descramble(payload.data(), payload.size());

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
}

uint16 Program::begin() {
	return entryPointOffset();
}

uint16 Program::entryPointOffset() {
	return ProgramFooter(_footer).entryPointOffset();
}

Common::Span<byte> Program::mutableCodeImage() {
	return Common::Span<byte>(_code.data(), _codeSize);
}

Common::Span<const byte> Program::codeImage() const {
	return Common::Span<const byte>(_code.data(), _codeSize);
}

bool Program::replaceCodeImage(const Common::Array<byte> &data) {
	if (data.size() != _codeSize)
		return false;
	if (_codeSize != 0)
		memcpy(_code.data(), &data[0], _codeSize);
	return true;
}

Common::Span<byte> Program::mutableDosSegment() {
	return Common::Span<byte>(_code.data(), _code.size());
}

Common::Span<const byte> Program::dosSegment() const {
	return Common::Span<const byte>(_code.data(), _code.size());
}

byte *Program::localVariable(uint16 offset) {
	return ProgramCodeSegment(mutableDosSegment()).mutablePtr(offset);
}

uint16 Program::roomHandler(uint16 room) {
	const ProgramCodeSegment code(codeImage());
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
	const ProgramCodeSegment code(codeImage());
	const uint16 spritemapOffset = footer.spriteMapOffset();

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

	const uint32 recordOffset = uint32(spritemapOffset) + uint32(index) * SpriteInfo::kSpriteMapRecordSize;
	return SpriteInfo(code.span(recordOffset, SpriteInfo::kSpriteMapRecordSize));
}

void Program::clearExits() {
	_exits.clear();
}

void Program::loadExits(Interpreter *in) {
	const ProgramFooter footer(_footer);
	uint16 nexits = footer.exitsCount();
	debugC(3, kDebugLevelFiles, "loading %d exits from the program file", nexits);
	uint16 exits = footer.exitsOffset();

	clearExits();

	for (int i = 0; i < nexits; ++i) {
		_exits.push_back(Common::ScopedPtr<Exit>(new Exit(CodePointer(exits, in), uint16(i + 1))));
		exits += Exit::Size;
	}
}

Exit *Program::getExit(uint16 index) const {
	if (index == 0 || index > exitsCount())
		return nullptr;
	return _exits[index - 1].get();
}

bool Program::getExitRecordField(uint16 index, uint8 off, uint8 size, uint16 &value) const {
	value = 0;
	if (index == 0 || index > exitsCount() || size == 0 || size > 2 || off + size > Exit::Size)
		return false;

	const ProgramFooter footer(_footer);
	const ProgramCodeSegment code(codeImage());
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
	const ProgramCodeSegment code(dosSegment());
	const uint16 exits = footer.exitsOffset();
	const uint16 off = uint16(exits + uint16(index - 1) * Exit::Size);
	if (!code.contains(off, 2))
		return false;

	room = code.wordAt(off);
	return true;
}

Common::List<Exit *> Program::exitsForRoom(uint16 room) const {
	Common::List<Exit *> room_exits;
	for (uint i = 0; i < _exits.size(); i++) {
		Exit *exit = _exits[i].get();
		if (exit->room() == room)
			room_exits.push_back(exit);
	}

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
