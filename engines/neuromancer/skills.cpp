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
#include "neuromancer/inventory.h"
#include "neuromancer/music_player.h"
#include "neuromancer/neuro_vm.h"
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

// Skill names map 1:1 to item codes 0x43..0x52 in the DOS item-name
// table (items.c:60-75). Rather than keep a second copy we route
// through Inventory::itemName so edits land in a single place.
static const char *skillName(int idx) {
	if (idx < 0 || idx >= 16) return "?";
	return Inventory::itemName((uint8)(0x43 + idx));
}

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
	  _selectedSkill(0),
	  _crypDecoded(false),
	  _pickerSoftware(false),
	  _pickerPageStart(0),
	  _pickerCount(0) {
	for (int i = 0; i < 4; ++i) _pickerSlots[i] = -1;
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
			case kStateMusicianship:
			case kStateCryptology:
			case kStateCryptologyResult:
			case kStateItemPicker:
			case kStateItemResult:
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

		// Cryptology result screen: any key returns to the skills list.
		if (_state == kStateCryptologyResult) {
			drawList();
			return true;
		}

		// Item-picker result screen: any key returns to the list.
		if (_state == kStateItemResult) {
			drawList();
			return true;
		}

		// Cryptology input captures keystrokes before the generic
		// dispatch so typed ASCII becomes part of the word.
		if (_state == kStateCryptology)
			return dispatchCryptology(event);

		char key = (char)tolower((byte)(event.kbd.ascii & 0x7F));
		if (_state == kStateList)
			return dispatchList(key);
		if (_state == kStateMusicianship)
			return dispatchMusicianship(key);
		if (_state == kStateItemPicker)
			return dispatchItemPicker(key);
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
		                                             skillName(sidx),
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

// Cryptology decode tables (rw_state_skills.c:667-716). The word
// in[i] decodes to out[i] if the player's Cryptology skill level
// meets or exceeds difficulty[i]. Kept as a single const block so
// future skills that need word-matching can reuse the pattern.
static const char *const kCryptoInputs[20] = {
	"DUMBO", "IMASMURF", "SMEEGLDIPO", "ABURAKKOI", "KIKENNA",
	"PANCAKE", "SNORSKEE", "AGABATUR", "EINHOVEN", "VULCAN",
	"SELIM", "GALILEO", "GNU", "TRIDENT", "OGRAF",
	"EGGPLANT", "TURNIP", "PLEIADES", "CAL23LNZD8", "THERMOPYLAE"
};
static const char *const kCryptoOutputs[20] = {
	"ROMCARDS", "WARRANTS", "PERMAFROST", "UCHIKATSU", "PERILOUS",
	"VENDORS", "SUPERTAC", "EINTRITT", "VERBOTEN", "BIOSOFT",
	"BIOTECH", "APOLLO", "YAK", "FUNGEKI", "AUDIT",
	"LONGISLAND", "LOSER", "SUBARU", "NINJUTSU", "SOCRATES"
};
static const uint8 kCryptoDifficulty[20] = {
	0, 0, 1, 1, 1, 0, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 7, 8
};

// Apply the currently-selected skill. DOS rw_state_skills.c:571-615
// writes active_skill / active_skill_level to DSEG and then either
// closes (default skills) or switches to a sub-flow (Warez, Debug,
// HW Repair, Cryptology, Musicianship). We mirror that control flow
// here: specialised flows get their own draw*; the rest commit and
// close immediately so their level scripts can react.
void Skills::drawDescription() {
	// Sub-flow skills go to dedicated states (DOS skills_use switch).
	if (_selectedSkill == 15) {            // MUSICIANSHIP
		drawMusicianship();
		return;
	}
	if (_selectedSkill == 7) {             // CRYPTOLOGY
		_crypTyped.clear();
		_crypResult.clear();
		_crypDecoded = false;
		drawCryptology();
		return;
	}
	// Warez (2) / Debug (3) pick software[]; HW Repair (4) picks items[].
	// DOS skills_use_warez_skill + skills_use_hw_repair open a paged
	// inventory list (rw_state_skills.c:543-557).
	if (_selectedSkill == 2 || _selectedSkill == 3 || _selectedSkill == 4) {
		_pickerSoftware = (_selectedSkill != 4);
		_pickerPageStart = 0;
		drawItemPicker();
		return;
	}

	// Generic skills (Bargaining, CopTalk, Evasion, ICE Breaking,
	// Japanese, Logic, Psychoanalysis, Phenomenology, Philosophy,
	// Sophistry, Zen): commit the skill + level to DSEG and close
	// immediately, matching DOS's fall-through behaviour.
	if (NeuroVM *vm = _engine->vm()) {
		vm->writeVar8(0x4BF4, (uint8)_selectedSkill);       // active_skill
		vm->writeVar8(0x4BF5, _scene->skills()[_selectedSkill]); // level
	}
	close();
}

// Cryptology decode prompt. DOS skills_use_cryptology
// (rw_state_skills.c:507-517) shows "Enter word to decode:" and a
// `<` cursor where the player types.
void Skills::drawCryptology() {
	_state = kStateCryptology;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	Common::String body = Common::String::format(
		"Cryptology\n"
		"\n"
		"Enter word to decode:\n"
		"< %s_",
		_crypTyped.c_str());
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 4, pixels);
	pushSprite();
}

// Result panel after the player hits Enter. Shows the decoded word or
// "Unable to decode word." matches DOS skill_cryptology_apply
// (rw_state_skills.c:718-748).
void Skills::drawCryptologyResult() {
	_state = kStateCryptologyResult;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	Common::String body;
	if (_crypDecoded) {
		body = Common::String::format(
			"Cryptology\n"
			"\n"
			"Uncoded word is:\n"
			"%s\n"
			"\n"
			"Press any key.",
			_crypResult.c_str());
	} else {
		body =
			"Cryptology\n"
			"\n"
			"Unable to decode word.\n"
			"\n"
			"Press any key.";
	}
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 4, pixels);
	pushSprite();
}

// Musicianship sub-menu. DOS rw_state_skills.c:519 presents the 4
// tracks; picking one writes active_skill = MUSICIANSHIP (15),
// active_skill_level = track idx (0..3), and closes the skills window
// so the VM can pick up the new state. Exit with X.
void Skills::drawMusicianship() {
	_state = kStateMusicianship;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	static const char kMenu[] =
		"Musicianship\n"
		"\n"
		"X. Exit Skill Chip\n"
		"1. Play Dub         2. Play Jazz\n"
		"3. Play New Wave    4. Play Classical";
	drawString(kMenu, kWindowWidthPx, kWindowHeightPx, 8, 4, pixels);
	pushSprite();
}

// HW Repair bug-fix difficulty table (DOS g_fix_difficulty at
// rw_state_skills.c:270): skill 0 fixes up to 0x3F, skill 1 up to
// 0x7F, skill 2 up to 0xBF, skill 3+ everything.
static const uint16 kFixDifficulty[4] = { 0x3F, 0x7F, 0xBF, 0xFF };

// DOS plays track 11 on success, track 6 on failure for HW Repair
// (rw_state_skills.c:299,305) and Debug (:381,387). We route through
// the engine's shared MusicPlayer queue.
static void playSkillSfx(NeuromancerEngine *engine, bool success) {
	if (MusicPlayer *mp = engine->music())
		mp->setTrack(success ? 11 : 6);
}

// Warez Analysis category-description table, transcribed from DOS
// rw_state_skills.c:428-440 (g_warez_desc[11]).
static const char *const kWarezDesc[11] = {
	"Unknown.",
	"A cyberspace info\nprogram.",
	"A cyberspace ICE breaker\nprogram.",
	"A cyberspace virus\nprogram.",
	"A cyberspace shielding\nprogram.",
	"A cyberspace ICE bypass\nprogram.",
	"A cyberspace interface\ncorruptor program.",
	"A database password\ngenerator.",
	"A database info program.",
	"A database chess program.",
	"A system communications\nprogram."
};

// Mapping used by skill_warez_analysis_apply (DOS
// rw_state_skills.c:425) to pick the description string from a
// composed op index (0..13).
static const uint8 kWarezMap[14] = {
	0, 1, 2, 3, 4, 5, 0, 6, 6, 7, 8, 9, 10, 10
};

// Look up the Warez-Analysis description for a given item code by
// feeding its item_op byte through the same arithmetic DOS uses in
// skill_warez_analysis_apply (rw_state_skills.c:442-469).
static const char *warezDescriptionForCode(uint8 itemCode) {
	uint8 itemOp = Inventory::itemOp(itemCode);
	uint8 opN    = itemOp & 0x0F;
	uint8 str    = 0;
	if (opN == 9 || opN == 11) {
		str = 0;  // Unknown
	} else if ((itemOp & 0x30) == 0x10) {
		str = opN + 9;
	} else if ((itemOp & 0x30) == 0x20) {
		str = opN;
	} else if (opN <= 8) {
		str = opN + 12;
	}
	if (str >= sizeof(kWarezMap) / sizeof(kWarezMap[0]))
		str = 0;
	uint8 idx = kWarezMap[str];
	if (idx >= sizeof(kWarezDesc) / sizeof(kWarezDesc[0]))
		idx = 0;
	return kWarezDesc[idx];
}

// Rebuild _pickerSlots from the scene's inventory starting at
// _pickerPageStart. A slot is listed only when its code != 0xFF
// (matches DOS item_page's slot-enumeration). Returns _pickerCount.
static int collectSlots(const uint8 *bucket, int &cursor, int *out, int outMax) {
	int found = 0;
	for (int s = cursor; s < 32 && found < outMax; ++s) {
		if (bucket[s * 4] != 0xFF) {
			out[found++] = s;
		}
	}
	cursor = (found > 0) ? (out[found - 1] + 1) : 32;
	return found;
}

void Skills::drawItemPicker() {
	_state = kStateItemPicker;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);

	const uint8 *bucket = _pickerSoftware ? _scene->softwareSlots() : _scene->itemSlots();
	int cursor = _pickerPageStart;
	_pickerCount = collectSlots(bucket, cursor, _pickerSlots, 4);
	for (int i = _pickerCount; i < 4; ++i) _pickerSlots[i] = -1;

	const char *title =
		(_selectedSkill == 4) ? "Hardware Repair" :
		(_selectedSkill == 3) ? "Debug" : "Software Analysis";
	Common::String body = Common::String::format("%s\n\n", title);
	for (int i = 0; i < 4; ++i) {
		if (_pickerSlots[i] >= 0) {
			int slot = _pickerSlots[i];
			uint8 code = bucket[slot * 4];
			uint8 bug  = bucket[slot * 4 + 2];
			body += Common::String::format("%d.%c %-12s  bug %u\n",
			                               i + 1,
			                               bug ? '-' : ' ',
			                               Inventory::itemName(code),
			                               (unsigned)bug);
		} else {
			body += Common::String::format("%d.  (empty)\n", i + 1);
		}
	}
	body += "\nM=more  X=exit";
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 4, 4, pixels);
	pushSprite();
}

void Skills::drawItemResult() {
	_state = kStateItemResult;
	drawWindowFrame();
	byte *pixels = _sprite.data() + sizeof(ImhHeader);
	Common::String body = _itemResult;
	body += "\n\nPress any key.";
	drawString(body.c_str(), kWindowWidthPx, kWindowHeightPx, 8, 8, pixels);
	pushSprite();
}

bool Skills::applyItemSkill(int listIdx) {
	if (listIdx < 0 || listIdx >= _pickerCount) return false;
	int slot = _pickerSlots[listIdx];
	if (slot < 0) return false;
	uint8 *bucket = _pickerSoftware ? _scene->softwareSlots() : _scene->itemSlots();
	uint8 code = bucket[slot * 4];
	uint8 bug  = bucket[slot * 4 + 2];
	uint8 skillLevel = _scene->skills()[_selectedSkill];
	uint16 canFix = (skillLevel < 4) ? kFixDifficulty[skillLevel]
	                                 : kFixDifficulty[3];

	const char *name = Inventory::itemName(code);
	uint8 version = bucket[slot * 4 + 1];
	switch (_selectedSkill) {
	case 4: // HW Repair -- items[] bucket
		// DOS: item is reparable if code==0x53 or code in 0x1D..0x34.
		if (code == 0x53 || (code >= 0x1D && code <= 0x34)) {
			if (bug == 0) {
				_itemResult = Common::String::format(
					"%s\nhas no bugs.", name);
			} else if (bug <= canFix) {
				bucket[slot * 4 + 2] = 0;
				_scene->mirrorInventory();
				playSkillSfx(_engine, true);
				_itemResult = Common::String::format(
					"%s\ndamage repaired.", name);
			} else {
				playSkillSfx(_engine, false);
				_itemResult = Common::String::format(
					"Unable to repair\n%s.", name);
			}
		} else {
			playSkillSfx(_engine, false);
			_itemResult = Common::String::format(
				"%s is not\nreparable.", name);
		}
		return true;
	case 3: // Debug -- software[] bucket
		if (bug == 0) {
			_itemResult = Common::String::format(
				"%s v%u\nhas no bugs.", name, version);
		} else if (bug <= canFix) {
			bucket[slot * 4 + 2] = 0;
			_scene->mirrorInventory();
			playSkillSfx(_engine, true);
			_itemResult = Common::String::format(
				"Bug in %s v%u\nfixed.", name, version);
		} else {
			playSkillSfx(_engine, false);
			_itemResult = Common::String::format(
				"Unable to debug\n%s v%u.", name, version);
		}
		return true;
	case 2: // Warez Analysis -- describe software category (DOS
	        // skill_warez_analysis_apply, rw_state_skills.c:442).
		_itemResult = Common::String::format(
			"%s v%u\n%s",
			name, version, warezDescriptionForCode(code));
		return true;
	}
	return false;
}

bool Skills::dispatchItemPicker(char code) {
	const uint8 *bucket = _pickerSoftware ? _scene->softwareSlots() : _scene->itemSlots();
	switch (code) {
	case 'x':
		// DOS writes active_skill = 0xFF on exit-without-apply.
		if (NeuroVM *vm = _engine->vm())
			vm->writeVar8(0x4BF4, 0xFF);
		close();
		return true;
	case 'm': {
		// Advance page if there are more non-empty slots; else wrap.
		int probe = _pickerPageStart + _pickerCount;
		while (probe < 32 && bucket[probe * 4] == 0xFF) ++probe;
		if (probe >= 32) probe = 0;
		_pickerPageStart = probe;
		drawItemPicker();
		return true;
	}
	case '1': case '2': case '3': case '4':
		if (applyItemSkill(code - '1')) {
			drawItemResult();
			return true;
		}
		return false;
	default:
		return false;
	}
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

// Cryptology text-input handler. Mirrors DOS
// on_skill_cryptology_text_enter (rw_state_skills.c:750+). Enter
// commits the word through skill_cryptology_apply; Escape aborts;
// printable characters append to the buffer; Backspace shortens.
bool Skills::dispatchCryptology(const Common::Event &ev) {
	if (ev.type != Common::EVENT_KEYDOWN)
		return false;
	if (ev.kbd.keycode == Common::KEYCODE_BACKSPACE) {
		if (!_crypTyped.empty())
			_crypTyped.deleteLastChar();
		drawCryptology();
		return true;
	}
	if (ev.kbd.keycode == Common::KEYCODE_RETURN ||
	    ev.kbd.keycode == Common::KEYCODE_KP_ENTER) {
		// Look up the typed word (case-insensitive) against the 20-
		// entry decode table. If matched AND difficulty <= skill level,
		// record the decoded word; otherwise flag as undecodable.
		uint8 level = _scene->skills()[7]; // CRYPTOLOGY
		_crypDecoded = false;
		_crypResult.clear();
		for (int i = 0; i < 20; ++i) {
			if (_crypTyped.equalsIgnoreCase(kCryptoInputs[i])) {
				if (kCryptoDifficulty[i] <= level) {
					_crypDecoded = true;
					_crypResult = kCryptoOutputs[i];
				}
				break;
			}
		}
		drawCryptologyResult();
		return true;
	}
	// Printable ASCII (16-char cap per DOS buffer).
	if (ev.kbd.ascii >= 0x20 && ev.kbd.ascii < 0x7F && _crypTyped.size() < 16) {
		_crypTyped += (char)ev.kbd.ascii;
		drawCryptology();
		return true;
	}
	return false;
}

// DOS on_skill_musicianship_button (rw_state_skills.c:319-336):
// X exits without applying, 1-4 commit (active_skill, level) and close
// the skills panel so the VM re-runs with the new state.
bool Skills::dispatchMusicianship(char code) {
	NeuroVM *vm = _engine->vm();
	switch (code) {
	case 'x':
		// Exit without applying. DOS writes active_skill = 0xFF on exit
		// so scripts know nothing was activated.
		if (vm) vm->writeVar8(0x4BF4, 0xFF);
		close();
		return true;
	case '1': case '2': case '3': case '4':
		if (vm) {
			vm->writeVar8(0x4BF4, 15);              // active_skill = MUSICIANSHIP
			vm->writeVar8(0x4BF5, (uint8)(code - '1')); // level/track idx
		}
		close();
		return true;
	default:
		return false;
	}
}

} // End of namespace Neuromancer
