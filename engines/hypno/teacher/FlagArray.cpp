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

#include "hypno/teacher/FlagArray.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/archive.h"
#include "engines/util.h"

namespace Hypno {

FlagArray::FlagArray(const Common::String &filename, int maxStates) : _filename(filename), _maxStates(maxStates) {
	_flags.resize(maxStates);
	load();
}

FlagArray::~FlagArray() {
	save();
}

void FlagArray::load() {
	Common::InSaveFile *file = g_system->getSavefileManager()->openForLoading(_filename);
	if (file) {
		for (int i = 0; i < _maxStates; ++i) {
			_flags[i] = file->readUint32LE();
		}
		delete file;
	} else {
		// Fallback to SearchMan (for initial question.sav in DATA bucket)
		Common::SeekableReadStream *stream = SearchMan.createReadStreamForMember(Common::Path(_filename));
		if (stream) {
			for (int i = 0; i < _maxStates; ++i) {
				if (!stream->eos())
					_flags[i] = stream->readUint32LE();
				else
					_flags[i] = 0;
			}
			delete stream;
		}
	}
}

void FlagArray::save() {
	Common::OutSaveFile *file = g_system->getSavefileManager()->openForSaving(_filename);
	if (!file) return;
	for (int i = 0; i < _maxStates; ++i) {
		file->writeUint32LE(_flags[i]);
	}
	delete file;
}

void FlagArray::clearAllFlags() {
	for (int i = 0; i < _maxStates; ++i)
		_flags[i] = 0;
}

uint32 FlagArray::getFlag(int index, uint32 mask) {
	if (index >= 0 && index < _maxStates)
		return _flags[index] & mask;
	return 0;
}

void FlagArray::setFlag(int index, uint32 mask) {
	if (index >= 0 && index < _maxStates)
		_flags[index] |= mask;
}

} // End of namespace Hypno
