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

#include "neuromancer/text_scroller.h"

#include "neuromancer/font.h"

namespace Neuromancer {

TextScroller::TextScroller()
	: _state(kIdle),
	  _lineCursor(0),
	  _onScreen(0),
	  _maxLinesVisible(1),
	  _frameCapMs(120),
	  _lastStepMs(0) {}

void TextScroller::start(const char *body, int columns,
                         int maxLinesVisible, int frameCapMs) {
	_lines.clear();
	_lineCursor = 0;
	_onScreen   = 0;
	_maxLinesVisible = (maxLinesVisible > 0) ? maxLinesVisible : 1;
	_frameCapMs = (frameCapMs > 0) ? frameCapMs : 1;
	_lastStepMs = 0;

	if (!body || !*body) {
		_state = kComplete;
		return;
	}

	// Word-wrap once up front; the scroller then reveals one wrapped line
	// per tick interval. Preserves explicit newlines in the source.
	Common::String wrapped = wrapText(body, columns);

	Common::String cur;
	for (uint i = 0; i < wrapped.size(); i++) {
		char c = wrapped[i];
		if (c == '\n') {
			_lines.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	if (!cur.empty())
		_lines.push_back(cur);

	if (_lines.empty()) {
		_state = kComplete;
		return;
	}

	_state = kRunning;
}

TextScroller::State TextScroller::tick(uint32 nowMs) {
	if (_state != kRunning)
		return _state;

	// First tick: seed timer + reveal the opening line so there's always
	// something on screen before the first frame cap fires.
	if (_lastStepMs == 0) {
		_lastStepMs = nowMs;
		if (_lineCursor < (int)_lines.size() && _onScreen < _maxLinesVisible) {
			_onScreen++;
			_lineCursor++;
			if (_lineCursor >= (int)_lines.size()) {
				_state = kComplete;
				return _state;
			}
			if (_onScreen == _maxLinesVisible) {
				_state = kWaitingForInput;
				return _state;
			}
		}
		return _state;
	}

	// Step one line forward per frame cap interval. Catch up on multiple
	// elapsed intervals in a single call (useful after a long pause /
	// resume) but stop as soon as we hit a blocking transition.
	while (nowMs - _lastStepMs >= (uint32)_frameCapMs) {
		_lastStepMs += (uint32)_frameCapMs;
		if (_lineCursor >= (int)_lines.size()) {
			_state = kComplete;
			break;
		}
		_onScreen++;
		_lineCursor++;
		if (_lineCursor >= (int)_lines.size()) {
			_state = kComplete;
			break;
		}
		if (_onScreen >= _maxLinesVisible) {
			_state = kWaitingForInput;
			break;
		}
	}

	return _state;
}

void TextScroller::acknowledge() {
	if (_state == kWaitingForInput) {
		// Clear the screenful and resume revealing.
		_onScreen = 0;
		_lastStepMs = 0;
		_state = kRunning;
	}
}

int TextScroller::visibleLines() const {
	return _onScreen;
}

const Common::String &TextScroller::lineAt(int i) const {
	static const Common::String empty;
	if (i < 0 || i >= _onScreen)
		return empty;
	int idx = _lineCursor - _onScreen + i;
	if (idx < 0 || idx >= (int)_lines.size())
		return empty;
	return _lines[idx];
}

} // End of namespace Neuromancer
