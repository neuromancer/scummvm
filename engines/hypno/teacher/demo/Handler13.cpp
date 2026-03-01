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

#include "hypno/teacher/demo/Handler13.h"

namespace Hypno {

Handler13::Handler13() {
	handlerId = 13;
	timer1.reset();
}

Handler13::~Handler13() {
	for (auto e : timedEvents) delete e;
}

void Handler13::init(SC_Message *msg) {
	copyCommandData(msg);
	moduleParam = msg->data;
}

int Handler13::shutDown(SC_Message *msg) {
	return 0;
}

int Handler13::addMessage(SC_Message *msg) {
	return 1;
}

int Handler13::deinit(SC_Message *msg) {
	if (msg->targetAddress != handlerId) return 0;

	timer1.reset();
	switch (msg->priority) {
	case 0xF:
		for (auto e : timedEvents) delete e;
		timedEvents.clear();
		break;
	case 0x13:
		if (msg->userPtr) {
			TimedEvent *e = new TimedEvent();
			e->duration = msg->timeStamp;
			e->sourceAddress = msg->sourceAddress;
			e->eventData = (SC_Message *)(uintptr)msg->userPtr;
			msg->userPtr = 0;
			e->timer.reset();
			timedEvents.push_back(e);
		}
		break;
	case 0x14:
		// Delete specific event (simplified for now)
		break;
	}
	return 1;
}

void Handler13::update(int param1, int param2) {
	uint32 elapsed = timer1.update();
	if (elapsed > 10000 && timedEvents.empty()) {
		// Send deinit message to self via targetAddress 3
		// SC_Message_Send(3, handlerId, handlerId, moduleParam, 0x14, 0, 0, 0, 0, 0);
	}

	timer1.reset();
	for (auto it = timedEvents.begin(); it != timedEvents.end(); ) {
		TimedEvent *e = *it;
		if (e->update() >= e->duration) {
			if (e->eventData) {
				// Send the actual event message (in a real implementation, this would go through TeacherEngine)
				// TeacherEngine::instance()->processMessage(e->eventData);
			}
			delete e;
			it = timedEvents.erase(it);
		} else {
			++it;
		}
	}
}

} // End of namespace Hypno
