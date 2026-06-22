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

#ifndef INTERSPECTIVE_PROGRAM_H
#define INTERSPECTIVE_PROGRAM_H

#include "common/array.h"
#include "common/list.h"
#include "common/ptr.h"
#include "common/span.h"
#include "common/stream.h"

#include "interspective/debug.h"
#include "interspective/exit.h"
#include "interspective/resources.h"

namespace Interspective {
//

class Actor;

class Program : public StaticInspectable {
	DEBUG_INFO
public:
	Program(Common::ReadStream &file, uint16 id);
	~Program();

	uint16 begin();
	uint16 roomHandler(uint16 room);
	uint16 codeSize() const { return _codeSize; }
	Common::Span<byte> mutableCodeImage();
	Common::Span<const byte> codeImage() const;
	bool replaceCodeImage(const Common::Array<byte> &data);

	SpriteInfo getSpriteInfo(uint16 index) const;

	void loadActors(Interpreter *i);
	void loadExits(Interpreter *i);

	// DOS GetExitOffset uses 1-based script ids (DEC AX before scaling).
	Exit *getExit(uint16 index) const;
	bool getExitRecordField(uint16 index, uint8 off, uint8 size, uint16 &value) const;
	bool getExitRoomWord(uint16 index, uint16 &room) const;
	uint16 exitsCount() const { return uint16(_exits.size()); }
	Common::List<Exit *> exitsForRoom(uint16 room) const;

	Actor *actor(uint16 index) const;
	uint16 actorsCount() const { return uint16(_actors.size()); }

private:
	Program() { /* can only be created from a file */ }

	void clearExits();

	uint16 entryPointOffset();
	Common::Span<byte> mutableDosSegment();
	Common::Span<const byte> dosSegment() const;

	Common::Array<byte> _code;
	uint16 _codeSize;
	byte _footer[0x10];
	Common::List<Actor *> _actors;
	Common::Array<Common::ScopedPtr<Exit> > _exits;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_PROGRAM_H
