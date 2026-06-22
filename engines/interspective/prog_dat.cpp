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

#include "interspective/prog_dat.h"

#include "common/file.h"
#include "common/span.h"
#include "common/util.h"

#include "interspective/innocent.h"
#include "interspective/main_dat.h"
#include "interspective/program.h"
#include "interspective/resources.h"

using namespace Common;

namespace Interspective {

ProgDat::ProgDat(Resources *res) : Datafile(res) {}

const char *ProgDat::filename() const {
	return Engine::instance().progDatFilename().c_str();
}

void ProgDat::load() {
	Common::ScopedPtr<File> file(new File);

	if (!file->open(filename()))
		error("could not open %s", filename());

	readFile(*file);
	_file = Common::SharedPtr<SeekableReadStream>(file.release());
}

void ProgDat::readFile(Common::SeekableReadStream &stream) {
	// actually reads just header
	uint16 total_entries = _resources->mainDat()->progEntriesCount0();
	total_entries += _resources->mainDat()->progEntriesCount1();

	_data.resize(total_entries * 4);
	(void)stream.read(_data.data(), _data.size());
}

Program *ProgDat::getScript(uint16 id) {
	if (!id)
		return 0;

	const uint32 entryOffset = uint32(id - 1) * 4;
	if (entryOffset + 4 > _data.size())
		error("ProgDat::getScript: script id %u outside directory", id);

	const uint32 offset = Common::Span<const byte>(_data.data(), _data.size()).getUint32LEAt(entryOffset);
	_file.get()->seek(offset);

	return new Program(*_file, id);
}

} // End of namespace Interspective
