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

#ifndef INTERSPECTIVE_PROG_DAT_H
#define INTERSPECTIVE_PROG_DAT_H

#include "common/ptr.h"
#include "common/stream.h"

#include "interspective/datafile.h"

namespace Interspective {

class Resources;
class Program;

class ProgDat : public Datafile {
public:
	ProgDat(Resources *resources);
	~ProgDat();

	void load();

	// Resolved by the engine from the selected language: "iuc_prog.dat" for the
	// single-language release, or IUC_PROG.<ext> for the multilingual CD.
	const char *filename() const;
	void readFile(Common::SeekableReadStream &stream);

	Program *getScript(uint16 id);

private:
	byte *_data;
	Common::SharedPtr<Common::SeekableReadStream> _file;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_PROG_DAT_H
