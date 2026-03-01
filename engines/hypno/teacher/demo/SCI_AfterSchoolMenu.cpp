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

#include "hypno/teacher/demo/SCI_AfterSchoolMenu.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/debug.h"
#include "common/events.h"

namespace Hypno {

// Original: SCI_AfterSchoolMenu::SCI_AfterSchoolMenu() at 0x404CA0
Handler10::Handler10()
	: choiceScreen(nullptr), goButton(nullptr), cancelButton(nullptr),
	  savedCharacterIndex(-1), savedSubmenuIndex(-1),
	  currentCharacterIndex(-1), currentSubmenuIndex(-1),
	  hoverCharacterIndex(-1), hoverSubmenuIndex(-1),
	  needsRefresh(0), isInitialized(0),
	  prevHoverCharacter(-1), prevCharacter(-1),
	  prevSubmenuHover(-1), prevSubmenu(-1),
	  confirmFlag(0), updateFlag(0),
	  characterCount(0), isActive(false),
	  ambientSound(nullptr), currentSound(nullptr), playSoundsFlag(0) {
	handlerId = 10;
	sourceAddress = 1;  // Original: SCI_AfterSchoolMenu sets sourceAddress = 1
	characters[0] = characters[1] = characters[2] = nullptr;
	for (int i = 0; i < 8; i++) {
		sounds[i].sample = nullptr;
		sounds[i].enabled = 1;
	}

	// Parse the demo.mis script to load all assets
	Parser::parseFile(this, "mis/demo.mis", "");
}

// Original: SCI_AfterSchoolMenu::~SCI_AfterSchoolMenu() at 0x404E60
Handler10::~Handler10() {
	delete choiceScreen;
	delete goButton;
	delete cancelButton;
	for (int i = 0; i < 3; i++) {
		delete characters[i];
	}
	for (auto it : _backgroundSprites)
		delete it;
	_backgroundSprites.clear();

	for (int i = 0; i < 8; i++) {
		if (sounds[i].sample) {
			delete sounds[i].sample;
			sounds[i].sample = nullptr;
		}
	}
}

// Original: SCI_AfterSchoolMenu::Init() at 0x404FF0
void Handler10::init(SC_Message *msg) {
	if (!isActive) {
		isActive = true;
		debugC(1, kHypnoDebugScene, "ENTER AFTERSCHOOL MENU");

		initIconBar(msg);

		// Disable choice and mainmenu iconbar buttons (indices 0 and 4)
		buttons[0].enabled = false;
		buttons[4].enabled = false;

		// Load palette if we have one
		if (!_palettePath.empty()) {
			TeacherEngine *engine = (TeacherEngine *)g_engine;
			engine->loadPalette(_palettePath);
		}

		// Restore saved selection state
		currentCharacterIndex = savedCharacterIndex;
		currentSubmenuIndex = savedSubmenuIndex;

		// If character was previously selected, initialize choice screen
		if (savedCharacterIndex != -1) {
			fillOptionQueue();
			isInitialized = 1;
		}

		// Reset hover states
		hoverCharacterIndex = -1;
		hoverSubmenuIndex = -1;
		confirmFlag = 0;
		updateFlag = 0;

		// Create and load sound samples (original always creates fresh)
		for (int i = 0; i < 8; i++) {
			Common::String filename = Common::String::format("audio/Snd00%02d.wav", i + 1);
			sounds[i].sample = new Sample();
			sounds[i].sample->Load(filename);
		}

		ambientSound = sounds[7].sample;

		if (sounds[0].enabled != 0 && sounds[0].sample != 0) {
			currentCharacterIndex = 1;
			currentSound = sounds[0].sample;
		}

		playSoundsFlag = 1;
	}
}

// Original: SCI_AfterSchoolMenu::ShutDown() at 0x405190
int Handler10::shutDown(SC_Message *msg) {
	if (isActive) {
		isActive = false;

		// Original: background->StopAll() — stops playback without destroying
		for (auto it : _backgroundSprites) {
			if (it) {
				it->stopAnimationSound();
			}
		}

		// Deactivate choice screen
		if (choiceScreen) {
			choiceScreen->deactivate();
		}

		// Exit go and cancel buttons
		if (goButton)
			goButton->Exit();
		if (cancelButton)
			cancelButton->Exit();

		// Exit character sprites
		for (int i = 0; i < 3; i++) {
			if (characters[i])
				characters[i]->Exit();
		}

		// Save current state for later restoration
		isInitialized = 1;
		needsRefresh = 1;
		savedCharacterIndex = currentCharacterIndex;
		savedSubmenuIndex = currentSubmenuIndex;

		// Original: stop ambient sound and null it
		if (ambientSound) {
			ambientSound->Stop();
		}
		ambientSound = nullptr;

		// Original: unload and delete sounds[0] explicitly
		if (sounds[0].sample) {
			sounds[0].sample->Unload();
			delete sounds[0].sample;
			sounds[0].sample = nullptr;
		}

		// Original: stop current sound and null it
		if (currentSound) {
			currentSound->Stop();
		}
		currentSound = nullptr;

		// Original: unload and delete ALL 8 sound samples
		for (int i = 0; i < 8; i++) {
			if (sounds[i].sample) {
				sounds[i].sample->Unload();
				delete sounds[i].sample;
				sounds[i].sample = nullptr;
			}
		}

		cleanupIconBar(msg);
		debugC(1, kHypnoDebugScene, "EXIT AFTERSCHOOL MENU (char=%d)", currentCharacterIndex);
	}
	return 1;
}

// Original: SCI_AfterSchoolMenu::AddMessage() at 0x4052D0
int Handler10::addMessage(SC_Message *msg) {
	// Check iconbar button click first
	if (checkButtonClick(msg))
		return 1;

	// If intro voiceover is still playing, block all clicks
	// Original: if (sounds[0].enabled != 0) return 1;
	if (sounds[0].enabled != 0)
		return 1;

	// Check click type - need at least 2 (left click)
	if (msg->mouseX < 2)
		return 1;

	// Note: hover state is updated in update() via displaySubmenuHover(),
	// NOT here in addMessage. The original code relies on the hover state
	// computed during the last frame's Update() call.

	// Handle character selection click
	if (hoverCharacterIndex != -1) {
		currentCharacterIndex = hoverCharacterIndex;

		if (prevCharacter == -1) {
			fillOptionQueue();
		}

		characters[currentCharacterIndex]->SetState(1);
		isInitialized = 1;

		// Set selected character global in the engine
		TeacherEngine *engine = (TeacherEngine *)g_engine;
		if (currentCharacterIndex == 0)
			engine->_selectedCharacter = engine->_peter;
		else if (currentCharacterIndex == 1)
			engine->_selectedCharacter = engine->_susan;
		else
			engine->_selectedCharacter = engine->_duncan;

		debugC(1, kHypnoDebugScene, "Handler10: Selected character %d", currentCharacterIndex);
		// Original: update iconbar sprite with character state
		iconbarSprite->setState2(engine->_selectedCharacter->characterType + 1);

		// Original: PlayCharacterSound(2) for character click
		playCharacterSound(2);
	}
	// Handle submenu selection click
	else if (hoverSubmenuIndex != -1 && isInitialized == 1) {
		currentSubmenuIndex = hoverSubmenuIndex;
		setSubmenuOption(hoverSubmenuIndex, 1);
		debugC(1, kHypnoDebugScene, "Handler10: Selected submenu %d", currentSubmenuIndex);
		// Original: PlayCharacterSound(1) for submenu click
		playCharacterSound(1);
	}

	// Check if selection is complete and go button confirmed
	if (isSelectionComplete() && confirmFlag != 0) {
		// Build transition message
		// Original sets: targetAddress = dest handler, command = this handler's
		// targetAddress (10), data = this handler's sourceAddress (1), priority = 5
		if (currentCharacterIndex == 0) {
			msg->targetAddress = 4;   // Handler 4 (Peter puzzle)
		} else if (currentCharacterIndex == 1) {
			msg->targetAddress = 11;  // Handler 11 (Susan search screen)
		} else {
			msg->targetAddress = 16;  // Handler 16 (Duncan combat)
		}
		msg->command = handlerId;     // 10 - this handler's ID (original: targetAddress)
		msg->data = sourceAddress;    // 1 - set in constructor (original: sourceAddress)
		msg->priority = 5;

		debugC(1, kHypnoDebugScene, "Handler10: Starting game with character %d -> handler %d",
			currentCharacterIndex, msg->targetAddress);
	}

	return 1;
}

// Original: SCI_AfterSchoolMenu::Exit() at 0x405420
int Handler10::deinit(SC_Message *msg) {
	return handlerId == msg->targetAddress;
}

// Original: SCI_AfterSchoolMenu::Update() at 0x405490
void Handler10::update(int param1, int param2) {
	if (handlerId != param2) {
		return;
	}
	if (!isActive) {
		return;
	}

	// Get mouse position from ScummVM event system
	Common::Point mousePos = g_system->getEventManager()->getMousePos();
	int mouseX = mousePos.x;
	int mouseY = mousePos.y;

	// Process hover states for all interactive elements
	displaySubmenuHover(mouseX, mouseY);

	// Update iconbar
	IconBar::update(param1, param2);

	// Render background sprites
	for (auto it : _backgroundSprites) {
		if (it) it->Do(it->loc_x, it->loc_y, 1.0);
	}

	// Render go and cancel buttons
	renderGoButton();

	// Render character sprites
	renderCharacters();

	// Render choice screen (submenu options) if valid character selected
	if (choiceScreen) {
		renderChoiceScreen(currentCharacterIndex);
	}

	// Play sounds if needed (ambient + intro voice)
	playSoundsIfNeeded();

	// Check if current sound finished playing
	// Original uses AIL_sample_status() to check Miles Sound System status
	if (currentSound) {
		if (!g_engine->_mixer->isSoundHandleActive(currentSound->_handle)) {
			// Sound finished playing
			currentSound = nullptr;

			// Original: when intro voiceover finishes, clear the enabled flag
			// and reset selection so user can start fresh
			if (sounds[0].enabled != 0) {
				sounds[0].enabled = 0;
				if (currentCharacterIndex >= 0 && currentCharacterIndex < 3 && characters[currentCharacterIndex]) {
					characters[currentCharacterIndex]->SetState(0);
				}
				resetSelection();
			}
		}
	}
}

// Original: SCI_AfterSchoolMenu::DisplaySubmenuHover() at 0x405590
void Handler10::displaySubmenuHover(int mouseX, int mouseY) {
	Common::Point pt(mouseX, mouseY);

	resetHoverState();

	processCharacterHover(pt);
	processSubmenuHover(pt);
	processGoButtonHover(pt, goButton, &confirmFlag);

	// Original: if intro voiceover is still playing, force hover on character 1 (Susan)
	// This makes Susan appear highlighted during the intro voice
	if (sounds[0].enabled != 0) {
		hoverCharacterIndex = 1;
	}

	// Handle character switching
	if (prevCharacter != -1 && currentCharacterIndex != prevCharacter) {
		characters[prevCharacter]->SetState(0);
		currentSubmenuIndex = -1;
		prevSubmenu = -1;
		prevCharacter = currentCharacterIndex;
		fillOptionQueue();
	}

	// Highlight currently selected character
	if (currentCharacterIndex != -1) {
		if (hoverCharacterIndex != currentCharacterIndex) {
			characters[currentCharacterIndex]->SetState(1);
			prevCharacter = currentCharacterIndex;
		}
		if (currentCharacterIndex != -1 && hoverCharacterIndex == currentCharacterIndex) {
			characters[currentCharacterIndex]->SetState(2);
		}
	}

	// Handle submenu switching
	if (prevSubmenu != -1 && currentSubmenuIndex != prevSubmenu) {
		setSubmenuOption(prevSubmenu, 0);
	}

	// Highlight currently selected submenu
	if (currentSubmenuIndex != -1) {
		if (hoverSubmenuIndex != currentSubmenuIndex) {
			setSubmenuOption(currentSubmenuIndex, 1);
			prevSubmenu = currentSubmenuIndex;
		}
		if (currentSubmenuIndex != -1 && hoverSubmenuIndex == currentSubmenuIndex) {
			setSubmenuOption(currentSubmenuIndex, 2);
		}
	}

	// Refresh option queue if needed
	if (needsRefresh != 0 && currentCharacterIndex != -1) {
		fillOptionQueue();
		needsRefresh = 0;
	}
}

// Original: SCI_AfterSchoolMenu::ResetHoverState() at 0x405780
void Handler10::resetHoverState() {
	hoverCharacterIndex = -1;
	hoverSubmenuIndex = -1;
	updateFlag = 0;
	confirmFlag = 0;
}

// Original: SCI_AfterSchoolMenu::IsSelectionComplete() at 0x4057A0
int Handler10::isSelectionComplete() {
	if (currentCharacterIndex != -1 && currentSubmenuIndex != -1) {
		return 1;
	}
	return 0;
}

// Original: SCI_AfterSchoolMenu::ResetSelection() at 0x4057C0
void Handler10::resetSelection() {
	savedCharacterIndex = -1;
	savedSubmenuIndex = -1;
	currentCharacterIndex = -1;
	currentSubmenuIndex = -1;
	needsRefresh = 0;
	isInitialized = 0;
	prevHoverCharacter = -1;
	prevCharacter = -1;
	prevSubmenuHover = -1;
	prevSubmenu = -1;
}

// Original: SCI_AfterSchoolMenu::RenderGoButton() at 0x405880
void Handler10::renderGoButton() {
	if (goButton)
		goButton->Do();
	if (cancelButton)
		cancelButton->Do();
}

// Original: SCI_AfterSchoolMenu::RenderCharacters() at 0x4058B0
void Handler10::renderCharacters() {
	for (int i = 0; i < 3; i++) {
		if (characters[i]) {
			characters[i]->Do();
		}
	}
}

// Original: SCI_AfterSchoolMenu::RenderChoiceScreen() at 0x4058E0
void Handler10::renderChoiceScreen(int characterIndex) {
	int charIdx = currentCharacterIndex;
	if (charIdx >= 0 && charIdx <= 2) {
		choiceScreen->render(charIdx);
	}
}

// Original: SCI_AfterSchoolMenu::ProcessCharacterHover() at 0x405900
void Handler10::processCharacterHover(Common::Point pt) {
	bool noHit = true;

	for (int i = 0; i < 3; i++) {
		if (!characters[i] || characters[i]->enabled == 0)
			continue;

		bool isHit = characters[i]->rect.contains(pt.x, pt.y);

		if (isHit) {
			hoverCharacterIndex = i;

			if (characters[i]->GetState() != 2) {
				characters[i]->SetState(3);
			}

			if (prevHoverCharacter != -1) {
				if (prevHoverCharacter != i) {
					characters[prevHoverCharacter]->SetState(0);
					prevHoverCharacter = i;
				}
			} else {
				prevHoverCharacter = i;
				characters[i]->SetState(3);
			}
			noHit = false;
		}
	}

	if (noHit && prevHoverCharacter != -1) {
		characters[prevHoverCharacter]->SetState(0);
		prevHoverCharacter = -1;
	}
}

// Original: SCI_AfterSchoolMenu::ProcessGoButtonHover() at 0x405AA0
void Handler10::processGoButtonHover(Common::Point pt, TeacherHotspot *button, int *outConfirmFlag) {
	if (!button) {
		*outConfirmFlag = 0;
		return;
	}

	if (isSelectionComplete()) {
		bool isHit = false;
		if (button->enabled != 0) {
			isHit = button->rect.contains(pt.x, pt.y);
		}

		if (isHit) {
			*outConfirmFlag = 1;
			button->SetState(2);
		} else {
			*outConfirmFlag = 0;
			button->SetState(1);
		}
	} else {
		*outConfirmFlag = 0;
		button->SetState(0);
	}

	if (cancelButton) {
		cancelButton->SetState(0);
	}
}

// Original: SCI_AfterSchoolMenu::ProcessSubmenuHover() at 0x405BB0
void Handler10::processSubmenuHover(Common::Point pt) {
	if (currentCharacterIndex == -1)
		return;
	if (!choiceScreen)
		return;

	int indexOut = 0;
	int hitOut = 1;

	if (choiceScreen->hitTest(pt, &indexOut, &hitOut)) {
		hoverSubmenuIndex = indexOut;
	}

	if (hoverSubmenuIndex != prevSubmenuHover && prevSubmenuHover != -1) {
		indexOut = prevSubmenuHover;
		setSubmenuOption(indexOut, 0);
	}

	if (hitOut != 0) {
		prevSubmenuHover = -1;
	} else {
		prevSubmenuHover = hoverSubmenuIndex;
	}
}

// Original: SCI_AfterSchoolMenu::FillOptionQueue() at 0x405C80
void Handler10::fillOptionQueue() {
	int charIdx = currentCharacterIndex;
	if (charIdx < 0 || charIdx > 2) {
		warning("Error in DMChoScr.cpp - FillOptionQueue");
	} else {
		setCharacterOption(charIdx);
	}
	setSubmenuOption(-5, 0);
}

// Original: SCI_AfterSchoolMenu::SetCharacterOption() at 0x405CC0
void Handler10::setCharacterOption(int characterIndex) {
	if (choiceScreen) {
		choiceScreen->selectCharacter(characterIndex);
	} else {
		warning("Missing option_menu in GetOptionQ - opmenu.cpp");
	}
}

// Original: SCI_AfterSchoolMenu::SetSubmenuOption() at 0x405CF0
void Handler10::setSubmenuOption(int submenuIndex, int state) {
	if (choiceScreen) {
		choiceScreen->setOptionState(submenuIndex, state);
	} else {
		warning("Missing option_menu in SetOptionState");
	}
}

// Original: SCI_AfterSchoolMenu::LBLParse() at 0x405D20
int Handler10::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "BACK") {
		while (_file && !_file->eos()) {
			Common::String bgLine = readLine();
			Common::String bgKeyword, bgRest;
			tokenize(bgLine, bgKeyword, bgRest);
			if (bgKeyword == "END") break;
			if (bgKeyword == "SPRITE") {
				Sprite *sprite = new Sprite();
				Parser::processFile(sprite, this, "");
				_backgroundSprites.push_back(sprite);
			}
		}
	} else if (keyword == "PALE") {
		_palettePath = rest;
		for (uint32 i = 0; i < _palettePath.size(); ++i)
			if (_palettePath[i] == '\\') _palettePath[i] = '/';
		TeacherEngine *engine = (TeacherEngine *)g_engine;
		engine->loadPalette(_palettePath);
	} else if (keyword == "CHAR") {
		if (characterCount < 3) {
			characters[characterCount] = new TeacherHotspot();
			Parser::processFile(characters[characterCount], this, "");
			characterCount++;
		}
	} else if (keyword == "CANCEL") {
		cancelButton = new TeacherHotspot();
		Parser::processFile(cancelButton, this, "");
	} else if (keyword == "OKAY") {
		goButton = new TeacherHotspot();
		Parser::processFile(goButton, this, "");
	} else if (keyword == "OPTION_MENU") {
		choiceScreen = new OptionMenu();
		Parser::processFile(choiceScreen, this, "");
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

void Handler10::playCharacterSound(int soundIndex) {
	// Stop current iconbar sound
	playButtonSound(-1);

	if (currentSound) {
		currentSound->Stop();
		currentSound = nullptr;
	}

	int idx = currentCharacterIndex * 2 + soundIndex;
	if (idx >= 0 && idx < 8 && sounds[idx].enabled != 0 && sounds[idx].sample != nullptr) {
		// In original it sets enabled to 0 after first play
		sounds[idx].enabled = 0;
		currentSound = sounds[idx].sample;
		currentSound->Play(100, 1);
	}
}

void Handler10::playSoundsIfNeeded() {
	if (playSoundsFlag != 0) {
		playSoundsFlag = 0;

		if (ambientSound) {
			ambientSound->Play(100, 0); // Loop forever
		}

		if (sounds[0].enabled != 0 && sounds[0].sample != nullptr) {
			sounds[0].sample->Play(100, 1);
		}
	}
}

} // End of namespace Hypno
