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

#ifndef NEUROMANCER_TEXT_SCROLLER_H
#define NEUROMANCER_TEXT_SCROLLER_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Neuromancer {

// Driver for the "teletype" text reveal used by the DOS intro / news /
// bulletin-board / user-info screens. Matches the cadence of DOS
// window_animation.c's WA_TYPE_TEXT_SCROLLING: one line appears per tick
// interval; when the screen fills, state goes to kWaitingForInput and
// waits for the player to press something; when there are no more lines,
// state transitions to kComplete.
//
// The scroller does NOT own a sprite buffer -- callers supply one on
// render(). That keeps the surface lifetime owned by the widget that
// hosts the scroller (PAX window sprite, scene scroll widget, etc).
class TextScroller {
public:
	enum State {
		kIdle,             // nothing loaded
		kRunning,          // revealing lines on a timer
		kWaitingForInput,  // screen full; player must press any key
		kComplete          // final line shown, player may dismiss
	};

	TextScroller();

	// Load `body` as the text to scroll. The body is first expanded
	// (DOS 0x01/0x02 control codes -> player name / date) then word-
	// wrapped to `columns`. Resets state to kRunning (or kComplete if
	// the body is empty after wrap). `frameCapMs` is the per-line delay
	// and `maxLinesVisible` sets the pause threshold.
	void start(const char *body, int columns,
	           int maxLinesVisible, int frameCapMs);

	// Called per-frame with the current wall clock (g_system->getMillis).
	// Advances internal state and returns the new State. No-op if Idle or
	// Complete (they need an explicit start() / acknowledge() to move on).
	State tick(uint32 nowMs);

	// Clear a kWaitingForInput pause so the scroll resumes on the next
	// tick. No effect in other states; kComplete still requires callers
	// to decide what comes next.
	void acknowledge();

	State state() const { return _state; }

	// Number of lines currently displayed (== window height / 8).
	// Lines beyond this are queued for a future screenful.
	int visibleLines() const;

	// Access the line at visual slot `i` (0..visibleLines()-1). Empty if
	// `i` is beyond the currently-revealed count.
	const Common::String &lineAt(int i) const;

	// Raw access for rendering helpers; returns total revealed on the
	// current screenful.
	int currentScreenLineCount() const { return _onScreen; }

private:
	State _state;

	Common::Array<Common::String> _lines;
	int _lineCursor;        // index of the next line to reveal
	int _onScreen;          // how many lines currently painted
	int _maxLinesVisible;
	int _frameCapMs;
	uint32 _lastStepMs;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_TEXT_SCROLLER_H
