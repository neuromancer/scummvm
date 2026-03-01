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

#ifndef HYPNO_TEACHER_OPTIONMENU_H
#define HYPNO_TEACHER_OPTIONMENU_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Sprite.h"
#include "common/array.h"
#include "common/rect.h"

namespace Hypno {

// Each entry in the option menu list represents a submenu option
// with a visual state and a hit-test rectangle.
struct OptionMenuEntry {
	int state;
	Common::Rect rect;

	OptionMenuEntry() : state(0), rect(0xda, 0xcc, 0x1b3, 0xee) {}
};

class OptionMenu : public Parser {
public:
	OptionMenu();
	~OptionMenu() override;

	int lblParse(const Common::String &line) override;

	// Render the current character's submenu options
	void render(int characterIndex);

	// Hit-test the mouse position against submenu option rects
	// Returns true if a hit was found. Sets indexOut to the index
	// and hitOut to 0 on hit, 1 on miss.
	bool hitTest(Common::Point pt, int *indexOut, int *hitOut);

	// Set the state for an option entry. If submenuIndex == -5,
	// sets all entries to the given state.
	void setOptionState(int submenuIndex, int state);

	// Select a character's option set (switches which entries are displayed)
	void selectCharacter(int characterIndex);

	// Stop option sprite animations
	void deactivate();

	Sprite *options[3];     // Option sprites for each character
	int selectedOption;     // Max displayed options

	// Per-character option entries
	Common::Array<OptionMenuEntry> characterEntries[3];
	int activeCharacter;    // Which character's entries are active
};

} // End of namespace Hypno

#endif
