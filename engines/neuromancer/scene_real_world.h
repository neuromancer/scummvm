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

#include "neuromancer/body_parts_shop.h"
#include "neuromancer/inventory.h"
#include "neuromancer/pax.h"
#include "neuromancer/rom.h"
#include "neuromancer/scene.h"
#include "neuromancer/skills.h"
#include "neuromancer/text_scroller.h"

#include "common/array.h"
#include "common/serializer.h"
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
	void resume() override;
	SceneId update() override;
	void handleEvent(const Common::Event &event) override;

	// Save / load hook. Called by NeuromancerEngine::syncGame to persist
	// or restore all scene-owned state (player position, cash, bank,
	// inventory, skills, ...). On load the caller re-invokes loadLevel()
	// afterwards so the PIC / BIH / character sprite re-render.
	void syncGame(Common::Serializer &s);

	// Rebuild visual state after a save-file load. Keeps _charX/_charY/
	// _charDir overrides from the loaded state instead of applyRoompos()'s
	// defaults.
	void reinitializeAfterLoad();

	// Open the Skills sub-module directly. Used by the ROM panel's
	// "Software Analysis" option, which in DOS is a one-instruction
	// jump into the shared skill picker (FUN_1000_b679 -> FUN_1000_7e62).
	void openSkillsMenu();

	// The most recent free-form text the player typed at a dialog
	// "@"-prefixed reply. Mirrors DOS g_dialog_user_input (a 16-byte
	// buffer). BIH-script callback CB_CMD_NPC_REPLY (cmd 5) compares
	// this against a DSEG-hosted string list. Empty when no text has
	// been submitted since the last reset.
	const Common::String &dialogInput() const { return _dialogInput; }
	void setDialogInput(const Common::String &text) { _dialogInput = text; }

	// Request a transition into the cyberspace scene. Used by the ROM
	// panel's "Software Debug" / "Monitor Mode" options and by
	// Operating a deck item -- the DOS item_use_dispatch (1000:D846)
	// dispatches to cyberspace_main_loop for these cases.
	void enterCyberspace();

	// Open the PAX sub-module directly. Used by the comlink path from
	// the inventory (DOS item_use_dispatch mode-0 sub_idx-0 calls
	// sub_189AE, which shares the same bulletin-board UI that our PAX
	// implements). Sub-modules that need to bring up the PAX panel
	// outside of the real-world UI button can call this.
	void openPax();

	// Push the current inventory (items[128] + software[128] +
	// skills[16]) into the VM's DSEG at the DOS g_3f85 offsets. Call
	// after any direct mutation of itemSlots() / softwareSlots() /
	// skills() so the DSEG view stays in sync for BIH-script queries.
	void mirrorInventory() { mirrorInventoryToDseg(); }

	// ---- Game state accessors (used by PAX / Inventory sub-modules) -----
	int32 cash() const { return _cash; }
	void  setCash(int32 v);
	int16 constitution() const { return _constitution; }
	void  setConstitution(int16 v);
	int32 bankAccount() const { return _bankAccount; }
	void  setBankAccount(int32 v);
	uint8 bankTxIndex() const { return _bankTxIndex; }
	void  setBankTxIndex(uint8 v);
	uint8 bankTxOp(int i) const { return _bankTx[i & 3].op; }
	uint32 bankTxAmount(int i) const { return _bankTx[i & 3].amount; }
	void  setBankTxRecord(int i, uint8 op, uint32 amount) {
		_bankTx[i & 3].op = op;
		_bankTx[i & 3].amount = amount;
	}
	int16 dateDay() const { return _dateDay; }
	const Common::String &playerName() const { return _playerName; }
	void setPlayerName(const Common::String &v);

	// Render the current game-world date as "mm/dd/yy" (e.g. "11/16/58"
	// on day 0). Matches DOS build_date_string
	// (scene_real_world.c:567-593), used by the status widget and any
	// text-widget that splices opcode 0x02 (date) into the output.
	Common::String dateString() const { return dateStringForDay((int)_dateDay); }

	// Same formatter, but for an arbitrary day offset. Used e.g. by PAX
	// banking to format each transaction's date (low 6 bits of the op
	// byte) without duplicating the calendar wrap math.
	static Common::String dateStringForDay(int dateDay);

	// Inventory slots. 32 slots of 4 bytes each (item code, version /
	// sub-value, flag, aux) -- matches DOS g_3f85.inventory.items /
	// .software at offset 0x41D7 in data.h:179. code == 0xFF = empty.
	// The Inventory sub-module reads / writes through these helpers so
	// the rest of the scene (and future save / load) can stay agnostic
	// to the exact packing.
	uint8 *itemSlots()           { return _invItems;    }
	const uint8 *itemSlots() const { return _invItems;  }
	uint8 *softwareSlots()       { return _invSoftware; }
	const uint8 *softwareSlots() const { return _invSoftware; }

	// Player skills (DOS g_3f85.skills[16] at 0x42D7). Each byte tracks
	// a level 0..N for one of 16 trainable skills. Raising a skill
	// happens by operating the corresponding skill chip in the inventory;
	// NPCs / scripts read this to tell whether the player can perform a
	// specific task.
	uint8 *skills()              { return _skills;     }
	const uint8 *skills() const  { return _skills;     }

	// Gas-mask toggle state (DOS g_4bae.gas_mask_is_on at DSEG 0x4C19).
	// Toggled via Operate on the gas-mask item.
	bool gasMaskOn() const       { return _gasMaskOn; }
	void setGasMaskOn(bool on);

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
	// for name, 0x02 for date). For the scroll widget the text is handed
	// to the TextScroller for a DOS-style line-by-line reveal. For the
	// bubble widget the text is paginated and shown one page at a time.
	void showText(const char *text, TextWidget widget);
	bool pageTextForward(); // true if more pages remain; false if done
	void renderCurrentPage();
	void renderScrollerWidget();
	void clearTextWidgets();
	// Advance or dismiss the active text widget based on its current
	// state. Returns true if the widget consumed the input (caller should
	// return early from its event handler).
	bool advanceActiveText();

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

	// Text display state. The scroll widget (lower 17x7 box) uses the
	// line-by-line TextScroller to reveal text with the DOS teletype feel
	// (matching WA_TYPE_TEXT_SCROLLING). The bubble widget (dialog
	// replies / op 0x01, op 0x18) doesn't scroll -- it's short enough to
	// show in a single frame and dismiss on input.
	TextScroller _scroller;
	bool         _scrollerActive;
	TextScroller::State _lastScrollerState;
	int          _lastScrollerLines;

	// Bubble paging (short-message path). The bubble body can still be a
	// few lines, so we keep the old page-at-a-time flow for it.
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

	// Text-input sub-state of the dialog picker. DOS dialog replies
	// that begin with '@' (e.g. "@Enter password") switch from the
	// cycle-through picker to a free-form text entry widget; the typed
	// string is stashed in _dialogInput and checked by BIH-script
	// callback CB_CMD_NPC_REPLY. While active, key events append
	// printable chars (cap at 16), backspace shortens, enter commits.
	bool           _dialogTextInput;
	Common::String _dialogTyped;

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

	// Per-level walkable bounds derived from ROOMPOS exit rectangles.
	// Match DOS character_control_update (scene_real_world.c:82-85):
	//   L = exits[LEFT][0]  * 2
	//   R = (exits[RIGHT][0] + exits[RIGHT][2]) * 2
	//   T = exits[UP][1]
	//   B = exits[DOWN][1]  + exits[DOWN][3]
	int _walkL, _walkR, _walkT, _walkB;

	// Raw 4-byte exit rectangles for the current level (N, E, S, W). Each
	// byte group is { x_packed, y, w_packed, h } with x / w in packed
	// coordinates (multiply by 2 for pixels). Used to detect when the
	// character has walked into an exit zone and the level should change.
	uint8 _exitZones[4][4];

	// Direction the player used to leave the previous level, in the same
	// 0..3 enum as CharDir. -1 on a fresh start (initial entry from DOS
	// south-exit default). The opposite direction is where the character
	// spawns on the new level, matching DOS roompos_init's transition
	// formula (transition = (exit_point + 2) & 3).
	int _lastExitDir;

	void applyRoomposForCurrentLevel();

	// If (_charX, _charY) currently sits inside the exit rectangle for
	// direction `dir` AND the per-level transition table has a target,
	// returns that target level (0..57). Otherwise returns -1. Mirrors
	// DOS roompos_hit_exit_zone (scene_real_world.c:926).
	int hitExitZone(int dir) const;

	// Run exit detection after each character move. If an exit fires,
	// swap levels, seeding roompos_init from _lastExitDir.
	void checkForLevelExit();

	// Game state surfaced by the status widget. Defaults are the level 1
	// start values from the DOS save-slot template (11/16/58, cash=6).
	StatusMode _statusMode;
	int32 _cash;
	int16 _constitution;
	int16 _timeH, _timeM;
	int16 _dateDay; // day offset from 11/16/58 -- the DOS build_date_string input

	// Bank state. Mirrors g_4bae.bank_account / bank_transaction_record[4]
	// from the DOS build (data.h:290-300). The ring buffer index lives in
	// the low 2 bits of _bankTxIndex; op's high 2 bits encode the kind of
	// transaction (upload / download / transfer-in / fined) while the low
	// 6 bits hold the in-game date the op happened on.
	int32 _bankAccount;
	uint8 _bankTxIndex;
	struct BankTx { uint8 op; uint32 amount; } _bankTx[4];

	// Player name (DOS g_4bae.name). The DOS save slot defaults to
	// "{@Case\0..." with two leading decoration bytes; we store only the
	// printable part.
	Common::String _playerName;

	// Free-form text captured from an "@"-prefixed dialog reply. DOS
	// stores 16 chars in g_dialog_user_input; using Common::String lets
	// us handle upper/lower-case without a second buffer. Empty string
	// means no current input.
	Common::String _dialogInput;

	// Inventory slot arrays (32 * 4 bytes each), matching DOS
	// neuro_inventory_t at DSEG 0x41D7. Initial contents set in the
	// constructor to the DOS save-slot defaults: items[0]=0x5F (pawn
	// ticket), items[1]=0x53 (CyberEyes), software[0]=0x00 (Mimic v1).
	uint8 _invItems[128];
	uint8 _invSoftware[128];

	// Player skills (16 bytes). Parallel slot for each of the 16
	// trainable skills; raised by operating skill-chip items.
	uint8 _skills[16];

	// Gas mask on/off flag. Simple bool here -- the Operate handler
	// mirrors it into the VM DSEG (0x4C19) so level scripts can check.
	bool _gasMaskOn;

	// Wall-clock of the last in-game minute tick. The DOS ui_panel_update
	// bumps time_m every ~1 real second (scene_real_world.c:601).
	uint32 _lastClockTickMs;

	void tickGameClock(uint32 nowMs);
	void mirrorClockToDseg();
	void mirrorRoomposToDseg();
	void mirrorInventoryToDseg();

	// PAX ("Public Access eXchange") sub-module. Activated by the PAX UI
	// button; while _pax.isActive() the scene freezes VM / character ticks
	// and routes events to _pax. Mirrors DOS update_pax()'s sub-state.
	Pax _pax;
	void restoreCharacterAfterPax();

	// Inventory sub-module. Activated by the Inventory UI button; handles
	// item list + Give Credits (which sets active_item + cash_withdrawal
	// in DSEG so the per-level BIH script can detect the payment).
	Inventory _inventory;

	// Skills sub-module. Activated by the Skills UI button; currently a
	// list + description view. Complex per-skill flows (Warez / Debug /
	// HW Repair / Cryptology / Musicianship) land in a later phase.
	Skills _skillsMenu;

	// ROM-construct sub-module. Ports the DOS rom_panel_open /
	// rom_main_loop pair -- matches the Chiba BAMA ROM panel UI.
	// "Software Debug" / "Monitor Mode" bounce with the DOS
	// "Only in cyberspace." message; "Software Analysis" re-routes
	// into the Skills picker.
	Rom _rom;

public:
	// Body-parts shop sub-module. Opened by BIH-script callbacks
	// 8 (sell) / 9 (buy) which interrupt the VM and hand control to
	// Dr. Chrome's menu. Currently a minimum-viable panel; real
	// inventory / cash / constitution mutations land in follow-ups.
	BodyPartsShop &bodyPartsShop() { return _bodyPartsShop; }

private:
	BodyPartsShop _bodyPartsShop;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_REAL_WORLD_H
