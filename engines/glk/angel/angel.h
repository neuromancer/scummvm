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
	 * Setup video mode — 512x342 original compact Macintosh screen
	 */
	void initGraphicsMode() override;

	/**
	 * Create the screen
	 */
	Screen *createScreen() override;

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

	/** Handle EndSym section break — produces blank line if text was flushed
	 *  but no blank line was already emitted (matches original behavior where
	 *  kSpkOp+EndSym produces a blank line, but kForceOp+EndSym doesn't double). */
	void sectionBreak();

	/**
	 * Output a single character through the PutChar state machine.
	 *
	 * Matches the original IOHANDLER proc 5 (CXG 18,5). The state machine:
	 * - Defers spaces so word-wrap can act at word boundaries
	 * - Inserts a space after sentence-ending punctuation (. ! ?)
	 * - Capitalizes the first letter of each sentence (via state 3)
	 * - Absorbs consecutive spaces
	 * - Handles standalone "I" capitalization
	 */
	void putChar(char ch);

	/** Output a word (short string, no newline) */
	void putWord(const char *word);

	/** Output a newline */
	void newLine();

	/** Force output flush (alias for forceQ) */
	void forceOutput();

	/**
	 * End-of-speech: flush output and set PutChar state to capitalize next.
	 * Matches IOHANDLER proc 9 (CXG 18,9): double ForceQ + state=3.
	 */
	void endSpeak();

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

	/**
	 * PutChar state machine state (matches IOHANDLER global[1509]).
	 *   0: Normal text — detect punctuation for state transitions
	 *   1: After space — defer space output until next word starts
	 *   2: After sentence-ender (. ! ?) — next non-period char gets space
	 *   3: Capitalize first letter of new sentence
	 *   4: After 'i' — defer to check if standalone "I" pronoun
	 *   5: Insert space + transition to capitalize (state 3)
	 *   6: After comma/semicolon — transition to deferred space
	 *   7: After colon — digits pass through, else space + capitalize
	 */
	int _putCharState;

	/** True if any visible characters have been output on the current line.
	 *  ForceQ only outputs a newline when this is true (matching original). */
	bool _lineDirty;

	/** True when visible text has been output since the last blank line.
	 *  Used by section separators in describeLocation to produce exactly
	 *  one blank line between sections (matching original's CPG 28 pattern).
	 *  Set by rawPutChar; cleared by outLn. */
	bool _needsSeparator;

	/** Output a character directly to the GLK window (CPG 36 equivalent) */
	void rawPutChar(char ch);

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

	/** Process xReg timed events — decrement active counters, fire at zero */
	void processTimedEvents();

	/** Run the move/entry scripts for the current location */
	void runLocationScripts();

	/** Dispatch the player's command based on ThingToDo */
	void dispatchCommand(ThingToDo action);

	/** Display intro images (StartupScreen + BOOTUP) and wait for keypress */
	void showIntroImage();
};

} // End of namespace Angel
} // End of namespace Glk

#endif
