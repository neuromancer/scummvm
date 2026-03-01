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

#include "hypno/teacher/CombatEngine.h"
#include "hypno/teacher/Target.h"
#include "hypno/teacher/Weapon.h"
#include "hypno/teacher/CombatSprite.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/system.h"
#include "common/events.h"

namespace Hypno {

// Global combat engine pointers (mirrors original globals)
CombatEngine *g_combatEngine = nullptr;
Sprite *g_consoleSprite = nullptr;
TargetList *g_targetList = nullptr;
Weapon *g_weapon = nullptr;
SoundList *g_soundList = nullptr;
CombatSprite *g_combatSprite = nullptr;
ScoreManager *g_scoreManager = nullptr;
Viewport *g_viewport = nullptr;
GameOutcome *g_gameOutcome = nullptr;
Navigator *g_navigator = nullptr;
EngineInfoParser *g_engineInfoParser = nullptr;

// ============================================================================
// SoundList
// ============================================================================
SoundList::SoundList(int maxSamples) {
}

SoundList::~SoundList() {
	for (auto it = _samples.begin(); it != _samples.end(); ++it) {
		delete it->_value;
	}
	_samples.clear();
}

Sample *SoundList::Register(const Common::String &filename) {
	if (_samples.contains(filename)) {
		return _samples[filename];
	}
	Sample *s = new Sample();
	s->Load(filename);
	_samples[filename] = s;
	return s;
}

void SoundList::StopAll() {
	for (auto it = _samples.begin(); it != _samples.end(); ++it) {
		it->_value->Stop();
	}
}

// ============================================================================
// GameOutcome
// ============================================================================
GameOutcome::GameOutcome() : outcome(0) {
}

void GameOutcome::PlayOutcomeVideo() {
	// Original: plays rat1/lose.smk or rat1/win.smk based on outcome
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	if (outcome == 1) {
		TeacherAnimation anim;
		anim.play("rat1/lose.smk", 0x20);
		// Play the video
		while (!anim.endOfVideo() && !engine->shouldQuit()) {
			if (anim.needsUpdate()) {
				anim.draw(engine->_compositeSurface, 0, 0, false, false);
				engine->drawScreen();
			}
			g_system->delayMillis(10);
		}
	} else if (outcome == 2) {
		TeacherAnimation anim;
		anim.play("rat1/win.smk", 0x20);
		while (!anim.endOfVideo() && !engine->shouldQuit()) {
			if (anim.needsUpdate()) {
				anim.draw(engine->_compositeSurface, 0, 0, false, false);
				engine->drawScreen();
			}
			g_system->delayMillis(10);
		}
	}
}

// ============================================================================
// ScoreManager (original: CursorState)
// ============================================================================
ScoreManager::ScoreManager() {
	score = 0;
	scoreInitial = 0;
	completionCount = 0;
	missCount = 0;
	field_0x10 = 0;
	hitCount = 0;
	shotsFired = 0;
	field_0x1c = 0;
	field_0x20 = 0;
}

// Original: 0x416ba0
void ScoreManager::AdjustScore(int value) {
	int newVal = score + value;
	score = newVal;
	if (newVal < 0) {
		score = 0;
		return;
	}
	if (newVal > 0xc8) {
		score = 0xc8;
	}
}

// ============================================================================
// Viewport
// ============================================================================
Viewport::Viewport() {
	dimW = dimH = 0;
	anchorX = anchorY = 0;
	sizeW = sizeH = 0;
	centerX = centerY = 0;
	scrollX = scrollY = 0;
	anchorOffsetY = 0;
}

void Viewport::SetDimensions(int w, int h) {
	dimW = w;
	dimH = h;
}

void Viewport::SetAnchor(int x, int y) {
	anchorX = x;
	anchorY = y;
}

void Viewport::SetDimensions2(int w, int h) {
	sizeW = w;
	sizeH = h;
}

void Viewport::SetCenter() {
	centerX = (sizeW - dimW) / 2;
	centerY = (sizeH - dimH) / 2;
}

void Viewport::SetCenterX(int x) {
	if (x < 0) x = 0;
	if (x > sizeW - dimW) x = sizeW - dimW;
	scrollX = x;
}

void Viewport::SetCenterY(int y) {
	if (y < 0) y = 0;
	if (y > sizeH - dimH) y = sizeH - dimH;
	scrollY = y;
}

// ============================================================================
// EngineInfoParser
// ============================================================================
EngineInfoParser::EngineInfoParser() {
	anchorRect = Common::Rect(0, 0, 320, 200);
	paletteStart = 0;
	paletteEnd = 0;
}

EngineInfoParser::~EngineInfoParser() {
}

// Original: 0x416D70
int EngineInfoParser::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "DIM") {
		int w, h;
		if (sscanf(rest.c_str(), "%d %d", &w, &h) == 2) {
			anchorRect.right = w;
			anchorRect.bottom = h;
		}
	} else if (keyword == "ANCHOR") {
		int x, y;
		if (sscanf(rest.c_str(), "%d %d", &x, &y) == 2) {
			anchorRect.left = x;
			anchorRect.top = y;
		}
	} else if (keyword == "PALE") {
		int start, end;
		if (sscanf(rest.c_str(), "%d %d", &start, &end) == 2) {
			paletteStart = start;
			paletteEnd = end;
		}
	} else if (keyword == "SOUND") {
		// Parse background sound
		Common::String soundFile;
		tokenize(rest, soundFile, rest);
		if (!soundFile.empty() && g_soundList) {
			CombatEngine *eng = g_combatEngine;
			if (eng) {
				eng->m_backgroundSample = g_soundList->Register(soundFile);
			}
		}
	} else if (keyword == "END") {
		return 1;
	}
	return 0;
}

// ============================================================================
// Navigator (original: mCNavigator)
// ============================================================================
Navigator::Navigator() : sprite(nullptr), _startingNode(1), _currentNode(nullptr),
                         _nodeActive(false), _nodeCounter(0), _parsingBaseNode(false) {
}

Navigator::~Navigator() {
	delete sprite;
}

// Original: mCNavigator::OnProcessEnd (0x413840)
void Navigator::onProcessEnd() {
	// Init background sprite (original: sprite->Init())
	if (sprite) {
		sprite->ensureInit();
	}

	// Find the starting node
	if (_nodes.contains(_startingNode)) {
		_currentNode = &_nodes[_startingNode];
	}
}

// Original: mCNavNode::Activate (0x4130F0)
void Navigator::activateNode(NavNode *node) {
	// BG flag: set background sprite animation state
	if ((node->flags & 1) != 0 && sprite) {
		sprite->setState2(node->animationState);
	}

	// SPRITE flag: pick a random sprite list and call PlayById
	if ((node->flags & 8) != 0 && !node->spriteListIds.empty() && g_combatSprite) {
		int randIdx = 0;
		if (node->spriteListIds.size() > 1) {
			randIdx = ((HypnoEngine *)g_engine)->_rnd->getRandomNumber(node->spriteListIds.size() - 1);
		}
		g_combatSprite->PlayById(node->spriteListIds[randIdx]);
	}

	// Reset frame counter (original: g_CombatEngine->m_framesL = 1)
	if (g_combatEngine) {
		g_combatEngine->m_framesL = 1;
	}

	_nodeCounter = 0;
	_nodeActive = true;
}

// Original: mCNavigator::Update (0x413BC0) + mCNavNode::Update (0x413280)
int Navigator::Update() {
	if (!_currentNode)
		return 0;

	// Activate node on first visit (original: mCNavNode::Update checks active==0)
	if (!_nodeActive) {
		activateNode(_currentNode);
	}

	// BG animation: call Do() on background sprite
	if ((_currentNode->flags & 1) != 0 && sprite) {
		int animResult = sprite->Do(sprite->loc_x, sprite->loc_y, 1.0);
		if (animResult != 0) {
			// Animation cycle complete
			if ((_currentNode->flags & 4) != 0) {
				// LOOP: count iterations
				_nodeCounter++;
				if (_nodeCounter < _currentNode->counterLimit) {
					return 0; // keep going
				}
			}
			// Node done - try to advance to next node
			_nodeActive = false;
			int nextId = _currentNode->nextNodeId;
			if (_nodes.contains(nextId)) {
				_currentNode = &_nodes[nextId];
				return 0;
			}
			// No next node found
			return 2;
		}
	}

	// EXIT_CODE flag: signal completion
	if ((_currentNode->flags & 0x10) != 0) {
		_nodeActive = false;
		if (g_gameOutcome)
			g_gameOutcome->outcome = 2;
		return _currentNode->nextNodeId;
	}

	return 0;
}

int Navigator::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	// If we're inside a BASENODE block, parse node-specific keywords
	if (_parsingBaseNode) {
		if (keyword.equalsIgnoreCase("IAM")) {
			_parsingNode.nodeHandle = atoi(rest.c_str());
		} else if (keyword.equalsIgnoreCase("BG")) {
			_parsingNode.animationState = atoi(rest.c_str());
			_parsingNode.flags |= 1;
		} else if (keyword.equalsIgnoreCase("LOOP")) {
			_parsingNode.counterLimit = atoi(rest.c_str());
			_parsingNode.flags |= 4;
		} else if (keyword.equalsIgnoreCase("EXIT_CODE")) {
			_parsingNode.nextNodeId = atoi(rest.c_str());
			_parsingNode.flags |= 0x10;
		} else if (keyword.equalsIgnoreCase("NEXTNODE")) {
			Common::String dir, nodeIdStr;
			tokenize(rest, dir, nodeIdStr);
			_parsingNode.nextNodeId = atoi(nodeIdStr.c_str());
		} else if (keyword.equalsIgnoreCase("SPRITE")) {
			Common::String name, idStr;
			tokenize(rest, name, idStr);
			int id = atoi(idStr.c_str());
			_parsingNode.spriteListIds.push_back(id);
			_parsingNode.flags |= 8;
		} else if (keyword.equalsIgnoreCase("NAME") || keyword.equalsIgnoreCase("PLAY_ANIM")) {
			// Informational or not yet needed
			if (keyword.equalsIgnoreCase("PLAY_ANIM"))
				_parsingNode.flags |= 2;
		} else if (keyword.equalsIgnoreCase("END")) {
			return 1;
		}
		return 0;
	}

	// Top-level navigator keywords
	if (keyword.equalsIgnoreCase("SPRITE")) {
		sprite = new Sprite();
		Parser::processFile(sprite, this, "");
	} else if (keyword.equalsIgnoreCase("STARTING_NODE")) {
		_startingNode = atoi(rest.c_str());
	} else if (keyword.equalsIgnoreCase("BEARING")) {
		// Navigation direction - not critical for basic combat
	} else if (keyword.equalsIgnoreCase("BASENODE")) {
		_parsingNode = NavNode();
		_parsingBaseNode = true;
		Parser::processFile(this, this, "");
		_parsingBaseNode = false;
		if (_parsingNode.nodeHandle != 0) {
			_nodes[_parsingNode.nodeHandle] = _parsingNode;
		}
	} else if (keyword.equalsIgnoreCase("END")) {
		return 1;
	}
	return 0;
}

// ============================================================================
// CombatEngine (original: Engine)
// ============================================================================

// Original: 0x4110D0
CombatEngine::CombatEngine() {
	m_targetList = nullptr;
	m_engineInfoParser = nullptr;
	m_consoleSprite = nullptr;
	m_viewport = nullptr;
	m_combatSprite = nullptr;
	m_weapon = nullptr;
	m_soundList = nullptr;
	m_scoreManager = nullptr;
	m_navigator = nullptr;
	m_gameOutcome = nullptr;
	m_combatBonus1 = 0;
	m_scrollOffsetX = 0;
	field_0xbc = 0;
	m_scrollOffsetY = 0;
	m_combatBonus2 = 0;
	m_viewOffset1X = 0;
	field_0xcc = 0;
	m_viewOffset1Y = 0;
	m_framesA = 0;
	field_0xd8 = 0;
	m_framesL = 0;
	m_backgroundSample = nullptr;
	field_0xe4 = 0;
	_mouseX = _mouseY = 0;
	_mouseButtons = _prevMouseButtons = 0;

	Initialize();
}

// Original: 0x4111D0
CombatEngine::~CombatEngine() {
	CleanupSubsystems();
}

void CombatEngine::onProcessStart() {}
void CombatEngine::onProcessEnd() {}

// Original: 0x411550
void CombatEngine::Initialize() {
	m_weapon = new Weapon();
	m_soundList = new SoundList(0x32);
	m_engineInfoParser = new EngineInfoParser();
	m_navigator = new Navigator();
	m_combatSprite = new CombatSprite();
	m_targetList = new TargetList();
	m_scoreManager = new ScoreManager();
	m_viewport = new Viewport();
	m_gameOutcome = new GameOutcome();

	CopyToGlobals();
}

// Original: 0x411A40
void CombatEngine::CleanupSubsystems() {
	delete m_consoleSprite;
	m_consoleSprite = nullptr;
	delete m_gameOutcome;
	m_gameOutcome = nullptr;
	delete m_navigator;
	m_navigator = nullptr;
	delete m_scoreManager;
	m_scoreManager = nullptr;
	delete m_targetList;
	m_targetList = nullptr;
	delete m_engineInfoParser;
	m_engineInfoParser = nullptr;
	delete m_combatSprite;
	m_combatSprite = nullptr;
	delete m_viewport;
	m_viewport = nullptr;
	delete m_soundList;
	m_soundList = nullptr;
	delete m_weapon;
	m_weapon = nullptr;

	ClearGlobals();
}

// Original: 0x411CA0
void CombatEngine::CopyToGlobals() {
	g_consoleSprite = m_consoleSprite;
	g_weapon = m_weapon;
	g_soundList = m_soundList;
	g_engineInfoParser = m_engineInfoParser;
	g_navigator = m_navigator;
	g_combatSprite = m_combatSprite;
	g_targetList = m_targetList;
	g_scoreManager = m_scoreManager;
	g_viewport = m_viewport;
	g_gameOutcome = m_gameOutcome;
}

// Original: 0x411D20
void CombatEngine::ClearGlobals() {
	g_consoleSprite = nullptr;
	g_weapon = nullptr;
	g_soundList = nullptr;
	g_engineInfoParser = nullptr;
	g_navigator = nullptr;
	g_combatSprite = nullptr;
	g_targetList = nullptr;
	g_scoreManager = nullptr;
	g_viewport = nullptr;
	g_gameOutcome = nullptr;
}

// Original: 0x411320
void CombatEngine::StopAndCleanup() {
	// Stop all sounds
	if (m_soundList)
		m_soundList->StopAll();
	VirtCleanup();
}

// Original: 0x411540
void CombatEngine::VirtCleanup() {
	if (m_gameOutcome)
		m_gameOutcome->PlayOutcomeVideo();
}

// Original: 0x411230
void CombatEngine::SetupViewport() {
	// Original: g_EngineViewport->SetDimensions(anchorRect.right, anchorRect.bottom)
	// Note: anchorRect.right/bottom store width/height directly (not rect bounds)
	if (g_viewport && g_engineInfoParser) {
		g_viewport->SetDimensions(g_engineInfoParser->anchorRect.right,
		                          g_engineInfoParser->anchorRect.bottom);
	}

	// Eagerly initialize navigator sprite so we can read its dimensions
	if (g_navigator && g_navigator->sprite) {
		g_navigator->sprite->ensureInit();
	}

	// Original: reads width/height from navigator sprite's animation targetBuffer
	int h = 0;
	int w = 0;
	if (g_navigator && g_navigator->sprite && g_navigator->sprite->animation_data
	    && g_navigator->sprite->animation_data->_decoder) {
		h = g_navigator->sprite->animation_data->_lastFrame.h;
		w = g_navigator->sprite->animation_data->_lastFrame.w;
	}

	if (g_viewport) {
		g_viewport->SetDimensions2(w - g_viewport->dimW, h - g_viewport->dimH);
		g_viewport->SetCenter();
		if (g_engineInfoParser) {
			g_viewport->SetAnchor(g_engineInfoParser->anchorRect.left,
			                      g_engineInfoParser->anchorRect.top);
		}
	}

	if (g_scoreManager)
		g_scoreManager->scoreInitial = 100;
	m_framesL = 1;
	m_framesA = 1;

	// Original: BlankScreen()
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	if (engine->_compositeSurface)
		engine->_compositeSurface->fillRect(Common::Rect(0, 0, engine->_screenW, engine->_screenH), 0);

	// Original: g_EnginePalette->SetPalette(paletteStart, (paletteEnd - paletteStart) + 1)
	// Apply palette from the navigator sprite's SMK decoder
	if (g_navigator && g_navigator->sprite && g_navigator->sprite->animation_data
	    && g_navigator->sprite->animation_data->_decoder) {
		const byte *pal = g_navigator->sprite->animation_data->_decoder->getPalette();
		if (pal) {
			int start = 0;
			int count = 256;
			if (g_engineInfoParser && g_engineInfoParser->paletteEnd > g_engineInfoParser->paletteStart) {
				start = g_engineInfoParser->paletteStart;
				count = (g_engineInfoParser->paletteEnd - g_engineInfoParser->paletteStart) + 1;
				if (count > 256 - start)
					count = 256 - start;
			}
			engine->loadPalette(pal + start * 3, start, count);
		}
	}

	if (m_backgroundSample) {
		m_backgroundSample->Play(100, 0);
	}
}

void CombatEngine::updateMouseState() {
	Common::Point pos = g_system->getEventManager()->getMousePos();
	_mouseX = pos.x;
	_mouseY = pos.y;

	_prevMouseButtons = _mouseButtons;

	Common::Event event;
	// Mouse button state is updated through events in processInput
	// Here we just update position
}

// Original: 0x411340
int CombatEngine::UpdateAndCheck() {
	updateMouseState();

	int result = CheckNavState();
	if (result != 0) {
		return 1;
	}

	UpdateCrosshair();

	if (g_navigator) {
		result = g_navigator->Update();
		if (result != 0) {
			return 1;
		}
	}

	result = UpdateSpriteFrame();
	if (result != 0) {
		return 1;
	}

	ProcessTargets();

	m_framesA++;
	m_framesL++;

	return 0;
}

// Original: 0x4113A0
void CombatEngine::ProcessTargets() {
	if (g_targetList) {
		Target *target = g_targetList->ProcessTargets();

		if (g_weapon) {
			g_weapon->DrawCrosshairs();

			// Check mouse button release (click to shoot)
			int buttonDown = _mouseButtons & 1;
			if (buttonDown == 0 && (_prevMouseButtons & 1) != 0) {
				g_weapon->field_0xa0 = 1;
			} else {
				g_weapon->field_0xa0 = 0;
			}

			if (g_weapon->field_0xa0 != 0) {
				g_weapon->OnHit();
				if (target) {
					target->UpdateProgress(1);
				}
			}
		}
	}

	Draw();
	UpdateMeter();
}

// Original: 0x411440
int CombatEngine::UpdateSpriteFrame() {
	if (g_combatSprite) {
		g_combatSprite->ProcessFrame(m_framesL);
	}
	return 0;
}

// Original: 0x411460
void CombatEngine::UpdateCrosshair() {
	if (g_weapon) {
		g_weapon->m_crosshairX = _mouseX;
		g_weapon->m_crosshairY = _mouseY;
	}

	// Scroll viewport based on mouse position
	if (g_viewport) {
		if (_mouseX > 0xAA) {
			g_viewport->SetCenterX(g_viewport->scrollX + 4);
			return;
		}
		if (_mouseX < 0x96) {
			g_viewport->SetCenterX(g_viewport->scrollX - 4);
		}
	}
}

// Original: 0x411510
void CombatEngine::Draw() {
	if (g_consoleSprite) {
		g_consoleSprite->Do(g_consoleSprite->loc_x, g_consoleSprite->loc_y, 1.0);
	}
}

void CombatEngine::UpdateMeter() {}
void CombatEngine::PlayCompletionSound() {}

// Original: 0x411DD0
int CombatEngine::CheckNavState() {
	if (g_gameOutcome)
		return (g_gameOutcome->outcome >= 1) ? 1 : 0;
	return 0;
}

void CombatEngine::DisplayFrameRate() {
	// Debug display - not critical for gameplay
}

// Original: 0x411DE0
int CombatEngine::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty()) return 0;

	if (keyword == "[ENGINE_INFO]") {
		if (g_engineInfoParser)
			Parser::processFile(g_engineInfoParser, this, "");
	} else if (keyword == "[TARGETS]") {
		if (g_targetList)
			Parser::processFile(g_targetList, this, "");
	} else if (keyword == "[SPRITELIST]") {
		if (g_combatSprite)
			Parser::processFile(g_combatSprite, this, "");
	} else if (keyword == "[NAVIGATION]") {
		if (g_navigator)
			Parser::processFile(g_navigator, this, "");
	} else if (keyword == "WEAPON") {
		Common::String weaponType;
		tokenize(rest, weaponType, rest);
		if (weaponType == "ROCKTHROWER") {
			delete m_weapon;
			m_weapon = new RockThrower();
			g_weapon = m_weapon;
		}
	} else if (keyword == "CONSOLE") {
		Sprite *spr = new Sprite();
		m_consoleSprite = spr;
		g_consoleSprite = spr;
		Parser::processFile(spr, this, "");
	} else if (keyword == "END") {
		return 1;
	}

	return 0;
}

// ============================================================================
// EngineB
// ============================================================================

// Original: 0x412110
EngineB::EngineB() : CombatEngine() {
	m_localSoundList = nullptr;
	m_missSound = nullptr;
	field_0xf0 = 0;
	m_prevHitCount = 0;
	field_0xf8 = 0;
	m_prevMissCount = 0;
	m_completionSound = nullptr;
	m_ambientSound = nullptr;
	field_0x108 = 0;
	field_0x10c = 0;
	m_hitSound1 = nullptr;
	m_hitSound2 = nullptr;
	m_hitSound3 = nullptr;
	m_tauntSound1 = nullptr;
	m_tauntSound2 = nullptr;
	m_milestoneSound = nullptr;
	m_meterAnimation = nullptr;
	m_progressCurrent = 0;
	m_progressMax = 0x36;
	m_meterPosX = 0x20;
	m_meterPosY = 0x14;
	m_targetHitsRequired = 1;
	m_targetPointsPerHit = 3;
	m_weaponParser = nullptr;
	m_winTimerActive = false;
	m_winTimerStart = 0;
	m_winTimerDuration = 0;
}

// Original: 0x412210
EngineB::~EngineB() {
	delete m_meterAnimation;
	m_meterAnimation = nullptr;
	delete m_localSoundList;
	m_localSoundList = nullptr;
}

// Original: 0x412300
void EngineB::Draw() {
	if (!g_consoleSprite) {
		return;
	}

	if (g_scoreManager && m_prevHitCount != g_scoreManager->hitCount) {
		// Target was hit
		m_prevHitCount = g_scoreManager->hitCount;
		g_consoleSprite->setState2(8);
		m_progressCurrent += m_targetPointsPerHit;

		// Play random hit reaction sound
		int hitIdx = ((HypnoEngine *)g_engine)->_rnd->getRandomNumber(2);
		Sample *hitSounds[3] = {m_hitSound1, m_hitSound2, m_hitSound3};
		if (m_localSoundList)
			m_localSoundList->StopAll();
		if (hitSounds[hitIdx])
			hitSounds[hitIdx]->Play(0x64, 1);
	} else if (g_scoreManager && m_prevMissCount != g_scoreManager->missCount) {
		// Miss - play taunt
		m_prevMissCount = g_scoreManager->missCount;
		g_consoleSprite->setState2(9);

		int tauntIdx = ((HypnoEngine *)g_engine)->_rnd->getRandomNumber(1);
		Sample *tauntSounds[2] = {m_tauntSound1, m_tauntSound2};
		if (tauntSounds[tauntIdx])
			tauntSounds[tauntIdx]->Play(0x64, 1);
	}

	// Check weapon projectile hits
	if (m_weaponParser) {
		RockThrower *rt = (RockThrower *)m_weaponParser;
		int weaponHit = rt->field_0xb0;
		if (weaponHit != 0) {
			m_progressCurrent += weaponHit;

			if (m_missSound)
				m_missSound->Play(0x64, 1);

			if (m_progressCurrent == 0x13 || m_progressCurrent == 0x25) {
				if (m_milestoneSound)
					m_milestoneSound->Play(0x64, 1);
			}
		}
	}

	// Aim cursor based on mouse position
	if (g_weapon && g_weapon->field_0xa0 != 0) {
		int mouseX = _mouseX;
		g_consoleSprite->setState2(mouseX / 0x6a + 5);
	}

	// Animate sprite
	if (g_consoleSprite->Do(g_consoleSprite->loc_x, g_consoleSprite->loc_y, 1.0) != 0) {
		int mouseX = _mouseX;
		g_consoleSprite->setState2(mouseX / 64);
	}
}

// Original: 0x412490
void EngineB::UpdateMeter() {
	if (!m_meterAnimation || !m_meterAnimation->hasLastFrame()) {
		return;
	}

	TeacherEngine *engine = (TeacherEngine *)g_engine;
	Graphics::Surface *surface = engine->_compositeSurface;

	if (m_progressMax == 0 || m_progressCurrent < m_progressMax) {
		// Calculate progress bar position
		int barPos = (m_progressCurrent * 0x36) / m_progressMax
		             - (int)(((HypnoEngine *)g_engine)->_rnd->getRandomNumber(2)) + 1;
		if (barPos < 0) barPos = 0;
		if (barPos > 0x36) barPos = 0x36;

		// Draw meter using the animation frame
		// The meter animation has two strips: empty (top) and full (bottom)
		if (m_meterAnimation->hasLastFrame()) {
			const Graphics::Surface &meterFrame = m_meterAnimation->_lastFrame;

			// Draw filled portion
			if (barPos > 0) {
				int srcY = m_meterFullRect.top;
				int srcH = m_meterFullRect.bottom - m_meterFullRect.top;
				if (srcH > 0 && barPos > 0) {
					Common::Rect srcRect(0, srcY, barPos * 4 + 2, srcY + srcH);
					if (srcRect.right > meterFrame.w) srcRect.right = meterFrame.w;
					if (srcRect.bottom > meterFrame.h) srcRect.bottom = meterFrame.h;
					if (srcRect.isValidRect() && srcRect.width() > 0 && srcRect.height() > 0)
						surface->copyRectToSurface(meterFrame, m_meterPosX, m_meterPosY, srcRect);
				}
			}

			// Draw empty portion
			int emptyStart = barPos * 4 + 2;
			if (emptyStart < m_meterFullRect.right) {
				int srcY = m_meterEmptyRect.top;
				int srcH = m_meterEmptyRect.bottom - m_meterEmptyRect.top;
				if (srcH > 0) {
					Common::Rect srcRect(emptyStart, srcY, m_meterFullRect.right, srcY + srcH);
					if (srcRect.right > meterFrame.w) srcRect.right = meterFrame.w;
					if (srcRect.bottom > meterFrame.h) srcRect.bottom = meterFrame.h;
					if (srcRect.isValidRect() && srcRect.width() > 0 && srcRect.height() > 0)
						surface->copyRectToSurface(meterFrame, m_meterPosX + emptyStart, m_meterPosY, srcRect);
				}
			}
		}
	} else {
		// Progress complete - draw full meter
		if (m_meterAnimation->hasLastFrame()) {
			const Graphics::Surface &meterFrame = m_meterAnimation->_lastFrame;
			int srcY = m_meterFullRect.top;
			int srcH = m_meterFullRect.bottom - m_meterFullRect.top;
			if (srcH > 0) {
				Common::Rect srcRect(0, srcY, m_meterFullRect.right, srcY + srcH);
				if (srcRect.right > meterFrame.w) srcRect.right = meterFrame.w;
				if (srcRect.bottom > meterFrame.h) srcRect.bottom = meterFrame.h;
				if (srcRect.isValidRect() && srcRect.width() > 0 && srcRect.height() > 0)
					surface->copyRectToSurface(meterFrame, m_meterPosX, m_meterPosY, srcRect);
			}
		}

		// Win timer check (original: static TimeOut)
		if (m_winTimerActive) {
			uint32 now = g_system->getMillis();
			if (now - m_winTimerStart >= m_winTimerDuration) {
				if (g_gameOutcome)
					g_gameOutcome->outcome = 1; // Win
			}
			return;
		}

		m_winTimerActive = true;
		m_winTimerStart = g_system->getMillis();
		m_winTimerDuration = 1500; // 0x5dc ms
	}
}

// Original: 0x412610
void EngineB::ProcessTargets() {
	if (g_targetList)
		g_targetList->ProcessTargets();

	if (m_weaponParser) {
		((RockThrower *)m_weaponParser)->UpdateProjectiles();
	}

	Draw();
	UpdateMeter();
}

// Original: 0x412640
void EngineB::PlayCompletionSound() {
	if (!m_completionSound)
		return;

	if (m_localSoundList)
		m_localSoundList->StopAll();

	if (m_backgroundSample)
		m_backgroundSample->Fade(0x14, 0x2ee);

	m_completionSound->Play(0x64, 1);
}

// Original: 0x412690
void EngineB::onProcessEnd() {
	// Poll input
	updateMouseState();

	if (g_consoleSprite) {
		int mouseX = _mouseX;
		g_consoleSprite->setState2(mouseX / 64);
	}

	// Set target config
	m_targetHitsRequired = 1;
	m_targetPointsPerHit = 3;

	m_weaponParser = g_weapon;

	// Update targets with hit requirement
	if (g_targetList) {
		for (int i = 0; i < g_targetList->count; i++) {
			Target *t = g_targetList->targets[i];
			if (t)
				t->progressRange.end = m_targetHitsRequired;
		}
	}

	// Create local sound list
	m_localSoundList = new SoundList(10);

	// Create meter animation
	m_meterAnimation = new TeacherAnimation();
	m_meterAnimation->play("rat1/nmeter.smk", 0);

	if (m_meterAnimation->_decoder) {
		m_meterAnimation->decodeAndAdvance();
	}

	// Initialize meter config
	m_meterEmptyRect = Common::Rect(0, 0, 0xff, 0xf);
	m_meterFullRect = Common::Rect(0, 0x12, 0xff, 0x21);
	m_progressCurrent = 0;
	m_progressMax = 0x36;
	m_meterPosX = 0x20;
	m_meterPosY = 0x14;

	// Register sounds
	if (g_soundList) {
		m_missSound = g_soundList->Register("audio/slingmis.wav");
		m_completionSound = g_soundList->Register("audio/ldu013_1.wav");
	}

	if (m_localSoundList) {
		m_tauntSound1 = m_localSoundList->Register("audio/ldu010_1.wav");
		m_tauntSound2 = m_localSoundList->Register("audio/ldu011_1.wav");
		m_hitSound1 = m_localSoundList->Register("audio/ldu008_1.wav");
		m_hitSound2 = m_localSoundList->Register("audio/ldu009_1.wav");
		m_hitSound3 = m_localSoundList->Register("audio/ldu007_2.wav");
		m_milestoneSound = m_localSoundList->Register("audio/ldu006_1.wav");
		m_ambientSound = m_localSoundList->Register("audio/ldu005_1.wav");
		if (m_ambientSound)
			m_ambientSound->Play(0x64, 1);
	}
}

// Original: 0x4129A0
int EngineB::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);

	if (keyword == "END_ENGINEA_INFO") {
		return 1;
	}
	return CombatEngine::lblParse(line);
}

} // End of namespace Hypno
