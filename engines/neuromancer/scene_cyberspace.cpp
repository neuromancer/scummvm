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

#include "neuromancer/scene_cyberspace.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/inventory.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/resource.h"
#include "neuromancer/scene_real_world.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/events.h"
#include "common/keyboard.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

// Placeholder geometry for the visible grid view. DOS uses 5x3 cells at
// 16 px each (80x48 pixels). We render them larger during development so
// movement is easy to see; the final sizing will match DOS once sprite
// assets are wired in.
enum {
	kViewCellPx   = 20,
	kViewCols     = 5,
	kViewRows     = 3,
	kViewWidthPx  = kViewCellPx * kViewCols,  // 100
	kViewHeightPx = kViewCellPx * kViewRows,  // 60
	kViewPackedW  = kViewWidthPx / 2,
	kViewX        = 110, // roughly centred in the 320-wide screen
	kViewY        = 20,

	kHudX         = 0,
	kHudY         = 184,
	kHudWidthPx   = 200,
	kHudHeightPx  = 16,
	kHudPackedW   = kHudWidthPx / 2,

	// Right-side menu panel mirroring DOS cyb_ui_setup buttons at
	// (A0,7E)..(C7,B2). Drawn inline with the scene so it's always
	// visible; the kMenuOpen state is kept for future "pause loop on
	// selection" semantics.
	kMenuX        = 0xA0,
	kMenuY        = 0x7E,
	kMenuWidthPx  = 40,
	kMenuHeightPx = 54,
	kMenuPackedW  = kMenuWidthPx / 2,

	// Centred Y/N prompt.
	kPromptX        = 96,
	kPromptY        = 84,
	kPromptWidthPx  = 128,
	kPromptHeightPx = 32,
	kPromptPackedW  = kPromptWidthPx / 2,

	// Site-interior full-panel screen.
	kSiteX          = 8,
	kSiteY          = 8,
	kSiteWidthPx    = 144,
	kSiteHeightPx   = 160,
	kSitePackedW    = kSiteWidthPx / 2
};

// EGA palette indices used for the placeholder renderer.
enum Palette : byte {
	kColBlack = 0,
	kColBlue  = 1,
	kColGreen = 2,
	kColCyan  = 3,
	kColRed   = 4,
	kColYellow = 14,
	kColWhite = 15
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

// Fill a rectangle in a packed 4bpp sprite buffer.
void fillRect(byte *pixels, int packedW, int h,
              int x, int y, int w, int rectH, byte color) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { rectH += y; y = 0; }
	if (x + w > packedW * 2) w = packedW * 2 - x;
	if (y + rectH > h) rectH = h - y;
	if (w <= 0 || rectH <= 0) return;

	for (int row = y; row < y + rectH; ++row) {
		for (int px = x; px < x + w; ++px) {
			byte *b = pixels + row * packedW + (px / 2);
			if (px & 1)
				*b = (byte)((*b & 0xF0) | (color & 0x0F));
			else
				*b = (byte)((*b & 0x0F) | ((color & 0x0F) << 4));
		}
	}
}

byte colorForCell(uint8 cell) {
	if (cell == 0xFF) return kColBlack;
	if (cell == 0x23) return kColBlue;
	return kColGreen; // site
}

} // anonymous namespace

// Direction deltas (screen-pixel per animation tick). Match the DOS
// tables at DS:0x2576 (dx) and DS:0x257E (dy). Up-arrow maps to dir 0,
// which DOS defined as grid Y++ (screen-down); we mirror this exactly
// so save state round-trips.
static const int kDxByDir[4] = { 0, +4, 0, -4 };
static const int kDyByDir[4] = { +4, 0, -4, 0 };

CyberspaceScene::CyberspaceScene(NeuromancerEngine *engine)
	: Scene(engine),
	  _playerPx(0),
	  _playerPy(0),
	  _currentDb(0),
	  _moveDir(kDirNone),
	  _animPhase(0),
	  _currentSite(-1),
	  _inLinkMode(false),
	  _jackingOut(false),
	  _nextScene(kSceneCyberspace),
	  _state(kStateRoaming),
	  _pendingSite(0) {
	memset(_grid, 0xFF, sizeof(_grid));
	_viewSprite.resize(sizeof(ImhHeader) + kViewPackedW * kViewHeightPx);
	writeImhHeader(_viewSprite.data(), 0, 0, kViewPackedW, kViewHeightPx);
	_hudSprite.resize(sizeof(ImhHeader) + kHudPackedW * kHudHeightPx);
	writeImhHeader(_hudSprite.data(), 0, 0, kHudPackedW, kHudHeightPx);
	_menuSprite.resize(sizeof(ImhHeader) + kMenuPackedW * kMenuHeightPx);
	writeImhHeader(_menuSprite.data(), 0, 0, kMenuPackedW, kMenuHeightPx);
	_promptSprite.resize(sizeof(ImhHeader) + kPromptPackedW * kPromptHeightPx);
	writeImhHeader(_promptSprite.data(), 0, 0, kPromptPackedW, kPromptHeightPx);
	_siteScreen.resize(sizeof(ImhHeader) + kSitePackedW * kSiteHeightPx);
	writeImhHeader(_siteScreen.data(), 0, 0, kSitePackedW, kSiteHeightPx);
}

CyberspaceScene::~CyberspaceScene() = default;

void CyberspaceScene::init() {
	debugC(1, kDebugGeneral, "CyberspaceScene::init");
	// Pick up the current database index from DSEG (0x4CA1 aliases DOS
	// uRam00024CA1, which cyberspace_main_loop at 1000:9E32 uses to
	// select the DB). Falls back to 0 if the VM hasn't been set up yet.
	if (NeuroVM *vm = _engine->vm()) {
		_currentDb = (int)vm->readVar16(0x4CA1);
		if (_currentDb < 0 || _currentDb > 9)
			_currentDb = 0;
		// g_a61a at DSEG 0xA61A: 1 = in cyberspace, 2 = in comlink,
		// 0xFF = jacked out. DOS scripts branch on this to gate
		// cyberspace-only behaviours. Set on init; reset on deinit.
		vm->writeVar8(0xA61A, 1);
		// uRam00024CC1 mirrors the "cyberspace scene active" flag DOS
		// cyberspace_main_loop sets to 1 at entry (1000:9e32) and
		// clears to 0 on exit. Same purpose, different slot.
		vm->writeVar8(0x4CC1, 1);
	}
	loadGrid();
	_state = kStateRoaming;
	_jackingOut = false;
	_nextScene = kSceneCyberspace;

	// Seed a full-screen black background so the layers cleared by
	// RealWorldScene::deinit don't leave stale pixels under the
	// cyberspace UI. The viewport / HUD / menu overlay fill in over
	// this layer; everything else stays matte-black (matches DOS
	// cyberspace's flat-black backdrop).
	if (_bgSprite.empty()) {
		_bgSprite.resize(sizeof(ImhHeader) + (kScreenWidth / 2) * kScreenHeight);
		writeImhHeader(_bgSprite.data(), 0, 0, kScreenWidth / 2, kScreenHeight);
		memset(_bgSprite.data() + sizeof(ImhHeader), 0x00,
		       (kScreenWidth / 2) * kScreenHeight);
	}
	_engine->spriteChain()->addSprite(kLayerBackground, 0, 0,
	                                  _bgSprite.data(), /*opaque=*/true);

	renderFrame();
	_engine->render();
}

void CyberspaceScene::deinit() {
	debugC(1, kDebugGeneral, "CyberspaceScene::deinit");
	// Match DOS cyberspace_main_loop exit: g_a61a = 0xFF (jacked out),
	// uRam00024CC1 = 0 (scene inactive). Scripts that gated on these
	// flags for cyberspace-only behaviour now see the off state after
	// the player returns to real world.
	if (NeuroVM *vm = _engine->vm()) {
		vm->writeVar8(0xA61A, 0xFF);
		vm->writeVar8(0x4CC1, 0);
	}
	freeSprites();
}

SceneId CyberspaceScene::update() {
	if (_jackingOut)
		return _nextScene;

	switch (_state) {
	case kStateAnimating:
		tickAnimation();
		break;
	case kStateSiteInterior:
		// TODO: site interior tick (DOS cyb_site_interior_loop)
		break;
	case kStateRoaming:
	case kStateMenuOpen:
	case kStateSitePrompt:
		break;
	case kStateExiting:
		_jackingOut = true;
		_nextScene = kSceneRealWorld;
		break;
	}

	renderFrame();
	// The engine's main loop doesn't auto-composite -- scenes drive
	// the sprite-chain -> screen push themselves. RealWorldScene does
	// the same thing at every update path. Without this call the
	// scene would appear frozen because addSprite only dirties the
	// layer without flushing.
	_engine->render();
	return kSceneCyberspace;
}

void CyberspaceScene::handleEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN)
		return;

	const Common::KeyCode kc = event.kbd.keycode;
	const char ascii = (char)(event.kbd.ascii & 0x7F);
	const char lower = (char)tolower((unsigned char)ascii);

	// Site-interior: any key jacks out of the site back to roaming.
	// Matches DOS cyb_site_interior_loop returning to the main loop on
	// menu-exit. ESC still falls through to global jack-out.
	if (_state == kStateSiteInterior) {
		if (kc == Common::KEYCODE_ESCAPE) {
			_state = kStateExiting;
			return;
		}
		exitSite(/*result=*/1);
		return;
	}

	// Prompt state: y accepts, any other key cancels.
	if (_state == kStateSitePrompt) {
		if (kc == Common::KEYCODE_ESCAPE || lower == 'n') {
			_pendingSite = 0;
			_state = kStateRoaming;
			return;
		}
		if (lower == 'y') {
			uint8 t = _pendingSite;
			_pendingSite = 0;
			enterSite(t);
			return;
		}
		return;
	}

	// ESC always jacks out unless mid-prompt (handled above).
	if (kc == Common::KEYCODE_ESCAPE) {
		_state = kStateExiting;
		return;
	}

	// Menu hotkeys available in roaming/menu-open. Matches DOS: 6-button
	// UI is always live while in cyberspace.
	if (_state == kStateRoaming || _state == kStateMenuOpen) {
		switch (lower) {
		case 'x': dispatchMenu(kMenuExit);             return;
		case 'i': dispatchMenu(kMenuSoftwareDebug);    return;
		case 's': dispatchMenu(kMenuSoftwareAnalysis); return;
		case 'r': dispatchMenu(kMenuRoleMonitor);      return;
		case 'd': dispatchMenu(kMenuDiskOptions);      return;
		case 'e': dispatchMenu(kMenuEraseVirus);       return;
		default:  break;
		}
	}

	if (_state == kStateRoaming) {
		Direction d = kDirNone;
		switch (kc) {
		case Common::KEYCODE_UP:    d = kDirS; break;
		case Common::KEYCODE_DOWN:  d = kDirN; break;
		case Common::KEYCODE_LEFT:  d = kDirW; break;
		case Common::KEYCODE_RIGHT: d = kDirE; break;
		default: break;
		}
		if (d != kDirNone && canStep(d))
			startStep(d);
	}
}

// -------------------------------------------------------------------------
// Init / teardown
// -------------------------------------------------------------------------

void CyberspaceScene::loadGrid() {
	// DB{N}.BIH contains the per-database TEXT (site names, messages,
	// scripts), not the 32x32 grid -- the grid layout lives somewhere
	// else (still RE in progress). For now populate a visible sparse
	// placeholder so the scene is navigable; DB-file parsing for real
	// grid + site scripts comes in a follow-up.
	debugC(1, kDebugGeneral, "CyberspaceScene::loadGrid db=%d", _currentDb);
	memset(_grid, 0xFF, sizeof(_grid));

	// Eight sites scattered in a loose ring around the spawn (8, 8),
	// plus one link cell north. Sites alternate types so each renders
	// with a unique debug color and triggers a different script.
	static const struct { int col, row; uint8 type; } kPlaceholder[] = {
		{ 12,  8, 0x01 }, {  4,  8, 0x02 }, {  8, 12, 0x03 },
		{ 14, 12, 0x09 }, {  6,  4, 0x0B }, { 16, 16, 0x01 },
		{ 10, 20, 0x02 }, { 20,  4, 0x03 }
	};
	for (size_t i = 0; i < sizeof(kPlaceholder) / sizeof(kPlaceholder[0]); ++i) {
		int idx = (kPlaceholder[i].row & 0x1F) * kGridCols
		        + (kPlaceholder[i].col & 0x1F);
		_grid[idx] = kPlaceholder[i].type;
	}
	_grid[4 * kGridCols + 8] = 0x23; // link cell north of spawn

	_playerPx = 0x20 + 8 * kCellPixels;
	_playerPy = 8 * kCellPixels;
}

void CyberspaceScene::freeSprites() {
	NeuromancerEngine *e = _engine;
	e->spriteChain()->clearSprite(kLayerBackground);
	e->spriteChain()->clearSprite(kLayerLevelBg);
	e->spriteChain()->clearSprite(kLayerStatusWidget);
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void CyberspaceScene::renderFrame() {
	if (_state == kStateSiteInterior) {
		renderSiteScreen();
		renderHud();
		_engine->spriteChain()->clearSprite(kLayerDialogBubble);
		// Menu stays live even inside a site (DOS shares the menu
		// between roaming and site-interior loop).
		renderMenuOverlay();
		return;
	}
	renderGridWindow();
	renderHud();
	renderMenuOverlay();       // always drawn -- matches DOS always-live UI
	if (_state == kStateSitePrompt)
		renderSitePrompt();
	else
		_engine->spriteChain()->clearSprite(kLayerDialogBubble);
}

void CyberspaceScene::renderGridWindow() {
	byte *pixels = _viewSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0, kViewPackedW * kViewHeightPx);

	// Player logical cell (at col+2, row so the "camera" shows what's
	// ahead of the player -- mirrors DOS layout).
	int camCol = playerCol();
	int camRow = playerRow();

	for (int r = 0; r < kViewRows; ++r) {
		for (int c = 0; c < kViewCols; ++c) {
			int gc = (camCol + c) & 0x1F;
			int gr = (camRow + r) & 0x1F;
			uint8 cell = gridAt(gc, gr);
			byte color = colorForCell(cell);
			fillRect(pixels, kViewPackedW, kViewHeightPx,
			         c * kViewCellPx + 1, r * kViewCellPx + 1,
			         kViewCellPx - 2, kViewCellPx - 2,
			         color);
		}
	}

	// Player avatar: yellow diamond at cell (2, 1), drawn over the
	// underlying cell color so it stays visible while centred.
	int px = 2 * kViewCellPx + kViewCellPx / 2;
	int py = 1 * kViewCellPx + kViewCellPx / 2;
	fillRect(pixels, kViewPackedW, kViewHeightPx, px - 3, py - 3, 6, 6, kColYellow);

	_engine->spriteChain()->addSprite(kLayerLevelBg, kViewX, kViewY,
	                                  _viewSprite.data(), /*opaque=*/true);
}

// drawString renders black ink (index 0). Cyberspace widgets need a
// light-coloured background so the text shows up -- fill the interior
// with white (0xFF packed = two index-15 pixels) before drawing.

void CyberspaceScene::renderHud() {
	byte *pixels = _hudSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0xFF, kHudPackedW * kHudHeightPx);

	int zone = (_playerPx >= 0x100 ? 1 : 0) + (_playerPy >= 0x100 ? 2 : 0);
	char buf[64];
	Common::sprintf_s(buf, sizeof(buf), "X:%03d Y:%03d Zone:%d",
	                  _playerPx, _playerPy, zone);
	drawString(buf, kHudWidthPx, kHudHeightPx, 4, 4, pixels);

	_engine->spriteChain()->addSprite(kLayerStatusWidget, kHudX, kHudY,
	                                  _hudSprite.data(), /*opaque=*/true);
}

void CyberspaceScene::renderMenuOverlay() {
	byte *pixels = _menuSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0xFF, kMenuPackedW * kMenuHeightPx);

	// 1-pixel black frame on the white background for contrast.
	fillRect(pixels, kMenuPackedW, kMenuHeightPx, 0, 0, kMenuWidthPx, 1, kColBlack);
	fillRect(pixels, kMenuPackedW, kMenuHeightPx, 0, kMenuHeightPx - 1, kMenuWidthPx, 1, kColBlack);
	fillRect(pixels, kMenuPackedW, kMenuHeightPx, 0, 0, 1, kMenuHeightPx, kColBlack);
	fillRect(pixels, kMenuPackedW, kMenuHeightPx, kMenuWidthPx - 1, 0, 1, kMenuHeightPx, kColBlack);

	// Menu layout: the 40x54 window fits 5 chars per row at 8px, so the
	// labels are abbreviated. Each row is 8 px tall.
	drawString("iDebug", kMenuWidthPx, kMenuHeightPx, 2, 3,  pixels);
	drawString("sAnaly", kMenuWidthPx, kMenuHeightPx, 2, 11, pixels);
	drawString("rRole",  kMenuWidthPx, kMenuHeightPx, 2, 19, pixels);
	drawString("dDisk",  kMenuWidthPx, kMenuHeightPx, 2, 27, pixels);
	drawString("eErase", kMenuWidthPx, kMenuHeightPx, 2, 35, pixels);
	drawString("xExit",  kMenuWidthPx, kMenuHeightPx, 2, 43, pixels);

	_engine->spriteChain()->addSprite(kLayerPaxWindow, kMenuX, kMenuY,
	                                  _menuSprite.data(), /*opaque=*/true);
}

void CyberspaceScene::renderSiteScreen() {
	byte *pixels = _siteScreen.data() + sizeof(ImhHeader);
	memset(pixels, 0xFF, kSitePackedW * kSiteHeightPx);

	fillRect(pixels, kSitePackedW, kSiteHeightPx, 0, 0, kSiteWidthPx, 1, kColBlack);
	fillRect(pixels, kSitePackedW, kSiteHeightPx, 0, kSiteHeightPx - 1, kSiteWidthPx, 1, kColBlack);
	fillRect(pixels, kSitePackedW, kSiteHeightPx, 0, 0, 1, kSiteHeightPx, kColBlack);
	fillRect(pixels, kSitePackedW, kSiteHeightPx, kSiteWidthPx - 1, 0, 1, kSiteHeightPx, kColBlack);

	drawString(_siteText.c_str(), kSiteWidthPx, kSiteHeightPx, 6, 8, pixels);
	drawString("Press any key to jack out.",
	           kSiteWidthPx, kSiteHeightPx, 6, kSiteHeightPx - 16, pixels);

	_engine->spriteChain()->addSprite(kLayerLevelBg, kSiteX, kSiteY,
	                                  _siteScreen.data(), /*opaque=*/true);
}

void CyberspaceScene::renderSitePrompt() {
	byte *pixels = _promptSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0xFF, kPromptPackedW * kPromptHeightPx);

	fillRect(pixels, kPromptPackedW, kPromptHeightPx, 0, 0, kPromptWidthPx, 1, kColBlack);
	fillRect(pixels, kPromptPackedW, kPromptHeightPx, 0, kPromptHeightPx - 1, kPromptWidthPx, 1, kColBlack);
	fillRect(pixels, kPromptPackedW, kPromptHeightPx, 0, 0, 1, kPromptHeightPx, kColBlack);
	fillRect(pixels, kPromptPackedW, kPromptHeightPx, kPromptWidthPx - 1, 0, 1, kPromptHeightPx, kColBlack);

	drawString("Cant do that here?", kPromptWidthPx, kPromptHeightPx, 6, 6,  pixels);
	drawString("Y / N",              kPromptWidthPx, kPromptHeightPx, 6, 18, pixels);

	_engine->spriteChain()->addSprite(kLayerDialogBubble, kPromptX, kPromptY,
	                                  _promptSprite.data(), /*opaque=*/true);
}

// -------------------------------------------------------------------------
// Movement
// -------------------------------------------------------------------------

bool CyberspaceScene::canStep(Direction dir) const {
	if (dir < 0 || dir > 3)
		return false;
	int nx = (_playerPx + kDxByDir[dir] * kAnimFrames) & (kWorldPixels - 1);
	int ny = (_playerPy + kDyByDir[dir] * kAnimFrames) & (kWorldPixels - 1);
	int col = ((nx - 0x20) >> 4) & 0x1F;
	int row = (ny >> 4) & 0x1F;
	uint8 cell = gridAt(col, row);
	if (cell == 0xFF)
		return true;
	if (cell == 0x23)
		return _inLinkMode;
	return true; // site cell: move onto it, trigger enter
}

void CyberspaceScene::startStep(Direction dir) {
	_moveDir = dir;
	_animPhase = kAnimFrames;
	_state = kStateAnimating;
}

void CyberspaceScene::tickAnimation() {
	if (_animPhase <= 0) {
		_state = kStateRoaming;
		return;
	}
	_playerPx = (_playerPx + kDxByDir[_moveDir]) & (kWorldPixels - 1);
	_playerPy = (_playerPy + kDyByDir[_moveDir]) & (kWorldPixels - 1);
	--_animPhase;
	if (_animPhase == 0)
		postStep();
}

void CyberspaceScene::postStep() {
	// DOS cyb_move_post_tick checks grid[row][col+2] for site trigger.
	int col = (playerCol() + 2) & 0x1F;
	int row = playerRow();
	uint8 cell = gridAt(col, row);
	if (cell != 0xFF && (cell == 0x23 ? _inLinkMode : !_inLinkMode)) {
		_currentSite = cell;
		_pendingSite = cell;
		// Non-link-mode entry goes through the Y/N confirmation prompt
		// (matches DOS cyb_site_enter's "Can't do that here?" menu).
		// Link-mode cells go straight in (DOS bypasses the prompt when
		// cRam00024C75 != 0).
		if (_inLinkMode) {
			uint8 t = _pendingSite;
			_pendingSite = 0;
			enterSite(t);
		} else {
			_state = kStateSitePrompt;
		}
	} else {
		_state = kStateRoaming;
	}
}

// -------------------------------------------------------------------------
// Input
// -------------------------------------------------------------------------

CyberspaceScene::Direction CyberspaceScene::keyToDirection(uint16 ascii) const {
	// Intentionally no WASD here: those letters are menu hotkeys in
	// DOS (s=Software Analysis, d=Disk Options). Arrow keys are the
	// only movement input, matching cyb_input_to_direction.
	(void)ascii;
	return kDirNone;
}

// -------------------------------------------------------------------------
// Menu / site
// -------------------------------------------------------------------------

namespace {

// Skill name lookup, matches DOS items.c:60-75 (the trainable slice
// at item codes 0x43..0x52) and the Skills sub-module's table. Kept
// duplicated here instead of exposing it publicly -- both lists are
// derived from the same DOS table and change together.
const char *const kCyberSkillNames[16] = {
	"Bargain",  "CopTalk", "Warez",    "Debug",
	"HW Rep",   "ICE Brk", "Evasion",  "Crypto",
	"Japan",    "Logic",   "Psycho",   "Phenom",
	"Philo",    "Sophis",  "Zen",      "Music"
};

} // anonymous namespace

bool CyberspaceScene::dispatchMenu(MenuCode code) {
	RealWorldScene *rw = dynamic_cast<RealWorldScene *>(_engine->pausedScene());

	switch (code) {
	case kMenuExit:
		_state = kStateExiting;
		return true;

	case kMenuRoleMonitor:
		// DOS cyb_role_or_banking shows bank transfer info. We read
		// live real-world state via the paused scene.
		if (rw) {
			_siteText = Common::String::format(
				"Role / Monitor\n\n"
				"Cash:  %d\n"
				"Bank:  %d\n"
				"Name:  %s",
				(int)rw->cash(), (int)rw->bankAccount(),
				rw->playerName().c_str());
		} else {
			_siteText = "Role / Monitor\n\n(Real-world state unavailable.)";
		}
		_state = kStateSiteInterior;
		return true;

	case kMenuSoftwareAnalysis: {
		// DOS rom_software_analysis jumps into the shared skills picker.
		// We display the skill levels as a read-only list; the full
		// picker UI lives in RealWorldScene's Skills sub-module and is
		// reachable from the real world (Skills UI button).
		Common::String body = "Skills\n\n";
		if (rw) {
			const uint8 *sk = rw->skills();
			for (int i = 0; i < 16; i += 2) {
				body += Common::String::format("%-7s %2d   %-7s %2d\n",
					kCyberSkillNames[i],     sk[i],
					kCyberSkillNames[i + 1], sk[i + 1]);
			}
		} else {
			body += "(state unavailable)";
		}
		_siteText = body;
		_state = kStateSiteInterior;
		return true;
	}

	case kMenuSoftwareDebug: {
		// DOS rom_software_debug is a software picker. We list the
		// current software loadout (32 slots x 4 bytes; code at byte 0,
		// 0xFF = empty), bouncing with a note that the real run action
		// needs the overlay system.
		Common::String body = "Software loadout\n\n";
		if (rw) {
			const uint8 *sw = rw->softwareSlots();
			int shown = 0;
			for (int slot = 0; slot < 32 && shown < 10; ++slot) {
				uint8 id = sw[slot * 4];
				if (id == 0xFF) continue;
				body += Common::String::format("  %2d. %s\n", slot,
				                               Inventory::itemName(id));
				++shown;
			}
			if (shown == 0) body += "  (empty)\n";
		} else {
			body += "(state unavailable)";
		}
		_siteText = body;
		_state = kStateSiteInterior;
		return true;
	}

	case kMenuDiskOptions:
		// Open the ScummVM main menu (save / load / quit / prefs). This
		// is DOS-parity for the Disk Options menu which was a save /
		// load / quit picker. openMainMenuDialog pauses the engine
		// while the dialog is up, so control resumes here afterwards.
		_engine->openMainMenuDialog();
		return true;

	case kMenuEraseVirus:
		// Overlay-loaded software effects aren't implemented; would
		// erase a specific virus program slot in DOS.
		_siteText =
			"Erase Virus\n\n"
			"No active virus program\nto erase.";
		_state = kStateSiteInterior;
		return true;
	}
	return false;
}

void CyberspaceScene::enterSite(uint8 siteType) {
	debugC(1, kDebugGeneral, "CyberspaceScene: entering site type 0x%02x", siteType);
	_siteText = siteScriptText(siteType);
	_state = kStateSiteInterior;
}

void CyberspaceScene::exitSite(int result) {
	(void)result;
	// DOS cyb_site_enter sets uRam00024CC7 = 0xFFFF then clears the
	// cell via cyb_move_post_tick to prevent re-trigger. We clear
	// directly here.
	int col = (playerCol() + 2) & 0x1F;
	int row = playerRow();
	_grid[((row & 0x1F) << 5) | (col & 0x1F)] = 0xFF;
	_currentSite = -1;
	_state = kStateRoaming;
}

const char *CyberspaceScene::siteScriptText(uint8 siteType) const {
	// Placeholder site scripts. DOS sources: null-terminated string pool
	// at DS:0x724C, indexed by site_type*4 via FUN_1000_BBC8. Real scripts
	// need extraction from the binary -- meanwhile these stubs keep the
	// scene functional during development.
	switch (siteType) {
	case 0x01: return "Database online. No\nsecurity detected.\nScanning...";
	case 0x02: return "Uplink accepted.\nRunning diagnostics...";
	case 0x03: return "Audit stack detected.\nBegin analysis.";
	case 0x09: return "Music library access.\nBrowse tracks?";
	case 0x0B: return "Tomb-access terminal.\nEnter challenge.";
	default:   return "Unknown construct.\nExiting.";
	}
}

} // End of namespace Neuromancer
