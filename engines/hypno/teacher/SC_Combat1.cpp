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

#include "hypno/teacher/SC_Combat1.h"
#include "hypno/teacher/CombatEngine.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/system.h"
#include "common/events.h"
#include "engines/util.h"

namespace Hypno {

// Original: 0x410650
SC_Combat1::SC_Combat1() {
	handlerId = 16;
	targetAddress = 16;
	isInitialized = false;
	savedScreenW = 0;
	savedScreenH = 0;
	screenWidth = 320;
	screenHeight = 200;
	scriptParser = nullptr;
}

// Original: 0x410760
SC_Combat1::~SC_Combat1() {
	delete scriptParser;
	scriptParser = nullptr;
}

// Original: 0x410810
void SC_Combat1::init(SC_Message *msg) {
	if (isInitialized)
		return;

	debugC(1, kHypnoDebugScene, "SC_Combat1: ENTER COMBAT1");
	Handler::init(msg);

	if (msg)
		sourceAddress = msg->data;

	// Original: save work buffer dimensions, then InitWorkBuffer(320, 200)
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	savedScreenW = engine->_screenW;
	savedScreenH = engine->_screenH;

	// Resize to combat dimensions (original: InitWorkBuffer)
	engine->_compositeSurface->free();
	engine->_compositeSurface->create(screenWidth, screenHeight, engine->_pixelFormat);
	engine->_screenW = screenWidth;
	engine->_screenH = screenHeight;
	initGraphics(screenWidth, screenHeight, &engine->_pixelFormat);

	// Parse combat configuration
	filename = "mis/combat1.mis";
	Parser::parseFile(this, filename, "");

	isInitialized = true;
}

// Original: 0x4109B0
int SC_Combat1::shutDown(SC_Message *msg) {
	if (!isInitialized)
		return 0;

	isInitialized = false;

	// Clean up combat engine
	if (g_combatEngine) {
		g_combatEngine->StopAndCleanup();
		delete g_combatEngine;
		g_combatEngine = nullptr;
	}

	// Original: InitWorkBuffer(savedScreen.x, savedScreen.y) - restore work buffer
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	engine->_compositeSurface->free();
	engine->_compositeSurface->create(savedScreenW, savedScreenH, engine->_pixelFormat);
	engine->_screenW = savedScreenW;
	engine->_screenH = savedScreenH;
	initGraphics(savedScreenW, savedScreenH, &engine->_pixelFormat);

	debugC(1, kHypnoDebugScene, "SC_Combat1: EXIT COMBAT1");
	return 0;
}

// Original: 0x410B00
void SC_Combat1::update(int param1, int param2) {
	if (!isInitialized)
		return;
	if (targetAddress != param2)
		return;

	// Original: g_CombatEngine->UpdateAndCheck()
	if (g_combatEngine) {
		int result = g_combatEngine->UpdateAndCheck();
		if (result != 0) {
			ProcessMessage();
		}
	}
}

// Original: 0x410B40
int SC_Combat1::addMessage(SC_Message *msg) {
	msg->command = targetAddress;
	msg->data = sourceAddress;
	msg->priority = 0;
	if (msg->lastKey == 0x1b) {
		// ESC key pressed
		ProcessMessage();
	}
	return 1;
}

// Original: 0x410B80
int SC_Combat1::deinit(SC_Message *msg) {
	if (msg->targetAddress != targetAddress)
		return 0;

	if (msg->priority != 0) {
		if (msg->priority != 0x13)
			return 0;

		delete scriptParser;
		scriptParser = nullptr;

		if (msg->userPtr != 0) {
			// Use provided script
			scriptParser = (SC_Message *)(intptr_t)msg->userPtr;
			msg->userPtr = 0;
		} else {
			scriptParser = new SC_Message(command, data, targetAddress, sourceAddress, 5, 0, 0, 0, 0, 0);
		}
	}

	return 1;
}

// Original: 0x410CA0
void SC_Combat1::ProcessMessage() {
	if (scriptParser) {
		// Queue the stored transition message
		TeacherEngine *engine = (TeacherEngine *)g_engine;
		SC_Message *msg = new SC_Message(
			scriptParser->targetAddress,
			scriptParser->sourceAddress,
			scriptParser->command,
			scriptParser->data,
			scriptParser->priority,
			scriptParser->param1,
			scriptParser->param2,
			scriptParser->userPtr,
			scriptParser->clickPos.x,
			scriptParser->clickPos.y
		);
		engine->queueMessage(msg);

		delete scriptParser;
		scriptParser = nullptr;
		return;
	}

	// Default: send transition message back to source
	SC_Message_Send(command, data, targetAddress, sourceAddress, 5, 0, 0, 0, 0, 0);
}

// Original: 0x410E80
int SC_Combat1::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty())
		return 0;

	if (keyword == "MODULE") {
		Common::String moduleType;
		tokenize(rest, moduleType, rest);

		if (moduleType == "A") {
			g_combatEngine = new EngineB();
		} else if (moduleType == "B") {
			// EngineA (exploration) - not implemented yet, use base
			g_combatEngine = new CombatEngine();
		} else {
			warning("SC_Combat1: Unknown module type '%s'", moduleType.c_str());
			g_combatEngine = new CombatEngine();
		}

		Parser::processFile(g_combatEngine, this, "");
		g_combatEngine->SetupViewport();
		return 0;
	}

	if (keyword == "END") {
		return 1;
	}

	return 0;
}

} // End of namespace Hypno
