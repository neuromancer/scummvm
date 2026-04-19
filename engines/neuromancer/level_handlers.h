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

#ifndef NEUROMANCER_LEVEL_HANDLERS_H
#define NEUROMANCER_LEVEL_HANDLERS_H

#include "common/scummsys.h"

namespace Neuromancer {

class NeuromancerEngine;

// Per-level init / update / deinit / exec C++ handlers that replace the
// compiled 8086 code embedded in the original DOS BIH files.
//
// The original DOS build executes three "init_obj_code" entry points per
// level (init / update / deinit) as native 16-bit x86 through an embedded
// CPU emulator (see Reuromancer/NeuromancerWin64/neuro86.c). This class
// replaces that whole mechanism with a lookup table of C++ functions keyed
// by level number, which is the portable approach (option A of the porting
// evaluation).
//
// Strategy for populating entries:
//   1. Run the dev-only BIH disassembler against each BIH file
//      (planned: devtools/neuromancer/bih_disasm.py).
//   2. For each non-trivial level, hand-translate the disassembly into a
//      level-specific method (e.g. level1Init, level1Update, ...).
//   3. Register the method in the dispatch table below.
//
// Levels whose init/update/deinit are trivial (many BIH files are only
// ~45 bytes decompressed) can share a single empty handler.
class LevelHandlers {
public:
	// Dispatch. Returns true if a real handler ran, false if only the default
	// (no-op) stub fired. This lets callers log/trace unimplemented levels.
	bool init(uint8 level, NeuromancerEngine *engine);
	bool update(uint8 level, NeuromancerEngine *engine);
	bool deinit(uint8 level, NeuromancerEngine *engine);

	// neuro-VM opcode 0x16 ("exec") calls a per-level entry point referenced
	// by a BIH-relative offset. These were also compiled 8086 code in DOS.
	bool execTarget(uint8 level, uint16 bihOffset, NeuromancerEngine *engine);

private:
	// Example handler, demonstrating the pattern. Real levels will grow their
	// own methods, each a direct C++ translation of the corresponding DOS
	// 8086 entry point.
	void level1Init(NeuromancerEngine *engine);
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_LEVEL_HANDLERS_H
