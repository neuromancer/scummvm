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

#include "hypno/teacher/demo/Handler15.h"

namespace Hypno {

Handler15::Handler15() {
	handlerId = 15;
	timer.reset();
}

Handler15::~Handler15() {
	for (auto m : messages) delete m;
}

void Handler15::init(SC_Message *msg) {
	copyCommandData(msg);
}

int Handler15::shutDown(SC_Message *msg) {
	return 0;
}

int Handler15::addMessage(SC_Message *msg) {
	SC_Message *m = new SC_Message(msg->targetAddress, msg->sourceAddress, msg->command, msg->data, msg->priority, msg->param1, msg->param2, msg->userPtr, msg->clickPos.x, msg->clickPos.y);
	messages.push_back(m);
	return 1;
}

int Handler15::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler15::update(int param1, int param2) {
}

} // End of namespace Hypno
