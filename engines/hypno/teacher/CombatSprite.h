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

#ifndef HYPNO_TEACHER_COMBATSPRITE_H
#define HYPNO_TEACHER_COMBATSPRITE_H

#include "hypno/teacher/Parser.h"
#include "common/array.h"
#include "common/hashmap.h"

namespace Hypno {

// SpriteDataEntry - timed sprite spawn entry
// Original: SpriteDataEntry (CombatSprite.cpp)
// At frame `frame`, spawn target at index `targetIndex`
struct SpriteDataEntry {
	int frame;
	int targetIndex;
	SpriteDataEntry() : frame(0), targetIndex(0) {}
	SpriteDataEntry(int f, int t) : frame(f), targetIndex(t) {}
};

// SpriteSequence - a sequence of timed spawn entries for one sprite ID
typedef Common::Array<SpriteDataEntry> SpriteSequence;

// ============================================================================
// CombatSprite - Manages combat sprite definitions and timed spawning
// Original: CombatSprite (CombatSprite.h, size 0x98)
//
// The original uses a complex hash table of linked lists for sprite data.
// We simplify to HashMap<spriteId, SpriteSequence> + current playback state.
// ============================================================================
class CombatSprite : public Parser {
public:
	CombatSprite();
	~CombatSprite() override;

	int lblParse(const Common::String &line) override;
	int ProcessFrame(int frame);
	int PlayById(int spriteId);

private:
	// Map from sprite ID to sequence of timed entries
	Common::HashMap<int, SpriteSequence> _spriteData;

	// Current playback state
	int _currentSpriteId;
	SpriteSequence *_currentSequence;
	uint _currentIndex;

	// Parsing state
	int _parsingSpriteId;
	SpriteSequence _parsingSequence;

	void finishCurrentSprite();
	void parseSpriteData(const Common::String &data);
};

} // End of namespace Hypno

#endif
