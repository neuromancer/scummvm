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

#include "glk/angel/utilities.h"
#include "glk/angel/angel.h"
#include "common/debug.h"

namespace Glk {
namespace Angel {

Utilities::Utilities(Angel *engine, GameData *data, GameState *state)
	: _engine(engine), _data(data), _state(state) {
}

bool Utilities::local(int loc, MotionSpec &dir) const {
	if (loc == _state->_location)
		return true;

	const Place &here = _state->map(_state->_location);
	for (int d = 0; d < kNumDirections; d++) {
		if (here.nextPlace[d] == loc) {
			dir = (MotionSpec)d;
			return true;
		}
	}

	return false;
}

bool Utilities::deadEnd() const {
	const Place &here = _state->map(_state->_location);
	int dir = (int)_state->_direction;
	if (dir < 0 || dir >= kNumDirections)
		return true;
	return here.nextPlace[dir] == kNowhere;
}

void Utilities::recall(KindOfWord what, int which) {
	_state->_suggestion.kind = what;
	_state->_suggestion.ref = which;
	_state->_suggestion.m = _state->_moveNumber;
}

bool Utilities::probable(int p) {
	// Simple linear congruential generator matching original Pascal RANDOM
	_state->_seed = _state->_seed * 3.14159265 + 0.13579;
	_state->_seed -= (int)_state->_seed;  // keep fractional part
	int roll = (int)(_state->_seed * 100.0);
	return roll < p;
}

int Utilities::nextStop(int vehicleRef) const {
	if (vehicleRef < 1 || vehicleRef > _data->_nbrVehicles)
		return kNowhere;

	const Vehicle &vcl = _data->_fleet[vehicleRef];

	// For buses/excursions, find the next location on the route
	// after the current stopped position
	if (vcl.vclType == kABus || vcl.vclType == kAnExcursion) {
		bool foundCurrent = false;
		for (int loc = 1; loc <= kMaxNbrLocations; loc++) {
			if (vcl.route.has(loc)) {
				if (foundCurrent)
					return loc;
				if (loc == vcl.stopped)
					foundCurrent = true;
			}
		}
		// Wrap around — return first stop on route
		for (int loc = 1; loc <= kMaxNbrLocations; loc++) {
			if (vcl.route.has(loc))
				return loc;
		}
	}

	return kNowhere;
}

void Utilities::moveHim(int personRef, int locRef) {
	if (personRef < 1 || personRef > _data->_castSize)
		return;

	Person &p = _data->_cast[personRef];

	// Remove from old location's people set
	if (p.located >= 1 && p.located <= _data->_nbrLocations)
		_state->map(p.located).people.unset(personRef);

	// Place in new location
	p.located = locRef;
	if (locRef >= 1 && locRef <= _data->_nbrLocations)
		_state->map(locRef).people.set(personRef);
}

void Utilities::animate(int personRef) {
	if (personRef < 1 || personRef > _data->_castSize)
		return;

	Person &p = _data->_cast[personRef];

	// Skip resting characters
	if (p.resting || p.unseen)
		return;

	// Random direction change
	if (p.change > 0 && probable(p.change)) {
		MotionSpec newDir = (MotionSpec)((int)(_state->_seed * kNumDirections));
		p.direction = newDir;
	}

	// Random item drop
	if (p.dropping > 0 && probable(p.dropping)) {
		int item = cheapest(personRef);
		if (item > 0) {
			p.carrying.unset(item);
			if (p.located >= 1 && p.located <= _data->_nbrLocations)
				_state->map(p.located).objects.set(item);
		}
	}
}

Common::String Utilities::article() const {
	Common::String name = _state->_vocabWord;
	if (name.empty())
		return name;

	// Check if first letter is a vowel
	char first = name[0];
	if (first >= 'A' && first <= 'Z')
		first = first - 'A' + 'a';

	if (first == 'a' || first == 'e' || first == 'i' || first == 'o' || first == 'u')
		return Common::String("an ") + name;
	else
		return Common::String("a ") + name;
}

Common::String Utilities::putVWord() const {
	Common::String name = _state->_vocabWord;
	if (name.empty())
		return name;

	switch (_state->_vwDisplay) {
	case kAllCaps:
		// All uppercase — keep as-is (vocab stored uppercase)
		break;
	case kInitCap:
		// Capitalize first letter, rest lowercase
		for (uint i = 1; i < name.size(); i++) {
			if (name[i] >= 'A' && name[i] <= 'Z')
				name.setChar(name[i] - 'A' + 'a', i);
		}
		break;
	case kNoCaps:
		// All lowercase
		for (uint i = 0; i < name.size(); i++) {
			if (name[i] >= 'A' && name[i] <= 'Z')
				name.setChar(name[i] - 'A' + 'a', i);
		}
		break;
	}

	return name;
}

void Utilities::changeLocation(int locRef) {
	if (locRef < 1 || locRef > _data->_nbrLocations) {
		debugC(1, 0, "Angel: changeLocation(%d) — invalid location", locRef);
		return;
	}

	const Place &dest = _state->map(locRef);

	// Check access locks
	if (dest.accessLock > 0 && !_state->_capabilities.has(dest.accessLock)) {
		debugC(2, 0, "Angel: changeLocation(%d) — locked (access %d)", locRef, dest.accessLock);
		return;
	}

	// Check required object
	if (dest.mustHave > 0 && !_state->_possessions.has(dest.mustHave)) {
		debugC(2, 0, "Angel: changeLocation(%d) — missing required object %d", locRef, dest.mustHave);
		return;
	}

	// Update previous locations
	_state->_pprvLocation = _state->_prvLocation;
	_state->_prvLocation = _state->_location;
	_state->_prvDirection = _state->_direction;

	// Move player
	_state->_location = locRef;
	_state->_lastMove = _state->_moveNumber;

	// Mark trail
	_state->_trail.set(locRef);
}

bool Utilities::can(int prop) const {
	int obj = _state->_thing;
	if (obj < 1 || obj > _data->_nbrObjects)
		return false;
	return _data->_props[obj].properties.has(prop);
}

int Utilities::anybody(const PersonSet &set) const {
	for (int p = 1; p <= _data->_castSize; p++) {
		if (set.has(p))
			return p;
	}
	return 0;
}

int Utilities::cheapest(int personRef) const {
	if (personRef < 1 || personRef > _data->_castSize)
		return 0;

	const Person &p = _data->_cast[personRef];
	int cheapObj = 0;
	int cheapVal = 999;

	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (p.carrying.has(obj)) {
			int val = _data->_props[obj].value;
			if (val < cheapVal) {
				cheapVal = val;
				cheapObj = obj;
			}
		}
	}

	return cheapObj;
}

void Utilities::addContents(ObjSet &objSet) const {
	ObjSet additions;

	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (objSet.has(obj)) {
			const Object &container = _data->_props[obj];
			if (container.kindOfThing == kABox || container.kindOfThing == kASupport) {
				for (int inner = 1; inner <= _data->_nbrObjects; inner++) {
					if (_data->_props[inner].inOrOn == obj)
						additions.set(inner);
				}
			}
		}
	}

	objSet |= additions;
}

} // End of namespace Angel
} // End of namespace Glk
