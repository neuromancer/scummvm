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

#ifndef GLK_ANGEL_UTILITIES_H
#define GLK_ANGEL_UTILITIES_H

#include "glk/angel/types.h"
#include "glk/angel/game_data.h"
#include "glk/angel/game_state.h"

namespace Glk {
namespace Angel {

class Angel;

/**
 * Utility procedures for the AngelSoft engine.
 *
 * Corresponds to the UTILITIES unit in the original Pascal source.
 * These are stable helper functions called from the PLAY overlay
 * and the bytecode VM.
 */
class Utilities {
public:
	Utilities(Angel *engine, GameData *data, GameState *state);

	/**
	 * Check whether location Loc can be reached in a single move from
	 * the current location. If reachable, sets dir to the direction
	 * connecting them and returns true.
	 */
	bool local(int loc, MotionSpec &dir) const;

	/**
	 * Return true if there is no path leading from the current location
	 * in the current direction.
	 */
	bool deadEnd() const;

	/**
	 * "Remember" the most recently referenced object, place, person, etc.
	 * Updates the suggestion record.
	 */
	void recall(KindOfWord what, int which);

	/**
	 * Return true if the next pseudo-random number (0..100) is less than p.
	 */
	bool probable(int p);

	/**
	 * Return the LocRef of the next stop in the route of vehicle v.
	 */
	int nextStop(int vehicleRef) const;

	/**
	 * Move person p to location x.
	 */
	void moveHim(int personRef, int locRef);

	/**
	 * Make all time-dependent changes to the specified person.
	 * Called once per move for every character in the cast.
	 */
	void animate(int personRef);

	/**
	 * Generate the current object name preceded by "a" or "an".
	 * Assumes the name has been placed in _state->_vocabWord.
	 */
	Common::String article() const;

	/**
	 * Convert the last extracted vocabulary word to printable form
	 * (change letters to appropriate case) and return it.
	 */
	Common::String putVWord() const;

	/**
	 * Change the player's location to the given place if allowed.
	 */
	void changeLocation(int locRef);

	/**
	 * Return true if the given property is set on the current object
	 * (_state->_thing).
	 */
	bool can(int prop) const;

	/**
	 * Select any person from the given PersonSet.
	 */
	int anybody(const PersonSet &set) const;

	/**
	 * Select the cheapest item from person p's possessions.
	 */
	int cheapest(int personRef) const;

	/**
	 * Add the contents of all container objects in the set to the set.
	 */
	void addContents(ObjSet &objSet) const;

private:
	Angel *_engine;
	GameData *_data;
	GameState *_state;
};

} // End of namespace Angel
} // End of namespace Glk

#endif
