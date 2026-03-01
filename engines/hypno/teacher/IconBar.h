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

#ifndef HYPNO_TEACHER_ICONBAR_H
#define HYPNO_TEACHER_ICONBAR_H

#include "hypno/teacher/Handler.h"
#include "hypno/teacher/Message.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Sample.h"
#include "common/rect.h"
#include "common/str.h"

namespace Hypno {

// Original: IconBarButton at size 0xE0 bytes
// Contains an embedded SC_Message as the message template (not a pointer)
struct IconBarButton {
	Sprite *sprite;            // Button animation sprite
	SC_Message message;        // Embedded message template (copied to msg on click)
	Common::Rect bounds;       // Click detection bounds
	int enabled;               // Whether button is active
	Sample *clickSound;        // Click sound sample

	IconBarButton();
	~IconBarButton();
};

// Original: IconBar at size 0x600 bytes
// Inherits from Handler, adds icon bar UI with 6 buttons
class IconBar : public Handler {
public:
	IconBar();
	~IconBar() override;

	void initIconBar(SC_Message *msg);
	void cleanupIconBar(SC_Message *msg);
	int checkButtonClick(SC_Message *msg);
	void playButtonSound(int buttonIndex);
	void update(int param1, int param2) override;

	Common::Rect barBounds;
	Sprite *iconbarSprite;      // The icon bar background sprite
	IconBarButton buttons[6];
};

} // End of namespace Hypno

#endif
