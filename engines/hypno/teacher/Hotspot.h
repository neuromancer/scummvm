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

#ifndef HYPNO_TEACHER_HOTSPOT_H
#define HYPNO_TEACHER_HOTSPOT_H

#include "hypno/teacher/Parser.h"
#include "common/rect.h"
#include "common/str.h"
#include "hypno/teacher/Sprite.h"

namespace Hypno {

class TeacherHotspot : public Parser {
public:
	TeacherHotspot();
	~TeacherHotspot() override;

	int lblParse(const Common::String &line) override;

	void Do();              // Render sprite at its location
	void SetState(int newState);  // Set sprite animation state
	int GetState();         // Get current sprite state
	void Exit();            // Stop animations

	// New update method for sequences
	int update(Common::List<Sprite *> &bg, Common::List<Sprite *> &amb, int sourceAddr);

	Common::String label;
	Common::Rect rect;
	int state;
	int enabled;            // Whether this hotspot is active for hit-testing
	Common::String mouse;
	Sprite *sprite;

	// Sequenced animations (former T_Hotspot list1, list2, list3)
	Common::List<Sprite *> preSprites;
	Common::List<Sprite *> midSprites;
	Common::List<Sprite *> postSprites;

	int dialogFlag;         // DIALOG keyword
	int dialogParseFile;    // DIALOGPARSEFILENUMBER
	int parseFileIndex;     // PARSEFILEINDEX
};

} // End of namespace Hypno

#endif
