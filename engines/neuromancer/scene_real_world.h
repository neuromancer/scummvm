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

#ifndef NEUROMANCER_SCENE_REAL_WORLD_H
#define NEUROMANCER_SCENE_REAL_WORLD_H

#include "neuromancer/scene.h"

#include "common/array.h"
#include "common/str.h"

namespace Neuromancer {

// Real-world scene: UI frame (NEURO.IMH) + per-level PIC + neuro-VM tick +
// clickable UI buttons.
//
// Widgets and their layers (see SpriteLayerIndex in gfx.h):
//   - kLayerBackground : NEURO.IMH UI frame (opaque)
//   - kLayerLevelBg    : R{N+1}.PIC level image at (8, 8)
//   - kLayerNeuroMenu  : scroll text widget at (88, 134) 136x56 (intro,
//                        VM opcode 0x02 text output)
//   - kLayerDialogBubble: dialog bubble (VM opcodes 0x01 / 0x18)
//   - kLayerCharacter  : status display at (96, 149) showing cash / CON /
//                        time / date for the currently selected UI mode
//   - kLayerCursor     : mouse cursor sprite
//
// UI buttons: 10 clickable regions defined in the DOS build's
// g_ui_buttons (data.c:127-138). The icons are baked into NEURO.IMH so
// the scene only needs to maintain hit rectangles and handle input.
class RealWorldScene : public Scene {
public:
	explicit RealWorldScene(NeuromancerEngine *engine);
	~RealWorldScene() override;

	SceneId id() const override { return kSceneRealWorld; }

	void init() override;
	void deinit() override;
	SceneId update() override;
	void handleEvent(const Common::Event &event) override;

private:
	enum TextWidget {
		kWidgetScroll,  // lower 136x56 box -- intro + opcode 0x02
		kWidgetBubble   // overlay dialog bubble -- opcode 0x01 / 0x18
	};

	// UI button action codes. Values match g_ui_buttons[*].code in data.c
	// so future save/load and VM handlers can round-trip them directly.
	enum UiAction {
		kUiInventory    = 0x00,
		kUiPax          = 0x01,
		kUiDialog       = 0x02,
		kUiSkills       = 0x03,
		kUiChip         = 0x04,
		kUiDisk         = 0x05,
		kUiDate         = 0x0A,
		kUiTime         = 0x0B,
		kUiCash         = 0x0C,
		kUiConstitution = 0x0D
	};

	// Status-panel mode (what to render in the status widget at (96, 149)).
	enum StatusMode { kStatusCash = 0, kStatusCon, kStatusTime, kStatusDate };

	bool loadLevel();
	void gotoLevel(int delta);
	void advanceVmOnce();
	void showLevelIntro();
	void startVmForCurrentLevel();

	// `text` is raw BIH text: it may contain DOS control codes (\r, 0x01
	// for name, 0x02 for date). The helper wraps, splits into pages, and
	// shows page 0. Subsequent pages are shown by pageTextForward().
	void showText(const char *text, TextWidget widget);
	bool pageTextForward(); // true if more pages remain; false if done
	void renderCurrentPage();
	void clearTextWidgets();

	void updateStatusWidget();
	void onUiAction(int code);
	int hitTestUiButton(int x, int y) const; // returns action code or -1
	int keyToUiAction(uint16 ascii) const;   // returns action code or -1

	// Mouse click inside the PIC area (8, 8)-(312, 120). For now this
	// only logs + maps edge clicks to prev/next level navigation as a
	// placeholder for full ROOMPOS-based exit detection.
	void handlePicClick(int x, int y);

	// Character controller.
	enum CharDir { kDirNone = -1, kDirUp = 0, kDirRight = 1, kDirDown = 2, kDirLeft = 3 };
	void updateCharacter(uint32 nowMs);        // per-frame tick
	void renderCharacterFrame();               // place sprite at current pos
	void setCharDirFromCursor(int cursorX, int cursorY); // LMB-held direction pick

	// Dialog picker helpers (shared by the T-button UI action and the
	// VM op-0x17 "enter dialog" path).
	void openDialogPicker();
	void renderDialogPicker();
	void advanceDialogReply();
	void acceptDialogReply();

	Common::Array<byte> _neuroImh;
	Common::Array<byte> _picSprite;
	Common::Array<byte> _bihData;
	Common::Array<byte> _scrollSprite;
	Common::Array<byte> _bubbleSprite;
	Common::Array<byte> _statusSprite;  // status widget at (96, 149)

	SceneId _next;
	bool _textVisible;
	bool _introPending;

	// Paging state for the active text widget: the BIH strings exceed the
	// 17x7 scroll box and 38x8 bubble box, so we split wrapped text into
	// pages and advance one per keypress.
	Common::Array<Common::String> _pages;
	int        _currentPage;
	TextWidget _activeWidget;

	// Dialog picker state. When open, the bubble layer shows the player's
	// currently-highlighted reply; cycle with any key, accept with Enter /
	// right-click. Accepting writes var[16] = firstReply + currentReply
	// and var[0] = 0 (matching the DOS rw_state_dialog.c end-of-dialog
	// behaviour), then resumes the VM.
	bool _dialogOpen;
	int  _dialogCurrentReply; // 0 .. totalReplies-1
	uint8 _dialogFirst;
	uint8 _dialogTotal;

	// Character controller state. Mirrors character_control.c in the DOS
	// build: click-and-hold the LMB over the level image to drive the
	// player sprite; on release the character goes idle. Position is in
	// absolute screen pixels (same units as SpriteChain::addSprite).
	int     _charX;
	int     _charY;
	CharDir _charDir;
	int     _charFrame;   // 0..7 in the current direction's frame table
	bool    _charMoving;
	bool    _lmbHeld;
	uint32  _charLastStepMs;

	// Game state surfaced by the status widget. Defaults are the level 1
	// start values from the DOS save-slot template (11/16/58, cash=0).
	StatusMode _statusMode;
	int32 _cash;
	int16 _constitution;
	int16 _timeH, _timeM;
	int16 _dateDay; // day offset from 11/16/58 -- the DOS build_date_string input
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_REAL_WORLD_H
