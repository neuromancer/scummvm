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

#include "glk/angel/angel.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/textconsole.h"

namespace Glk {
namespace Angel {

Angel::Angel(OSystem *syst, const GlkGameDescription &gameDesc)
	: GlkAPI(syst, gameDesc),
	  _data(nullptr), _state(nullptr), _parser(nullptr),
	  _vm(nullptr), _utils(nullptr),
	  _mainWindow(nullptr), _statusWindow(nullptr) {
}

Angel::~Angel() {
	delete _utils;
	delete _vm;
	delete _parser;
	delete _state;
	delete _data;
}

// ============================================================
// Data loading
// ============================================================

bool Angel::loadGameData() {
	// The detection file key is "tables", but we need all three files:
	//   tables, vocab, message
	// They should be in the same directory as the detected file.

	Common::File tablesFile, vocabFile, messageFile;

	if (!tablesFile.open("tables")) {
		// Try case variations
		if (!tablesFile.open("TABLES") && !tablesFile.open("Tables")) {
			warning("Angel: Cannot open 'tables' file");
			return false;
		}
	}

	if (!vocabFile.open("vocab")) {
		if (!vocabFile.open("VOCAB") && !vocabFile.open("Vocab")) {
			warning("Angel: Cannot open 'vocab' file");
			return false;
		}
	}

	if (!messageFile.open("message")) {
		if (!messageFile.open("MESSAGE") && !messageFile.open("Message")) {
			warning("Angel: Cannot open 'message' file");
			return false;
		}
	}

	_data = new GameData();

	// We need a persistent stream for the message file since it's accessed
	// on demand via the VM page cache. The tables/vocab are fully loaded.
	// Create a memory stream copy of the message file.
	int msgSize = messageFile.size();
	byte *msgBuf = new byte[msgSize];
	messageFile.read(msgBuf, msgSize);
	Common::SeekableReadStream *msgStream = new Common::MemoryReadStream(
		msgBuf, msgSize, DisposeAfterUse::YES);

	// Load tables and vocab from their files
	int tabSize = tablesFile.size();
	byte *tabBuf = new byte[tabSize];
	tablesFile.read(tabBuf, tabSize);
	Common::SeekableReadStream *tabStream = new Common::MemoryReadStream(
		tabBuf, tabSize, DisposeAfterUse::YES);

	int vocSize = vocabFile.size();
	byte *vocBuf = new byte[vocSize];
	vocabFile.read(vocBuf, vocSize);
	Common::SeekableReadStream *vocStream = new Common::MemoryReadStream(
		vocBuf, vocSize, DisposeAfterUse::YES);

	bool ok = _data->load(tabStream, vocStream, msgStream);

	// tabStream and vocStream are consumed; msgStream is kept by _data
	delete tabStream;
	delete vocStream;

	if (!ok) {
		warning("Angel: Failed to load game data");
		return false;
	}

	debugC(1, 0, "Angel: Game data loaded successfully");
	debugC(1, 0, "  Locations: %d, Objects: %d, Cast: %d, Vehicles: %d, Vocab: %d",
	       _data->_nbrLocations, _data->_nbrObjects, _data->_castSize,
	       _data->_nbrVehicles, _data->_nbrVWords);

	return true;
}

// ============================================================
// Initialization
// ============================================================

void Angel::initGame() {
	_state = new GameState();
	_state->initFromData(_data);

	_parser = new Parser(this, _data, _state);
	_vm = new VM(this, _data, _state);
	_utils = new Utilities(this, _data, _state);

	_state->_stillPlaying = true;
}

// ============================================================
// I/O
// ============================================================

void Angel::print(const Common::String &text) {
	if (_mainWindow) {
		glk_set_window(_mainWindow);
		glk_put_string(text.c_str());
	}
}

void Angel::println(const Common::String &text) {
	print(text);
	print("\n");
}

Common::String Angel::readLine() {
	if (!_mainWindow)
		return Common::String();

	glk_set_window(_mainWindow);

	// Prompt
	glk_put_string("> ");

	// Read up to 255 characters
	char buf[256];
	memset(buf, 0, sizeof(buf));

	event_t ev;
	glk_request_line_event(_mainWindow, buf, 255, 0);

	do {
		glk_select(&ev);

		if (ev.type == evtype_Quit) {
			_state->_stillPlaying = false;
			return Common::String();
		}
	} while (ev.type != evtype_LineInput);

	// Null-terminate at the actual length
	buf[ev.val1] = '\0';
	return Common::String(buf);
}

void Angel::forceQ() {
	// Flush output queue to screen
	if (_state->_q > 0) {
		Common::String queued(_state->_msgQ, _state->_q);
		print(queued);
		_state->_q = 0;
	}
}

void Angel::outLn() {
	print("\n");
}

void Angel::putChar(char ch) {
	if (_mainWindow) {
		glk_set_window(_mainWindow);
		char buf[2] = { ch, '\0' };
		glk_put_string(buf);
	}
}

void Angel::putWord(const char *word) {
	if (_mainWindow) {
		glk_set_window(_mainWindow);
		glk_put_string(word);
	}
}

void Angel::newLine() {
	outLn();
}

void Angel::forceOutput() {
	forceQ();
}

int Angel::getRandom(int max) {
	if (max <= 0)
		return 0;
	return (int)getRandomNumber(max - 1);
}

void Angel::putStatus() {
	if (!_statusWindow)
		return;

	glk_set_window(_statusWindow);
	glk_window_clear(_statusWindow);

	// Day
	static const char *dayNames[] = {
		"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
		"THURSDAY", "FRIDAY", "SATURDAY"
	};

	if (_state->_dspDay && _state->_clock.day < kNumDays) {
		glk_put_string(dayNames[_state->_clock.day]);
		glk_put_string("  ");
	}

	// Time
	if (_state->_dspTime) {
		char timeBuf[16];
		snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s  ",
		         _state->_clock.hour, _state->_clock.minute,
		         _state->_clock.am ? "AM" : "PM");
		glk_put_string(timeBuf);
	}

	// Move
	if (_state->_dspMove) {
		char moveBuf[16];
		snprintf(moveBuf, sizeof(moveBuf), "MOVE %d  ", _state->_moveNumber);
		glk_put_string(moveBuf);
	}

	// Score
	if (_state->_dspScore) {
		char scoreBuf[16];
		snprintf(scoreBuf, sizeof(scoreBuf), "SCORE: %d",
		         _state->_possessions.count());
		glk_put_string(scoreBuf);
	}

	glk_set_window(_mainWindow);
}

// ============================================================
// Game loop
// ============================================================

void Angel::describeLocation() {
	const Place &here = _state->map(_state->_location);
	
	debug("Angel describeLocation: loc=%d, here.n=%d, here.view=%d", 
		_state->_location, here.n, (int)here.view);

	// Check darkness
	if (here.view == kDark && _state->_lightSources.isEmpty()) {
		println("It is too dark to see.");
		return;
	}

	// Display the location description via VM
	if (here.n > 0) {
		_vm->displayMsg(here.n);
		forceQ();
		outLn();
	}

	// List visible objects
	const ObjSet &objs = here.objects;
	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (objs.has(obj) && !_data->_props[obj].unseen) {
			if (_data->_props[obj].n > 0) {
				_vm->displayMsg(_data->_props[obj].n);
				forceQ();
				outLn();
			}
		}
	}

	// List visible people
	const PersonSet &people = here.people;
	for (int p = 1; p <= _data->_castSize; p++) {
		if (people.has(p) && !_data->_cast[p].unseen && _data->_cast[p].located == _state->_location) {
			if (_data->_cast[p].n > 0) {
				_vm->displayMsg(_data->_cast[p].n);
				forceQ();
				outLn();
			}
		}
	}
}

void Angel::animateAll() {
	for (int p = 1; p <= _data->_castSize; p++) {
		if (p != kNobody)
			_utils->animate(p);
	}
}

void Angel::tickClock() {
	_state->_clock.minute += 5;
	if (_state->_clock.minute >= 60) {
		_state->_clock.minute = 0;
		_state->_clock.hour++;
		if (_state->_clock.hour > 12) {
			_state->_clock.hour = 1;
		}
		// Toggle AM/PM at 12
		if (_state->_clock.hour == 12) {
			_state->_clock.am = !_state->_clock.am;
			if (_state->_clock.am) {
				// New day
				int d = (int)_state->_clock.day + 1;
				if (d >= kNumDays) d = 0;
				_state->_clock.day = (DayOfWeek)d;
			}
		}
	}
	_state->_clock.tickNumber++;
}

void Angel::runLocationScripts() {
	// This is a placeholder — the actual scripts are dispatched
	// via the bytecode VM based on location procedure addresses.
	// Full implementation requires the tables file decoder.
}

void Angel::dispatchCommand(ThingToDo action) {
	switch (action) {
	case kGiveUp:
		println("Are you sure you want to quit? (Y/N)");
		{
			Common::String answer = readLine();
			if (!answer.empty() && (answer[0] == 'Y' || answer[0] == 'y'))
				_state->_stillPlaying = false;
		}
		break;

	case kSaveGame:
		println("Save/restore is handled through the ScummVM menu.");
		break;

	case kAMove:
	case kATrip: {
		int dest = _state->_cur.whereTo;
		if (dest <= 0 || dest > _data->_nbrLocations) {
			println("You can't go that way.");
		} else {
			_utils->changeLocation(dest);
			outLn();
			describeLocation();
		}
		break;
	}

	case kAnAction:
		// Dispatch to appropriate action script via VM
		if (_state->_verb > 0 && _state->_verb <= _data->_nbrVWords) {
			const VECore &ve = _data->_vocab[_state->_verb].ve;
			if (ve.ref > 0) {
				_vm->displayMsg(ve.ref);
				forceQ();
			} else {
				println("Nothing happens.");
			}
		} else {
			println("I don't understand that action.");
		}
		break;

	case kAQuery:
		println("I don't know the answer to that.");
		break;

	case kAnOffer:
		println("Nobody seems interested in trading right now.");
		break;

	case kANod:
		if (_state->_cur.personNamed > 0) {
			println("Hello.");
		} else {
			println("Talking to yourself?");
		}
		break;

	case kInventory: {
		bool hasAny = false;
		for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
			if (_state->_possessions.has(obj)) {
				if (!hasAny) {
					println("You are carrying:");
					hasAny = true;
				}
				if (_data->_props[obj].oName > 0) {
					// Extract and print the object name
					Common::String name = _parser->getWordName(_data->_props[obj].oName);
					if (!name.empty()) {
						print("  ");
						println(name);
					}
				}
			}
		}
		if (!hasAny)
			println("You are empty-handed.");
		break;
	}

	case kATour:
		println("The tour continues...");
		break;

	case kARubout:
		_state->_nbrOffenses++;
		if (_state->_nbrOffenses >= 3) {
			println("Your language has offended me deeply. Goodbye.");
			_state->_stillPlaying = false;
		} else {
			println("Please watch your language.");
		}
		break;

	case kNothing:
		println("I don't understand. Try rephrasing that.");
		break;

	default:
		println("I'm not sure what you mean.");
		break;
	}
}

void Angel::doTurn() {
	putStatus();

	// Read and parse player input
	Common::String input = readLine();
	if (input.empty() || !_state->_stillPlaying)
		return;

	ThingToDo action = _parser->parse(input);

	debugC(1, 0, "Angel: parsed command → ThingToDo=%d verb=%d",
	       (int)action, _state->_verb);

	// Execute the command
	dispatchCommand(action);

	// Post-turn processing
	_state->_moveNumber++;
	tickClock();
	animateAll();

	// Flush any remaining output
	forceQ();
}

// ============================================================
// Main entry point
// ============================================================

void Angel::runGame() {
	// Open the main text window
	_mainWindow = glk_window_open(nullptr, 0, 0, wintype_TextBuffer, 1);
	if (!_mainWindow) {
		warning("Angel: Failed to open main window");
		return;
	}
	glk_set_window(_mainWindow);

	// Try to open a status line window (1 line at top)
	_statusWindow = glk_window_open(_mainWindow,
		winmethod_Above | winmethod_Fixed, 1, wintype_TextGrid, 2);

	// Load game data
	println("Loading Indiana Jones in Revenge of the Ancients...");
	println("");

	if (!loadGameData()) {
		println("Error: Could not load game data files.");
		println("Make sure 'tables', 'vocab', and 'message' are in the game directory.");
		return;
	}

	// Initialize game state
	initGame();

	// Welcome message
	println("Indiana Jones in Revenge of the Ancients");
	println("An interactive fiction adventure by Angelsoft, Inc.");
	println("(c) 1987 Lucasfilm Ltd.");
	println("");

	// Describe starting location
	describeLocation();

	// Main game loop
	while (_state->_stillPlaying && !shouldQuit()) {
		doTurn();
	}

	if (!shouldQuit()) {
		println("");
		println("Thanks for playing!");
	}
}

// ============================================================
// Save / Load stubs
// ============================================================

Common::Error Angel::readSaveData(Common::SeekableReadStream *rs) {
	// TODO: Implement save game loading
	// The original game uses a custom binary format from the SAVE segment
	warning("Angel: Save loading not yet implemented");
	return Common::kReadingFailed;
}

Common::Error Angel::writeGameData(Common::WriteStream *ws) {
	// TODO: Implement save game writing
	warning("Angel: Save writing not yet implemented");
	return Common::kWritingFailed;
}

} // End of namespace Angel
} // End of namespace Glk
