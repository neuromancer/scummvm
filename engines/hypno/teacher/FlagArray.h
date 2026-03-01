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

#ifndef HYPNO_TEACHER_FLAGARRAY_H
#define HYPNO_TEACHER_FLAGARRAY_H

#include "common/str.h"
#include "common/array.h"

namespace Hypno {

class FlagArray {
public:
	FlagArray(const Common::String &filename, int maxStates);
	~FlagArray();

	void clearAllFlags();
	uint32 getFlag(int index, uint32 mask);
	void setFlag(int index, uint32 mask);

private:
	Common::String _filename;
	Common::Array<uint32> _flags;
	int _maxStates;

	void load();
	void save();
};

} // End of namespace Hypno

#endif
