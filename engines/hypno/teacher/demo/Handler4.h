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

#ifndef HYPNO_TEACHER_DEMO_HANDLER4_H
#define HYPNO_TEACHER_DEMO_HANDLER4_H

#include "hypno/teacher/IconBar.h"
#include "hypno/teacher/Palette.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Sample.h"
#include "common/rect.h"

namespace Hypno {

// Original: SolutionEntry from globals.h
// Each entry defines 3 button indices that form a valid solution,
// and which floor (0-2) it activates.
struct SolutionEntry {
	int buttons[3];
	int result;
};

// Handler4 - Command 4 Handler (Forcefield Puzzle / SCIpuzz1)
// Original size: 0x6F0 bytes, vtable: 0x431288
// Inherits from IconBar
class Handler4 : public IconBar {
public:
	Handler4();
	~Handler4() override;

	void init(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	int addMessage(SC_Message *msg) override;
	int deinit(SC_Message *msg) override;
	void update(int param1, int param2) override;

	// Handler4-specific methods
	void ResetPuzzle();
	void OnClick(int x, int y);
	void OnButtonClick(int buttonIndex);
	int CheckSolution();
	void DisplayButtons();
	void DisplayMap();
	void DisplayLitDoors();
	void DisplayThisFloorRow();
	void PlayPuzzleSound(int index, int loop);

	// Handler4-specific fields (original offset 0x600+)
	TeacherPalette *palette;     // 0x600
	Sprite *puzztest;            // 0x604
	Sprite *litdoors;            // 0x608
	Sprite *paths1;              // 0x60C
	Sprite *paths2;              // 0x610
	Sprite *paths3;              // 0x614
	Sprite *buttons1;            // 0x618
	Sprite *buttons2;            // 0x61C
	Sprite *buttons3;            // 0x620
	Sprite *lowfloor;            // 0x624
	Sprite *midfloor;            // 0x628
	Sprite *topfloor;            // 0x62C
	Sample *sound1;              // 0x630
	Sample *sound2;              // 0x634
	int soundStates[10];         // 0x638
	int floorStates[3];          // 0x660
	int buttonStates[9];         // 0x66C
	int puzzleSolved;            // 0x690
	int initialized;             // 0x694
	int needsUpdate;             // 0x698
	Common::Rect rect1;          // 0x69C - left area
	Common::Rect rect2;          // 0x6AC - right display area
	Common::Rect rect3;          // 0x6BC - button grid area
	Common::Rect rect4;          // 0x6CC - center area
	Common::Rect rect5;          // 0x6DC - bottom area
};

} // End of namespace Hypno

#endif
