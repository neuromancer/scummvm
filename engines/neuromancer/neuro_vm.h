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

#ifndef NEUROMANCER_NEURO_VM_H
#define NEUROMANCER_NEURO_VM_H

#include "common/scummsys.h"

namespace Neuromancer {

class NeuromancerEngine;

// BIH header layout (first 30 bytes of a decompressed BIH resource).
// Preserved for documentation; numeric offsets match the original DOS layout
// as decoded in Reuromancer/NeuromancerWin64/data.h.
struct BihHeader {
	uint16 cbOffset;              // 0xa8e8: callback far ptr (unused in C++ port)
	uint16 cbSegment;             // 0xa8ea
	uint16 ctrlStructAddr;        // 0xa8ec
	uint16 textOffset;            // 0xa8ee: offset of per-line text within BIH body
	uint16 bytecodeArrayOffset[3];// 0xa8f0: per-phase dispatch tables
	uint16 initObjCodeOffset[3];  // 0xa8f6: init/update/deinit entry points (x86 in DOS)
	uint16 unknown[10];           // 0xa8fc
};

// Per-thread script state. The real engine keeps 4 of these in-flight at once.
struct VmThread {
	uint16 nextOpAddr;  // offset within the BIH body of the next opcode to execute
	uint16 var1;
	uint16 var2;
	uint8  flag;
	uint8  mark;
};

enum VmPhase {
	kPhaseInit   = 0,
	kPhaseUpdate = 1,
	kPhaseDeinit = 2
};

// neuro-VM: high-level bytecode interpreter (~20 opcodes). See the opcode
// switch in Reuromancer/NeuromancerWin64/scene_real_world.c:129+ for reference.
//
// This replaces *only* the high-level script dispatch. The per-level init/
// update/deinit hooks that the original DOS build implemented as compiled
// 8086 code are delegated to LevelHandlers (per-level C++ functions).
class NeuroVM {
public:
	explicit NeuroVM(NeuromancerEngine *engine);
	~NeuroVM();

	// Reset all active threads on the current level.
	void reset();

	// Run one VM cycle (up to 4 threads, round-robin).
	void tick();

	// Entry points invoked at level boundaries. These dispatch to
	// LevelHandlers, replacing the original cpu_reset/cpu_run pattern.
	void runLevelPhase(VmPhase phase);

	// opcode 0x16: "exec" -- call a per-level entry point by BIH offset.
	// Delegates to LevelHandlers::execTarget.
	void execTarget(uint16 bihOffset);

private:
	NeuromancerEngine *_engine;
	VmThread _threads[4];
	int _activeThread;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_NEURO_VM_H
