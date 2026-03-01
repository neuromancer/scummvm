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

#ifndef HYPNO_TEACHER_SPRITE_H
#define HYPNO_TEACHER_SPRITE_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Animation.h"

namespace Hypno {

// Range struct matching the original Range.h: start/end frame numbers for a sprite state
struct SpriteRange {
	int start;
	int end;
	SpriteRange() : start(1), end(5000) {}
};

class Sprite : public Parser {
public:
	Sprite();
	Sprite(const Common::String &filename);
	~Sprite() override;
	int Do(int x, int y, double scale);

	// State/range management (matching original Sprite API)
	void initStates(int numStates);       // Original: SetState - allocates N ranges
	void setRange(int idx, int s, int e); // Original: SetRange - sets frame range for a state
	void setState2(int newState);         // Original: SetState2 - seeks to state's frame range
	void ensureInit();                    // Lazy-init animation if needed
	void freeAnimation();                 // Original: FreeAnimation - releases animation data
	void stopAnimationSound();            // Original: StopAnimationSound - stops audio without changing visual state

	int lblParse(const Common::String &line) override;

	Common::String _filename;
	int flags;
	int current_state;
	int priority;
	int loc_x, loc_y;
	int num_states;           // Number of states (ranges allocated)
	SpriteRange *ranges;      // Array of frame ranges, one per state
	TeacherAnimation *animation_data;
};

} // End of namespace Hypno

#endif
