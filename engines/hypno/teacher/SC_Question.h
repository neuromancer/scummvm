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

#ifndef HYPNO_TEACHER_SC_QUESTION_H
#define HYPNO_TEACHER_SC_QUESTION_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Message.h"
#include "hypno/teacher/Sprite.h"
#include "common/list.h"
#include "common/str.h"

namespace Hypno {

class TeacherQuestion : public Parser {
public:
	TeacherQuestion(uint32 id);
	~TeacherQuestion() override;

	int lblParse(const Common::String &line) override;
	void update(int x, int y);
	void finalize();

	uint32 questionId;
	Common::String label;
	Common::List<SC_Message *> messageQueue;
	Common::List<Sprite *> overlaySprites;  // Original: mouseControl (MMPlayer from OVERLAYS)
	int state; // 0 = active, 1 = selected, 2 = finished
};

} // End of namespace Hypno

#endif
