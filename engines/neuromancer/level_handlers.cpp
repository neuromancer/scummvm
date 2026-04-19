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

#include "neuromancer/level_handlers.h"

#include "neuromancer/detection.h"
#include "neuromancer/neuromancer.h"

#include "common/debug.h"

namespace Neuromancer {

bool LevelHandlers::init(uint8 level, NeuromancerEngine *engine) {
	switch (level) {
	case 1:
		level1Init(engine);
		return true;
	default:
		debugC(2, kDebugLevel, "LevelHandlers::init: level %u unimplemented", level);
		return false;
	}
}

bool LevelHandlers::update(uint8 level, NeuromancerEngine *engine) {
	(void)engine;
	debugC(3, kDebugLevel, "LevelHandlers::update: level %u unimplemented", level);
	return false;
}

bool LevelHandlers::deinit(uint8 level, NeuromancerEngine *engine) {
	(void)engine;
	debugC(2, kDebugLevel, "LevelHandlers::deinit: level %u unimplemented", level);
	return false;
}

bool LevelHandlers::execTarget(uint8 level, uint16 bihOffset, NeuromancerEngine *engine) {
	(void)engine;
	debugC(1, kDebugLevel, "LevelHandlers::execTarget: level %u offset 0x%04X unimplemented",
	       level, bihOffset);
	return false;
}

// -----------------------------------------------------------------------------
// Level 1 (R1.BIH) -- reference handler.
//
// The DOS build executes the 8086 code at BIH offset
// hdr->initObjCodeOffset[0] + 0xA8E8. Once the BIH disassembler is in place,
// that compiled procedure will be manually translated here.
//
// Typical shape of a translated handler (pseudo):
//   - Zero or set fields on the engine's game-state struct
//   - Register animation callbacks
//   - Invoke high-level services (inventory / dialog / etc.) directly
//     -- no CALL FAR through an emulator callback
// -----------------------------------------------------------------------------

void LevelHandlers::level1Init(NeuromancerEngine *engine) {
	(void)engine;
	debugC(1, kDebugLevel, "LevelHandlers::level1Init: placeholder");
}

} // End of namespace Neuromancer
