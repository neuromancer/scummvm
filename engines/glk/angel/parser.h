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

#ifndef GLK_ANGEL_PARSER_H
#define GLK_ANGEL_PARSER_H

#include "glk/angel/types.h"
#include "glk/angel/game_data.h"
#include "glk/angel/game_state.h"
#include "common/str.h"
#include "common/array.h"

namespace Glk {
namespace Angel {

class Angel;

/**
 * Token from the input scanner.
 */
struct Token {
	Common::String word;         // The raw word text
	int vocabIndex;              // Index into the vocabulary table (-1 = unknown)
	KindOfWord kind;             // Classification of the word
	VWords code;                 // VWord code (kNotAVWord if unrecognized)
	int ref;                     // Object/Person/Location/Vehicle reference

	Token() : vocabIndex(-1), kind(kAnOther), code(kNotAVWord), ref(0) {}
};

/**
 * Parser for the AngelSoft adventure game system.
 *
 * The original Pascal code used procedures in COMMAREA and LISTENER
 * to scan, tokenize, and classify the player's input. This class
 * consolidates:
 *   - Tokenization: splitting raw input into words
 *   - SearchV: vocabulary lookup (exact and partial match)
 *   - Classification: determining ThingToDo from verb
 *   - Determiner building: identifying objects, people, directions
 */
class Parser {
public:
	Parser(Angel *engine, GameData *data, GameState *state);

	/**
	 * Parse a line of player input.
	 * Tokenizes, looks up each word in vocabulary, classifies the
	 * command, builds the Determiner, and returns the ThingToDo.
	 */
	ThingToDo parse(const Common::String &input);

	/**
	 * Get the list of tokens from the last parse.
	 */
	const Common::Array<Token> &tokens() const { return _tokens; }

	/**
	 * Search vocabulary for a word.
	 * Sets state->_found and state->_partial flags.
	 * Returns the vocabulary index, or -1 if not found.
	 */
	int searchV(const Common::String &word);

	/**
	 * Extract the printable name for a vocabulary word at the given index.
	 * Decodes nip-packed characters from the XVEntry.
	 */
	Common::String getWordName(int vocabIndex) const;

private:
	Angel *_engine;
	GameData *_data;
	GameState *_state;

	Common::Array<Token> _tokens;

	/** Tokenize input into words (whitespace-delimited, stripped of punctuation) */
	void tokenize(const Common::String &input);

	/** Look up each token in the vocabulary */
	void lookupAll();

	/** Classify the command and build the Determiner */
	ThingToDo classify();

	/** Find a direction token in the token list */
	int findDirection() const;

	/** Find a verb token in the token list */
	int findVerb() const;

	/** Find an object reference token after position pos */
	int findObject(int afterPos) const;

	/** Find a person reference token */
	int findPerson() const;

	/** Find a vehicle reference token */
	int findVehicle() const;

	/** Find a location reference token */
	int findLocation() const;

	/** Check if a word is a preposition */
	bool isPreposition(const Token &tok) const;

	/** Normalize a word for comparison (uppercase, trim) */
	static Common::String normalize(const Common::String &word);

	// ---- Entity resolution (matches RESPOND procs 35-41) ----

	/** Resolve an object from vocab index — checks location, possessions, etc. */
	int resolveObject(int vocabIndex) const;

	/** Resolve a person from vocab index — prefers person at current location */
	int resolvePerson(int vocabIndex) const;

	/** Resolve a vehicle from vocab index */
	int resolveVehicle(int vocabIndex) const;

	/** Resolve a location from vocab index */
	int resolveLocation(int vocabIndex) const;
};

} // End of namespace Angel
} // End of namespace Glk

#endif
