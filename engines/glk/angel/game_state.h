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

#ifndef GLK_ANGEL_GAME_STATE_H
#define GLK_ANGEL_GAME_STATE_H

#include "glk/angel/types.h"
#include "glk/angel/game_data.h"

namespace Glk {
namespace Angel {

/**
 * Holds all mutable game state for the AngelSoft game system.
 * Corresponds to the global variables declared in TABLEHAN, IOHANDLE,
 * COMMAREA, and LISTENER units of the original Pascal source.
 */
class GameState {
public:
	// ---- Core world state (from TABLEHAN) ----
	GameData *_data;                             // Game data (tables/vocab/message)

	AccessSet _capabilities;                     // Rights of use & entry
	ObjSet _possessions;                         // Player's possessions
	ObjSet _wearing;                             // Player's clothing
	ObjSet _concealed;                           // Objects in boxes / under things
	ObjSet _buried;                              // Objects buried under things
	ObjSet _veiled;                              // Concealed + Buried
	ObjSet _boxed;                               // In a box, open or closed
	ObjSet _structures;                          // Buried + things on supports
	ObjSet _fixtures;                            // Immovable objects
	ObjSet _lightSources;                        // Lit lamps (from COMMAREA)

	int _location;                               // Player's current location (LocRef)
	MotionSpec _direction;                       // Player's current direction
	int _moveNumber;                             // Number of moves so far
	int _nbrPossessions;                         // Cardinality of Possessions

	// ---- Vocabulary state ----
	Common::String _vocabWord;                   // Extracted vocab word
	DsplType _vwDisplay;                         // Display type of vocabWord
	int _asgV;                                   // Current vocabulary word index
	int _putOnVerb;
	int _putInVerb;
	int _tkOutVerb;
	int _tkOffVerb;
	int _takeVerb;
	bool _found;                                 // TRUE after successful SearchV
	bool _partial;                               // TRUE if partial match in SearchV
	VSet _irritants;                             // Cuss words

	// ---- Determiner (parser result) ----
	Determiner _ex;                              // Former values
	Determiner _cur;                             // Current values

	// ---- Time & events ----
	TimeRecord _clock;
	TimeRecord _ntgrRegisters;                   // Event registers X0..X9
	FogList _fogRoute;

	// ---- Tracking state ----
	PersonSet _friends;                          // People seen (1 session)
	LocSet _carDoors;
	LocSet _trail;                               // Places visited
	VehicleSet _vTrail;                          // Vehicles described
	LocSet _vclInterior;                         // Vehicle "Inside" locations (from COMMAREA)
	LocSet _vStops;                              // Car stops (from COMMAREA)

	bool _touring;
	bool _civilianTime;
	bool _completeGame;
	int _tourPoint;                              // LocRef
	bool _stillPlaying;
	int _robotAddr;

	// ---- Communication record state ----
	int _prvLocation;
	int _pprvLocation;
	int _vLocation;                              // Location of driven vehicle
	MotionSpec _prvDirection;
	bool _gotHim;                                // Pursuer has caught player
	int _nbrOffenses;                            // Curse counter
	int _lastPerson;                             // PersonRef
	int _pursuer;                                // PersonRef
	bool _atTheHelm;                             // Player in vehicle, door closed

	// Display flags
	bool _dspTime;
	bool _dspDay;
	bool _dspMove;
	bool _dspScore;
	int _probPickUp;                             // ProbRange

	// ---- COMMAREA parser state ----
	VSet _placeWords;
	VSet _vclWords;
	VSet _objWords;
	VSet _personWords;
	VSet _pfxWords;
	int _dayWords[kNumDays];
	int _otherPerson;                            // PersonRef
	int _placeNamed;                             // LocRef
	int _thing;                                  // ObjRef
	int _cab;                                    // VehicleRef
	int _lastMove;                               // Move# for last location change
	double _seed;                                // Random seed
	VWordSet _codeSet;                           // VWords from current input
	VSet _keyWords;                              // Keywords from current input
	ThingToDo _whatNext;
	int _verb;                                   // VWordIndex
	ThingToDo _thisAction[kNumVWords];           // Verb → Action mapping
	VWordSet _cueWords;                          // Prepositions
	int _newArrival;                             // PersonRef
	Determiner _curBase;                         // Copy prepared by parser
	PropSet _active;                             // Union of all props in scope

	// ---- Suggestion ----
	ASuggestion _suggestion;

	// ---- LISTENER local state ----
	int _thingBase;                              // ObjRef
	int _actualPerson;                           // PersonRef
	bool _heCursed;
	int _procAddr;
	ObjSet _inObj;
	PersonSet _inPerson;
	LocSet _inLoc;
	VehicleSet _inVcl;
	PropSet _inVerb;
	OtherSet _inOther;
	DaySet _inDay;

	// ---- Message VM state ----
	bool _eom;                                   // End of message
	bool _eoCom;                                 // End of command
	bool _aQuestion;                             // "?" in input
	int _msgCursor;                              // Position within current chunk
	int _msgPos;                                 // Position within current message
	int _msgBase;                                // Seek addr of current message
	int _msgLength;                              // Length of current message
	int _msgLoc;                                 // Msg location for seeking
	int _msgNext;                                // Next available location
	bool _printSw;                               // Printer switch
	Chunk _vmCurRecord;                          // Current VM record

	// Output queue
	char _msgQ[kDepth + 1];                      // Output queue
	int _q;                                      // Current queue depth

	// VM result
	bool _tfIndicator;                           // TRUE/FALSE result of test

	GameState();

	/** Initialize state from loaded game data */
	void initFromData(GameData *data);

	/** Reset to initial state (for restart) */
	void reset();

	/** Recompute derived sets (concealed, buried, etc.) */
	void recomputeSets();

	// ---- Convenience accessors ----
	Place &map(int loc) { return _data->_map[loc]; }
	const Place &map(int loc) const { return _data->_map[loc]; }
	Object &prop(int obj) { return _data->_props[obj]; }
	const Object &prop(int obj) const { return _data->_props[obj]; }
	Person &cast(int p) { return _data->_cast[p]; }
	const Person &cast(int p) const { return _data->_cast[p]; }
	Vehicle &fleet(int v) { return _data->_fleet[v]; }
	const Vehicle &fleet(int v) const { return _data->_fleet[v]; }
};

} // End of namespace Angel
} // End of namespace Glk

#endif
