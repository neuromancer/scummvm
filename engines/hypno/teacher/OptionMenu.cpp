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

#include "hypno/teacher/OptionMenu.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/debug.h"

namespace Hypno {

OptionMenu::OptionMenu() : selectedOption(5), activeCharacter(-1) {
	// Create the three option sprites matching original behavior
	// Original AFTER.MIS: OPTION_GRAPHICS uses teachdat\option.smk with 4 states
	// State 0: frame 1 (unselected), State 1: frame 2 (selected),
	// State 2: frames 3-12 (hover anim), State 3: frame 13 (hover static)
	for (int i = 0; i < 3; i++) {
		char fname[64];
		Common::sprintf_s(fname, "demo/option%d.smk", i + 1);
		options[i] = new Sprite(fname);
		options[i]->loc_x = 216;
		options[i]->loc_y = 202;
		options[i]->priority = 10;
		// Set up 4 states matching the original
		options[i]->initStates(4);
		options[i]->setRange(0, 1, 1);
		options[i]->setRange(1, 2, 2);
		options[i]->setRange(2, 3, 12);
		options[i]->setRange(3, 13, 13);
	}

	// Initialize per-character entries.
	// The original uses a linked list from Character::queue;
	// for the demo, each character has a fixed set of submenu options.
	// The demo has submenu options with fixed hit rects at (0xda, 0xcc) to (0x1b3, 0xee).
	for (int c = 0; c < 3; c++) {
		// Each character has one submenu option in the demo
		OptionMenuEntry entry;
		entry.state = 0;
		entry.rect = Common::Rect(0xda, 0xcc, 0x1b3, 0xee);
		characterEntries[c].push_back(entry);
	}
}

OptionMenu::~OptionMenu() {
	for (int i = 0; i < 3; i++) {
		delete options[i];
		options[i] = nullptr;
	}
}

int OptionMenu::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword == "END")
		return 1;
	return 0;
}

void OptionMenu::render(int characterIndex) {
	if (activeCharacter < 0 || activeCharacter > 2)
		return;

	Common::Array<OptionMenuEntry> &entries = characterEntries[activeCharacter];
	int count = (int)entries.size();
	if (count > selectedOption)
		count = selectedOption;

	for (int i = 0; i < count; i++) {
		if (characterIndex >= 0 && characterIndex <= 2 && options[characterIndex]) {
			options[characterIndex]->setState2(entries[i].state);
			options[characterIndex]->Do(options[characterIndex]->loc_x, options[characterIndex]->loc_y, 1.0);
		}
	}
}

bool OptionMenu::hitTest(Common::Point pt, int *indexOut, int *hitOut) {
	if (activeCharacter < 0 || activeCharacter > 2)
		return false;

	Common::Array<OptionMenuEntry> &entries = characterEntries[activeCharacter];
	*hitOut = 1; // miss by default

	for (uint i = 0; i < entries.size(); i++) {
		if (entries[i].rect.contains(pt.x, pt.y)) {
			entries[i].state = 3; // hover state
			*hitOut = 0;
			*indexOut = i;
			return true;
		}
	}
	return false;
}

void OptionMenu::setOptionState(int submenuIndex, int state) {
	if (activeCharacter < 0 || activeCharacter > 2)
		return;

	Common::Array<OptionMenuEntry> &entries = characterEntries[activeCharacter];

	if (submenuIndex == -5) {
		// Set all entries to the given state
		for (uint i = 0; i < entries.size(); i++) {
			entries[i].state = state;
		}
	} else if (submenuIndex >= 0 && submenuIndex < (int)entries.size()) {
		entries[submenuIndex].state = state;
	}
}

void OptionMenu::selectCharacter(int characterIndex) {
	if (characterIndex >= 0 && characterIndex <= 2) {
		activeCharacter = characterIndex;
	}
}

void OptionMenu::deactivate() {
	// Stop option sprite animations
	for (int i = 0; i < 3; i++) {
		if (options[i] && options[i]->animation_data) {
			options[i]->animation_data->close();
		}
	}
}

} // End of namespace Hypno
