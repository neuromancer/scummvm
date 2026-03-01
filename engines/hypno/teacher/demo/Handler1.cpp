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

#include "hypno/teacher/demo/Handler1.h"
#include "hypno/teacher/teacher.h"

namespace Hypno {

Handler1::Handler1() : sprite(nullptr), palette(nullptr) {
	handlerId = 1;
	sourceAddress = 1;  // Original: Handler1 sets sourceAddress = 1
}

Handler1::~Handler1() {
	delete sprite;
	delete palette;
}

void Handler1::init(SC_Message *msg) {
	copyCommandData(msg);

	// In the original demo, this loaded the main menu splash screen and waited for a click
	_spritePath = "demo/Demoscrn.smk";
	_palettePath = "demo/Demoscrn.col";

	sprite = new Sprite(_spritePath);
	palette = new TeacherPalette();
	palette->load(_palettePath);
}

int Handler1::shutDown(SC_Message *msg) {
	// Original: destroy assets and send self-removal message
	if (sprite) { delete sprite; sprite = nullptr; }
	if (palette) { delete palette; palette = nullptr; }

	// Send self-removal message: target=3, priority=0x14 (RemoveHandler)
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	engine->sendMessage(3, handlerId, handlerId, moduleParam, 0x14);

	debugC(1, kHypnoDebugScene, "EXIT INTRO GAME TEXT");
	return 0;
}

int Handler1::addMessage(SC_Message *msg) {
	// Original: Handler::AddMessage(msg) sets default fields, then override
	writeMessageAddress(msg);

	// If mouse button pressed (mouseX >= 2), transition to Handler 10 (AfterSchoolMenu)
	if (msg->mouseX >= 2) {
		msg->targetAddress = 10;
		msg->priority = 5;
		msg->sourceAddress = 1;
		msg->command = 1;
		msg->data = 1;
	}
	return 1;
}

int Handler1::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler1::update(int param1, int param2) {
	if (handlerId == param2 && sprite) {
		sprite->Do(sprite->loc_x, sprite->loc_y, 1.0);
	}
}

} // End of namespace Hypno
