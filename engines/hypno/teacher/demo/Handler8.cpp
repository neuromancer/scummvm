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

#include "hypno/teacher/demo/Handler8.h"
#include "hypno/teacher/teacher.h"
#include "hypno/teacher/Animation.h"
#include "hypno/hypno.h"
#include "common/events.h"
#include "common/system.h"

namespace Hypno {

Handler8::Handler8() : message(nullptr), field_A4(0) {
	handlerId = 8;
}

Handler8::~Handler8() {
	delete message;
}

void Handler8::init(SC_Message *msg) {
	copyCommandData(msg);
	if (msg) {
		moduleParam = msg->data;
		char filename[64];
		Common::sprintf_s(filename, "cine/cine%04d.smk", moduleParam);
		
		TeacherAnimation anim;
		anim.play(filename, 0);

		Common::Event event;
		TeacherEngine *engine = (TeacherEngine *)g_engine;
		
		while (!anim.endOfVideo() && !engine->shouldQuit()) {
			bool skip = false;
			while (g_system->getEventManager()->pollEvent(event)) {
				if (event.type == Common::EVENT_KEYDOWN) {
					if (event.kbd.keycode == Common::KEYCODE_ESCAPE || event.kbd.keycode == Common::KEYCODE_SPACE)
						skip = true;
				} else if (event.type == Common::EVENT_LBUTTONDOWN) {
					skip = true;
				}
			}
			if (skip) break;
			
			if (anim.needsUpdate()) {
				anim.draw(engine->_compositeSurface, 0, 0, false, true);
				engine->drawScreen();
			} else {
				g_system->delayMillis(10);
			}
		}

		if (msg->userPtr) {
			message = (SC_Message *)msg->userPtr;
			msg->userPtr = 0;
		}

		processMessage();
	}
}

int Handler8::shutDown(SC_Message *msg) {
	return 0;
}

int Handler8::addMessage(SC_Message *msg) {
	// Original: Handler::AddMessage(msg) + check for ESC or mouse click
	writeMessageAddress(msg);
	if (msg->lastKey == 0x1b || msg->mouseY > 1) {
		msg->targetAddress = savedCommand;
		msg->priority = 5;
		msg->sourceAddress = savedData;
	}
	return 1;
}

int Handler8::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler8::update(int param1, int param2) {
}

// Original: Handler8::ProcessMessage (0x406400)
// If a chained message was stored, queue it. Otherwise send a transition
// message using the handler's saved fields.
void Handler8::processMessage() {
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	if (message) {
		// Queue the stored chained message
		engine->_messageQueue.push(message);
		message = nullptr;
	} else {
		// Original: SC_Message_Send(command, data, targetAddress, sourceAddress, 5, ...)
		// In ScummVM field naming: savedCommand, savedData, handlerId, moduleParam
		engine->sendMessage(savedCommand, savedData, handlerId, moduleParam, 5);
	}
}

} // End of namespace Hypno
