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

#ifndef HYPNO_TEACHER_DEMO_HANDLER2_H
#define HYPNO_TEACHER_DEMO_HANDLER2_H

#include "hypno/teacher/Handler.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Palette.h"

namespace Hypno {

class Handler2 : public Handler {
public:
	Handler2();
	~Handler2() override;

	void init(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	int addMessage(SC_Message *msg) override;
	int deinit(SC_Message *msg) override;
	void update(int param1, int param2) override;

	Common::String _palettePath;
	Common::String _spritePath;
	Common::String _samplePath;
	Sprite *sprite;
	TeacherPalette *palette;
};

} // End of namespace Hypno

#endif
