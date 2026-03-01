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

#include "hypno/teacher/GameState.h"
#include "hypno/hypno.h"
#include "common/array.h"

namespace Hypno {

GameState::GameState() : maxStates(0) {}
GameState::~GameState() {}

void GameState::clearStates() {
	for (uint i = 0; i < stateValues.size(); ++i)
		stateValues[i] = 0;
}

void GameState::setMaxStates(int count) {
	maxStates = count;
	stateValues.resize(count);
	stateLabels.resize(count);
	clearStates();
}

int GameState::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "MAXSTATES") {
		// Original: sscanf(line, "%s %d %s", keyword, &index, labelName)
		// MAXSTATES <count> [optional_label]
		Common::String countStr, dummy;
		tokenize(rest, countStr, dummy);
		int count = atoi(countStr.c_str());
		setMaxStates(count);
	} else if (keyword == "LABEL") {
		// Original: sscanf(line, "%s %d %s", keyword, &index, labelName)
		Common::String indexStr, labelName;
		tokenize(rest, indexStr, labelName);
		labelName.trim();
		int index = atoi(indexStr.c_str());
		if (index >= 0 && index < maxStates) {
			stateLabels[index] = labelName;
		}
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

int GameState::findState(const Common::String &stateName) {
	for (int i = 0; i < maxStates; ++i) {
		if (stateLabels[i] == stateName)
			return i;
	}
	debugC(1, kHypnoDebugScene, "GameState::findState: '%s' not found (maxStates=%d)", stateName.c_str(), maxStates);
	return -1;
}

Common::String GameState::getState(int stateIndex) {
	if (stateIndex >= 0 && stateIndex < maxStates)
		return stateLabels[stateIndex];
	return "";
}

} // End of namespace Hypno
