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

#include "hypno/teacher/demo/Handler2.h"
#include "hypno/teacher/teacher.h"

namespace Hypno {

Handler2::Handler2() : sprite(nullptr), palette(nullptr) {
	handlerId = 2;
	sourceAddress = 1;  // Original: Handler2 sets sourceAddress = 1
	_spritePath = "demo/Dummy.smk";
	_samplePath = "teacher_leave.wav";
}

Handler2::~Handler2() {
	delete sprite;
	delete palette;
}

void Handler2::init(SC_Message *msg) {
	copyCommandData(msg);
	if (palette) {
		palette->setPalette(0, 0x100);
	}
}

int Handler2::shutDown(SC_Message *msg) {
	return 0;
}

int Handler2::addMessage(SC_Message *msg) {
	// Original: Handler::AddMessage(msg) sets default fields
	writeMessageAddress(msg);

	// If mouse button pressed, send quit message (target=3, priority=6)
	if (msg->mouseX >= 2) {
		msg->targetAddress = 3;
		msg->priority = 6;
	}
	return 1;
}

int Handler2::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler2::update(int param1, int param2) {
	if (handlerId == param2 && sprite) {
		sprite->Do(sprite->loc_x, sprite->loc_y, 1.0);
	}
}

} // End of namespace Hypno
