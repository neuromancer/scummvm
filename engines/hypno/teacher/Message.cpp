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

#include "hypno/teacher/Message.h"
#include "hypno/teacher/teacher.h"
#include "common/debug.h"

namespace Hypno {

SC_Message::SC_Message(int target, int source, int cmd, int d, int prio, int p1, int p2, int uPtr, int clickX, int clickY)
	: targetAddress(target), sourceAddress(source), command(cmd), data(d), priority(prio),
	  param1(p1), param2(p2), userPtr(uPtr), mouseX(0), mouseY(0), lastKey(0), timeStamp(0) {
	clickPos.x = clickX;
	clickPos.y = clickY;
}

SC_Message::~SC_Message() {}

// Original: SC_Message::LBLParse at 0x419A10
// Parses ADDRESS, FROM, INSTRUCTION, MESSAGE, MOUSE, BUTTON1/2, LASTKEY, TIME, EXTRA1/2
// ADDRESS and FROM use GameState3 (index 2) to resolve symbolic names to handler IDs.
// INSTRUCTION uses GameState4 (index 3) to resolve priority names.
// When targetAddress or command == 5, sourceAddress or data is resolved via GameState (index 0).
int SC_Message::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	TeacherEngine *engine = (TeacherEngine *)g_engine;

	if (keyword == "ADDRESS") {
		// FORMAT: ADDRESS <targetName> <sourceValue>
		Common::String targetName, sourceVal;
		tokenize(rest, targetName, sourceVal);
		sourceVal.trim();

		int idx = engine->_gameState[2]->findState(targetName);
		targetAddress = idx;
		if (idx < 0 || idx >= 0x18) {
			warning("SC_Message: illegal ADDRESS index %d for '%s'", idx, targetName.c_str());
		}
		if (targetAddress == 5) {
			idx = engine->_gameState[0]->findState(sourceVal);
			if (idx > 0 && engine->_gameState[0]->maxStates <= idx) {
				warning("SC_Message: GameState Error #1");
			}
			sourceAddress = idx;
		} else {
			sourceAddress = atoi(sourceVal.c_str());
		}
	} else if (keyword == "FROM") {
		// FORMAT: FROM <commandName> <dataValue>
		Common::String cmdName, dataVal;
		tokenize(rest, cmdName, dataVal);
		dataVal.trim();

		int idx = engine->_gameState[2]->findState(cmdName);
		command = idx;
		if (idx < 0 || idx >= 0x18) {
			warning("SC_Message: illegal FROM index %d for '%s'", idx, cmdName.c_str());
		}
		if (command == 5) {
			idx = engine->_gameState[0]->findState(dataVal);
			if (idx > 0 && engine->_gameState[0]->maxStates <= idx) {
				warning("SC_Message: GameState Error #1");
			}
			data = idx;
		} else {
			data = atoi(dataVal.c_str());
		}
	} else if (keyword == "INSTRUCTION") {
		// FORMAT: INSTRUCTION <priorityName>
		Common::String prioName;
		tokenize(rest, prioName, rest);

		int idx = engine->_gameState[3]->findState(prioName);
		priority = idx;
		if (idx < 0 || idx >= 0x1e) {
			warning("SC_Message: illegal INSTRUCTION index %d for '%s'", idx, prioName.c_str());
		}
	} else if (keyword == "MESSAGE") {
		// Nested SC_Message stored in userPtr
		if (userPtr != 0) {
			warning("SC_Message: double reserve in MESSAGE");
		}
		SC_Message *msg = new SC_Message(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		userPtr = (int)(intptr_t)msg;
		Parser::processFile(msg, this, "");
	} else if (keyword == "MOUSE") {
		int cx, cy;
		sscanf(rest.c_str(), "%d %d", &cx, &cy);
		clickPos.x = cx;
		clickPos.y = cy;
	} else if (keyword == "BUTTON1") {
		mouseX = atoi(rest.c_str());
	} else if (keyword == "BUTTON2") {
		mouseY = atoi(rest.c_str());
	} else if (keyword == "LASTKEY") {
		lastKey = atoi(rest.c_str());
	} else if (keyword == "TIME") {
		timeStamp = atoi(rest.c_str());
	} else if (keyword == "EXTRA1") {
		param1 = atoi(rest.c_str());
	} else if (keyword == "EXTRA2") {
		param2 = atoi(rest.c_str());
	} else if (keyword == "END") {
		return 1;
	}

	return 0;
}

void SC_Message::dump(int unused) {
	debug("SC_Message: target=%d source=%d command=%d data=%d priority=%d param1=%d param2=%d pos=(%d,%d)",
		targetAddress, sourceAddress, command, data, priority, param1, param2, clickPos.x, clickPos.y);
}

} // End of namespace Hypno
