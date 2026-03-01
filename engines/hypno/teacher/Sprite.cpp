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

#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"

namespace Hypno {

Sprite::Sprite() {
	flags = 0;
	current_state = 0;
	priority = 0;
	loc_x = 0;
	loc_y = 0;
	num_states = 1;
	ranges = new SpriteRange[1];
	ranges[0].start = 1;
	ranges[0].end = 5000;
	animation_data = nullptr;
}

Sprite::Sprite(const Common::String &filename) : _filename(filename) {
	flags = 0;
	current_state = 0;
	priority = 0;
	loc_x = 0;
	loc_y = 0;
	num_states = 1;
	ranges = new SpriteRange[1];
	ranges[0].start = 1;
	ranges[0].end = 5000;
	animation_data = nullptr;
}

Sprite::~Sprite() {
	delete animation_data;
	delete[] ranges;
}

// Original: Sprite::SetState (0x41D740) - sets number of states and allocates ranges
void Sprite::initStates(int numStates) {
	num_states = numStates;
	delete[] ranges;
	ranges = new SpriteRange[num_states];
	for (int i = 0; i < num_states; i++) {
		ranges[i].start = 1;
		ranges[i].end = 5000;
	}
	flags |= 0x20;
}

// Original: Sprite::SetRange (0x41D6D0)
void Sprite::setRange(int idx, int start, int end) {
	if (idx >= num_states) {
		warning("Sprite::SetRange out of range: %d >= %d in '%s'", idx, num_states, _filename.c_str());
		return;
	}
	ranges[idx].start = start;
	ranges[idx].end = end;
	flags |= 0x20;
}

// Lazy-init animation if needed (original: checks animation_data == 0 || targetBuffer == 0 → Init())
void Sprite::ensureInit() {
	if (!animation_data && !_filename.empty()) {
		animation_data = new TeacherAnimation();
		animation_data->play(_filename, 0);
		if (!animation_data->_decoder) {
			warning("Sprite: FAILED to load '%s'", _filename.c_str());
		}
		flags |= 0x20;
		setState2(current_state);
	}
}

// Original: Sprite::SetState2 (0x41D190) - ported line by line
// Uses 1-based frame numbers to match the original Smacker API.
// ScummVM's getCurFrame() is 0-based, so we add 1 when comparing to ranges.
void Sprite::setState2(int param_1) {
	if (param_1 == -1) {
		current_state = param_1;
		return;
	}

	int offset = 0;

	// Lazy init
	ensureInit();

	if (!animation_data || !animation_data->_decoder) {
		current_state = param_1;
		return;
	}

	if (param_1 < 0 || param_1 >= num_states) {
		warning("Sprite::SetState2 %d out of range (0..%d) '%s'", param_1, num_states - 1, _filename.c_str());
		return;
	}

	// Get current frame in 1-based (like the original smk->FrameNum)
	int current_frame = animation_data->getCurFrame() + 1;

	// Check if current frame is within the target state's range
	int in_range;
	if (ranges[param_1].start > current_frame || ranges[param_1].end < current_frame) {
		in_range = 0;
	} else {
		in_range = 1;
	}

	// If not in range, set the "needs reinit" flag
	if (in_range == 0) {
		flags |= 0x20;
	}

	// Early return: if flag 0x20 is not set and we're already in this state, nothing to do
	if ((flags & 0x20) == 0 && current_state == param_1) {
		return;
	}

	// KEEPOFFSET flag (0x10): preserve relative frame offset when switching states
	if ((flags & 0x10) != 0) {
		offset = current_frame - ranges[current_state].start;
		int new_end_frame = ranges[param_1].start + offset + 1;
		offset++;
		// Check if the offset position is within the new state's range
		if (new_end_frame < ranges[param_1].start || ranges[param_1].end < new_end_frame) {
			offset = 0;
		}
	}

	current_state = param_1;

	// Seek to the start of the state's range (+ optional offset)
	// Original: animation_data->GotoFrame(ranges[param_1].start + offset)
	// GotoFrame uses 1-based frame numbers. forceSeekToFrame uses 0-based.
	int targetFrame1 = ranges[param_1].start + offset;  // 1-based
	animation_data->gotoFrame(targetFrame1 - 1);        // convert to 0-based

	// If single-frame state, set the "first draw" flag
	if (ranges[current_state].end == ranges[current_state].start) {
		flags |= 4;
	}
	flags &= ~0x20;
}

// Original: Sprite::Do (0x41D300)
// The original manually advances one SMK frame per game tick (~84ms).
// In ScummVM, the game loop may run faster, so we use needsUpdate()
// to only advance when the decoder's timing says it's time.
int Sprite::Do(int x, int y, double scale) {
	if (current_state == -1) {
		return 1;
	}

	int done = 0;

	// Skip if NOGRAPHIC
	if ((flags & 0x80) != 0) {
		return 1;
	}

	// Lazy init
	ensureInit();

	if (!animation_data || !animation_data->_decoder)
		return 1;

	// Check if single-frame state (range.end == range.start)
	if (ranges[current_state].end == ranges[current_state].start) {
		// gotoFrame() in setState2() already decoded and cached the correct frame
		// via forceSeekToFrame(). No need to decodeAndAdvance() here — doing so
		// would advance past the desired frame and show the wrong image.
		flags &= ~4;
		// Draw the cached frame and report done
		if (!(flags & 0x100)) {
			bool transparent = (flags & 0x40) != 0;
			animation_data->drawLastFrame(((TeacherEngine *)g_engine)->_compositeSurface, x, y, transparent);
		}
		return 1;
	}

	// Multi-frame state: only advance when the decoder says it's time
	if (animation_data->needsUpdate()) {
		// Advance one frame
		animation_data->decodeAndAdvance();

		// Check position within range (1-based frame numbering)
		int frameNum1 = animation_data->getCurFrame() + 1;
		int remaining = ranges[current_state].end - frameNum1;

		if (remaining <= 1) {
			// At end of range
			if ((flags & 0x200) != 0) {
				// Flag 0x200: advance past end (don't loop)
			} else {
				// Loop back to start of range
				animation_data->gotoFrame(ranges[current_state].start - 1);
			}
			if ((flags & 1) == 0) {
				done = 1;
			}
		}
	}

	// Draw the cached frame to the composite surface
	if (!(flags & 0x100)) {
		bool transparent = (flags & 0x40) != 0;
		animation_data->drawLastFrame(((TeacherEngine *)g_engine)->_compositeSurface, x, y, transparent);
	}

	if ((flags & 1) != 0) {
		return 0;  // Looping sprites never report done
	}
	return done;
}

// Original: Sprite::FreeAnimation
// Releases animation data (used when deactivating targets).
void Sprite::freeAnimation() {
	if (animation_data) {
		delete animation_data;
		animation_data = nullptr;
	}
}

// Original: Sprite::StopAnimationSound
// Stops audio playback from the SMK decoder without changing visual state.
void Sprite::stopAnimationSound() {
	if (animation_data && animation_data->_decoder) {
		animation_data->_decoder->setVolume(0);
	}
}

int Sprite::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "FNAME") {
		// Original uses sscanf("%s %s") which gets only the first token,
		// ignoring trailing comments (;...) in the MIS file.
		Common::String fname;
		tokenize(rest, fname, rest);
		_filename = fname;
		for (uint32 i = 0; i < _filename.size(); ++i)
			if (_filename[i] == '\\') _filename[i] = '/';
	} else if (keyword == "LOC") {
		sscanf(rest.c_str(), "%d %d", &loc_x, &loc_y);
	} else if (keyword == "PRIORITY") {
		priority = atoi(rest.c_str());
	} else if (keyword == "TOPLEFT") {
		flags &= ~2;
	} else if (keyword == "BOTTOMLEFT") {
		flags |= 2;
	} else if (keyword == "LOOP") {
		flags |= 1;
	} else if (keyword == "TRANSPARENT") {
		flags |= 0x40;
	} else if (keyword == "NOGRAPHIC") {
		flags |= 0x80;
	} else if (keyword == "SCALE") {
		flags |= 8;
	} else if (keyword == "KEEPOFFSET") {
		flags |= 0x10;
	} else if (keyword == "STATES") {
		int n = atoi(rest.c_str());
		initStates(n);
	} else if (keyword == "RANGE") {
		int idx, s, e;
		if (sscanf(rest.c_str(), "%d %d %d", &idx, &s, &e) == 3) {
			setRange(idx, s, e);
		}
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

} // End of namespace Hypno
