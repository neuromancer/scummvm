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

#ifndef GLK_ANGEL_ANGEL_H
#define GLK_ANGEL_ANGEL_H

#include "glk/glk_api.h"
#include "glk/angel/types.h"
#include "glk/angel/game_data.h"
#include "glk/angel/game_state.h"
#include "glk/angel/parser.h"
#include "glk/angel/vm.h"
#include "glk/angel/utilities.h"

namespace Glk {
namespace Angel {

/**
 * Main engine class for the AngelSoft adventure game system.
 *
 * Implements the game loop, I/O, and coordinates the subcomponents
 * (data loader, parser, bytecode VM, utilities).
 */
class Angel : public GlkAPI {
public:
	Angel(OSystem *syst, const GlkGameDescription &gameDesc);
	~Angel() override;

	/**
	 * Returns the engine type
	 */
	InterpreterType getInterpreterType() const override {
		return INTERPRETER_ANGEL;
	}

	/**
	 * Main game loop — called by GlkEngine::run()
	 */
	void runGame() override;

	/**
	 * Load a saved game (Quetzal format)
	 */
	Common::Error readSaveData(Common::SeekableReadStream *rs) override;

	/**
	 * Save the current game state (Quetzal format)
	 */
	Common::Error writeGameData(Common::WriteStream *ws) override;

	// ---- I/O helpers (accessible to VM, Parser, etc.) ----

	/** Output a string to the GLK main window, with word-wrapping */
	void print(const Common::String &text);

	/** Output a string followed by a newline */
	void println(const Common::String &text);

	/** Read a line of input from the player */
	Common::String readLine();

	/** Force-flush the output queue to the window */
	void forceQ();

	/** Close off the current line and start a new one */
	void outLn();

	/** Output a single character */
	void putChar(char ch);

	/** Output a word (short string, no newline) */
	void putWord(const char *word);

	/** Output a newline */
	void newLine();

	/** Force output flush (alias for forceQ) */
	void forceOutput();

	/** Return a random number in range [0, max) */
	int getRandom(int max);

	/** Display the status bar (day, time, move, score) */
	void putStatus();

	// ---- Subsystems ----
	GameData *data() { return _data; }
	GameState *state() { return _state; }
	Parser *parser() { return _parser; }
	VM *vm() { return _vm; }
	Utilities *utils() { return _utils; }

private:
	GameData *_data;
	GameState *_state;
	Parser *_parser;
	VM *_vm;
	Utilities *_utils;

	winid_t _mainWindow;
	winid_t _statusWindow;

	/** Load the three game data files (tables, vocab, message) */
	bool loadGameData();

	/** Initialize the game world from loaded data */
	void initGame();

	/** Execute one turn of the game loop */
	void doTurn();

	/** Describe the current location */
	void describeLocation();

	/** Process NPC animation for all cast members */
	void animateAll();

	/** Advance the game clock by one tick */
	void tickClock();

	/** Run the move/entry scripts for the current location */
	void runLocationScripts();

	/** Dispatch the player's command based on ThingToDo */
	void dispatchCommand(ThingToDo action);
};

} // End of namespace Angel
} // End of namespace Glk

#endif
