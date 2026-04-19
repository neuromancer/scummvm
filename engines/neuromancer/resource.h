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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#ifndef NEUROMANCER_RESOURCE_H
#define NEUROMANCER_RESOURCE_H

#include "common/array.h"
#include "common/file.h"
#include "common/str.h"

namespace Neuromancer {

// A single entry in NEURO1.DAT / NEURO2.DAT. Offsets and sizes are absolute
// within the owning archive. The `file` field indexes into {NEURO1, NEURO2}.
struct ResourceEntry {
	byte file;          // 0 = NEURO1.DAT, 1 = NEURO2.DAT
	const char *name;   // uppercase, e.g. "R1.BIH"
	uint32 offset;
	uint32 size;
};

// Resource tables are taken verbatim from Reuromancer's reverse-engineered
// offsets (resources_lists.c). These are position-dependent within the
// original DOS distribution and must match the retail data.
extern const ResourceEntry kImhResources[];
extern const ResourceEntry kPicResources[];
extern const ResourceEntry kBihResources[];
extern const ResourceEntry kAnhResources[];
extern const ResourceEntry kTxhResources[];
extern const ResourceEntry kSavegameResource;

class ResourceManager {
public:
	ResourceManager();
	~ResourceManager();

	bool open();
	void close();

	// Load and decompress a resource by name. `dst` must be large enough
	// for the decompressed payload. Returns decompressed length, or 0 on miss.
	// The 32-byte IMH/PIC file preamble is skipped automatically.
	uint32 load(const Common::String &name, byte *dst);

private:
	Common::File _neuro1;
	Common::File _neuro2;
	bool _opened;

	const ResourceEntry *findEntry(const Common::String &name) const;
	Common::File *fileFor(const ResourceEntry *entry);
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_RESOURCE_H
