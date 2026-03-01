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

#ifndef HYPNO_TEACHER_DEMO_SCI_AFTERSCHOOLMENU_H
#define HYPNO_TEACHER_DEMO_SCI_AFTERSCHOOLMENU_H

#include "hypno/teacher/IconBar.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Hotspot.h"
#include "hypno/teacher/OptionMenu.h"
#include "hypno/teacher/Sample.h"
#include "common/list.h"

namespace Hypno {

class Handler10 : public IconBar {
public:
	Handler10();
	~Handler10() override;

	// Virtual method overrides
	void init(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	int addMessage(SC_Message *msg) override;
	int deinit(SC_Message *msg) override;
	void update(int param1, int param2) override;
	int lblParse(const Common::String &line) override;

	// AfterSchoolMenu-specific methods
	void resetHoverState();
	int isSelectionComplete();
	void resetSelection();

	void processCharacterHover(Common::Point pt);
	void processSubmenuHover(Common::Point pt);
	void processGoButtonHover(Common::Point pt, TeacherHotspot *button, int *outConfirmFlag);
	void displaySubmenuHover(int mouseX, int mouseY);

	void renderGoButton();
	void renderCharacters();
	void renderChoiceScreen(int characterIndex);

	void fillOptionQueue();
	void setCharacterOption(int characterIndex);
	void setSubmenuOption(int submenuIndex, int state);

	void playCharacterSound(int soundIndex);
	void playSoundsIfNeeded();

	struct SoundSlot {
		Sample *sample;
		int enabled;
		SoundSlot() : sample(nullptr), enabled(0) {}
	};

	SoundSlot sounds[8];
	Sample *ambientSound;
	Sample *currentSound;
	int playSoundsFlag;

	// State fields matching original SCI_AfterSchoolMenu layout
	Common::List<Sprite *> _backgroundSprites; // Background sprites
	OptionMenu *choiceScreen;          // Submenu option screen
	TeacherHotspot *goButton;          // "Go" button
	TeacherHotspot *cancelButton;      // "Cancel" button
	TeacherHotspot *characters[3];     // Character hotspots (peter, susan, duncan)

	int savedCharacterIndex;           // Saved character selection across init/shutdown
	int savedSubmenuIndex;             // Saved submenu selection across init/shutdown
	int currentCharacterIndex;         // Currently selected character (-1 = none)
	int currentSubmenuIndex;           // Currently selected submenu option (-1 = none)
	int hoverCharacterIndex;           // Character currently being hovered (-1 = none)
	int hoverSubmenuIndex;             // Submenu option currently being hovered (-1 = none)
	int needsRefresh;                  // Whether option queue needs refresh
	int isInitialized;                 // Whether a character has been selected at least once
	int prevHoverCharacter;            // Previous character hover index
	int prevCharacter;                 // Previous selected character index
	int prevSubmenuHover;              // Previous submenu hover index
	int prevSubmenu;                   // Previous selected submenu index
	int confirmFlag;                   // Whether go button is being hovered
	int updateFlag;                    // Update flag
	int characterCount;                // 0x69C: Count of characters parsed from script

	bool isActive;                     // Whether menu is currently active (DAT_00435448)

	Common::String _palettePath;
};

} // End of namespace Hypno

#endif
