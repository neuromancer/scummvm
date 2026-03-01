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

#ifndef HYPNO_TEACHER_DEMO_SCI_SEARCHSCREEN_H
#define HYPNO_TEACHER_DEMO_SCI_SEARCHSCREEN_H

#include "hypno/teacher/IconBar.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Hotspot.h"
#include "hypno/teacher/Sample.h"
#include "common/list.h"

namespace Hypno {

class Handler11 : public IconBar {
public:
	Handler11();
	~Handler11() override;

	void init(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	int addMessage(SC_Message *msg) override;
	int deinit(SC_Message *msg) override;
	void update(int param1, int param2) override;
	int lblParse(const Common::String &line) override;

	int findControlAtMouse();

	Common::List<Sprite *> backgroundSprites;
	Common::List<Sprite *> ambientSprites;
	TeacherHotspot *hotspots[10];
	TeacherHotspot *activeHotspot;
	int hotspotCount;
	int activeCount;
	int exitTimer;

	Sample *music;
	Common::String _palettePath;
};

} // End of namespace Hypno

#endif
