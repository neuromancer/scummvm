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

#ifndef HYPNO_TEACHER_SC_COMBAT1_H
#define HYPNO_TEACHER_SC_COMBAT1_H

#include "hypno/teacher/Handler.h"

namespace Hypno {

class CombatEngine;

// ============================================================================
// SC_Combat1 - Combat handler class with ID 16
// Original: SC_Combat1 (SC_Combat1.h, size 0xF8)
//
// This handler manages combat scenes. It:
// 1. Saves screen state and switches to 320x200 combat mode
// 2. Parses COMBAT1.mis to create the combat engine (EngineB)
// 3. Runs the combat loop via Update() -> CombatEngine::UpdateAndCheck()
// 4. On completion, restores screen state and transitions back
// ============================================================================
class SC_Combat1 : public Handler {
public:
	SC_Combat1();
	~SC_Combat1() override;

	void init(SC_Message *msg) override;
	int addMessage(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	void update(int param1, int param2) override;
	int deinit(SC_Message *msg) override;
	int lblParse(const Common::String &line) override;

	void ProcessMessage();

	bool isInitialized;
	int savedScreenW;       // original: savedScreen.x (saved work buffer width)
	int savedScreenH;       // original: savedScreen.y (saved work buffer height)
	int screenWidth;        // original: screenDim.x = 320
	int screenHeight;       // original: screenDim.y = 200
	Common::String filename;
	SC_Message *scriptParser;  // stored transition message
};

} // End of namespace Hypno

#endif
