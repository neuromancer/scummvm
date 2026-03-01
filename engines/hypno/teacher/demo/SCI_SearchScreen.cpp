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

#include "hypno/teacher/demo/SCI_SearchScreen.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"

namespace Hypno {

// Original: SCI_SearchScreen constructor (0x40ACC0)
Handler11::Handler11() : activeHotspot(nullptr), hotspotCount(0), activeCount(0), exitTimer(0), music(nullptr) {
	handlerId = 11;
	sourceAddress = 1;  // Original sets sourceAddress = 1
	for (int i = 0; i < 10; i++) hotspots[i] = nullptr;
	// Original parses the MIS file in the constructor, not in Init()
	Parser::parseFile(this, "mis/SrSc0001.mis", "");
	debugC(1, kHypnoDebugScene, "H11: parsed %d bg sprites, %d ambient sprites, %d hotspots",
		(int)backgroundSprites.size(), (int)ambientSprites.size(), hotspotCount);
	for (int i = 0; i < hotspotCount; i++) {
		if (hotspots[i]) {
			debugC(1, kHypnoDebugScene, "H11: hotspot[%d] rect=(%d,%d,%d,%d) dialog=%d enabled=%d",
				i, hotspots[i]->rect.left, hotspots[i]->rect.top,
				hotspots[i]->rect.right, hotspots[i]->rect.bottom,
				hotspots[i]->dialogFlag, hotspots[i]->enabled);
		}
	}
}

Handler11::~Handler11() {
	for (auto s : backgroundSprites) delete s;
	for (auto s : ambientSprites) delete s;
	for (int i = 0; i < 10; i++) delete hotspots[i];
	delete music;
}

// Original: SCI_SearchScreen::Init (0x40AE80)
void Handler11::init(SC_Message *msg) {
	initIconBar(msg);

	// Original: if (msg->param1 == 5) field_634 = 0;
	if (msg->param1 == 5) activeHotspot = nullptr;
	// Original: if (msg->param1 == 6) { field_634->enabled = 0; field_634 = 0; }
	if (msg->param1 == 6) {
		if (activeHotspot) activeHotspot->enabled = 0;
		activeHotspot = nullptr;
	}

	// Count active hotspots
	activeCount = 0;
	for (int i = 0; i < 10; i++) {
		if (hotspots[i] && hotspots[i]->enabled)
			activeCount++;
	}

	// Set palette from MIS file
	if (!_palettePath.empty()) {
		((TeacherEngine *)g_engine)->loadPalette(_palettePath);
	}

	// Start music
	delete music;
	music = new Sample();
	music->Load("audio/Snd0027.wav");
	music->Play(100, 0); // Loop

	exitTimer = 0;
}

int Handler11::shutDown(SC_Message *msg) {
	// Original: conditionally stops background/ambient
	// When transitioning to Dialog (handler 9), keep them alive since Dialog borrows them
	if (!msg || msg->targetAddress != 9) {
		for (auto s : backgroundSprites) s->stopAnimationSound();
		for (auto s : ambientSprites) s->stopAnimationSound();
	}

	// Original: conditionally stops music
	// if ((msg == 0 || msg->command == 6) || field_640 == 0)
	if (!msg || msg->command == 6) {
		if (music) music->Stop();
	}

	for (int i = 0; i < 10; i++) if (hotspots[i]) hotspots[i]->Exit();

	cleanupIconBar(msg);
	return 0;
}

int Handler11::addMessage(SC_Message *msg) {
	if (checkButtonClick(msg)) return 1;
	if (msg->mouseX >= 2 && !activeHotspot) {
		int idx = findControlAtMouse();
		if (idx >= 0) {
			activeHotspot = hotspots[idx];
		}
	}
	return 1;
}

int Handler11::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

void Handler11::update(int param1, int param2) {
	if (handlerId != param2) return;
	IconBar::update(param1, param2);

	for (auto s : backgroundSprites) {
		int r = s->Do(s->loc_x, s->loc_y, 1.0);
		debugC(5, kHypnoDebugScene, "H11 bg sprite '%s' Do() returned %d, curFrame=%d, hasLast=%d",
			s->_filename.c_str(), r,
			s->animation_data ? s->animation_data->getCurFrame() : -99,
			s->animation_data ? (int)s->animation_data->hasLastFrame() : -1);
	}
	for (auto s : ambientSprites) s->Do(s->loc_x, s->loc_y, 1.0);

	if (activeHotspot) {
		if (activeHotspot->update(backgroundSprites, ambientSprites, sourceAddress)) {
			activeHotspot = nullptr;
			activeCount = 0;
			for (int i = 0; i < 10; i++) {
				if (hotspots[i] && hotspots[i]->enabled)
					activeCount++;
			}
		}
		return;
	}

	if (activeCount > 0) {
		int idx = findControlAtMouse();
		if (idx >= 0) {
			((TeacherEngine *)g_engine)->setCursor(1);
		} else {
			((TeacherEngine *)g_engine)->setCursor(0);
		}
	} else {
		exitTimer++;
		debugC(1, kHypnoDebugScene, "H11: activeCount=0, exitTimer=%d", exitTimer);
		if (exitTimer >= 60) {
			debugC(1, kHypnoDebugScene, "H11: EXIT TIMER TRIGGERED, transitioning to Handler 10");
			SC_Message_Send(10, 1, 11, 1, 5, 0, 0, 0, 0, 0);
		}
	}
}

int Handler11::findControlAtMouse() {
	Common::Point mousePos = ((TeacherEngine *)g_engine)->getMousePos();
	for (int i = 0; i < 10; i++) {
		if (hotspots[i] && hotspots[i]->enabled && hotspots[i]->rect.contains(mousePos)) {
			return i;
		}
	}
	return -1;
}

int Handler11::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "BACKGROUND") {
		// BACKGROUND is a section containing SPRITE sub-blocks (original: creates MMPlayer)
		while (_file && !_file->eos()) {
			Common::String subLine = readLine();
			Common::String subKw, subRest;
			tokenize(subLine, subKw, subRest);
			if (subKw.empty()) continue;
			if (subKw == "END") break;
			if (subKw == "SPRITE") {
				Sprite *s = new Sprite();
				Parser::processFile(s, this, "");
				backgroundSprites.push_back(s);
			}
		}
	} else if (keyword == "AMBIENTS") {
		// AMBIENTS is a section containing SPRITE sub-blocks (original: creates MMPlayer2)
		while (_file && !_file->eos()) {
			Common::String subLine = readLine();
			Common::String subKw, subRest;
			tokenize(subLine, subKw, subRest);
			if (subKw.empty()) continue;
			if (subKw == "END") break;
			if (subKw == "SPRITE") {
				Sprite *s = new Sprite();
				Parser::processFile(s, this, "");
				ambientSprites.push_back(s);
			}
		}
	} else if (keyword == "PALE") {
		_palettePath = rest;
		for (uint32 i = 0; i < _palettePath.size(); ++i)
			if (_palettePath[i] == '\\') _palettePath[i] = '/';
	} else if (keyword == "HOTSPOT") {
		if (hotspotCount < 10) {
			TeacherHotspot *h = new TeacherHotspot();
			hotspots[hotspotCount++] = h;
			Parser::processFile(h, this, "");
		}
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

} // End of namespace Hypno
