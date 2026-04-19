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

#include "neuromancer/neuro_vm.h"

#include "neuromancer/detection.h"
#include "neuromancer/level_handlers.h"
#include "neuromancer/neuromancer.h"

#include "common/debug.h"

namespace Neuromancer {

NeuroVM::NeuroVM(NeuromancerEngine *engine)
	: _engine(engine), _activeThread(3) {
	reset();
}

NeuroVM::~NeuroVM() = default;

void NeuroVM::reset() {
	for (int i = 0; i < 4; i++) {
		_threads[i].nextOpAddr = 0;
		_threads[i].var1 = 0;
		_threads[i].var2 = 0;
		_threads[i].flag = 0;
		_threads[i].mark = 0xFF;
	}
	_activeThread = 3;
}

void NeuroVM::tick() {
	// Stub. The full opcode table (0x00-0x18) lives in the Reuromancer
	// neuro_vm() function; opcodes 0x16 (exec) and level init/update/deinit
	// call-outs are the only ones that need LevelHandlers dispatch. The
	// other opcodes are pure data manipulation on VmThread and game state,
	// portable as-is.
	debugC(3, kDebugScript, "NeuroVM::tick (stub)");
}

void NeuroVM::runLevelPhase(VmPhase phase) {
	uint8 level = _engine->currentLevel();
	debugC(1, kDebugLevel, "NeuroVM::runLevelPhase level=%u phase=%d", level, (int)phase);

	LevelHandlers *handlers = _engine->levelHandlers();
	if (!handlers)
		return;

	switch (phase) {
	case kPhaseInit:   handlers->init(level, _engine);   break;
	case kPhaseUpdate: handlers->update(level, _engine); break;
	case kPhaseDeinit: handlers->deinit(level, _engine); break;
	}
}

void NeuroVM::execTarget(uint16 bihOffset) {
	uint8 level = _engine->currentLevel();
	debugC(1, kDebugLevel, "NeuroVM::execTarget level=%u offset=0x%04X", level, bihOffset);

	LevelHandlers *handlers = _engine->levelHandlers();
	if (handlers)
		handlers->execTarget(level, bihOffset, _engine);
}

} // End of namespace Neuromancer
