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

#include "hypno/teacher/demo/Handler16.h"

namespace Hypno {

Handler16::Handler16() : isInitialized(false) {
	handlerId = 16;
}

Handler16::~Handler16() {
}

void Handler16::init(SC_Message *msg) {
	copyCommandData(msg);
	isInitialized = true;
	filename = Common::String::format("mis/combat%02d.mis", msg->data);
	Parser::parseFile(this, filename, "");
}

int Handler16::shutDown(SC_Message *msg) {
	return 0;
}

int Handler16::addMessage(SC_Message *msg) {
	return 1;
}

int Handler16::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler16::update(int param1, int param2) {
}

int Handler16::lblParse(const Common::String &line) {
	// Simplified combat parsing for demo
	return 0;
}

} // End of namespace Hypno
