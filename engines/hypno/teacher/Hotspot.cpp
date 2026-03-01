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

#include "hypno/teacher/Hotspot.h"
#include "hypno/teacher/teacher.h"

namespace Hypno {

TeacherHotspot::TeacherHotspot() : state(1), enabled(1), sprite(nullptr), dialogFlag(0), dialogParseFile(-1), parseFileIndex(-1) {
}

TeacherHotspot::~TeacherHotspot() {
	delete sprite;
	for (auto s : preSprites) delete s;
	for (auto s : midSprites) delete s;
	for (auto s : postSprites) delete s;
}

// Helper to draw a list of sprites and check if all have finished their animations
static int drawSpriteList(Common::List<Sprite *> &list) {
	int finished = 1;
	for (auto s : list) {
		if (s->Do(s->loc_x, s->loc_y, 1.0) == 0)
			finished = 0;
	}
	return finished;
}

int TeacherHotspot::update(Common::List<Sprite *> &bg, Common::List<Sprite *> &amb, int sourceAddr) {
	switch (state) {
	case 1: // Pre-action animation
		if (preSprites.empty() || drawSpriteList(preSprites)) {
			state = 2;
		}
		if (state != 2) return 0;
		// Fall through
	case 2: // Mid-action animation / Trigger dialog
		if (!midSprites.empty()) {
			if (drawSpriteList(midSprites)) {
				state = 3;
				return 0;
			}
		} else {
			state = 3;
			if (dialogFlag) {
				// Send message to SCI_Dialog (9)
				SC_Message_Send(9, parseFileIndex, 11, sourceAddr, 5, dialogParseFile, 0, 0, 0, 0);
				// In original, userPtr would pass pointers to current players, but Dialog can find them
			}
		}
		return 0;
	case 3: // Post-action animation
		if (!postSprites.empty()) {
			if (drawSpriteList(postSprites)) {
				state = 1;
				return 1; // Sequence finished
			}
		} else {
			state = 1;
			return 1; // Sequence finished
		}
		return 0;
	default:
		return 1;
	}
}

void TeacherHotspot::Do() {
	if (sprite) {
		sprite->Do(sprite->loc_x, sprite->loc_y, 1.0);
	}
}

void TeacherHotspot::SetState(int newState) {
	if (sprite) {
		sprite->setState2(newState);
	}
}

int TeacherHotspot::GetState() {
	if (sprite) {
		return sprite->current_state;
	}
	return 0;
}

// Original: T_Hotspot::Exit (0x4094A0)
// Stops animation sounds but does NOT change visual state.
void TeacherHotspot::Exit() {
	if (sprite) sprite->stopAnimationSound();
	for (auto s : preSprites) s->stopAnimationSound();
	for (auto s : midSprites) s->stopAnimationSound();
	for (auto s : postSprites) s->stopAnimationSound();
}

int TeacherHotspot::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "RECT") {
		int l, t, r, b;
		if (sscanf(rest.c_str(), "%d %d %d %d", &l, &t, &r, &b) == 4) {
			rect = Common::Rect(l, t, r, b);
		}
	} else if (keyword == "LABEL") {
		label = rest;
	} else if (keyword == "MOUSE") {
		mouse = rest;
	} else if (keyword == "SPRITE") {
		if (!sprite) sprite = new Sprite();
		Parser::processFile(sprite, this, "");
	} else if (keyword == "SPRITE1") {
		Sprite *s = new Sprite();
		Parser::processFile(s, this, "");
		preSprites.push_back(s);
	} else if (keyword == "SPRITE2") {
		Sprite *s = new Sprite();
		Parser::processFile(s, this, "");
		midSprites.push_back(s);
	} else if (keyword == "SPRITE3") {
		Sprite *s = new Sprite();
		Parser::processFile(s, this, "");
		postSprites.push_back(s);
	} else if (keyword == "DIALOG") {
		dialogFlag = 1;
	} else if (keyword == "DIALOGPARSEFILENUMBER") {
		dialogParseFile = atoi(rest.c_str());
	} else if (keyword == "PARSEFILEINDEX") {
		parseFileIndex = atoi(rest.c_str());
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

} // End of namespace Hypno
