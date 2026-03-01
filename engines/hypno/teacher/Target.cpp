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

#include "hypno/teacher/Target.h"
#include "hypno/teacher/CombatEngine.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/system.h"

namespace Hypno {

// ============================================================================
// Target
// ============================================================================

// Original: 0x413DC0
Target::Target() : Sprite() {
	active = 0;
	targetFlags = 0;
	id = -1;
	combatBonus2 = 0;
	hotspotIndex = 0;
	stopSound = nullptr;
	progressSound = nullptr;
	hitSound = nullptr;
	sound3 = nullptr;
	pendingAction = 0;
	field_0x154 = 0;
	_rangeCounter = 0;
}

// Original: 0x413F10
Target::~Target() {
	// Sounds are owned by SoundList, don't delete them here
}

// Original: 0x414060
void Target::Spawn() {
	Activate();

	// Original: animation_data->SetPalette() which uses SmackColorRemap
	// to remap pixel indices to match the system palette. In ScummVM,
	// the system palette is set once in SetupViewport and we don't
	// remap per-sprite pixels, so we skip palette changes here.

	if (stopSound)
		stopSound->Play(100, 1);

	if (g_scoreManager)
		g_scoreManager->completionCount++;
}

// Original: 0x4140B0
void Target::Activate() {
	if (active != 0)
		return;

	// Register in active targets hash
	if (g_targetList) {
		g_targetList->activeTargets[id] = this;
	}

	// Initialize hotspot processing
	hotspotIndex = 0;

	progressRange.start = 0;
	active = 1;
	setState2(animRange.start);

	loc_x = animParam.start;
	loc_y = animParam.end;

	pendingAction = 0;
}

// Original: 0x414230
void Target::Deactivate() {
	if (active == 0)
		return;

	// Remove from active targets
	if (g_targetList) {
		g_targetList->activeTargets.erase(id);
	}

	freeAnimation();
	active = 0;
}

// Original: 0x4142C0
int Target::CheckTimeInRange() {
	// In the original, this checks if the pixel at the mouse position
	// falls within the target's palette range (timeRange).
	// In ScummVM, we approximate with bounding box collision using mouse position.
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	Common::Point mouse = engine->getMousePos();

	// Check if mouse is within the sprite's bounding area
	if (animation_data && animation_data->hasLastFrame()) {
		int w = animation_data->_lastFrame.w;
		int h = animation_data->_lastFrame.h;
		if (mouse.x >= loc_x && mouse.x < loc_x + w &&
		    mouse.y >= loc_y && mouse.y < loc_y + h) {
			return 1;
		}
	}
	return 0;
}

// Original: 0x414310
int Target::CheckTimeInRangeParam(int px, int py) {
	// Check collision at specific coordinates
	if (animation_data && animation_data->hasLastFrame()) {
		int w = animation_data->_lastFrame.w;
		int h = animation_data->_lastFrame.h;
		if (px >= loc_x && px < loc_x + w &&
		    py >= loc_y && py < loc_y + h) {
			return 1;
		}
	}
	return 0;
}

// Original: 0x414350
int Target::AdvanceHotspot() {
	if (hotspotFrames.empty())
		return 0;

	int currentId = -1;
	if (hotspotIndex < (int)hotspotFrames.size()) {
		currentId = hotspotFrames[hotspotIndex];
	} else {
		return 0;
	}

	int frameNum = 0;
	if (animation_data && animation_data->_decoder) {
		frameNum = animation_data->getCurFrame();
	}

	if (frameNum >= currentId || currentId == 0) {
		// Advance to next hotspot
		hotspotIndex++;
		if (g_targetList)
			g_targetList->currentTarget = this;
		return 1;
	}
	return 0;
}

// Original: 0x4143B0
void Target::UpdateProgress(int delta) {
	if (active != 1)
		return;

	int newValue = progressRange.start + delta;
	progressRange.start = newValue;

	int shouldTrigger = 0;
	if (progressRange.end != 0) {
		shouldTrigger = (newValue >= progressRange.end) ? 1 : 0;
	}

	if (shouldTrigger) {
		pendingAction = 3;
		return;
	}

	if (progressSound)
		progressSound->Play(100, 1);
}

// Original: 0x414410
int Target::Update() {
	if (active == 0)
		return 1;

	switch (pendingAction) {
	case 1:
		// Miss animation
		if (current_state == animRange.end) {
			if (g_scoreManager)
				g_scoreManager->score -= hitMissPoints.end;
			Deactivate();
			return 1;
		}
		setState2(current_state + 1);
		break;

	case 3:
		// Hit animation
		active = 3;
		setState2(hitRange.start + current_state);
		if ((targetFlags & 1) != 0) {
			loc_x = hitOffset.start;
			loc_y = hitOffset.end;
		}
		if (stopSound)
			stopSound->Stop();
		if (hitSound)
			hitSound->Play(100, 1);
		if (g_scoreManager) {
			g_scoreManager->missCount++;
			g_scoreManager->score += hitMissPoints.start;
			g_scoreManager->AdjustScore(scoreWeight.start);
		}
		if (g_combatEngine) {
			g_combatEngine->m_combatBonus1 += combatBonus.start;
			g_combatEngine->m_combatBonus2 += combatBonus2;
		}
		break;
	}

	int y = loc_y;
	int x = loc_x;
	pendingAction = 0;

	if (Do(x, y, 1.0)) {
		if (active == 3) {
			Deactivate();
		} else {
			pendingAction = active;
		}
	}
	return 0;
}

// Original: 0x4145E0
void Target::Init(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (!keyword.empty() && keyword.compareToIgnoreCase("INIT") != 0) {
		animFilename = keyword;
		// Convert backslashes to forward slashes
		for (uint32 i = 0; i < animFilename.size(); ++i) {
			if (animFilename[i] == '\\')
				animFilename.setChar('/', i);
		}
	}
}

// Original: 0x414690
void Target::ParseSound(const Common::String &line) {
	if (line.size() < 4)
		return;

	int index = line[1] - '0';
	Common::String soundFile = line.substr(3);
	soundFile.trim();
	// Convert backslashes
	for (uint32 i = 0; i < soundFile.size(); ++i) {
		if (soundFile[i] == '\\')
			soundFile.setChar('/', i);
	}

	Sample *sound = nullptr;
	if (g_soundList)
		sound = g_soundList->Register(soundFile);

	switch (index) {
	case 0:
		stopSound = sound;
		break;
	case 1:
		progressSound = sound;
		break;
	case 2:
		hitSound = sound;
		break;
	case 3:
		sound3 = sound;
		break;
	default:
		warning("Target::ParseSound - Undefined sound type: %s", line.c_str());
		break;
	}
}

// Original: 0x414730
void Target::onProcessStart() {
	_rangeCounter = 0;

	// Set filename and initialize sprite
	if (!animFilename.empty()) {
		Common::String fnameCmd = Common::String::format("FNAME %s", animFilename.c_str());
		Sprite::lblParse(fnameCmd);
	}
	initStates(40);

	flags |= 0x40;   // TRANSPARENT
	flags &= ~2;     // TOPLEFT
	flags |= 0x200;  // Don't loop at end

	ensureInit();

	// Set default sounds from target list
	if (g_targetList) {
		stopSound = g_targetList->defaultStopSound;
		progressSound = g_targetList->defaultProgressSound;
		hitSound = g_targetList->defaultHitSound;
		sound3 = g_targetList->defaultSound;
	}
}

// Original: 0x4147F0
void Target::onProcessEnd() {
	// If no hotspot was specified but we have a bearing value from 'B' parsing,
	// add a default hotspot at the last B value's end frame
	// This is handled implicitly since _rangeCounter tracks the state
}

// Original: 0x414930
int Target::lblParse(const Common::String &line) {
	if (line.empty())
		return 0;

	char firstChar = line[0];
	Common::String rest = (line.size() > 2) ? line.substr(2) : "";
	rest.trim();

	if (firstChar == 'A') {
		// Animation position: A <x> <y>
		if (rest.size() > 0)
			sscanf(rest.c_str(), "%d %d", &animParam.start, &animParam.end);
	} else if (firstChar == 'B') {
		// Animation range: B <startFrame> <endFrame>
		int value1 = 0, value2 = 0;
		if (sscanf(rest.c_str(), "%d %d", &value1, &value2) == 2) {
			setRange(_rangeCounter, value1, value2);
			animRange.end = _rangeCounter;
			_rangeCounter++;
		}
	} else if (firstChar == 'C') {
		// Progress required: C<count>
		Common::String val = line.substr(1);
		val.trim();
		sscanf(val.c_str(), "%d", &progressRange.end);
	} else if (firstChar == 'D') {
		// Hit points: D <points>
		sscanf(rest.c_str(), "%d", &hitMissPoints.start);
	} else if (firstChar == 'F') {
		// Do nothing (flag)
	} else if (firstChar == 'H') {
		// Hotspot frame: H <frame>
		int value1 = 0;
		if (sscanf(rest.c_str(), "%d", &value1) == 1) {
			hotspotFrames.push_back(value1);
		}
	} else if (firstChar == 'I') {
		// Identifier: I <name>
		Common::String idStr;
		tokenize(rest, idStr, rest);
		if (!idStr.empty()) {
			identifier = idStr;
		}
	} else if (firstChar == 'K') {
		// Kill/hit animation range: K <startFrame> <endFrame>
		int value1 = 0, value2 = 0;
		if (sscanf(rest.c_str(), "%d %d", &value1, &value2) == 2) {
			setRange(_rangeCounter, value1, value2);
			if (hitRange.start == 0)
				hitRange.start = _rangeCounter;
			hitRange.end = _rangeCounter;
			_rangeCounter++;
		}
	} else if (firstChar == 'O') {
		// Hit offset: O <x> <y>
		sscanf(rest.c_str(), "%d %d", &hitOffset.start, &hitOffset.end);
		targetFlags |= 1;
	} else if (firstChar == 'P') {
		// Palette range: P <start> <end>
		sscanf(rest.c_str(), "%d %d", &timeRange.start, &timeRange.end);
	} else if (firstChar == 'S') {
		// Sound: S<index> <filename>
		ParseSound(line);
	} else if (firstChar == 'V') {
		// Score weight: V <value>
		sscanf(rest.c_str(), "%d", &scoreWeight.start);
	} else if (firstChar == 'W') {
		// Score weight loss: W <value>
		sscanf(rest.c_str(), "%d", &scoreWeight.end);
	} else if (firstChar == 'Z') {
		return 1;
	}

	return 0;
}

// ============================================================================
// TargetList
// ============================================================================

// Original: 0x414D80
TargetList::TargetList() : Parser() {
	count = 0;
	for (int i = 0; i < 70; i++)
		targets[i] = nullptr;
	currentTarget = nullptr;
	field_0x1a8 = 0;
	field_0x1ac = 0;
	defaultStopSound = nullptr;
	defaultProgressSound = nullptr;
	defaultHitSound = nullptr;
	defaultSound = nullptr;
}

// Original: 0x414DF0
TargetList::~TargetList() {
	for (int i = 0; i < count; i++) {
		delete targets[i];
		targets[i] = nullptr;
	}
	activeTargets.clear();
}

// Original: 0x414F30
void TargetList::onProcessEnd() {
	// Hash table is implicitly managed via Common::HashMap
}

// Original: 0x414FD0
int TargetList::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty())
		return 0;

	if (keyword[0] == 'F') {
		// Target definition: F<id> <filename>
		Target *pTarget = new Target();
		targets[count] = pTarget;
		targets[count]->id = count;
		count++;

		// Parse INIT filename from rest of line after F<id>
		Common::String initLine = (keyword.size() > 1) ? rest : "";
		pTarget->Init(initLine);
		Parser::processFile(pTarget, this, "");
		return 0;
	}

	if (keyword.equalsIgnoreCase("END")) {
		return 1;
	}

	return 0;
}

// Original: 0x4150F0
Target *TargetList::ProcessTargets() {
	Target *fallbackTarget = nullptr;
	currentTarget = nullptr;

	if (activeTargets.empty())
		return nullptr;

	// Iterate all active targets
	// Use a copy of keys since Update/Deactivate may modify the map
	Common::Array<int> activeIds;
	for (auto it = activeTargets.begin(); it != activeTargets.end(); ++it) {
		activeIds.push_back(it->_key);
	}

	for (uint i = 0; i < activeIds.size(); i++) {
		if (!activeTargets.contains(activeIds[i]))
			continue;

		Target *target = activeTargets[activeIds[i]];
		if (!target)
			continue;

		if (target->Update() == 0) {
			if (target->AdvanceHotspot() != 0) {
				currentTarget = target;
				if (g_scoreManager) {
					g_scoreManager->hitCount++;
					g_scoreManager->AdjustScore(-target->scoreWeight.end);
				}
				if (defaultSound)
					defaultSound->Play(100, 1);
			}
			if (!fallbackTarget && target->CheckTimeInRange() != 0) {
				fallbackTarget = target;
			}
		}
	}

	return fallbackTarget;
}

} // End of namespace Hypno
