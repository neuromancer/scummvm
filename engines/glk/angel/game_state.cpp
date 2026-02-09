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

#include "glk/angel/game_state.h"
#include "common/debug.h"

namespace Glk {
namespace Angel {

GameState::GameState() : _data(nullptr), _location(kNowhere), _direction(kNorth),
                         _moveNumber(0), _nbrPossessions(0), _vwDisplay(kNoCaps),
                         _asgV(0), _putOnVerb(0), _putInVerb(0), _tkOutVerb(0),
                         _tkOffVerb(0), _takeVerb(0), _found(false), _partial(false),
                         _touring(false), _civilianTime(true), _completeGame(false),
                         _tourPoint(kNowhere), _stillPlaying(true), _robotAddr(0),
                         _prvLocation(kNowhere), _pprvLocation(kNowhere),
                         _vLocation(kNowhere), _prvDirection(kNorth),
                         _gotHim(false), _nbrOffenses(0), _lastPerson(0),
                         _pursuer(0), _atTheHelm(false),
                         _dspTime(false), _dspDay(false), _dspMove(false),
                         _dspScore(false), _probPickUp(0),
                         _otherPerson(0), _placeNamed(0), _thing(0), _cab(0),
                         _lastMove(0), _seed(0.0), _whatNext(kNothing), _verb(0),
                         _newArrival(0),
                         _thingBase(0), _actualPerson(0), _heCursed(false),
                         _procAddr(0),
                         _eom(false), _eoCom(false), _aQuestion(false),
                         _msgCursor(0), _msgPos(0), _msgBase(0), _msgLength(0),
                         _msgLoc(0), _msgNext(0), _printSw(false), _q(0),
                         _tfIndicator(false) {
	memset(_msgQ, 0, sizeof(_msgQ));
	memset(_dayWords, 0, sizeof(_dayWords));
	memset(_thisAction, 0, sizeof(_thisAction));
}

void GameState::initFromData(GameData *data) {
	_data = data;

	// Copy initial state from tables
	_capabilities = data->_initGeneral.capabilities;
	_possessions = data->_initGeneral.possessions;
	_wearing = data->_initGeneral.wearing;
	_location = data->_initGeneral.location;
	_direction = data->_initGeneral.direction;
	_nbrPossessions = data->_initGeneral.nbrPossessions;
	_fogRoute = data->_initGeneral.fogRoute;
	_robotAddr = data->_initGeneral.robotAddr;
	_civilianTime = data->_initGeneral.civilianTime;
	_completeGame = data->_initGeneral.completeGame;

	// Copy ComRecord
	_prvLocation = data->_initCom.prvLocation;
	_pprvLocation = data->_initCom.pprvLocation;
	_vLocation = data->_initCom.vLocation;
	_prvDirection = data->_initCom.prvDirection;
	_nbrOffenses = data->_initCom.nbrOffenses;
	_lastPerson = data->_initCom.lastPerson;
	_pursuer = data->_initCom.pursuer;
	_gotHim = data->_initCom.gotHim;
	_dspTime = data->_initCom.dspTime;
	_dspDay = data->_initCom.dspDay;
	_dspMove = data->_initCom.dspMove;
	_dspScore = data->_initCom.dspScore;
	_probPickUp = data->_initCom.probPickUp;

	// Copy determiner
	_cur = data->_initDeterminer;
	_ex = _cur;

	// Copy suggestion
	_suggestion = data->_initSuggestion;

	// Copy time
	_clock.day = data->_initTime.day;
	_clock.hour = data->_initTime.hour;
	_clock.minute = data->_initTime.minute;
	_clock.am = data->_initTime.am;
	_clock.tickNumber = data->_initTime.tickNumber;
	// Copy the 5 event registers from IntTimeRecord to full TimeRecord
	for (int i = 0; i < 5; i++)
		_clock.xReg[i] = data->_initTime.xReg[i];
	for (int i = 5; i < 10; i++) {
		_clock.xReg[i].x = 0;
		_clock.xReg[i].proc = 0;
	}

	_stillPlaying = true;
	_moveNumber = 0;

	// Initialize verb → action mapping
	// Standard verb classifications
	for (int i = 0; i < kNumVWords; i++)
		_thisAction[i] = kNothing;

	// Directional words → ATrip
	_thisAction[kVNorth] = kATrip;
	_thisAction[kVSouth] = kATrip;
	_thisAction[kVEast] = kATrip;
	_thisAction[kVWest] = kATrip;
	_thisAction[kVUp] = kATrip;
	_thisAction[kVDown] = kATrip;

	// Action verbs → AnAction
	_thisAction[kVEnter] = kAnAction;
	_thisAction[kVOpen] = kAnAction;
	_thisAction[kVClose] = kAnAction;
	_thisAction[kVExit] = kAnAction;
	_thisAction[kVTake] = kAnAction;
	_thisAction[kVRead] = kAnAction;
	_thisAction[kVThrow] = kAnAction;
	_thisAction[kVDrop] = kAnAction;
	_thisAction[kVWear] = kAnAction;
	_thisAction[kVRemove] = kAnAction;
	_thisAction[kVBreak] = kAnAction;
	_thisAction[kVDestroy] = kARubout;
	_thisAction[kVExamine] = kAnAction;
	_thisAction[kVPutIn] = kAnAction;
	_thisAction[kVPutOn] = kAnAction;
	_thisAction[kVTakeOut] = kAnAction;
	_thisAction[kVTakeOff] = kAnAction;
	_thisAction[kVLock] = kAnAction;
	_thisAction[kVUnlock] = kAnAction;
	_thisAction[kVKill] = kAnAction;
	_thisAction[kVPour] = kAnAction;

	// Query words
	_thisAction[kVWhere] = kAQuery;
	_thisAction[kVHow] = kAQuery;
	_thisAction[kVWhy] = kAQuery;
	_thisAction[kVLook] = kAnAction;  // Look can be both

	// Offer / Trade
	_thisAction[kVTrade] = kAnOffer;
	_thisAction[kVGive] = kAnAction;

	// Ride → special
	_thisAction[kVRide] = kAnAction;
	_thisAction[kVGo] = kAMove;

	// Greet
	_thisAction[kVHello] = kANod;

	// System
	_thisAction[kVQuit] = kGiveUp;
	_thisAction[kVSave] = kSaveGame;
	_thisAction[kVRestore] = kSaveGame;
	_thisAction[kVInventory] = kInventory;
	_thisAction[kVHave] = kInventory;

	// Bluff / Curse
	_thisAction[kVCurse] = kAnAction;
	_thisAction[kVBluff] = kAnAction;

	// Cue words (prepositions)
	_cueWords.clear();
	_cueWords.set(kVTo);
	_cueWords.set(kVWith);
	_cueWords.set(kVOn);
	_cueWords.set(kVFor);
	_cueWords.set(kVBy);
	_cueWords.set(kVIn);

	recomputeSets();
}

void GameState::reset() {
	if (_data)
		initFromData(_data);
}

void GameState::recomputeSets() {
	// Recompute derived object sets from the current world state
	_concealed.clear();
	_buried.clear();
	_boxed.clear();
	_structures.clear();
	_lightSources.clear();
	_vclInterior.clear();
	_vStops.clear();

	for (int i = 1; i <= _data->_nbrObjects; i++) {
		const Object &obj = _data->_props[i];

		if (obj.inOrOn > 0) {
			const Object &container = _data->_props[obj.inOrOn];
			if (container.kindOfThing == kABox) {
				_boxed.set(i);
				if (!container.itsOpen)
					_concealed.set(i);
			} else if (container.kindOfThing == kASupport) {
				// Objects on supports are "structures"
				_structures.set(i);
			}
		}

		if (obj.kindOfThing == kALamp && obj.litUp) {
			_lightSources.set(i);
		}
	}

	_buried = _concealed;  // Initial approximation
	_veiled = _concealed;
	_veiled |= _buried;

	// Build vehicle interior/stops sets
	for (int i = 1; i <= _data->_nbrVehicles; i++) {
		const Vehicle &vcl = _data->_fleet[i];
		if (vcl.vclType == kACar && vcl.inside > 0) {
			_vclInterior.set(vcl.inside);
		}
		if (vcl.vclType == kABus || vcl.vclType == kAnExcursion) {
			// Route locations are stops
			for (int j = 1; j <= kMaxNbrLocations; j++) {
				if (vcl.route.has(j))
					_vStops.set(j);
			}
		}
	}
}

} // End of namespace Angel
} // End of namespace Glk
