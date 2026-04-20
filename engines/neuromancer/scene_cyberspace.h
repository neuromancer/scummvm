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

#ifndef NEUROMANCER_SCENE_CYBERSPACE_H
#define NEUROMANCER_SCENE_CYBERSPACE_H

#include "neuromancer/scene.h"

#include "common/array.h"
#include "common/scummsys.h"

namespace Neuromancer {

// Cyberspace scene. Mirrors the DOS cyberspace_main_loop (1000:9E32);
// see engines/neuromancer/cyberspace.md for the full RE spec.
//
// Control flow: entered by a deck-item Operate (or the ROM panel's
// Monitor Mode / Software Debug in a future iteration). Owns the 32x32
// byte grid at DS:0x1544, player pixel position inside a 512x512 world,
// a 4-tick slide animation per grid step, and a 5x3 visible render
// window centered on the player. On exit (jack-out) control returns to
// RealWorldScene.
//
// Currently a minimal scaffold: grid/movement state wired, menu & site
// entry stubbed. Software effects are overlay-loaded in DOS (see
// cyberspace.md phase 3) and are intentionally stubbed with
// "Not implemented" until overlay extraction lands.
class CyberspaceScene : public Scene {
public:
	explicit CyberspaceScene(NeuromancerEngine *engine);
	~CyberspaceScene() override;

	SceneId id() const override { return kSceneCyberspace; }

	void init() override;
	void deinit() override;
	SceneId update() override;
	void handleEvent(const Common::Event &event) override;

private:
	// Grid geometry. Matches DOS DS:0x1544 storage: 32 cols x 32 rows,
	// cell byte values: 0xFF = empty, 0x23 = link-mode marker, other =
	// site id (index into site-record table).
	enum {
		kGridCols        = 32,
		kGridRows        = 32,
		kCellPixels      = 16,  // 16x16 pixels per cell
		kWorldPixels     = kGridCols * kCellPixels, // 512
		kRenderCols      = 5,
		kRenderRows      = 3,
		kAnimFrames      = 4
	};

	// Movement direction. Matches DOS uRam00021E44 enum:
	// 0 = grid Y++, 1 = grid X++, 2 = grid Y--, 3 = grid X--.
	enum Direction { kDirN = 0, kDirE = 1, kDirS = 2, kDirW = 3, kDirNone = -1 };

	// Main-loop menu codes. Match DOS cyb_menu_poll (1000:310B) return
	// values. Hotkeys: i/s/r/d/e/x.
	enum MenuCode {
		kMenuExit          = 0, // x
		kMenuSoftwareDebug = 1, // i
		kMenuSoftwareAnalysis = 2, // s
		kMenuRoleMonitor   = 3, // r
		kMenuDiskOptions   = 4, // d
		kMenuEraseVirus    = 5  // e
	};

	// Running sub-state. DOS cyberspace_main_loop runs a single tight
	// loop; here we split it into phases so ScummVM's per-frame update()
	// stays responsive.
	enum State {
		kStateRoaming    = 0, // grid view, accepting input
		kStateAnimating  = 1, // slide animation in progress
		kStateMenuOpen   = 2, // 6-button menu displayed
		kStateSitePrompt = 3, // "Can't do that here?" Y/N before site
		kStateSiteInterior = 4, // inside a site
		kStateExiting    = 5  // jacking out, one-shot cleanup
	};

	// ---- Init / teardown --------------------------------------------------
	void loadGrid();           // TODO: pull from MATRIX file
	void freeSprites();

	// ---- Rendering --------------------------------------------------------
	void renderFrame();
	void renderGridWindow();   // 5x3 window centered on player
	void renderHud();          // X, Y, Zone at y=184
	void renderMenuOverlay();  // 6 buttons at (A0,7E)..(C7,B2)
	void renderSitePrompt();   // "Can't do that here?" Y/N bubble
	void renderSiteScreen();   // site-interior script text overlay

	// ---- Movement / animation --------------------------------------------
	// Return true if a step in `dir` is allowed from the current cell.
	bool canStep(Direction dir) const;
	void startStep(Direction dir);
	void tickAnimation();      // one of 4 frames; on frame 0 run post-step
	void postStep();           // site check + redraw

	// ---- Input ------------------------------------------------------------
	Direction keyToDirection(uint16 ascii) const;

	// ---- Menu / site entry -----------------------------------------------
	bool dispatchMenu(MenuCode code);
	void enterSite(uint8 siteType);
	void exitSite(int result);

	// Return a placeholder site-script text for the given type. DOS
	// walks the null-terminated string pool at DS:0x724C; we supply
	// canned strings until the pool is extracted.
	const char *siteScriptText(uint8 siteType) const;

	// ---- Grid accessors --------------------------------------------------
	uint8 gridAt(int col, int row) const {
		return _grid[((row & 0x1F) << 5) | (col & 0x1F)];
	}
	int playerCol() const { return ((_playerPx - 0x20) >> 4) & 0x1F; }
	int playerRow() const { return (_playerPy >> 4) & 0x1F; }

	// ---- State ----------------------------------------------------------

	// 32 x 32 grid buffer. Currently zero-init placeholder; real content
	// will be pulled from MATRIX per active DB.
	uint8 _grid[kGridCols * kGridRows];

	// Packed 4bpp IMH sprite for the 5x3 grid viewport and the HUD band.
	// Regenerated every frame so movement shows immediately.
	Common::Array<byte> _viewSprite;
	Common::Array<byte> _hudSprite;

	// Menu overlay at (A0,7E)..(C7,B2): 6 buttons in a 40x54 panel with
	// hotkeys i/s/r/d/e/x. Only re-painted when kStateMenuOpen is set.
	Common::Array<byte> _menuSprite;

	// Y/N prompt overlay used for the "Can't do that here?" site-enter
	// confirmation. Drawn centred at (96, 96); hotkeys y/n.
	Common::Array<byte> _promptSprite;

	// Which site type the pending prompt concerns (0 when no prompt).
	uint8 _pendingSite;

	// Site-interior script text buffer. When in kStateSiteInterior we
	// render this text in place of the grid window; any key returns to
	// roaming. Real DOS site scripts live in DS:0x724C and run through
	// FUN_1000_BBC8; we display a stub message keyed on site type.
	Common::Array<byte> _siteScreen;
	Common::String      _siteText;

	// Player pixel position in a 512x512 world (masked to 9 bits on
	// every update, matching DOS uRam00024CA3 / uRam00024CA5).
	int _playerPx;
	int _playerPy;

	// Current DB index (0..9). Indexes into DOS tables at DS:0x2464 /
	// 0x2470 / 0x247C / 0x2488 / 0x24D4 (see cyberspace.md section 2).
	int _currentDb;

	// Movement animation.
	Direction _moveDir;
	int       _animPhase;   // 0 = idle, 1..4 = slide frames remaining

	// Currently-entered site id (== last grid cell the player moved onto,
	// -1 when roaming on empty cells).
	int _currentSite;

	// Flag: player arrived via a link-mode cell (0x23). Restricts which
	// cells are walkable (matches DOS cRam00024C75).
	bool _inLinkMode;

	// Jack-out sentinel. When true, update() returns kSceneRealWorld
	// after one more paint to give cleanup a frame.
	bool _jackingOut;

	// Exit destination — always RealWorld for now. Endgame handling can
	// redirect this later.
	SceneId _nextScene;

	State _state;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_CYBERSPACE_H
