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

#ifndef HYPNO_TEACHER_HANDLER_H
#define HYPNO_TEACHER_HANDLER_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Message.h"

namespace Hypno {

class Handler : public Parser {
public:
	Handler();
	~Handler() override;

	virtual void init(SC_Message *msg);
	virtual int addMessage(SC_Message *msg);
	virtual int shutDown(SC_Message *msg);
	virtual void update(int param1, int param2);
	virtual int deinit(SC_Message *msg);
	virtual void onInput();

	void copyCommandData(SC_Message *msg);
	int writeMessageAddress(SC_Message *msg);

	int handlerId;      // identifies the handler type
	int targetAddress;
	int sourceAddress;
	int command;
	int data;
	int moduleParam;    // handler-specific parameter
	int savedCommand;   // saved command from CopyCommandData
	int savedData;      // saved data from CopyCommandData
};

} // End of namespace Hypno

#endif
