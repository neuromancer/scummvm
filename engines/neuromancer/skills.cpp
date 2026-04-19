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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#include "neuromancer/skills.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/scene_real_world.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// Skills window geometry. DOS g_open_frame_data's final frame is
// (216, 64, 48, 128) -- width, height, left, top. So the window lives
// at (48, 128) and is 216 x 64 px.
enum {
	kWindowX        = 48,
	kWindowY        = 128,
	kWindowWidthPx  = 216,
	kWindowHeightPx = 64,
	kWindowPackedW  = kWindowWidthPx / 2,
	kWindowBytes    = kWindowPackedW * kWindowHeightPx
};

// Skill names (parallel to the trainable-skill slice of the item-name
// table, item codes 0x43..0x52). Transcribed from DOS items.c:60-75.
static const char *const kSkillNames[16] = {
	"Bargaining", "CopTalk", "Warez Analysis", "Debug",
	"Hardware Repair", "ICE Breaking", "Evasion", "Cryptology",
	"Japanese", "Logic", "Psychoanalysis", "Phenomenology",
	"Philosophy", "Sophistry", "Zen", "Musicianship"
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

} // anonymous namespace

Skills::Skills(NeuromancerEngine *engine, RealWorldScene *scene)
	: _engine(engine),
	  _scene(scene),
	  _active(false),
	  _state(kStateList),
	  _pageStart(0),
	  _pageCount(0),
	  _selectedSkill(0) {
	_sprite.resize(sizeof(ImhHeader) + kWindowBytes);
	writeImhHeader(_sprite.data(), 0, 0, kWindowPackedW, kWindowHeightPx);
	for (int i = 0; i < 4; i++) _pageSkills[i] = -1;
}

void Skills::open() {
	_active    = true;
	_state     = kStateList;
	_pageStart = 0;
	rebuildPage();
	if (_pageCount == 0)
		drawNoSkills();
	else
		drawList();
	debugC(1, kDebugGeneral, "Skills: opened (%d acquired)", _pageCount);
}

void Skills::close() {
	_active = false;
	_engine->spriteChain()->clearSprite(kLayerPaxWindow);
	debugC(1, kDebugGeneral, "Skills: closed");
}

void Skills::update() {
	// Static view -- no per-frame animation in Phase 1.
}

// Collect the player's acquired skills (skills[i] != 0xFF) starting at
// _pageStart, up to 4 per page. Returns _pageCount for convenience.
int Skills::rebuildPage() {
	_pageCount = 0;
	for (int i = 0; i < 4; i++) _pageSkills[i] = -1;

	const uint8 *sk = _scene->skills();
	int seen = 0;
	for (int i = 0; i < 16 && _pageCount < 4; i++) {
		if (sk[i] == 0xFF)
			continue;
		if (seen < _pageStart) {
			seen++;
			continue;
		}
		_pageSkills[_pageCount++] = i;
	}
	return _pageCount;
}

bool Skills::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_KEYDOWN) {
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			switch (_state) {
			case kStateList:
			case kStateNoSkills:
				close();
				break;
			case kStateDescription:
			default:
				drawList();
				break;
			}
			return true;
		}

		// No-skills screen: any key closes.
		if (_state == kStateNoSkills) {
			close();
			return true;
		}

		// Description screen: any key returns to the list.
		if (_state == kStateDescription) {
			drawList();
			return true;
		}

		char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));
		if (_state == kStateList)
			return dispatchList(key);
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		int x = event.mouse.x;
		int y = event.mouse.y;

		if (_state == kStateNoSkills) {
			close();
			return true;
		}
		if (_state == kStateDescription) {
			drawList();
			return true;
		}
		if (_state == kStateList) {
			// Skill rows: y = kWindowY + 8 + i*8 for i in 0..3. Each row
			// is full-width within the window.
			for (int i = 0; i < _pageCount; i++) {
				int rowTop    = kWindowY + 8 + i * 8;
				int rowBottom = rowTop + 7;
				if (x >= kWindowX && x <= kWindowX + kWindowWidthPx - 1 &&
				    y >= rowTop && y <= rowBottom)
					return dispatchList((char)('1' + i));
			}
			// Footer row at y = kWindowY + 40 ("exit" / "more").
			int footerTop    = kWindowY + 40;
			int footerBottom = footerTop + 7;
			if (y >= footerTop && y <= footerBottom) {
				// exit is on the left portion, more on the right.
				if (x >= kWindowX + 48 && x <= kWindowX + 80)
					return dispatchList('x');
				if (x >= kWindowX + 96 && x <= kWindowX + 128)
					return dispatchList('m');
			}
		}
	}

	return false;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void Skills::drawWindowFrame() {
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	// DOS build_text_frame: black top/bottom, white interior with thin
	// black column on each vertical edge.
	const int packedW = kWindowPackedW;
	for (int row = 0; row < (int)kWindowHeightPx; row++) {
		byte *line = pixels + row * packedW;
		if (row == 0 || row == (int)kWindowHeightPx - 1) {
			memset(line, 0x00, packedW);
		} else {
			memset(line, 0xFF, packedW);
			line[0]           = 0x0F;
			line[packedW - 1] = 0xF0;
		}
	}
}

void Skills::drawList() {
	_state = kStateList;
	rebuildPage();
	drawWindowFrame();

	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	// Header centred at cell (9, 0) in DOS (pixels 72, 0 relative). Our
	// window is slightly narrower than DOS neuro_menu's internal buffer,
	// so keep things inside the padded interior.
	drawString("SKILLS", kWindowWidthPx, kWindowHeightPx, 88, 0, pixels);

	const uint8 *sk = _scene->skills();
	for (int i = 0; i < _pageCount; i++) {
		int sidx = _pageSkills[i];
		uint8 level = sk[sidx];
		// DOS displays level + 1 -- skills[i] == 0 means "level 1".
		Common::String row = Common::String::format("%d. %-20s %2u",
		                                             i + 1,
		                                             kSkillNames[sidx],
		                                             level + 1);
		drawString(row.c_str(), kWindowWidthPx, kWindowHeightPx,
		           8, 8 + i * 8, pixels);
	}

	// Footer: always show "exit"; add "more" when paging is available.
	drawString("exit", kWindowWidthPx, kWindowHeightPx, 56, 40, pixels);

	// countAllAcquired == _pageStart + _pageCount + remaining-after-page.
	// We recompute the total here since rebuildPage stops once _pageCount
	// hits 4.
	int total = 0;
	for (int i = 0; i < 16; i++)
		if (sk[i] != 0xFF) total++;
	if (total > _pageStart + _pageCount)
		drawString("more", kWindowWidthPx, kWindowHeightPx, 104, 40, pixels);

	pushSprite();
}

void Skills::drawNoSkills() {
	_state = kStateNoSkills;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	drawString("No skills.", kWindowWidthPx, kWindowHeightPx, 80, 24, pixels);
	pushSprite();
}

// Show a skill's name + current level and a placeholder for the "apply"
// flow. Clicking / pressing a key bounces back to the list.
void Skills::drawDescription() {
	_state = kStateDescription;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	const uint8 *sk = _scene->skills();
	uint8 level = sk[_selectedSkill];

	Common::String head = Common::String::format(
		"%s  Level %u\n\n"
		"(No active effect yet.)\n"
		"\n"
		"Press any key to return.",
		kSkillNames[_selectedSkill],
		(uint)(level + 1));
	drawString(head.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

void Skills::pushSprite() {
	_engine->spriteChain()->addSprite(kLayerPaxWindow, kWindowX, kWindowY,
	                                  _sprite.data(), /*opaque=*/true);
}

// -------------------------------------------------------------------------
// Dispatch
// -------------------------------------------------------------------------

bool Skills::dispatchList(char code) {
	switch (code) {
	case 'x':
		close();
		return true;
	case 'm': {
		// Advance past the current page. Wrap back to 0 when we fall
		// off the end. DOS uses `listed %= g_skills_total` for the same
		// effect (rw_state_skills.c:104).
		int total = 0;
		const uint8 *sk = _scene->skills();
		for (int i = 0; i < 16; i++)
			if (sk[i] != 0xFF) total++;
		if (total == 0) return true;
		_pageStart += _pageCount;
		if (_pageStart >= total) _pageStart = 0;
		drawList();
		return true;
	}
	case '1': case '2': case '3': case '4': {
		int idx = code - '1';
		if (idx >= _pageCount) return false;
		_selectedSkill = _pageSkills[idx];
		drawDescription();
		return true;
	}
	default:
		return false;
	}
}

} // End of namespace Neuromancer
