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

#ifndef NEUROMANCER_SKILLS_H
#define NEUROMANCER_SKILLS_H

#include "common/array.h"
#include "common/events.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Neuromancer {

class NeuromancerEngine;
class RealWorldScene;

// Skills sub-module for the real-world scene. Mirrors DOS
// rw_state_skills.c: a small window listing the skills the player has
// acquired and their current level. Clicking a skill invokes its
// specific "apply" flow (Warez Analysis picks a program, Cryptology
// prompts for a word, etc). Phase 1 here covers the list view + a
// "description" screen for skills without a complex flow; the applied
// skills will land in a later phase.
class Skills {
public:
	explicit Skills(NeuromancerEngine *engine, RealWorldScene *scene);
	~Skills() = default;

	bool isActive() const { return _active; }
	void open();
	void close();
	void update();
	bool handleEvent(const Common::Event &event);

private:
	enum State {
		kStateList         = 0, // DOS SS_SKILLS_PAGE
		kStateNoSkills     = 1, // DOS SS_NO_SKILLS_WFI
		kStateDescription  = 2, // local: skill details + "Nothing happens."
		kStateMusicianship = 3, // DOS SS_SKILL_MUSICIANSHIP
		kStateCryptology   = 4, // DOS SS_SKILL_CRYPTOLOGY (text input)
		kStateCryptologyResult = 5, // DOS SS_SKILL_CRYPTOLOGY_WFI
		kStateItemPicker   = 6, // DOS SS_SKILL_HW_REPAIR_ITEM_PAGE / WAREZ / DEBUG
		kStateItemResult   = 7  // DOS SS_SKILL_HW_REPAIR_WFI / WAREZ / DEBUG WFI
	};

	void drawWindowFrame();
	void drawList();
	void drawNoSkills();
	void drawDescription();
	void drawMusicianship();
	void drawCryptology();          // input prompt
	void drawCryptologyResult();    // decoded / fail screen
	void drawItemPicker();          // HW Repair / Warez / Debug list
	void drawItemResult();          // per-skill apply outcome
	void pushSprite();

	bool dispatchList(char code);
	bool dispatchMusicianship(char code);
	bool dispatchCryptology(const Common::Event &ev);
	bool dispatchItemPicker(char code);

	// Apply the current skill to the selected inventory slot and set
	// _itemResult for drawItemResult. Returns true if the state should
	// advance to kStateItemResult; false if the call was invalid.
	bool applyItemSkill(int slotIdx);

	// Rebuild _pageSkills from the scene's _skills[16] array, honouring
	// the current _pageStart offset. Sets _pageCount (0..4).
	int rebuildPage();

	NeuromancerEngine *_engine;
	RealWorldScene   *_scene;

	bool  _active;
	State _state;

	Common::Array<byte> _sprite;

	// Pagination. _pageStart is the skill-entry offset into the list of
	// acquired skills (not raw skill indices). _pageSkills maps row 0..3
	// to the underlying skill index (0..15) or -1 for empty rows.
	int _pageStart;
	int _pageCount;
	int _pageSkills[4];

	int _selectedSkill; // 0..15 -- skill index for the description screen

	// Cryptology text-input state. `_crypTyped` accumulates the word
	// being entered; `_crypResult` is set when the user hits Enter so
	// drawCryptologyResult can show either the decoded word or a fail
	// message.
	Common::String _crypTyped;
	Common::String _crypResult;
	bool           _crypDecoded;

	// Inventory item-picker state (HW Repair / Warez / Debug).
	// _pickerSoftware chooses which DOS bucket the picker iterates:
	// false = items[] (HW Repair), true = software[] (Warez / Debug).
	bool           _pickerSoftware;
	int            _pickerPageStart;   // first visible slot index
	int            _pickerSlots[4];    // slot indices (0..31) on current page
	int            _pickerCount;       // entries in _pickerSlots
	Common::String _itemResult;        // text for kStateItemResult
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SKILLS_H
