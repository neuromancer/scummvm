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

/**
 * @file parser.cpp
 * @brief Command parser for the AngelSoft adventure game system.
 *
 * Reverse-engineered from p-code disassembly of the INITIALI and RESPOND
 * segments of the original UCSD Pascal codefile.
 *
 * The parser pipeline is:
 *   1. Tokenizer   (INITIALI proc 9)  — split input on whitespace/punctuation
 *   2. SearchV     (INITIALI proc 6)  — vocab lookup using alphabetic index
 *   3. LookupAll   (INITIALI proc 13) — identify every token against vocab
 *   4. Classify    (COMMAREA/LISTENER) — determine ThingToDo, build Determiner
 *
 * Entity resolution follows the dispatch pattern found in RESPOND proc 35,
 * with type-specific resolvers mapping to procs 36-41:
 *   proc 36 → person   (KindOfWord = kAPerson)
 *   proc 40 → location (KindOfWord = kALocation)
 *   proc 41 → object   (KindOfWord = kAnObject)
 *   proc 37 → vocab    (KindOfWord = kAVerb)
 */

#include "glk/angel/parser.h"
#include "common/debug.h"
#include "common/textconsole.h"

namespace Glk {
namespace Angel {

// ============================================================
// Constructor
// ============================================================

Parser::Parser(Angel *engine, GameData *data, GameState *state)
	: _engine(engine), _data(data), _state(state) {
}

// ============================================================
// Static helpers
// ============================================================

Common::String Parser::normalize(const Common::String &word) {
	Common::String result;
	for (uint i = 0; i < word.size(); i++) {
		char c = word[i];
		if (c >= 'a' && c <= 'z')
			c = c - 'a' + 'A';
		result += c;
	}
	return result;
}

// ============================================================
// Tokenizer  (mirrors INITIALI proc 9)
// ============================================================

/**
 * Split the player's input line into tokens.
 *
 * The original p-code (INITIALI proc 9) reads characters one at a time,
 * classifying them into alphanumeric, punctuation, and whitespace.
 * It tracks whether a '?' appears anywhere in the input (sets _aQuestion).
 * Punctuation is stripped; hyphens and apostrophes are kept within words
 * (e.g., "can't", "south-east").
 */
void Parser::tokenize(const Common::String &input) {
	_tokens.clear();
	_state->_aQuestion = false;
	_state->_eoCom = false;

	Common::String current;

	for (uint i = 0; i < input.size(); i++) {
		char c = input[i];

		if (c == '?') {
			// Question mark — set flag and treat as word separator
			_state->_aQuestion = true;
			if (!current.empty()) {
				Token tok;
				tok.word = current;
				_tokens.push_back(tok);
				current.clear();
			}
		} else if (c == ' ' || c == '\t') {
			// Whitespace — end current word
			if (!current.empty()) {
				Token tok;
				tok.word = current;
				_tokens.push_back(tok);
				current.clear();
			}
		} else if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!') {
			// Punctuation — end current word, don't add punctuation as token
			if (!current.empty()) {
				Token tok;
				tok.word = current;
				_tokens.push_back(tok);
				current.clear();
			}
		} else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		           (c >= '0' && c <= '9') || c == '-' || c == '\'') {
			// Alphanumeric or word-internal punctuation
			current += c;
		}
		// Other characters are silently ignored
	}

	if (!current.empty()) {
		Token tok;
		tok.word = current;
		_tokens.push_back(tok);
	}
}

// ============================================================
// Vocabulary search  (mirrors INITIALI procs 5-6)
// ============================================================

/**
 * Extract the printable name for a vocabulary entry.
 *
 * VEntry stores words via block references: the text for vocab word i
 * lives in _vText[_vocab[i].vbi], starting at offset _vocab[i].dsp,
 * for _vocab[i].len characters.
 */
Common::String Parser::getWordName(int vocabIndex) const {
	if (vocabIndex < 0 || vocabIndex > _data->_nbrVWords)
		return Common::String();

	const VEntry &entry = _data->_vocab[vocabIndex];
	if (entry.vbi < 1 || entry.vbi > kNbrVBlocks)
		return Common::String();
	if (entry.len <= 0)
		return Common::String();

	const Common::String &block = _data->_vText[entry.vbi];
	if (entry.dsp < 0 || (uint)(entry.dsp + entry.len) > block.size())
		return Common::String();

	return block.substr(entry.dsp, entry.len);
}

/**
 * Search the vocabulary table for a word.
 *
 * The original INITIALI proc 6 uses an alphabetic index (Vector[0..27])
 * to jump to the first entry starting with a given letter, then scans
 * forward comparing compressed nip-packed words via GEQS 48. Two boolean
 * flags are updated:
 *   - _found  = true after an exact match
 *   - _partial = true after a prefix match (≥3 characters of input match
 *     the start of a longer vocab word)
 *
 * Our implementation compares decoded uppercase strings directly —
 * functionally equivalent but avoids re-encoding into nip arrays.
 *
 * @param word  The raw word to look up (any case).
 * @return Vocabulary index (0.._nbrVWords), or -1 if not found.
 */
int Parser::searchV(const Common::String &word) {
	_state->_found = false;
	_state->_partial = false;

	if (word.empty() || _data->_nbrVWords <= 0)
		return -1;

	Common::String upper = normalize(word);
	int bestPartial = -1;

	for (int i = 0; i <= _data->_nbrVWords; i++) {
		Common::String vocabName = normalize(getWordName(i));
		if (vocabName.empty())
			continue;

		if (upper == vocabName) {
			// Exact match
			_state->_found = true;
			_state->_partial = false;
			return i;
		}

		// Partial match: input must be at least 3 chars and must be a
		// prefix of the vocab word (original proc 6 uses MOV 3328 +
		// IXP 2,6 to compare leading nips)
		if (upper.size() >= 3 && vocabName.size() > upper.size() &&
		    vocabName.hasPrefix(upper)) {
			if (bestPartial < 0) {
				bestPartial = i;
			}
		}
	}

	if (bestPartial >= 0) {
		_state->_partial = true;
		_state->_found = false;
		return bestPartial;
	}

	return -1;
}

// ============================================================
// Batch lookup  (mirrors INITIALI proc 13 — main word lookup loop)
// ============================================================

/**
 * Look up every token in the vocabulary and populate the game state's
 * word-type sets (CodeSet, KeyWords, PlaceWords, ObjWords, etc.).
 *
 * INITIALI proc 13 iterates over the extracted word list, calling proc 11
 * (which converts each word to compressed form and invokes searchV / proc 6).
 * The result for each word updates seg19 bitsets (code set, keyword set) and
 * the appropriate word-type set depending on VECore.VType.
 */
void Parser::lookupAll() {
	// Clear all word-category sets
	_state->_codeSet.clear();
	_state->_keyWords.clear();
	_state->_placeWords.clear();
	_state->_vclWords.clear();
	_state->_objWords.clear();
	_state->_personWords.clear();
	_state->_pfxWords.clear();

	for (uint i = 0; i < _tokens.size(); i++) {
		Token &tok = _tokens[i];
		int idx = searchV(tok.word);

		if (idx < 0) {
			tok.vocabIndex = -1;
			tok.kind = kAnOther;
			tok.code = kNotAVWord;
			tok.ref = 0;
			continue;
		}

		tok.vocabIndex = idx;
		const VECore &ve = _data->_vocab[idx].ve;
		tok.kind = ve.vType;
		tok.code = ve.code;
		tok.ref = ve.ref;

		// Add the VWords code to the code set (SET OF VWords)
		if (ve.code != kNotAVWord && (int)ve.code < kNumVWords)
			_state->_codeSet.set(ve.code);

		// Add vocab index to the keyword set (SET OF VWordIndex)
		_state->_keyWords.set(idx);

		// Add to the type-specific word set
		switch (ve.vType) {
		case kALocation:
		case kABuilding:
			_state->_placeWords.set(idx);
			break;
		case kAVehicle:
			_state->_vclWords.set(idx);
			break;
		case kAnObject:
			_state->_objWords.set(idx);
			break;
		case kAPerson:
			_state->_personWords.set(idx);
			break;
		case kADirection:
			// Directions are not added to a word-type set; they're
			// handled directly via VWords code in _codeSet
			break;
		case kAVerb:
		case kAPronoun:
		case kAPreposition:
		case kADay:
		case kAnOther:
		default:
			break;
		}
	}
}

// ============================================================
// Token search helpers
// ============================================================

int Parser::findDirection() const {
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].kind == kADirection)
			return (int)i;
	}
	// Also recognise cardinal direction codes in the VWords range
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].code >= kVNorth && _tokens[i].code <= kVDown)
			return (int)i;
	}
	return -1;
}

int Parser::findVerb() const {
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].kind == kAVerb)
			return (int)i;
		// VWords from kVEnter onward are verb-class words even if typed
		// differently in the vocab (e.g. kAnOther but with code kVQuit)
		if (_tokens[i].code >= kVEnter && _tokens[i].code < kNotAVWord)
			return (int)i;
	}
	return -1;
}

int Parser::findObject(int afterPos) const {
	for (uint i = (uint)(afterPos + 1); i < _tokens.size(); i++) {
		if (_tokens[i].kind == kAnObject)
			return (int)i;
	}
	return -1;
}

int Parser::findPerson() const {
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].kind == kAPerson)
			return (int)i;
	}
	return -1;
}

int Parser::findVehicle() const {
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].kind == kAVehicle)
			return (int)i;
	}
	return -1;
}

int Parser::findLocation() const {
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].kind == kALocation || _tokens[i].kind == kABuilding)
			return (int)i;
	}
	return -1;
}

bool Parser::isPreposition(const Token &tok) const {
	return tok.kind == kAPreposition ||
	       (tok.code >= kVTo && tok.code <= kVIn);
}

// ============================================================
// Entity resolution  (mirrors RESPOND procs 35-41)
// ============================================================

/**
 * Resolve an object by matching a vocab index against object names.
 *
 * RESPOND proc 41 scans seg17:1..nbrObjects, checking each Object's
 * oName field against the given vocab index. Prefers objects that are
 * accessible (at the current location, carried, or worn).
 */
int Parser::resolveObject(int vocabIndex) const {
	if (vocabIndex < 0)
		return 0;

	// First pass: prefer objects at the player's location or in inventory
	int loc = _state->_location;
	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (_data->_props[obj].oName == vocabIndex) {
			if (_state->map(loc).objects.has(obj) ||
			    _state->_possessions.has(obj) ||
			    _state->_wearing.has(obj))
				return obj;
		}
	}

	// Second pass: return first match anywhere in the world
	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (_data->_props[obj].oName == vocabIndex)
			return obj;
	}

	return 0;
}

/**
 * Resolve a person by matching a vocab index against person names.
 *
 * RESPOND proc 36 scans seg17:151..castSize, checking pName.
 * Prefers a person co-located with the player.
 */
int Parser::resolvePerson(int vocabIndex) const {
	if (vocabIndex < 0)
		return 0;

	// Prefer person at Current location
	for (int p = 1; p <= _data->_castSize; p++) {
		if (_data->_cast[p].pName == vocabIndex &&
		    _data->_cast[p].located == _state->_location)
			return p;
	}

	// Fall back to any person with that name
	for (int p = 1; p <= _data->_castSize; p++) {
		if (_data->_cast[p].pName == vocabIndex)
			return p;
	}

	return 0;
}

/**
 * Resolve a vehicle by matching a vocab index against vehicle names.
 */
int Parser::resolveVehicle(int vocabIndex) const {
	if (vocabIndex < 0)
		return 0;

	for (int v = 1; v <= _data->_nbrVehicles; v++) {
		if (_data->_fleet[v].vName == vocabIndex)
			return v;
	}

	return 0;
}

/**
 * Resolve a location by matching a vocab index against place short
 * descriptions (the VWordIndex stored in Place.shortDscr).
 *
 * RESPOND proc 40 scans seg17:120..nbrLocations.
 */
int Parser::resolveLocation(int vocabIndex) const {
	if (vocabIndex < 0)
		return 0;

	for (int loc = 1; loc <= _data->_nbrLocations; loc++) {
		if (_data->_map[loc].shortDscr == vocabIndex)
			return loc;
	}

	return 0;
}

// ============================================================
// Command classification & Determiner building
// ============================================================

/**
 * After all tokens have been looked up, classify the command and build
 * the Determiner structure.
 *
 * The classification logic comes from the COMMAREA and LISTENER units:
 *   - CodeSet / KeyWords contain the VWords and vocab indices found
 *   - The This[] array (GameState::_thisAction) maps verb VWords to ThingToDo
 *   - Directions without a verb → kAMove
 *   - Special verbs (Quit, Save, Inventory) → dedicated ThingToDo values
 *   - Questions (input ending with '?') bias toward kAQuery
 *   - Words matching _irritants (curse words) → kARubout
 *
 * The Determiner (GameState::_cur) collects:
 *   - doItToWhat:  first object mentioned
 *   - withWhat:    object after "WITH"
 *   - forWhat:     object after "FOR"
 *   - personNamed: first person mentioned
 *   - rideWhat:    first vehicle mentioned
 *   - whereTo:     destination location (from direction or explicit place)
 */
ThingToDo Parser::classify() {
	// Reset Determiner — save previous as _ex already done in parse()
	_state->_cur = Determiner();
	_state->_verb = 0;
	_state->_whatNext = kNothing;
	_state->_thing = 0;
	_state->_cab = 0;
	_state->_placeNamed = 0;
	_state->_otherPerson = 0;

	// ---- Resolve direction ----
	int dirIdx = findDirection();
	if (dirIdx >= 0) {
		int dirCode = _tokens[dirIdx].code;
		// Direction VWords codes 0..5 map directly to MotionSpec
		if (dirCode >= kVNorth && dirCode <= kVDown) {
			MotionSpec dir = (MotionSpec)(dirCode - kVNorth);
			int dest = _state->map(_state->_location).nextPlace[dir];
			_state->_cur.whereTo = dest;
			_state->_direction = dir;
		}
	}

	// ---- Resolve verb ----
	int verbIdx = findVerb();
	VWords verbCode = kNotAVWord;
	if (verbIdx >= 0) {
		_state->_verb = _tokens[verbIdx].vocabIndex;
		verbCode = _tokens[verbIdx].code;
	}

	// ---- Resolve objects ----
	int objIdx = findObject(-1);
	if (objIdx >= 0) {
		_state->_thing = resolveObject(_tokens[objIdx].vocabIndex);
		_state->_cur.doItToWhat = _state->_thing;

		// Look for a second object separated by a preposition
		int obj2Idx = findObject(objIdx);
		if (obj2Idx >= 0) {
			int resolved2 = resolveObject(_tokens[obj2Idx].vocabIndex);

			// Scan tokens between the two objects for a preposition
			bool assigned = false;
			for (int p = objIdx + 1; p < obj2Idx; p++) {
				VWords pc = _tokens[p].code;
				if (pc == kVWith) {
					_state->_cur.withWhat = resolved2;
					assigned = true;
					break;
				} else if (pc == kVFor) {
					_state->_cur.forWhat = resolved2;
					assigned = true;
					break;
				} else if (pc == kVOn || pc == kVIn) {
					// "put X in/on Y" — second object is the container
					_state->_cur.withWhat = resolved2;
					assigned = true;
					break;
				}
			}
			if (!assigned) {
				// No intervening preposition — default to withWhat
				_state->_cur.withWhat = resolved2;
			}
		}
	}

	// ---- Handle "IT" pronoun — refer to previous object ----
	if (_state->_codeSet.has(kVIt) && _state->_cur.doItToWhat == 0) {
		_state->_cur.doItToWhat = _state->_ex.doItToWhat;
		_state->_thing = _state->_cur.doItToWhat;
	}

	// ---- Resolve person(s) ----
	int personIdx = findPerson();
	if (personIdx >= 0) {
		_state->_cur.personNamed = resolvePerson(_tokens[personIdx].vocabIndex);

		// Check for a second person
		for (uint i = (uint)(personIdx + 1); i < _tokens.size(); i++) {
			if (_tokens[i].kind == kAPerson) {
				_state->_otherPerson = resolvePerson(_tokens[i].vocabIndex);
				break;
			}
		}
	}

	// ---- Resolve vehicle ----
	int vclIdx = findVehicle();
	if (vclIdx >= 0) {
		_state->_cab = resolveVehicle(_tokens[vclIdx].vocabIndex);
		_state->_cur.rideWhat = _state->_cab;
	}

	// ---- Resolve explicit location ----
	int locIdx = findLocation();
	if (locIdx >= 0) {
		_state->_placeNamed = resolveLocation(_tokens[locIdx].vocabIndex);
		if (_state->_cur.whereTo == 0)
			_state->_cur.whereTo = _state->_placeNamed;
	}

	// ---- Determine ThingToDo ----

	// Special system commands take priority
	if (verbCode == kVQuit)
		return kGiveUp;
	if (verbCode == kVSave)
		return kSaveGame;
	if (verbCode == kVRestore)
		return kSaveGame;
	if (verbCode == kVInventory || verbCode == kVHave)
		return kInventory;

	// Look up verb in the This[] action table
	if (verbCode != kNotAVWord && (int)verbCode >= (int)kVEnter &&
	    (int)verbCode < kNumVWords) {
		ThingToDo action = _state->_thisAction[(int)verbCode];
		if (action != kNothing)
			return action;
	}

	// Pure direction → movement
	if (dirIdx >= 0 && verbIdx < 0)
		return kAMove;

	// "GO" + direction → movement
	if (verbCode == kVGo && dirIdx >= 0)
		return kAMove;

	// "GO" + place name → movement
	if (verbCode == kVGo && locIdx >= 0)
		return kAMove;

	// Direction present in any context → movement
	if (dirIdx >= 0)
		return kAMove;

	// Question mark detected → query
	if (_state->_aQuestion)
		return kAQuery;

	// Check for curse/irritant words
	for (uint i = 0; i < _tokens.size(); i++) {
		if (_tokens[i].vocabIndex >= 0 && _state->_irritants.has(_tokens[i].vocabIndex))
			return kARubout;
	}

	// Greeting (hello with a person)
	if (verbCode == kVHello)
		return kANod;

	// Nothing recognised
	return kNothing;
}

// ============================================================
// Main entry point
// ============================================================

/**
 * Parse a complete line of player input.
 *
 * Preserves the previous Determiner in _ex (so "IT" can reference the
 * last object), then runs the full pipeline: tokenize → lookupAll →
 * classify. Stores the result in _state->_whatNext and _state->_curBase.
 */
ThingToDo Parser::parse(const Common::String &input) {
	// Preserve previous Determiner for pronoun resolution ("IT")
	_state->_ex = _state->_cur;

	// Step 1 — Tokenize the raw input
	tokenize(input);

	if (_tokens.empty()) {
		_state->_whatNext = kNothing;
		return kNothing;
	}

	debugC(2, 0, "Angel parser: %d token(s) from \"%s\"",
	       (int)_tokens.size(), input.c_str());

	// Step 2 — Vocabulary lookup for every token
	lookupAll();

	// Debug: report what was found
	for (uint i = 0; i < _tokens.size(); i++) {
		const Token &t = _tokens[i];
		if (t.vocabIndex >= 0) {
			debugC(2, 0, "  token[%d] \"%s\" → vocab %d kind=%d code=%d ref=%d",
			       i, t.word.c_str(), t.vocabIndex, (int)t.kind,
			       (int)t.code, t.ref);
		} else {
			debugC(2, 0, "  token[%d] \"%s\" → unknown", i, t.word.c_str());
		}
	}

	// Step 3 — Classify command and build Determiner
	ThingToDo result = classify();
	_state->_whatNext = result;
	_state->_curBase = _state->_cur;

	debugC(2, 0, "Angel parser: whatNext = %d, verb idx = %d, "
	       "obj = %d, person = %d, vehicle = %d, dest = %d",
	       (int)result, _state->_verb,
	       _state->_cur.doItToWhat, _state->_cur.personNamed,
	       _state->_cur.rideWhat, _state->_cur.whereTo);

	return result;
}

} // End of namespace Angel
} // End of namespace Glk
