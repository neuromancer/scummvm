/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too list here. Please refer to the COPYRIGHT
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

#include "hypno/teacher/demo/Handler12.h"

namespace Hypno {

Handler12::Handler12() {
	handlerId = 12;
}

Handler12::~Handler12() {
}

void Handler12::init(SC_Message *msg) {
	copyCommandData(msg);
	timer.reset();
}

int Handler12::shutDown(SC_Message *msg) {
	return 0;
}

int Handler12::addMessage(SC_Message *msg) {
	return 1;
}

int Handler12::deinit(SC_Message *msg) {
	return handlerId <= msg->targetAddress;
}

void Handler12::update(int param1, int param2) {
}

} // End of namespace Hypno
