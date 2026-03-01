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

#include "hypno/teacher/IconBar.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"

namespace Hypno {

// Original: IconBarButton::IconBarButton at 0x402CD0
IconBarButton::IconBarButton()
	: sprite(nullptr), message(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
	  enabled(0), clickSound(nullptr) {
}

// Original: IconBarButton::~IconBarButton at 0x402D60
IconBarButton::~IconBarButton() {
	delete sprite;
	delete clickSound;
}

// Original: IconBar::IconBar at 0x402730
IconBar::IconBar() : iconbarSprite(nullptr) {
	// Set icon bar bounds: (0, 0x1ab, 0x280, 0x1e0)
	barBounds = Common::Rect(0, 0x1ab, 0x280, 0x1e0);

	// Create iconbar background sprite
	iconbarSprite = new Sprite("elements/iconbar.smk");
	iconbarSprite->flags &= ~2;
	iconbarSprite->loc_x = 0;
	iconbarSprite->loc_y = 0x1ab;
	iconbarSprite->initStates(4);
	iconbarSprite->setRange(0, 1, 1);
	iconbarSprite->setRange(1, 2, 2);
	iconbarSprite->setRange(2, 3, 3);
	iconbarSprite->setRange(3, 4, 4);
	iconbarSprite->priority = 1000;

	// Create button sprites with positions from original
	buttons[0].sprite = new Sprite("elements/choice.smk");
	buttons[0].sprite->loc_x = 0x5e;
	buttons[0].sprite->loc_y = 0x1b5;

	buttons[1].sprite = new Sprite("elements/backpack.smk");
	buttons[1].sprite->loc_x = 0xad;
	buttons[1].sprite->loc_y = 0x1b5;

	buttons[2].sprite = new Sprite("elements/aliencom.smk");
	buttons[2].sprite->loc_x = 0x115;
	buttons[2].sprite->loc_y = 0x1b4;

	buttons[3].sprite = new Sprite("elements/schedule.smk");
	buttons[3].sprite->loc_x = 0x165;
	buttons[3].sprite->loc_y = 0x1b5;

	buttons[4].sprite = new Sprite("elements/mainmenu.smk");
	buttons[4].sprite->loc_x = 0x1d1;
	buttons[4].sprite->loc_y = 0x1b6;

	buttons[5].sprite = new Sprite("elements/quit.smk");
	buttons[5].sprite->loc_x = 0x22a;
	buttons[5].sprite->loc_y = 0x1b6;

	// Configure all 6 buttons: disable flag 2, set 2 states, priority 1001
	for (int i = 0; i < 6; i++) {
		Sprite *btn = buttons[i].sprite;
		btn->flags &= ~2;
		btn->initStates(2);
		btn->setRange(0, 1, 1);
		btn->setRange(1, 2, 2);
		btn->priority = 1001;
	}

	// Set button bounds matching original
	buttons[0].bounds = Common::Rect(0x5e, 0x1b5, 0x9f, 0x1d3);
	buttons[1].bounds = Common::Rect(0xad, 0x1b5, 0xf9, 0x1d6);
	buttons[2].bounds = Common::Rect(0x115, 0x1b4, 0x155, 0x1d6);
	buttons[3].bounds = Common::Rect(0x165, 0x1b5, 0x1ae, 0x1d2);
	buttons[4].bounds = Common::Rect(0x1d1, 0x1b6, 0x207, 0x1d2);
	buttons[5].bounds = Common::Rect(0x22a, 0x1b6, 0x270, 0x1d5);

	// Message templates for buttons 0, 4, 5
	buttons[0].message.targetAddress = 10;
	buttons[0].message.sourceAddress = 1;
	buttons[0].message.command = 6;
	buttons[0].message.priority = 5;

	buttons[4].message.targetAddress = 10;
	buttons[4].message.sourceAddress = 1;
	buttons[4].message.command = 6;
	buttons[4].message.priority = 5;

	buttons[5].message.targetAddress = 2;
	buttons[5].message.priority = 5;
}

IconBar::~IconBar() {
	delete iconbarSprite;
}

// Original: IconBar::Init at 0x402ED0
// Calls Handler::Init (0x417180) which sets command/data from message.
void IconBar::initIconBar(SC_Message *msg) {
	// Handler::Init — save command and data from the init message.
	// These are used by the exit path: SC_Message_Send(command, data, ...)
	if (msg) {
		command = msg->command;
		data = msg->data;
	}
	copyCommandData(msg);

	// Set iconbar state based on selected character
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	if (engine->_selectedCharacter != nullptr && iconbarSprite != nullptr) {
		iconbarSprite->setState2(engine->_selectedCharacter->characterType + 1);
	}

	// Enable all buttons
	for (int i = 0; i < 6; i++) {
		buttons[i].enabled = 1;
	}

	// Register click sounds for buttons 1-3
	if (buttons[1].clickSound == nullptr) {
		buttons[1].clickSound = new Sample();
		buttons[1].clickSound->Load("audio/Snd0023.wav");
	}
	if (buttons[2].clickSound == nullptr) {
		buttons[2].clickSound = new Sample();
		buttons[2].clickSound->Load("audio/Snd0024.wav");
	}
	if (buttons[3].clickSound == nullptr) {
		buttons[3].clickSound = new Sample();
		buttons[3].clickSound->Load("audio/Snd0025.wav");
	}
}

// Original: IconBar::ShutDown at 0x402FD0
void IconBar::cleanupIconBar(SC_Message *msg) {
	// Stop iconbar sprite
	if (iconbarSprite != nullptr) {
		iconbarSprite->setState2(-1);
	}

	// Stop button sprites
	for (int i = 0; i < 6; i++) {
		if (buttons[i].sprite != nullptr) {
			buttons[i].sprite->setState2(-1);
		}
	}

	// Stop all sounds
	for (int i = 0; i < 6; i++) {
		if (buttons[i].clickSound != nullptr)
			buttons[i].clickSound->Stop();
	}
}

// Original: IconBar::AddMessage at 0x403040
int IconBar::checkButtonClick(SC_Message *msg) {
	if (!barBounds.contains(msg->clickPos))
		return 0;

	if (msg->mouseX >= 2) {
		for (int i = 0; i < 6; i++) {
			if (buttons[i].enabled != 0 && buttons[i].bounds.contains(msg->clickPos)) {
				playButtonSound(i);

				// Copy button message template fields to msg
				SC_Message *pSrc = &buttons[i].message;
				msg->targetAddress = pSrc->targetAddress;
				msg->sourceAddress = pSrc->sourceAddress;
				msg->command = pSrc->command;
				msg->data = pSrc->data;
				msg->priority = pSrc->priority;
				msg->param1 = pSrc->param1;
				msg->param2 = pSrc->param2;
				msg->clickPos.x = pSrc->clickPos.x;
				msg->clickPos.y = pSrc->clickPos.y;
				msg->mouseX = pSrc->mouseX;
				msg->mouseY = pSrc->mouseY;
				msg->lastKey = pSrc->lastKey;
				msg->timeStamp = pSrc->timeStamp;
				msg->userPtr = pSrc->userPtr;
				return 1;
			}
		}
	}

	return 1;
}

// Original: IconBar::PlayButtonSound at 0x403300
void IconBar::playButtonSound(int buttonIndex) {
	// Stop all button sounds first
	for (int i = 0; i < 6; i++) {
		if (buttons[i].clickSound != nullptr)
			buttons[i].clickSound->Stop();
	}

	if (buttonIndex != -1 && buttonIndex >= 0 && buttonIndex < 6) {
		if (buttons[buttonIndex].clickSound != nullptr)
			buttons[buttonIndex].clickSound->Play(100, 1);
	}
}

// Original: IconBar::Update at 0x403230
void IconBar::update(int param1, int param2) {
	if (handlerId != param2)
		return;

	TeacherEngine *engine = (TeacherEngine *)g_engine;
	Common::Point mousePos = engine->getMousePos();
	int mx = mousePos.x;
	int my = mousePos.y;

	// Draw iconbar background
	if (iconbarSprite != nullptr) {
		iconbarSprite->Do(iconbarSprite->loc_x, iconbarSprite->loc_y, 1.0);
	}

	// Draw each enabled button with hover state
	for (int i = 0; i < 6; i++) {
		if (buttons[i].sprite != nullptr) {
			if (buttons[i].bounds.contains(mx, my)) {
				buttons[i].sprite->setState2(1);
			} else {
				buttons[i].sprite->setState2(0);
			}

			if (buttons[i].enabled != 0) {
				buttons[i].sprite->Do(buttons[i].sprite->loc_x, buttons[i].sprite->loc_y, 1.0);
			}
		}
	}
}

} // End of namespace Hypno
