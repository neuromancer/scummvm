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
#include "glk/angel/screen.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "engines/util.h"
#include "graphics/pixelformat.h"
#include "image/macpaint.h"


namespace Glk {
namespace Angel {

Angel::Angel(OSystem *syst, const GlkGameDescription &gameDesc)
	: GlkAPI(syst, gameDesc),
	  _data(nullptr), _state(nullptr), _parser(nullptr),
	  _vm(nullptr), _utils(nullptr),
	  _mainWindow(nullptr), _statusWindow(nullptr),
	  _putCharState(3),   // Start in state 3 (capitalize first letter)
	  _lineDirty(false),
	  _needsSeparator(false),
	  _debugInputPos(0) {
}

Angel::~Angel() {
	delete _utils;
	delete _vm;
	delete _parser;
	delete _state;
	delete _data;
}

void Angel::initGraphicsMode() {
	Graphics::PixelFormat pixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
	initGraphics(640, 480, &pixelFormat);
}

Screen *Angel::createScreen() {
	return new AngelScreen();
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

	warning("Angel: Game data loaded successfully");
	warning("  Locations: %d, Objects: %d, Cast: %d, Vehicles: %d, Vocab: %d",
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

	debugC(1, 0, "Angel: robotAddr=%d (tables), location=%d", _state->_robotAddr, _state->_location);
}

// ============================================================
// I/O
// ============================================================

void Angel::print(const Common::String &text) {
	if (_mainWindow) {
		debug("ANGEL_TEXT: \"%s\"", text.c_str());
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
	glk_put_string("-> ");

	// Debug replay: return next line from ANGEL_INPUT file.
	if (_debugInputPos < _debugInputLines.size()) {
		Common::String line = _debugInputLines[_debugInputPos++];
		// Echo the scripted input so the window shows a real session.
		glk_put_string(line.c_str());
		glk_put_char('\n');
		warning("Angel: debug input [%u]: '%s'", _debugInputPos - 1, line.c_str());
		return line;
	}

	// Normal GLK input
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
	// Matches IOHANDLER proc 7 (CXG 18,7).
	// Original: "if there is any text in the current line, force the queue
	// out to the screen."  ForceQ also adjusts PutChar state (at 0x04D9).

	// Flush legacy queue (from print-based output).
	if (_state->_q > 0) {
		Common::String queued(_state->_msgQ, _state->_q);
		print(queued);
		_state->_q = 0;
	}

	// End the current line only if there's visible content on it.
	// This matches the original where ForceQ appends CRChar to the queue
	// before flushing, but only when Q > 0.
	if (_lineDirty) {
		print("\n");
		_lineDirty = false;
	}

	// Adjust PutChar state (proc 7, 0x04D9-0x04F8):
	//   state == 3 → keep 3
	//   state in {5, 6} → set 3
	//   else → set 1
	if (_putCharState != 3) {
		if (_putCharState == 5 || _putCharState == 6) {
			_putCharState = 3;
		} else {
			_putCharState = 1;
		}
	}
}

void Angel::outLn() {
	print("\n");
	_needsSeparator = false;  // Blank line produced — no more separator needed
}

void Angel::sectionBreak() {
	// EndSym section break within a message. Produces a blank line only when:
	//   1. The current line has been flushed (_lineDirty == false), meaning
	//      the cursor is at the start of a new line (e.g., after endSpeak/kSpkOp).
	//   2. Visible text was output since the last blank line (_needsSeparator == true).
	// This matches the original behavior:
	//   - kSpkOp + EndSym → endSpeak flushes line, then EndSym adds blank line.
	//   - kForceOp + EndSym → kForceOp's outLn clears _needsSeparator, EndSym is no-op.
	//   - mid-word EndSym (e.g., "E"[EndSym]"arth's") → _lineDirty is true, no blank line.
	if (_needsSeparator && !_lineDirty) {
		outLn();
	}
}

void Angel::rawPutChar(char ch) {
	// Direct character output to GLK window (equivalent to CPG 36 in asglib).
	if (_mainWindow) {
		glk_set_window(_mainWindow);
		char buf[2] = { ch, '\0' };
		glk_put_string(buf);
		_lineDirty = true;
		_needsSeparator = true;  // Visible text → separator needed before next section
	}
}

// Helper: is ch a sentence-ending punctuation mark (. ! ?)
static bool isSentenceEnder(char ch) {
	return ch == '.' || ch == '!' || ch == '?';
}

// Helper: is ch a clause punctuation mark (comma, semicolon, etc.)
// These trigger state 6 (deferred space via recursive call).
static bool isClausePunct(char ch) {
	return ch == ',' || ch == ';';
}

// Helper: is ch ANY punctuation that triggers state transitions in state 0
static bool isPunctuation(char ch) {
	return isSentenceEnder(ch) || isClausePunct(ch) || ch == ':' || ch == '"';
}

void Angel::putChar(char ch) {
	// PutChar state machine matching IOHANDLER proc 5 (CXG 18,5).
	// Disassembled from the asglib binary, segment 18.
	//
	// State transitions implement:
	// - Deferred spaces (word-wrapping support)
	// - Auto-spacing after sentence-ending punctuation
	// - Auto-capitalization of sentence-initial letters
	// - Standalone "I" pronoun capitalization
	// - Absorption of multiple consecutive spaces

	switch (_putCharState) {
	case 0:
		// Normal text. Space → defer (state 1). Punctuation → state transitions.
		if (ch == ' ') {
			_putCharState = 1;
		} else {
			rawPutChar(ch);
			if (isSentenceEnder(ch)) {
				_putCharState = 2;  // After period/!/?
			} else if (ch == ':') {
				_putCharState = 7;  // After colon
			} else if (isClausePunct(ch) || ch == '"') {
				_putCharState = 6;  // After comma/semicolon/quote
			}
			// else: stay in state 0
		}
		break;

	case 1:
		// After space (deferred). Absorb extra spaces. When a real char arrives:
		// - Punctuation: output directly (no space before punct), transition
		// - Regular char: output the deferred SPACE, then the char
		if (ch == ' ') {
			// Absorb consecutive spaces — stay in state 1
		} else if (isPunctuation(ch)) {
			// Punctuation after space: output punct directly, no leading space.
			// Original p-code checks a punct set (LDC 2,20).
			rawPutChar(ch);
			if (ch == ',') {
				_putCharState = 0;
			} else {
				_putCharState = 2;  // After sentence-ender
			}
		} else {
			// Regular character: output the deferred space, then the char.
			rawPutChar(' ');
			if (ch == 'i') {
				// Defer 'i' to check if standalone pronoun "I"
				_putCharState = 4;
			} else {
				rawPutChar(ch);
				_putCharState = 0;
			}
		}
		break;

	case 2:
		// After sentence-ending punctuation (. ! ?).
		// Another period → output it, stay in state 2 (ellipsis "..").
		// Any other char → state 5 (insert space + capitalize).
		if (ch == '.') {
			rawPutChar('.');
			// Stay in state 2
		} else {
			_putCharState = 5;
			putChar(ch);  // Recursive: process char in state 5
		}
		break;

	case 3:
		// Capitalize first letter of new sentence.
		// Set by endSpeak() (CXG 18,9) after paragraph breaks.
		// '@' → output '@' literally, stay state 3.
		// Space → absorb, stay state 3.
		// Lowercase → capitalize, output, state 0.
		// Other → output as-is, state 0.
		if (ch == '@') {
			rawPutChar('@');
			// Stay in state 3
		} else if (ch == ' ') {
			// Absorb spaces at start of sentence — stay state 3
		} else {
			if (ch >= 'a' && ch <= 'z') {
				ch = ch - 32;  // Capitalize
			}
			rawPutChar(ch);
			_putCharState = 0;
		}
		break;

	case 4:
		// After 'i' (deferred from state 1). Check if standalone "I".
		// Space → was standalone "I": output 'I', state 1 (defer new space).
		// Non-space → was part of word: output 'i', state 0, reprocess char.
		if (ch == ' ') {
			rawPutChar('I');  // Capitalize standalone "I"
			_putCharState = 1;
		} else {
			rawPutChar('i');  // Part of a word, keep lowercase
			_putCharState = 0;
			putChar(ch);  // Recursive: reprocess char in state 0
		}
		break;

	case 5:
		// Insert space before next word (after sentence-ending punct).
		// Then capitalize (state 3) via recursive call.
		rawPutChar(' ');
		_putCharState = 3;
		putChar(ch);  // Recursive: process char in state 3
		break;

	case 6:
		// After comma/semicolon/closing-quote.
		// Quote → output space + quote, state 0.
		// Other → transition to state 1 (deferred space), reprocess.
		if (ch == '"') {
			_putCharState = 0;
			rawPutChar(' ');
			rawPutChar('"');
		} else {
			_putCharState = 1;
			putChar(ch);  // Recursive: process char in state 1
		}
		break;

	case 7:
		// After colon.
		// Digit → output directly, state 0 (e.g., "12:00").
		// Quote → output quote, stay state 7(?), or go to state 5.
		// Other → state 5 (insert space + capitalize), reprocess.
		if (ch >= '0' && ch <= '9') {
			rawPutChar(ch);
			_putCharState = 0;
		} else {
			_putCharState = 5;
			if (ch == '"') {
				rawPutChar('"');
			} else {
				putChar(ch);  // Recursive: process char in state 5
			}
		}
		break;

	default:
		// Fallback: output directly
		rawPutChar(ch);
		_putCharState = 0;
		break;
	}
}

void Angel::putWord(const char *word) {
	// Output a word character by character through the state machine.
	if (word) {
		for (int i = 0; word[i]; i++) {
			putChar(word[i]);
		}
	}
}

void Angel::newLine() {
	outLn();
}

void Angel::forceOutput() {
	forceQ();
}

void Angel::endSpeak() {
	// CXG 18,9 (proc 9): ForceQ + ForceQ + set PutChar state to 3.
	// "Force the output queue and generate a blank line to differentiate
	// one unit of textual output from another."
	forceQ();
	forceQ();
	_putCharState = 3;  // Capitalize first letter of next sentence
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

	// Display the location description via VM.
	// Messages handle their own paragraph breaks via kForceOp.
	// We just forceQ after to end any remaining text on the line.
	// Section separators (outLn) match original's CPG 28 pattern in proc 27:
	// each display section is preceded by a blank-line separator (if needed).
	if (here.n > 0) {
		if (_needsSeparator)
			outLn();
		_vm->displayMsg(here.n);
		forceQ();
	}

	// List visible objects — separator before first object (matches CPG 28 at 0x1063)
	warning("Angel describeLocation: checking %d objects at loc %d", _data->_nbrObjects, _state->_location);
	const ObjSet &objs = here.objects;
	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (objs.has(obj)) {
			warning("Angel describeLocation: obj %d at loc, unseen=%d n=%d", obj, _data->_props[obj].unseen ? 1 : 0, _data->_props[obj].n);
			if (!_data->_props[obj].unseen) {
				if (_data->_props[obj].n > 0) {
					if (_needsSeparator)
						outLn();
					_vm->displayMsg(_data->_props[obj].n);
					forceQ();
				}
			}
		}
	}

	// List visible people — separator before first person (matches CPG 28 at 0x10E3)
	warning("Angel describeLocation: checking %d people at loc %d", _data->_castSize, _state->_location);
	const PersonSet &people = here.people;
	for (int p = 1; p <= _data->_castSize; p++) {
		if (people.has(p)) {
			warning("Angel describeLocation: person %d at loc, unseen=%d n=%d located=%d", p, _data->_cast[p].unseen ? 1 : 0, _data->_cast[p].n, _data->_cast[p].located);
			if (!_data->_cast[p].unseen && _data->_cast[p].located == _state->_location) {
				if (_data->_cast[p].n > 0) {
					if (_needsSeparator)
						outLn();
					_vm->displayMsg(_data->_cast[p].n);
					forceQ();
				}
			}
		}
	}
	warning("Angel describeLocation: done");
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

void Angel::processTimedEvents() {
	// Process xReg timed events: decrement active counters, fire when reaching 0.
	// xReg[i].x > 0 → active countdown. Decrement each call.
	// xReg[i].x == 0 → fires the event (displayMsg of proc), then stays at 0.
	// xReg[i].x < 0 → suspended (via opSsp), skip.
	for (int i = 0; i < 32; i++) {
		if (_state->_clock.xReg[i].x > 0) {
			_state->_clock.xReg[i].x--;
			debugC(2, 0, "Angel: xReg[%d] decremented to %d (proc=%d)",
			       i, _state->_clock.xReg[i].x, _state->_clock.xReg[i].proc);
			if (_state->_clock.xReg[i].x == 0 && _state->_clock.xReg[i].proc > 0) {
				warning("Angel: FIRING xReg[%d] proc=%d (loc=%d)",
				        i, _state->_clock.xReg[i].proc, _state->_location);
				_vm->displayMsg(_state->_clock.xReg[i].proc);
				forceQ();
			}
		}
	}
}

void Angel::runLocationScripts() {
	// This is a placeholder — the actual scripts are dispatched
	// via the bytecode VM based on location procedure addresses.
	// Full implementation requires the tables file decoder.
}

void Angel::dispatchCommand(ThingToDo action) {
	// Engine-level commands handled directly (not by the game script).
	if (action == kGiveUp) {
		println("Are you sure you want to quit? (Y/N)");
		Common::String answer = readLine();
		if (!answer.empty() && (answer[0] == 'Y' || answer[0] == 'y'))
			_state->_stillPlaying = false;
		return;
	}
	if (action == kSaveGame) {
		println("Save/restore is handled through the ScummVM menu.");
		return;
	}
	// Handle movement commands directly.
	if (action == kAMove || action == kATrip) {
		int dest = _state->_cur.whereTo;
		if (dest > kNowhere && dest <= _data->_nbrLocations) {
			debugC(1, 0, "Angel: move to location %d", dest);
			_utils->changeLocation(dest);
			outLn();
			describeLocation();
		} else {
			// Dead end — dispatch location script for "you can't go that way".
			int scriptAddr = _data->_map[_state->_location].n;
			debugC(1, 0, "Angel: dead end, dispatch loc script addr=%d", scriptAddr);
			_vm->displayMsg(scriptAddr);
			forceQ();
		}
		return;
	}

	// Dispatch to the default response script for non-movement commands.
	static const int kDefaultResponseAddr = 6660;
	int scriptAddr = kDefaultResponseAddr;

	debugC(1, 0, "Angel: dispatch response addr=%d (action=%d, loc=%d, verb=%d)",
	       scriptAddr, (int)action, _state->_location, _state->_verb);

	_vm->displayMsg(scriptAddr);
	forceQ();
}

void Angel::doTurn() {
	putStatus();

	// Output centered dash separator before prompt (matches IOHANDLER proc 4).
	// Original: (screenWidth - 10) / 2 spaces, then 10 dashes, then newline.
	forceQ();
	outLn();  // blank line before separator

	uint winW = 0, winH = 0;
	if (_mainWindow)
		glk_window_get_size(_mainWindow, &winW, &winH);
	if (winW < 10)
		winW = 60;  // fallback
	debugC(5, 0, "Angel: window size %u x %u chars", winW, winH);

	int pad = ((int)winW - 10) / 2;
	Common::String sep;
	for (int i = 0; i < pad; i++)
		sep += ' ';
	for (int i = 0; i < 10; i++)
		sep += '-';
	print(sep);
	outLn();

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
	processTimedEvents();
	animateAll();

	// Flush any remaining output
	forceQ();
}

// ============================================================
// Intro image display
// ============================================================

void Angel::showIntroImage() {
	// Try to open and display intro images before the text game begins.
	// The Macintosh version has two image files:
	//   StartupScreen — raw 512x342 Mac screen bitmap (Angelsoft logo)
	//   BOOTUP        — MacPaint 2.0 file (Indiana Jones title screen)
	//
	// The screen is 640x480 with 15px GLK margins, giving a usable area
	// of 610x450.  The 512x342 images are centered within that area.

	static const char *startupNames[] = { "StartupScreen", "STARTUPSCREEN", nullptr };
	static const char *bootupNames[] = { "BOOTUP", "Bootup", "bootup", nullptr };

	// We need a temporary text window to receive events, since GLK requires
	// at least one window.
	winid_t tempWin = glk_window_open(nullptr, 0, 0, wintype_TextBuffer, 0);
	if (!tempWin)
		return;

	for (int img = 0; img < 2 && !shouldQuit(); img++) {
		const char **names = (img == 0) ? startupNames : bootupNames;

		Common::File file;
		bool found = false;
		for (int i = 0; names[i] && !found; i++)
			found = file.open(names[i]);

		if (!found)
			continue;

		Image::MacPaintDecoder decoder;
		bool decoded;

		if (img == 0) {
			// StartupScreen: raw 512x342 bitmap
			decoded = decoder.loadRawBitmap(file, 512, 342);
		} else {
			// BOOTUP: MacPaint 2.0 file
			decoded = decoder.loadStream(file);
		}
		file.close();

		if (!decoded)
			continue;

		const Graphics::Surface *clut8 = decoder.getSurface();
		if (!clut8)
			continue;

		// Convert CLUT8 to screen pixel format so blitting doesn't
		// need palette lookups (Graphics::Surface carries no palette).
		Graphics::PixelFormat screenFmt = g_system->getScreenFormat();
		const Graphics::Palette &pal = decoder.getPalette();
		Graphics::Surface *surface = clut8->convertTo(screenFmt, pal.data(), pal.size());

		// Open a graphics window filling the entire display
		winid_t gfxWin = glk_window_open(tempWin,
			winmethod_Above | winmethod_Proportional, 100,
			wintype_Graphics, 0);
		if (!gfxWin) {
			surface->free();
			delete surface;
			continue;
		}

		// Center the image within the GLK graphics window.
		// Never scale — scaling destroys 1-bit dither patterns.
		uint winW, winH;
		glk_window_get_size(gfxWin, &winW, &winH);

		int xOff = MAX(0, ((int)winW - surface->w) / 2);
		int yOff = MAX(0, ((int)winH - surface->h) / 2);

		glk_image_draw(gfxWin, *surface, (uint)-1, xOff, yOff);

		surface->free();
		delete surface;

		// Wait for any keypress to dismiss
		glk_request_char_event(tempWin);

		event_t ev;
		do {
			glk_select(&ev);
			if (ev.type == evtype_Quit)
				break;
		} while (ev.type != evtype_CharInput);

		// Close the graphics window
		glk_window_close(gfxWin, nullptr);
	}

	// Close the temporary text window
	glk_window_close(tempWin, nullptr);
}

// ============================================================
// Main entry point
// ============================================================

void Angel::runGame() {
	warning("Angel: runGame() entered");

	// Show intro images (StartupScreen + BOOTUP) if present
	//showIntroImage();

	// Retro Aesthetics: override user input style to match story text.
	// This ensures regular weight (Monaco Regular) and black background.
	glk_stylehint_set(wintype_TextBuffer, style_Input, stylehint_Weight, 0);
	//glk_stylehint_set(wintype_TextBuffer, style_Input, stylehint_BackColor, 0x000000);

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

	// Debug: load scripted input from ANGEL_INPUT file (one command per line).
	// If present, readLine() returns lines from this file instead of GLK input.
	{
		Common::File inputFile;
		if (gDebugLevel > 1 && inputFile.open("ANGEL_INPUT")) {
			while (!inputFile.eos()) {
				Common::String line = inputFile.readLine();
				if (!inputFile.eos() || !line.empty())
					_debugInputLines.push_back(line);
			}
			inputFile.close();
			warning("Angel: loaded %u debug input lines from ANGEL_INPUT",
			        _debugInputLines.size());
		}
	}

	// Load game data
	if (!loadGameData()) {
		println("Error: Could not load game data files.");
		println("Make sure 'tables', 'vocab', and 'message' are in the game directory.");
		return;
	}

	// Initialize game state
	initGame();

	// Execute the WELCOME event procedure from the NtgrRegisters.
	// xReg[kXWelcome] holds the proc address for the game's intro text.
	// Set baseSuppressText=true so kForceOp re-evaluates to "suppress"
	// after each paragraph (hiding init code garbage). But keep
	// _suppressText=false so the initial text ("With the whine of
	// bullets...") before the first kForceOp is visible.
	_vm->setBaseSuppressText(true);
	if (_state->_clock.xReg[kXWelcome].proc > 0) {
		warning("Angel: Executing WELCOME event at proc=%d",
		       _state->_clock.xReg[kXWelcome].proc);
		_vm->displayMsg(_state->_clock.xReg[kXWelcome].proc);
		// The WELCOME message ends with kForceOp which handles its own
		// paragraph break (EndSpeak + outLn). Just flush any remnants.
		forceQ();
	}

	// Suppress text for ENTRY — ENTRY's content should not be displayed.
	_vm->setSuppressText(true);

	// Execute the ENTRY event procedure (xReg[kXEntry]).
	if (_state->_clock.xReg[kXEntry].proc > 0) {
		warning("Angel: Executing ENTRY event at proc=%d",
		       _state->_clock.xReg[kXEntry].proc);
		_vm->displayMsg(_state->_clock.xReg[kXEntry].proc);
		forceQ();
	}

	// Enable text output for room description display.
	_vm->setSuppressText(false);
	describeLocation();

	// Timer events (e.g., xReg[22] countdown=1) set during WELCOME will
	// fire naturally during the first doTurn() via processTimedEvents().
	// Don't call processTimedEvents() here — firing timers before the
	// first player turn produces premature text (e.g., msg 63's person
	// check "R.P." before the game state is ready for it).

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
