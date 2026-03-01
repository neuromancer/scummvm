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

#ifndef HYPNO_TEACHER_MOUSECONTROL_H
#define HYPNO_TEACHER_MOUSECONTROL_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Sprite.h"
#include "common/array.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace Hypno {

// Original: MouseControl class (0x41ECA0-0x41F200, 0x422D98)
// Manages the visual cursor (pointer) using a Smacker sprite sheet.
// Each cursor state (POINTER, EXAMINE, PICKUP) maps to a frame range
// in the SMK and has its own hotspot offset.
//
// In ScummVM, instead of rendering the sprite to the framebuffer each tick
// (as the original does via Sprite::Do → ZBufferManager blit), we decode
// each state's frame and use CursorMan to set the hardware cursor.

class MouseControl : public Parser {
public:
	MouseControl();
	~MouseControl() override;

	int lblParse(const Common::String &line) override;

	// Decode cursor frames from the sprite's SMK and register with CursorMan
	void loadCursorFrames();

	// Original: DrawCursor (0x41F200)
	// Sets the active cursor state and updates CursorMan
	void setCursorState(int state);

	int getCursorState() const { return _currentState; }

	// Original: labels[0]="POINTER", [1]="EXAMINE", [2]="PICKUP"
	static const int kMaxStates = 25;

private:
	Sprite *_sprite;           // Original: m_sprite (0xEC)

	struct CursorFrame {
		Graphics::Surface surface;
		byte palette[256 * 3];
		bool valid;
		CursorFrame() : valid(false) { memset(palette, 0, sizeof(palette)); }
	};

	Common::Array<CursorFrame> _cursorFrames;  // Decoded frame per state
	Common::Point _hotspots[kMaxStates];       // Original: m_hotspots (0xF4)
	Common::String _labels[kMaxStates];        // Original: m_labels (0x88)
	int _currentState;
};

} // End of namespace Hypno

#endif
