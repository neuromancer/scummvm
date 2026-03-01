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

#ifndef HYPNO_TEACHER_DEMO_SCI_DIALOG_H
#define HYPNO_TEACHER_DEMO_SCI_DIALOG_H

#include "hypno/teacher/IconBar.h"
#include "hypno/teacher/SC_Question.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/demo/SCI_SearchScreen.h"
#include "common/list.h"

namespace Hypno {

// Original: SCI_Dialog at 0x650 bytes
// Inherits from IconBar. Handler ID 9.
class Handler9 : public IconBar {
public:
	Handler9();
	~Handler9() override;

	void init(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	int addMessage(SC_Message *msg) override;
	int deinit(SC_Message *msg) override;
	void update(int param1, int param2) override;
	int lblParse(const Common::String &line) override;

	TeacherQuestion *getQuestionByIndex(int index);
	TeacherQuestion *findQuestionById(int id);
	void insertQuestionSorted(TeacherQuestion *q);

	Common::List<TeacherQuestion *> _questions;  // field_610: Queue of dialog questions
	TeacherQuestion *_activeQuestion;            // field_614: Current active question
	Sprite *_buttonSprite;                       // field_608: Button sprite (unselected)
	Sprite *_highlightSprite;                    // field_60C: Highlight sprite (selected)
	int _states[10];                             // field_618: Ambient controller states

	// Borrowed from SearchScreen (Handler11) — NOT owned, do not delete
	Handler11 *_searchScreen;                    // field_600/604: background + ambient source
};

} // End of namespace Hypno

#endif
