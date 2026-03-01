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

#include "hypno/teacher/demo/Handler4.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"

namespace Hypno {

// Original: g_SolutionData_00435b88 from globals.cpp
// 9 valid solutions: each defines 3 button indices and which floor (0-2) it activates
static const SolutionEntry g_SolutionData[9] = {
	{{1, 4, 6}, 0},
	{{2, 4, 6}, 0},
	{{0, 5, 8}, 1},
	{{1, 3, 8}, 1},
	{{2, 3, 8}, 1},
	{{0, 4, 0}, 2},
	{{0, 4, 6}, 2},
	{{0, 4, 7}, 2},
	{{0, 4, 8}, 2}
};

// Simple rect data to avoid global constructor warnings
struct PuzzleRect {
	int16 left, top, right, bottom;
	bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// Original: g_PuzzleRects1_0043d068 - 9 button clickable regions (3x3 grid)
static const PuzzleRect g_PuzzleRects1[9] = {
	{0x1de, 0x147, 0x205, 0x16d},  // [0] bottom-left
	{0x209, 0x147, 0x230, 0x16d},  // [1] bottom-center
	{0x234, 0x147, 0x25b, 0x16d},  // [2] bottom-right
	{0x1de, 0x11c, 0x205, 0x143},  // [3] middle-left
	{0x209, 0x11c, 0x230, 0x143},  // [4] middle-center
	{0x234, 0x11c, 0x25b, 0x143},  // [5] middle-right
	{0x1de, 0x0f1, 0x205, 0x118},  // [6] top-left
	{0x209, 0x0f1, 0x230, 0x118},  // [7] top-center
	{0x234, 0x0f1, 0x25b, 0x118},  // [8] top-right
};

// Original: g_PuzzleRects2_0043d100 - 3 floor clickable regions
static const PuzzleRect g_PuzzleRects2[3] = {
	{0x014, 0x0a1, 0x046, 0x137},  // [0] left floor
	{0x0b2, 0x0ac, 0x0ef, 0x118},  // [1] middle floor
	{0x15d, 0x0a1, 0x18f, 0x137},  // [2] right floor
};

// Original: 0x40E220
Handler4::Handler4() {
	handlerId = 4;
	targetAddress = 4;

	palette = nullptr;
	puzztest = nullptr;
	litdoors = nullptr;
	paths1 = nullptr;
	paths2 = nullptr;
	paths3 = nullptr;
	buttons1 = nullptr;
	buttons2 = nullptr;
	buttons3 = nullptr;
	lowfloor = nullptr;
	midfloor = nullptr;
	topfloor = nullptr;
	sound1 = nullptr;
	sound2 = nullptr;

	memset(soundStates, 0, sizeof(soundStates));
	memset(floorStates, 0, sizeof(floorStates));
	memset(buttonStates, 0, sizeof(buttonStates));
	puzzleSolved = 0;
	initialized = 0;
	needsUpdate = 1;

	// Click regions from original constructor
	rect1 = Common::Rect(0, 0, 0x197, 0x1aa);
	rect2 = Common::Rect(0x1d7, 0x3b, 0x25c, 0xbb);
	rect3 = Common::Rect(0x1de, 0xf1, 0x25b, 0x16d);
	rect4 = Common::Rect(0x73, 0x11d, 0x12f, 0x149);
	rect5 = Common::Rect(0x32, 0x14a, 0x172, 0x192);

	palette = new TeacherPalette();
	palette->load("puzzle1/Puzztest.col");

	// Create button sprites for 3 rows
	buttons1 = new Sprite("puzzle1/buttons1.smk");
	buttons1->loc_x = 0x1dc;
	buttons1->loc_y = 0x147;

	buttons2 = new Sprite("puzzle1/buttons2.smk");
	buttons2->loc_x = 0x1dc;
	buttons2->loc_y = 0x11c;

	buttons3 = new Sprite("puzzle1/buttons3.smk");
	buttons3->loc_x = 0x1dc;
	buttons3->loc_y = 0xf1;

	// Create path indicator sprites for 3 rows
	paths1 = new Sprite("puzzle1/paths1.smk");
	paths1->loc_x = 0x1ee;
	paths1->loc_y = 0x8d;

	paths2 = new Sprite("puzzle1/paths2.smk");
	paths2->loc_x = 0x1ee;
	paths2->loc_y = 0x70;

	paths3 = new Sprite("puzzle1/paths3.smk");
	paths3->loc_x = 0x1ee;
	paths3->loc_y = 0x53;

	// Configure buttons and paths: 3 states each
	Sprite *btnArray[3] = {buttons1, buttons2, buttons3};
	Sprite *pathArray[3] = {paths1, paths2, paths3};

	for (int i = 0; i < 3; i++) {
		btnArray[i]->flags &= ~2;
		btnArray[i]->priority = 10;
		btnArray[i]->initStates(3);
		btnArray[i]->setRange(0, 1, 1);
		btnArray[i]->setRange(1, 2, 2);
		btnArray[i]->setRange(2, 3, 3);

		pathArray[i]->flags &= ~2;
		pathArray[i]->priority = 10;
		pathArray[i]->initStates(3);
		pathArray[i]->setRange(0, 1, 1);
		pathArray[i]->setRange(1, 2, 2);
		pathArray[i]->setRange(2, 3, 3);
	}

	// Background sprite
	puzztest = new Sprite("puzzle1/puzztest.smk");
	puzztest->flags &= ~2;
	puzztest->priority = 5;
	puzztest->loc_x = 0;
	puzztest->loc_y = 0;

	// Lit doors indicator
	litdoors = new Sprite("puzzle1/litdoors.smk");
	litdoors->flags &= ~2;
	litdoors->priority = 10;
	litdoors->flags |= 0x40;
	litdoors->initStates(3);
	litdoors->setRange(0, 1, 1);
	litdoors->setRange(1, 2, 2);
	litdoors->setRange(2, 3, 3);

	// Floor sprites
	lowfloor = new Sprite("puzzle1/lowfloor.smk");
	lowfloor->loc_x = 0x1d;
	lowfloor->loc_y = 0x150;

	midfloor = new Sprite("puzzle1/midfloor.smk");
	midfloor->loc_x = 0x2c;
	midfloor->loc_y = 0x130;

	topfloor = new Sprite("puzzle1/topfloor.smk");
	topfloor->loc_x = 0x5a;
	topfloor->loc_y = 0x11c;

	// Configure floor sprites: 4 states each
	Sprite *floorArray[3] = {lowfloor, midfloor, topfloor};
	for (int i = 0; i < 3; i++) {
		floorArray[i]->flags &= ~2;
		floorArray[i]->priority = 10;
		floorArray[i]->flags |= 0x40;
		floorArray[i]->initStates(4);
		floorArray[i]->setRange(0, 1, 4);
		floorArray[i]->setRange(1, 5, 8);
		floorArray[i]->setRange(2, 9, 12);
		floorArray[i]->setRange(3, 13, 16);
	}

	for (int i = 0; i < 10; i++) {
		soundStates[i] = 1;
	}
}

// Original: 0x40E9A0
Handler4::~Handler4() {
	Sprite *floorArray[3] = {lowfloor, midfloor, topfloor};
	Sprite *pathArray[3] = {paths1, paths2, paths3};
	Sprite *btnArray[3] = {buttons1, buttons2, buttons3};

	for (int i = 0; i < 3; i++) {
		delete floorArray[i];
		delete pathArray[i];
		delete btnArray[i];
	}

	lowfloor = midfloor = topfloor = nullptr;
	paths1 = paths2 = paths3 = nullptr;
	buttons1 = buttons2 = buttons3 = nullptr;

	delete litdoors;
	litdoors = nullptr;

	delete puzztest;
	puzztest = nullptr;

	delete palette;
	palette = nullptr;
}

// Original: 0x40EB80
void Handler4::init(SC_Message *msg) {
	debugC(1, kHypnoDebugScene, "Handler4: ENTER FORCEFIELD PUZZLE");
	initIconBar(msg);

	// Set palette
	if (palette) {
		palette->setPalette(0, 256);
	}

	// Play background music
	if (soundStates[9] != 0) {
		sound2 = new Sample();
		sound2->Load("audio/Snd0020.wav");
		sound2->Play(100, 0);
	}

	// Play intro sound
	if (soundStates[4] != 0) {
		PlayPuzzleSound(4, 0);
	}
}

// Original: 0x40EC80
int Handler4::shutDown(SC_Message *msg) {
	if (puzztest) puzztest->stopAnimationSound();
	if (litdoors) litdoors->stopAnimationSound();

	Sprite *pathArray[3] = {paths1, paths2, paths3};
	Sprite *btnArray[3] = {buttons1, buttons2, buttons3};
	Sprite *floorArray[3] = {lowfloor, midfloor, topfloor};

	for (int i = 0; i < 3; i++) {
		if (pathArray[i]) pathArray[i]->stopAnimationSound();
		if (btnArray[i]) btnArray[i]->stopAnimationSound();
		if (floorArray[i]) floorArray[i]->stopAnimationSound();
	}

	if (sound1) {
		sound1->Stop();
		delete sound1;
		sound1 = nullptr;
	}

	if (sound2) {
		sound2->Stop();
		delete sound2;
		sound2 = nullptr;
	}

	cleanupIconBar(msg);
	debugC(1, kHypnoDebugScene, "Handler4: EXIT FORCEFIELD PUZZLE");
	return 0;
}

// Original: 0x40ED50
int Handler4::addMessage(SC_Message *msg) {
	if (checkButtonClick(msg)) {
		return 1;
	}

	if (msg->mouseX >= 2) {
		int mouseX = msg->clickPos.x;
		int mouseY = msg->clickPos.y;

		OnClick(mouseX, mouseY);

		// Check floor region clicks for transitions via Handler 8 (cinematic player).
		// Original bug at 0x40ED50: the decompiler confused msg->field with this->field.
		// The per-floor cinematic number is computed as (i < 1) ? 2 : 3 and stored in
		// msg->sourceAddress. The original then had if/else branches that incorrectly
		// used this->targetAddress (=4) and this->sourceAddress (=0) for msg->command
		// and msg->data, instead of reading from the msg fields. The floor 0 hardcoded
		// data=1 was also wrong (should be 2 = msg->sourceAddress for floor 0).
		// Fix: msg->data = cinematic number (2 or 3), msg->command = return handler.
		for (int i = 0; i < 3; i++) {
			if (floorStates[i] != 0) {
				int curX = msg->clickPos.x;
				int curY = msg->clickPos.y;

				if (g_PuzzleRects2[i].contains(curX, curY)) {
					if (i == 0) {
						ResetPuzzle();
					}

					msg->targetAddress = 8;
					msg->sourceAddress = (i < 1) ? 2 : 3;
					msg->command = command;            // return handler (10 = AfterSchoolMenu)
					msg->data = msg->sourceAddress;    // cinematic number (2 or 3 per floor)
					msg->priority = 5;
				}
			}
		}
	}

	return 1;
}

// Original: 0x40EEB0
int Handler4::deinit(SC_Message *msg) {
	return targetAddress == msg->targetAddress;
}

// Original: 0x40EED0
void Handler4::update(int param1, int param2) {
	if (targetAddress != param2) {
		return;
	}

	// Check if sound1 finished playing and clean up
	// Original checks AIL_sample_status != 4 (SMP_PLAYING)
	if (sound1 != nullptr) {
		if (!g_engine->_mixer->isSoundHandleActive(sound1->_handle)) {
			delete sound1;
			sound1 = nullptr;
		}
	}

	IconBar::update(param1, param2);

	if (puzztest != nullptr) {
		puzztest->Do(puzztest->loc_x, puzztest->loc_y, 1.0);
	}

	DisplayButtons();
	DisplayMap();

	if (litdoors != nullptr) {
		DisplayLitDoors();
	}

	if (initialized != 0) {
		DisplayThisFloorRow();
	}
}

// Original: 0x40EFB0
void Handler4::OnClick(int x, int y) {
	if (initialized == 0) {
		initialized = 1;
		PlayPuzzleSound(0, 1);
	} else {
		// Check if click is in the button grid area (rect3)
		if (rect3.contains(x, y)) {
			// Check individual button hit
			for (int i = 0; i < 9; i++) {
				if (g_PuzzleRects1[i].contains(x, y)) {
					OnButtonClick(i);
				}
			}
			CheckSolution();
		} else if (rect2.contains(x, y)) {
			// Right display area click
			PlayPuzzleSound(3, 1);
		} else if (rect1.contains(x, y)) {
			// Left area click - check floor regions
			for (int i = 0; i < 3; i++) {
				if (g_PuzzleRects2[i].contains(x, y)) {
					PlayPuzzleSound(1, 1);
				}
			}

			// Check center (rect4) and bottom (rect5) areas
			if (rect4.contains(x, y)) {
				PlayPuzzleSound(0, 1);
			} else if (rect5.contains(x, y)) {
				PlayPuzzleSound(0, 1);
			}
		}
	}
}

// Original: 0x40F1C0
void Handler4::OnButtonClick(int buttonIndex) {
	int oldVal = buttonStates[buttonIndex];

	int rowStart = (buttonIndex / 3) * 3;
	PlayPuzzleSound(rowStart / 3 + 6, 1);

	// Clear entire row
	buttonStates[rowStart] = 0;
	buttonStates[rowStart + 1] = 0;
	buttonStates[rowStart + 2] = 0;

	// Toggle: if was off, turn on
	if (oldVal == 0) {
		buttonStates[buttonIndex] = 1;
	}

	needsUpdate = 1;
}

// Original: 0x40F220
int Handler4::CheckSolution() {
	floorStates[0] = 0;
	floorStates[1] = 0;
	floorStates[2] = 0;

	for (int entryIdx = 0; entryIdx < 9; entryIdx++) {
		// Build expected button state from the solution entry's 3 button indices
		int expected[9];
		memset(expected, 0, sizeof(expected));

		for (int j = 0; j < 3; j++) {
			expected[g_SolutionData[entryIdx].buttons[j]] = 1;
		}

		// Compare against current button states
		puzzleSolved = 1;
		for (int k = 0; k < 9; k++) {
			if (buttonStates[k] != expected[k]) {
				puzzleSolved = 0;
				break;
			}
		}

		if (puzzleSolved != 0) {
			// Determine which floor this solution activates
			int floorIdx;
			if (entryIdx < 2) {
				floorIdx = 0;
			} else if (entryIdx < 5) {
				floorIdx = 1;
			} else {
				floorIdx = 2;
			}

			floorStates[floorIdx] = 1;
			int soundIdx = (entryIdx < 2) ? 5 : 2;
			PlayPuzzleSound(soundIdx, 1);

			return g_SolutionData[entryIdx].result;
		}
	}

	return -1;
}

// Original: 0x40F310
void Handler4::ResetPuzzle() {
	floorStates[0] = 0;
	floorStates[1] = 0;
	floorStates[2] = 0;
	memset(buttonStates, 0, sizeof(buttonStates));
	puzzleSolved = 0;
	initialized = 0;

	Sprite *floorArray[3] = {lowfloor, midfloor, topfloor};
	for (int i = 0; i < 3; i++) {
		floorArray[i]->setState2(3);
	}

	for (int i = 0; i < 10; i++) {
		soundStates[i] = 1;
	}
}

// Original: 0x40F370
void Handler4::PlayPuzzleSound(int index, int loop) {
	playButtonSound(-1);

	if (index < 0 || index >= 10) {
		warning("Handler4::PlayPuzzleSound: %d is out of sound array range", index);
		return;
	}

	if (sound1 != nullptr) {
		sound1->Stop();
		delete sound1;
		sound1 = nullptr;
	}

	if (soundStates[index] != 0) {
		Common::String filename = Common::String::format("audio/snd%04d.wav", index + 11);
		soundStates[index] = loop;
		sound1 = new Sample();
		sound1->Load(filename);
		sound1->Play(100, 1);
	}
}

// Original: 0x40F490
void Handler4::DisplayButtons() {
	Sprite *btnArray[3] = {buttons1, buttons2, buttons3};

	for (int i = 0; i < 9; i++) {
		if (buttonStates[i] != 0) {
			Sprite *btn = btnArray[i / 3];
			btn->setState2(i % 3);
			btn->Do(btn->loc_x, btn->loc_y, 1.0);
		}
	}
}

// Original: 0x40F4F0
void Handler4::DisplayMap() {
	if (paths1 == nullptr || paths2 == nullptr || paths3 == nullptr) {
		warning("Handler4::DisplayMap: missing path sprites");
		return;
	}

	Sprite *pathArray[3] = {paths1, paths2, paths3};

	for (int i = 0; i < 9; i++) {
		if (buttonStates[i] != 0) {
			Sprite *path = pathArray[i / 3];
			path->setState2(i % 3);
			path->Do(path->loc_x, path->loc_y, 1.0);
		}
	}
}

// Original: 0x40F580
void Handler4::DisplayLitDoors() {
	for (int i = 0; i < 3; i++) {
		if (floorStates[i] != 0) {
			litdoors->setState2(i);
			int x = 0;
			if (i != 0) {
				// Original: x = ((i-1) < 1 ? 0xffffff5a : 0) + 0x138
				// When i==1: x = 0xffffff5a + 0x138 = -0xa6 + 0x138 = 0x92
				// When i==2: x = 0 + 0x138 = 0x138
				x = ((i == 1) ? 0x92 : 0x138);
			}
			// Original: y = ((i-1) < 1 ? 0xe : 0) + 0x60
			// When i==0: y = 0 + 0x60 = 0x60
			// When i==1: y = 0xe + 0x60 = 0x6e
			// When i==2: y = 0 + 0x60 = 0x60
			int y = ((i == 1) ? 0x6e : 0x60);
			litdoors->Do(x, y, 1.0);
		}
	}
}

// Original: 0x40F5F0
void Handler4::DisplayThisFloorRow() {
	Sprite *floorArray[3] = {lowfloor, midfloor, topfloor};

	if (lowfloor == nullptr || midfloor == nullptr || topfloor == nullptr)
		return;

	int floorRow[3] = {-1, -1, -1};

	// Determine which button is active in each row
	for (int i = 0; i < 9; i++) {
		if (buttonStates[i] != 0) {
			if (i <= 2) {
				floorRow[0] = i;
			} else if (i <= 5) {
				floorRow[1] = i % 3;
			} else {
				floorRow[2] = i % 3;
			}
		}
	}

	if (needsUpdate != 0) {
		needsUpdate = 0;
		for (int i = 0; i < 3; i++) {
			int state = floorRow[i];
			floorArray[i]->flags |= 0x20;
			if (state == -1) state = 3;
			floorArray[i]->setState2(state);
		}
	}

	for (int i = 0; i < 3; i++) {
		floorArray[i]->Do(floorArray[i]->loc_x, floorArray[i]->loc_y, 1.0);
	}
}

} // End of namespace Hypno
