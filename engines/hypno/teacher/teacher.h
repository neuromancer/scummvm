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

#ifndef HYPNO_TEACHER_H
#define HYPNO_TEACHER_H

#include "hypno/hypno.h"
#include "hypno/teacher/Message.h"
#include "hypno/teacher/StringTable.h"
#include "hypno/teacher/FlagArray.h"
#include "hypno/teacher/GameState.h"
#include "hypno/teacher/Character.h"
#include "hypno/teacher/Handler.h"
#include "hypno/teacher/TeacherFont.h"
#include "hypno/teacher/MouseControl.h"

namespace Hypno {

class TeacherEngine : public HypnoEngine {
public:
	TeacherEngine(OSystem *syst, const ADGameDescription *gd);
	virtual ~TeacherEngine();
	Common::Error run() override;

	void loadAssets() override;
	void initializePath(const Common::FSNode &gamePath) override;
	void runIntros(Videos &videos) override;
	void disableGameKeymaps() override {}
	void enableGameKeymaps() override {}

	// Additional methods ported from reverse engineered code
	void sendMessage(int target, int source, int cmd, int data, int prio, int p1 = 0, int p2 = 0, int uPtr = 0, int clickX = 0, int clickY = 0);
	void queueMessage(SC_Message *msg);
	void setCursor(int cursorId);
	Common::Point getMousePos();
	Handler *findHandler(int id);

	Common::Queue<SC_Message *> _messageQueue;

	Character *_peter;
	Character *_susan;
	Character *_duncan;
	Character *_selectedCharacter;

	StringTable *_strings;
	FlagArray *_flagManager;
	GameState *_gameState[4];
	TeacherFont *_font;  // Original: g_TextManager_00436990 (AnimatedAsset from barrel06.smk)
	MouseControl *_mouseControl;  // Original: g_Mouse_00436978

private:
	void loadAssetsDemo();
	void processInput();
	void updateGame();
	void drawFrame();
	void processMessage(SC_Message *msg);
	void handleSystemMessage(SC_Message *msg);
	int processControlMessage(SC_Message *msg);
	Handler *getOrCreateHandler(int id);
	Handler *createHandler(int id);
	void removeHandler(int id);

	Common::List<Handler *> _eventList;
	Handler *_currentHandler;
	uint32 _frameDelay;
	bool _shouldQuitLoop;
};

void SC_Message_Send(int target, int source, int command, int data, int priority, int param1, int param2, int userPtr, int clickX, int clickY);

} // End of namespace Hypno

#endif
