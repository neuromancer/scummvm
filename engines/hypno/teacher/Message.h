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

#ifndef HYPNO_TEACHER_MESSAGE_H
#define HYPNO_TEACHER_MESSAGE_H

#include "hypno/teacher/Parser.h"
#include "common/rect.h"

namespace Hypno {

class SC_Message : public Parser {
public:
	int targetAddress;  // destination handler address
	int sourceAddress;  // source/from address
	int command;        // message type/command code
	int data;           // associated data
	int priority;       // queue priority
	int param1;
	int param2;
	Common::Point clickPos; // click position
	int mouseX;         // current mouse X
	int mouseY;         // current mouse Y
	int lastKey;        // last key pressed
	int timeStamp;      // time value
	int userPtr;        // user pointer (param8)

	SC_Message(int targetAddress, int sourceAddress, int command, int data, int priority, int param1, int param2, int userPtr, int clickX, int clickY);
	~SC_Message() override;

	int lblParse(const Common::String &line) override;

	void dump(int unused);
};

} // End of namespace Hypno

#endif
