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

#include "hypno/teacher/MouseControl.h"
#include "hypno/grammar.h"
#include "hypno/hypno.h"
#include "common/file.h"
#include "common/path.h"
#include "common/debug.h"
#include "common/system.h"
#include "graphics/cursorman.h"

namespace Hypno {

// Original: MouseControl::MouseControl (0x41ECA0)
MouseControl::MouseControl() : _sprite(nullptr), _currentState(0) {
	// Original hardcoded labels
	_labels[0] = "POINTER";
	_labels[1] = "EXAMINE";
	_labels[2] = "PICKUP";
	_labels[3] = "";
}

MouseControl::~MouseControl() {
	delete _sprite;
	for (uint i = 0; i < _cursorFrames.size(); i++) {
		if (_cursorFrames[i].valid) {
			_cursorFrames[i].surface.free();
		}
	}
}

// Original: MouseControl::LBLParse (0x41EF50)
int MouseControl::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "AUDIO") {
		// Original: loads Sample for cursor audio - skip for now
		debugC(1, kHypnoDebugScene, "MouseControl: AUDIO '%s' (skipped)", rest.c_str());
	} else if (keyword == "SPRITE") {
		// Original: m_sprite = new Sprite(0); m_sprite->flags &= ~2;
		// Parser::ProcessFile(m_sprite, this, 0);
		_sprite = new Sprite();
		_sprite->flags &= ~2;  // Clear BOTTOMLEFT flag (TOPLEFT mode)
		Parser::processFile(_sprite, this, "");
	} else if (keyword == "HOTPIXEL") {
		// Original: sscanf(line, " %s %d %d %d", cmd, &index, &x, &y)
		int index = 0, x = 0, y = 0;
		if (sscanf(rest.c_str(), "%d %d %d", &index, &x, &y) >= 3) {
			if (index >= 0 && index < kMaxStates) {
				_hotspots[index].x = x;
				_hotspots[index].y = y;
			}
		}
	} else if (keyword == "LABLE") {
		// Original: note the typo "LABLE" in the original code
		int index = 0;
		char labelBuf[32] = {0};
		if (sscanf(rest.c_str(), "%d %31s", &index, labelBuf) >= 2) {
			if (index >= 0 && index < kMaxStates) {
				_labels[index] = labelBuf;
			}
		}
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

// Decode cursor frames from the sprite's SMK file.
// For each state, seek to the first frame of that state's range and
// capture the surface + palette for use with CursorMan.
void MouseControl::loadCursorFrames() {
	if (!_sprite || _sprite->_filename.empty()) {
		warning("MouseControl: no sprite to load cursor frames from");
		return;
	}

	// Open the SMK file directly
	HypnoSmackerDecoder decoder;
	Common::File *file = new Common::File();
	if (!file->open(Common::Path(_sprite->_filename))) {
		warning("MouseControl: cannot open '%s'", _sprite->_filename.c_str());
		delete file;
		return;
	}
	if (!decoder.loadStream(file)) {
		warning("MouseControl: cannot load SMK '%s'", _sprite->_filename.c_str());
		return;
	}
	decoder.start();

	_cursorFrames.resize(_sprite->num_states);

	for (int i = 0; i < _sprite->num_states; i++) {
		// Seek to the first frame of this state's range
		// Ranges are 1-based in the original; forceSeekToFrame is 0-based
		int targetFrame = _sprite->ranges[i].start - 1;
		if (targetFrame < 0) targetFrame = 0;

		const Graphics::Surface *frame = decoder.forceSeekToFrame(targetFrame);
		if (frame) {
			_cursorFrames[i].surface.copyFrom(*frame);
			_cursorFrames[i].valid = true;

			// Grab the palette
			if (decoder.hasDirtyPalette() && decoder.getPalette()) {
				memcpy(_cursorFrames[i].palette, decoder.getPalette(), 256 * 3);
			} else if (i > 0 && _cursorFrames[0].valid) {
				// Reuse first frame's palette if no dirty palette for this frame
				memcpy(_cursorFrames[i].palette, _cursorFrames[0].palette, 256 * 3);
			}
		} else {
			warning("MouseControl: failed to decode frame %d for state %d", targetFrame, i);
		}
	}

	decoder.close();

	warning("MouseControl: loaded %d cursor states from '%s'",
		_sprite->num_states, _sprite->_filename.c_str());

	// Set initial cursor state
	setCursorState(0);
}

// Original: MouseControl::DrawCursor (0x41F200)
// In the original, this renders the sprite at mouse position minus hotspot.
// In ScummVM, we use CursorMan which handles this automatically via hotspot.
void MouseControl::setCursorState(int state) {
	if (state < 0 || state >= (int)_cursorFrames.size()) {
		warning("MouseControl: invalid cursor state %d", state);
		return;
	}

	if (!_cursorFrames[state].valid) {
		warning("MouseControl: cursor state %d has no valid frame", state);
		return;
	}

	_currentState = state;

	const Graphics::Surface &surf = _cursorFrames[state].surface;
	int hotX = _hotspots[state].x;
	int hotY = _hotspots[state].y;

	CursorMan.replaceCursor(surf.getPixels(), surf.w, surf.h, hotX, hotY, 0);
	CursorMan.replaceCursorPalette(_cursorFrames[state].palette, 0, 256);
	CursorMan.showMouse(true);
}

} // End of namespace Hypno
