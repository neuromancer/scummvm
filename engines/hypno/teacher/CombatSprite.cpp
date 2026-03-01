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

#include "hypno/teacher/CombatSprite.h"
#include "hypno/teacher/CombatEngine.h"
#include "hypno/teacher/Target.h"

namespace Hypno {

// Original: 0x415410
CombatSprite::CombatSprite() : Parser() {
	_currentSpriteId = -1;
	_currentSequence = nullptr;
	_currentIndex = 0;
	_parsingSpriteId = 0;
}

// Original: 0x415480
CombatSprite::~CombatSprite() {
	_spriteData.clear();
}

void CombatSprite::finishCurrentSprite() {
	if (!_parsingSequence.empty()) {
		_spriteData[_parsingSpriteId] = _parsingSequence;
		_parsingSequence.clear();
	}
}

// Original: ParseSpriteData (0x415960)
// Parses "frame,spriteName frame,spriteName ..." entries
void CombatSprite::parseSpriteData(const Common::String &data) {
	Common::String remaining = data;
	remaining.trim();

	while (!remaining.empty()) {
		// Find next token
		Common::String token;
		size_t spacePos = remaining.findFirstOf(" \t");
		if (spacePos == Common::String::npos) {
			token = remaining;
			remaining = "";
		} else {
			token = remaining.substr(0, spacePos);
			remaining = remaining.substr(spacePos + 1);
			remaining.trim();
		}

		if (token.empty())
			continue;

		// Parse "frame,spriteName"
		size_t commaPos = token.find(',');
		if (commaPos == Common::String::npos)
			continue;

		Common::String frameStr = token.substr(0, commaPos);
		Common::String spriteName = token.substr(commaPos + 1);

		int frameIndex = atoi(frameStr.c_str());
		if (frameIndex == 0)
			continue;

		// Find target index by identifier
		int spriteIdx = -1;
		if (g_targetList) {
			for (int i = 0; i < g_targetList->count; i++) {
				if (g_targetList->targets[i] &&
				    g_targetList->targets[i]->identifier.equalsIgnoreCase(spriteName)) {
					spriteIdx = i;
					break;
				}
			}
		}

		if (spriteIdx < 0) {
			warning("CombatSprite: Unknown sprite id '%s'", spriteName.c_str());
			continue;
		}

		_parsingSequence.push_back(SpriteDataEntry(frameIndex, spriteIdx));
	}
}

// Original: 0x41569E
int CombatSprite::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty())
		return 0;

	if (keyword[0] == 'S' && keyword.size() > 1) {
		// New sprite definition: S <id> <data...>
		finishCurrentSprite();

		// Parse sprite ID
		Common::String idStr;
		tokenize(rest, idStr, rest);
		_parsingSpriteId = atoi(idStr.c_str());

		// Parse inline data if present
		if (!rest.empty())
			parseSpriteData(rest);

		return 0;
	}

	if (keyword.equalsIgnoreCase("END")) {
		finishCurrentSprite();
		return 1;
	}

	if (keyword.equalsIgnoreCase("S")) {
		// Standalone S token - finish current sprite
		finishCurrentSprite();
		return 0;
	}

	// Otherwise it's continuation data for the current sprite
	parseSpriteData(line);
	return 0;
}

// Original: 0x4155E0
int CombatSprite::PlayById(int spriteId) {
	_currentIndex = 0;
	_currentSequence = nullptr;

	if (_spriteData.contains(spriteId)) {
		_currentSpriteId = spriteId;
		_currentSequence = &_spriteData[spriteId];
		if (_currentSequence->empty()) {
			_currentSequence = nullptr;
			return 0;
		}
		_currentIndex = 0;
		return 1;
	}
	return 0;
}

// Original: 0x415B90
int CombatSprite::ProcessFrame(int frame) {
	int count = -1;

	if (!_currentSequence || _currentIndex >= _currentSequence->size())
		return count;

	count = 0;

	while (_currentIndex < _currentSequence->size()) {
		const SpriteDataEntry &entry = (*_currentSequence)[_currentIndex];

		if (entry.frame > frame)
			break;  // Not time yet

		// Spawn target
		if (g_targetList && entry.targetIndex >= 0 && entry.targetIndex < g_targetList->count) {
			Target *target = g_targetList->targets[entry.targetIndex];
			if (target && target->active == 0) {
				count++;
				target->Spawn();
			}
		}

		_currentIndex++;
	}

	return count;
}

} // End of namespace Hypno
