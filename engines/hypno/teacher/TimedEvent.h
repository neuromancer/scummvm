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

#ifndef HYPNO_TEACHER_TIMEDEVENT_H
#define HYPNO_TEACHER_TIMEDEVENT_H

#include "hypno/teacher/Message.h"
#include "hypno/teacher/SC_Timer.h"

namespace Hypno {

class TimedEvent {
public:
	TimedEvent();
	~TimedEvent();

	uint32 update();

	int type;
	int sourceAddress;
	uint32 duration;
	SC_Message *eventData;
	TeacherTimer timer;
};

} // End of namespace Hypno

#endif
