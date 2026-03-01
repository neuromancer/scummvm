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

#ifndef HYPNO_TEACHER_TARGET_H
#define HYPNO_TEACHER_TARGET_H

#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Sample.h"
#include "common/array.h"
#include "common/hashmap.h"

namespace Hypno {

// IntPair - simple pair of ints (matches original Range/IntPair)
struct IntPair {
	int start;
	int end;
	IntPair() : start(0), end(0) {}
};

// ============================================================================
// Target - Individual alien/enemy target
// Original: Target (Target.h, size 0x158)
// Extends Sprite with combat-specific fields
// ============================================================================
class Target : public Sprite {
public:
	int active;                    // 0=inactive, 1=active, 3=hit
	int targetFlags;               // bit 0: use hit offset position
	Common::String animFilename;   // animation filename from INIT
	Common::String identifier;     // identifier string from 'I' label
	int id;                        // target index
	IntPair animRange;             // animation frame range (start/end state indices)
	IntPair hitRange;              // hit animation frame range
	IntPair timeRange;             // palette range for collision detection
	IntPair progressRange;         // progress tracking {current, max}
	IntPair scoreWeight;           // score index (start) and weight (end)
	IntPair hitMissPoints;         // hit points (start) and miss points (end)
	IntPair combatBonus;           // combatBonus1 value
	int combatBonus2;              // added to combat engine m_combatBonus2
	Common::Array<int> hotspotFrames; // hotspot frame numbers (original: HotspotListData)
	int hotspotIndex;              // current position in hotspotFrames
	Sample *stopSound;             // sound stopped on hit
	Sample *progressSound;         // sound played on progress
	Sample *hitSound;              // sound played on hit
	Sample *sound3;                // sound 3
	IntPair animParam;             // from 'A' label (initial position)
	IntPair hitOffset;             // hit offset (from 'O' label)
	int pendingAction;             // pending action (0=none, 1=miss, 3=hit)
	int field_0x154;

	// State counter for range building during parse
	int _rangeCounter;

	Target();
	~Target() override;
	int lblParse(const Common::String &line) override;
	void onProcessStart() override;
	void onProcessEnd() override;

	void Spawn();
	void Activate();
	void Deactivate();
	int CheckTimeInRange();
	int CheckTimeInRangeParam(int px, int py);
	int AdvanceHotspot();
	void UpdateProgress(int delta);
	int Update();
	void Init(const Common::String &line);
	void ParseSound(const Common::String &line);
};

// ============================================================================
// TargetList - Manager for all targets
// Original: TargetList (Target.h, extends Parser)
// ============================================================================
class TargetList : public Parser {
public:
	int count;
	Target *targets[70];
	Target *currentTarget;
	int field_0x1a8;
	int field_0x1ac;

	// Hash table for active target lookup by ID
	Common::HashMap<int, Target *> activeTargets;

	Sample *defaultStopSound;
	Sample *defaultProgressSound;
	Sample *defaultHitSound;
	Sample *defaultSound;

	TargetList();
	~TargetList() override;
	int lblParse(const Common::String &line) override;
	void onProcessEnd() override;

	Target *ProcessTargets();
};

} // End of namespace Hypno

#endif
