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

	debugC(1, kDebugScripts, "Angel: Game data loaded successfully");
	debugC(1, kDebugScripts, "  Locations: %d, Objects: %d, Cast: %d, Vehicles: %d, Vocab: %d",
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

	debugC(1, kDebugScripts, "Angel: robotAddr=%d (tables), location=%d", _state->_robotAddr, _state->_location);
}

// ============================================================
// I/O
// ============================================================

void Angel::print(const Common::String &text) {
	if (_mainWindow) {
		glk_set_window(_mainWindow);
		glk_put_string(text.c_str());
		// Flush screen line log on newline
		if (text.contains('\n') && !_screenLine.empty()) {
			debugC(kDebugScripts, "Angel SCREEN: %s", _screenLine.c_str());
			_screenLine.clear();
		}
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
		debugC(1, kDebugScripts, "Angel: debug input [%u]: '%s'", _debugInputPos - 1, line.c_str());
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
		_screenLine += ch;
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

	// Clear CmdEntry before running location script (RESPOND re-initializes each time).
	// The location script's opSet calls populate CmdEntry with dispatch addresses
	// for direction-specific responses (e.g., CmdEntry[1] = trapdoor at addr 277).
	memset(_state->_cmdEntry, 0, sizeof(_state->_cmdEntry));

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

	// Mark location as seen BEFORE entity display (no longer "new" for testNew).
	// Entity messages test location.unseen to control description vs response.
	_data->_map[_state->_location].unseen = false;

	// Entity display: save and restore verb/entity context.
	// Original kRoleOp (proc 97) runs within a clean description context.
	// Entity messages test the verb for command response dispatch — with
	// verb=0, those tests fail and only description text is produced.
	int savedVerb = _state->_verb;
	int savedDoItToWhat = _state->_cur.doItToWhat;
	int savedPersonNamed = _state->_cur.personNamed;
	_state->_verb = 0;

	// List visible objects at this location.
	// Original P-code (proc 97) sets doItToWhat before dispatching each object.
	debugC(2, kDebugScripts, "Angel describeLocation: checking %d objects at loc %d", _data->_nbrObjects, _state->_location);
	const ObjSet &objs = here.objects;
	for (int obj = 1; obj <= _data->_nbrObjects; obj++) {
		if (objs.has(obj)) {
			debugC(2, kDebugScripts, "Angel describeLocation: obj %d at loc, unseen=%d n=%d", obj, _data->_props[obj].unseen ? 1 : 0, _data->_props[obj].n);
			if (_data->_props[obj].n > 0) {
				if (_needsSeparator)
					outLn();
				_state->_cur.doItToWhat = obj;
				_vm->displayMsg(_data->_props[obj].n, true);
				forceQ();
				_data->_props[obj].unseen = false;
			}
		}
	}

	// People at this location are NOT listed in describeLocation.
	// In the original game, person descriptions appear through timed
	// events (xReg) or explicit player commands, not during room entry
	// or the look command. The DOS reference confirms this behavior.
	debugC(2, kDebugScripts, "Angel describeLocation: checking %d people at loc %d (display via events only)", _data->_castSize, _state->_location);

	// Restore verb/entity context
	_state->_verb = savedVerb;
	_state->_cur.doItToWhat = savedDoItToWhat;
	_state->_cur.personNamed = savedPersonNamed;
	debugC(2, kDebugScripts, "Angel describeLocation: done");
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
	// Two-pass approach to prevent cascading event fires.
	// When an event fires, its message script may re-register events (opEvent+opSet).
	// A single-pass loop would decrement those new registrations immediately,
	// causing multiple events to fire on the same turn.
	//
	// The P-code timer loop (RESPOND proc 1) starts at index 2 (kXCurse),
	// skipping xReg[0] (gas counter managed by scripts) and xReg[1] (WELCOME).
	//
	// Pass 1: Decrement all active counters, track which just expired.
	// Pass 2: Fire only events that were just decremented to 0.
	// Events registered during Pass 2 won't fire until a future turn.

	// Pass 1: Decrement and record which expired (start at kXCurse=2)
	uint32 justExpired = 0;
	for (int i = kXCurse; i < 32; i++) {
		if (_state->_clock.xReg[i].x > 0) {
			_state->_clock.xReg[i].x--;
			debugC(2, kDebugScripts, "Angel: xReg[%d] decremented to %d (proc=%d)",
			       i, _state->_clock.xReg[i].x, _state->_clock.xReg[i].proc);
			if (_state->_clock.xReg[i].x == 0)
				justExpired |= (1u << i);
		}
	}

	// Pass 2: Fire only events that just expired and are still at 0
	for (int i = kXCurse; i < 32; i++) {
		if ((justExpired & (1u << i)) && _state->_clock.xReg[i].x == 0
		    && _state->_clock.xReg[i].proc > 0) {
			debugC(1, kDebugScripts, "Angel: FIRING xReg[%d] proc=%d (loc=%d)",
			       i, _state->_clock.xReg[i].proc, _state->_location);
			_vm->displayMsg(_state->_clock.xReg[i].proc);
			forceQ();
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

	// Movement destination for kAMove/kATrip.
	int moveDest = kNowhere;
	int locBefore = _state->_location;  // Capture before any events fire
	if (action == kAMove || action == kATrip) {
		moveDest = _state->_cur.whereTo;
		debugC(1, kDebugScripts, "Angel: dispatchCommand move: loc=%d dest=%d dir=%d verb=%d cmdEntry[1]=%d",
		       _state->_location, moveDest, _state->_direction, _state->_verb, _state->_cmdEntry[1]);
		// Set target so kTargOp returns the destination — even for kNowhere,
		// because the MOVE script checks kTargOp to decide traps vs dead-ends.
		_state->_target = moveDest;
		_state->_placeNamed = moveDest;
	}

	// Dispatch the MOVE event for ALL movement commands (including kNowhere).
	// The MOVE script fires BEFORE changeLocation — it checks current location
	// (kLocOp) vs target (kTargOp) to handle traps, special events, etc.
	// The script itself decides whether to allow movement, block it, or kill
	// the player (e.g., trapdoor when going south/east from location 7).
	if (_state->_stillPlaying && (action == kAMove || action == kATrip)) {
		int moveProc = _state->_clock.xReg[kXMove].proc;
		if (moveProc > 0) {
			debugC(1, kDebugScripts, "Angel: MOVE event proc=%d at loc=%d target=%d",
			       moveProc, _state->_location, moveDest);
			_vm->setSuppressText(false);
			_vm->setRespondMode(true);
			_vm->displayMsg(moveProc);
			_vm->setRespondMode(false);
			forceQ();
		}
	}

	// Dispatch CmdEntry[1] OR location default response for movement commands.
	// RESPOND proc 1 at L_3430:
	//   if g[3090] (entity matched) → dispatch CmdEntry[1]
	//   else → dispatch seg[21].global[4] (= map[loc].n, location default)
	// For bare direction commands like "south", g[3090] = 0 so the location
	// default response runs.  This is where traps and dead-end handling live.
	if (_state->_stillPlaying && (action == kAMove || action == kATrip)) {
		int cmdAddr = _state->_cmdEntry[1];
		if (cmdAddr > 0) {
			debugC(1, kDebugScripts, "Angel: CmdEntry[1] dispatch addr=%d loc=%d target=%d",
			       cmdAddr, _state->_location, moveDest);
			_vm->setSuppressText(false);
			_vm->displayMsg(cmdAddr);
			forceQ();
		} else {
			// No entity match — dispatch location default response.
			// This script handles movement validation: dead-ends, traps,
			// and valid exits (via kMovOp or other state changes).
			int locDefault = _data->_map[_state->_location].n;
			if (locDefault > 0) {
				debugC(1, kDebugScripts, "Angel: location default dispatch addr=%d loc=%d target=%d",
				       locDefault, _state->_location, moveDest);
				_vm->setSuppressText(false);
				_vm->setRespondMode(true);
				_vm->displayMsg(locDefault);
				_vm->setRespondMode(false);
				forceQ();
			}
		}
	}

	// Fallback movement handling — if the location script didn't change
	// location (e.g. script lacks movement logic), do it in C++.
	if (_state->_stillPlaying && (action == kAMove || action == kATrip)) {
		if (_state->_location == locBefore && moveDest > kNowhere) {
			// Valid destination but script didn't move us — default move.
			debugC(1, kDebugScripts, "Angel: fallback move loc %d -> %d dir=%d",
			       _state->_location, moveDest, _state->_direction);
			_state->_pprvLocation = _state->_prvLocation;
			_state->_prvLocation = _state->_location;
			_state->_prvDirection = _state->_direction;
			_state->_location = moveDest;
			_state->_trail.set(moveDest);
		}

		// Post-move: if location changed, handle entry events and description.
		if (_state->_location != locBefore && _state->_stillPlaying) {
			debugC(1, kDebugScripts, "Angel: moved from loc %d to loc %d",
			       locBefore, _state->_location);
			_utils->changeLocation(_state->_location);

			// Fire ENTRY event after location change.
			if (_state->_clock.xReg[kXEntry].proc > 0) {
				_vm->setSuppressText(true);
				debugC(1, kDebugScripts, "Angel: ENTRY event at new loc=%d proc=%d",
				       _state->_location, _state->_clock.xReg[kXEntry].proc);
				_vm->displayMsg(_state->_clock.xReg[kXEntry].proc);
				forceQ();
				_vm->setSuppressText(false);
			}

			if (_state->_stillPlaying) {
				outLn();
				describeLocation();
			}
		}
	}

	// Inventory command: dispatch to the inventory message (addr 2496).
	// RESPOND proc 1 XJP case 8 (kInventory) → CPL 2 with addr 2496.
	// Message contains Fer kInvOp opcodes for dynamic item listing.
	if (_state->_stillPlaying && action == kInventory) {
		static const int kInventoryMsgAddr = 2496;
		debugC(1, kDebugScripts, "Angel: inventory dispatch addr=%d", kInventoryMsgAddr);
		_vm->setSuppressText(false);
		_vm->displayMsg(kInventoryMsgAddr);
		forceQ();
		return;
	}

	// Dispatch response script for non-movement commands.
	// RESPOND proc 1 flow:
	//   1. CPL 19 (set computation) → populates CmdEntry
	//   2. If g[3090] (entity resolved) → dispatch CmdEntry[1]
	//   3. Else → dispatch seg[21].global[4] (= msg 3, the default response)
	if (_state->_stillPlaying && action != kAMove && action != kATrip) {
		// "look" re-describes the current location: reset the unseen flag
		// so testNew returns true and the full description displays.
		if (_state->_codeSet.has(kVLook)) {
			_state->map(_state->_location).unseen = true;
		}

		int scriptAddr = 0;

		// Entity-specific dispatch: when an entity was targeted,
		// run its .n script for the response.
		if (_state->_cur.doItToWhat > 0 && _state->_cur.doItToWhat <= _data->_nbrObjects) {
			scriptAddr = _data->_props[_state->_cur.doItToWhat].n;
			debugC(1, kDebugScripts, "Angel: object dispatch obj=%d addr=%d",
			       _state->_cur.doItToWhat, scriptAddr);
		} else if (_state->_cur.personNamed > 0 && _state->_cur.personNamed <= _data->_castSize) {
			scriptAddr = _data->_cast[_state->_cur.personNamed].n;
			debugC(1, kDebugScripts, "Angel: person dispatch person=%d addr=%d",
			       _state->_cur.personNamed, scriptAddr);
		} else {
			// Default: dispatch current location's script.
			scriptAddr = _data->_map[_state->_location].n;
			debugC(1, kDebugScripts, "Angel: location dispatch loc=%d addr=%d",
			       _state->_location, scriptAddr);
		}

		if (scriptAddr > 0) {
			debugC(1, kDebugScripts, "Angel: dispatch response addr=%d (action=%d, loc=%d, verb=%d)",
			       scriptAddr, (int)action, _state->_location, _state->_verb);

			_vm->setSuppressText(false);
			_vm->displayMsg(scriptAddr);
			forceQ();
		}
	}
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
	debugC(5, kDebugScripts, "Angel: window size %u x %u chars", winW, winH);

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

	debugC(1, kDebugScripts, "Angel: parsed command → ThingToDo=%d verb=%d",
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
	debugC(1, kDebugScripts, "Angel: runGame() entered");

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
			debugC(1, kDebugScripts, "Angel: loaded %u debug input lines from ANGEL_INPUT",
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
		debugC(1, kDebugScripts, "Angel: Executing WELCOME event at proc=%d",
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
		debugC(1, kDebugScripts, "Angel: Executing ENTRY event at proc=%d",
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
