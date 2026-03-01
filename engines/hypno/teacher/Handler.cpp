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

#include "hypno/teacher/Handler.h"

namespace Hypno {

Handler::Handler() : handlerId(0), targetAddress(0), sourceAddress(0), command(0), data(0), moduleParam(0), savedCommand(0), savedData(0) {
}

Handler::~Handler() {}

// Original: Handler::Init (0x417180) from ScriptHandler.cpp
// The original reinterprets the SC_Message* as a Handler* and copies only
// the command and data fields. It does NOT copy targetAddress or sourceAddress,
// since those define the handler's own identity (set in the constructor).
void Handler::init(SC_Message *msg) {
	if (msg) {
		command = msg->command;
		data = msg->data;
	}
	copyCommandData(msg);
}

int Handler::addMessage(SC_Message *msg) {
	writeMessageAddress(msg);
	return 1;
}

int Handler::shutDown(SC_Message *msg) {
	return 0;
}

void Handler::update(int param1, int param2) {
}

int Handler::deinit(SC_Message *msg) {
	return 0;
}

void Handler::onInput() {
}

void Handler::copyCommandData(SC_Message *msg) {
	if (msg) {
		savedCommand = msg->command;
		savedData = msg->data;
	}
}

// Original: Handler::AddMessage (0x4171B0) from ScriptHandler.cpp
// Sets message fields to identify the handler as the source.
// Original used handler's own targetAddress and sourceAddress fields directly.
int Handler::writeMessageAddress(SC_Message *msg) {
	if (!msg) return -1;
	msg->targetAddress = handlerId;       // = original's targetAddress
	msg->sourceAddress = sourceAddress;    // handler's sourceAddress (e.g. 1)
	msg->command = handlerId;             // = original's targetAddress
	msg->data = sourceAddress;            // = original's sourceAddress
	msg->priority = 0;
	return 0;
}

} // End of namespace Hypno
