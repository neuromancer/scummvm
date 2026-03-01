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

#ifndef HYPNO_TEACHER_COMBATENGINE_H
#define HYPNO_TEACHER_COMBATENGINE_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Sprite.h"
#include "hypno/teacher/Sample.h"
#include "hypno/teacher/Animation.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/array.h"
#include "common/rect.h"

namespace Hypno {

// Forward declarations
class CombatEngine;
class TargetList;
class CombatSprite;
class Weapon;
class RockThrower;
class Target;

// ============================================================================
// SoundList - Sound registration and management
// Original: SoundList (SoundList.h)
// ============================================================================
class SoundList {
public:
	SoundList(int maxSamples);
	~SoundList();
	Sample *Register(const Common::String &filename);
	void StopAll();

private:
	Common::HashMap<Common::String, Sample *> _samples;
};

// ============================================================================
// GameOutcome - Holds combat outcome state
// Original: GameOutcome (GameOutcome.h)
// outcome: 0 = not set, 1 = lose, 2 = win
// ============================================================================
class GameOutcome {
public:
	int outcome;

	GameOutcome();
	void PlayOutcomeVideo();
};

// ============================================================================
// ScoreManager - Tracks game score/stats
// Original: CursorState (CursorState.h)
// ============================================================================
class ScoreManager {
public:
	int score;
	int scoreInitial;
	int completionCount;
	int missCount;
	int field_0x10;
	int hitCount;
	int shotsFired;
	int field_0x1c;
	int field_0x20;

	ScoreManager();
	void AdjustScore(int value);
};

// ============================================================================
// Viewport - Camera/viewport management
// Original: Viewport (Viewport.h)
// ============================================================================
class Viewport {
public:
	int dimW, dimH;
	int anchorX, anchorY;
	int sizeW, sizeH;
	int centerX, centerY;
	int scrollX, scrollY;
	int anchorOffsetY;

	Viewport();
	void SetDimensions(int w, int h);
	void SetAnchor(int x, int y);
	void SetDimensions2(int w, int h);
	void SetCenter();
	void SetCenterX(int x);
	void SetCenterY(int y);
};

// ============================================================================
// EngineInfoParser - Parses engine configuration from MIS files
// Original: EngineInfoParser (EngineSubsystems.h)
// ============================================================================
class EngineInfoParser : public Parser {
public:
	// anchorRect: left=anchorX, top=anchorY, right=width, bottom=height
	Common::Rect anchorRect;
	int paletteStart;
	int paletteEnd;

	EngineInfoParser();
	~EngineInfoParser() override;
	int lblParse(const Common::String &line) override;
};

// ============================================================================
// NavNode - Navigation node (original: mCNavNode)
// Simplified for ScummVM - stores per-node activation data
// ============================================================================
struct NavNode {
	int nodeHandle;              // IAM <id>
	int animationState;          // BG <state>
	int nextNodeId;              // implicit from NEXTNODE or EXIT_CODE
	int counterLimit;            // LOOP <count>
	int flags;                   // 1=BG, 2=PLAY_ANIM, 4=LOOP, 8=SPRITE, 0x10=EXIT
	Common::Array<int> spriteListIds;  // SPRITE <name> <id> entries

	NavNode() : nodeHandle(0), animationState(0), nextNodeId(1),
	            counterLimit(0), flags(0) {}
};

// ============================================================================
// Navigator - Background navigation/scrolling
// Original: mCNavigator (mCNavigator.h)
// Manages background sprite + nav nodes for target spawning
// ============================================================================
class Navigator : public Parser {
public:
	Sprite *sprite;

	// Node system (original: ObjectPool of mCNavNode)
	Common::HashMap<int, NavNode> _nodes;
	int _startingNode;
	NavNode *_currentNode;
	bool _nodeActive;
	int _nodeCounter;

	Navigator();
	~Navigator() override;
	void onProcessEnd() override;
	int Update();
	int lblParse(const Common::String &line) override;

private:
	// Parsing state for current BASENODE
	NavNode _parsingNode;
	bool _parsingBaseNode;
	void activateNode(NavNode *node);
};

// ============================================================================
// CombatEngine - Base class for combat engine variants
// Original: Engine (Engine.h, size 0xe8)
//
// Class hierarchy:
//   Parser
//     +-- CombatEngine (base, original: Engine)
//           +-- EngineB (combat variant)
// ============================================================================
class CombatEngine : public Parser {
public:
	TargetList *m_targetList;
	EngineInfoParser *m_engineInfoParser;
	Sprite *m_consoleSprite;
	Viewport *m_viewport;
	CombatSprite *m_combatSprite;
	Weapon *m_weapon;
	SoundList *m_soundList;
	ScoreManager *m_scoreManager;
	Navigator *m_navigator;
	GameOutcome *m_gameOutcome;
	int m_combatBonus1;
	int m_scrollOffsetX;
	int field_0xbc;
	int m_scrollOffsetY;
	int m_combatBonus2;
	int m_viewOffset1X;
	int field_0xcc;
	int m_viewOffset1Y;
	int m_framesA;
	int field_0xd8;
	int m_framesL;
	Sample *m_backgroundSample;
	int field_0xe4;

	CombatEngine();
	virtual ~CombatEngine();

	// Virtual methods matching original vtable
	int lblParse(const Common::String &line) override;
	void onProcessStart() override;
	void onProcessEnd() override;

	virtual void Initialize();
	virtual void CleanupSubsystems();
	virtual void VirtCleanup();
	virtual void DisplayFrameRate();
	virtual void UpdateCrosshair();
	virtual int CheckNavState();
	virtual int UpdateSpriteFrame();
	virtual void ProcessTargets();
	virtual void Draw();
	virtual void UpdateMeter();
	virtual int UpdateAndCheck();
	virtual void CopyToGlobals();
	virtual void ClearGlobals();
	virtual void PlayCompletionSound();

	void StopAndCleanup();
	void SetupViewport();

	// Mouse state (replaces g_InputManager)
	int _mouseX, _mouseY;
	int _mouseButtons, _prevMouseButtons;
	void updateMouseState();
};

// ============================================================================
// EngineB - Combat engine variant with meter, sounds, progress
// Original: EngineB (EngineB.h, size 0x168)
// ============================================================================
class EngineB : public CombatEngine {
public:
	SoundList *m_localSoundList;
	Sample *m_missSound;
	int field_0xf0;
	int m_prevHitCount;
	int field_0xf8;
	int m_prevMissCount;
	Sample *m_completionSound;
	Sample *m_ambientSound;
	int field_0x108;
	int field_0x10c;
	Sample *m_hitSound1;
	Sample *m_hitSound2;
	Sample *m_hitSound3;
	Sample *m_tauntSound1;
	Sample *m_tauntSound2;
	Sample *m_milestoneSound;
	TeacherAnimation *m_meterAnimation;

	// Meter rendering
	Common::Rect m_meterEmptyRect;
	Common::Rect m_meterFullRect;
	int m_progressCurrent;
	int m_progressMax;
	int m_meterPosX, m_meterPosY;

	// Combat config
	int m_targetHitsRequired;
	int m_targetPointsPerHit;

	// Weapon reference for EngineB-specific logic
	Weapon *m_weaponParser;

	// Timeout for win condition
	bool m_winTimerActive;
	uint32 m_winTimerStart;
	uint32 m_winTimerDuration;

	EngineB();
	~EngineB() override;

	int lblParse(const Common::String &line) override;
	void onProcessEnd() override;
	void ProcessTargets() override;
	void Draw() override;
	void UpdateMeter() override;
	void PlayCompletionSound() override;
};

// ============================================================================
// Global combat engine pointer (mirrors original g_CombatEngine_00435eb0)
// ============================================================================
extern CombatEngine *g_combatEngine;
extern Sprite *g_consoleSprite;
extern TargetList *g_targetList;
extern Weapon *g_weapon;
extern SoundList *g_soundList;
extern CombatSprite *g_combatSprite;
extern ScoreManager *g_scoreManager;
extern Viewport *g_viewport;
extern GameOutcome *g_gameOutcome;
extern Navigator *g_navigator;
extern EngineInfoParser *g_engineInfoParser;

} // End of namespace Hypno

#endif
