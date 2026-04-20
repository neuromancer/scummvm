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

#include "neuromancer/scene_real_world.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"
#include "neuromancer/font.h"
#include "neuromancer/gfx.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/neuro_vm.h"
#include "neuromancer/resource.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/keyboard.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"

namespace Neuromancer {

namespace {

enum {
	kPicPackedW = 152,
	kPicHeight  = 112,
	kPicBytes   = kPicPackedW * kPicHeight,

	// Scroll widget geometry. The DOS source stores l/w in packed-byte
	// coordinates (window_animation.c:290-297) -- pixel X = l*2, pixel
	// width = w*2. Height = b - t + 1.
	kScrollX          = 176,        // DOS l=88 (packed) * 2
	kScrollY          = 134,        // DOS t=134 (pixels)
	kScrollWidthPx    = 136,        // DOS w=68 (packed) * 2
	kScrollHeightPx   = 58,         // DOS b-t+1 = 191-134+1
	kScrollPackedW    = kScrollWidthPx / 2,
	kScrollBytes      = kScrollPackedW * kScrollHeightPx,
	kScrollColumns    = kScrollWidthPx / 8,   // 17
	kScrollRows       = 7,                    // DOS max_lines

	// Dialog bubble: full-width bordered frame at top=4. DOS formula
	// (neuro_window_control.c:96) is bottom = lines*8 + 19, so height =
	// lines*8 + 16 for the drawable region. We provision for up to 8
	// lines statically; unused rows get drawn as border-only.
	kBubbleX          = 0,
	kBubbleY          = 4,
	kBubbleWidthPx    = 320,
	kBubbleHeightPx   = 8 * 8 + 16, // 80 px (room for 8 wrapped lines + borders)
	kBubblePackedW    = kBubbleWidthPx / 2,
	kBubbleBytes      = kBubblePackedW * kBubbleHeightPx,
	kBubbleInnerLeft  = 8,          // inner text padding
	kBubbleInnerTop   = 8,
	kBubbleInnerWidth = kBubbleWidthPx  - 2 * kBubbleInnerLeft,
	kBubbleColumns    = kBubbleInnerWidth / 8,  // 38
	kBubbleRows       = 8,

	// Status widget at (96, 149): 8 chars wide, 1 row tall.
	// Matches the DOS build's ui_panel_update() which writes 8-char
	// formatted strings ("$    0", "   00:00", "11/16/58", etc.)
	// directly into NEURO.IMH at that offset.
	kStatusX        = 96,
	kStatusY        = 149,
	kStatusWidthPx  = 64,
	kStatusHeightPx = 8,
	kStatusPackedW  = kStatusWidthPx / 2,
	kStatusBytes    = kStatusPackedW * kStatusHeightPx,

	kMaxLevel = 57
};

// Clickable button footprint. Matches neuro_button_t in the DOS build.
struct UiButtonRect {
	int16 left, top, right, bottom;
	int   code;
	char  label;
};

// Transcribed verbatim from data.c:127-138. Coordinates are absolute pixel
// positions on the 320x200 screen. The icons themselves are painted into
// NEURO.IMH, so we only need click/keyboard dispatch here.
static const UiButtonRect kUiButtons[] = {
	{ 0x10, 0x93, 0x23, 0xA5, 0x00, 'i' }, // inventory
	{ 0x28, 0x93, 0x3B, 0xA5, 0x01, 'p' }, // pax
	{ 0x40, 0x93, 0x53, 0xA5, 0x02, 't' }, // dialog / talk
	{ 0x10, 0xAB, 0x23, 0xBD, 0x03, 's' }, // skills
	{ 0x28, 0xAB, 0x3B, 0xBD, 0x04, 'r' }, // ROM / chip
	{ 0x40, 0xAB, 0x53, 0xBD, 0x05, 'd' }, // disk options
	{ 0x70, 0xA8, 0x7D, 0xB2, 0x0A, '1' }, // date
	{ 0x80, 0xA8, 0x8F, 0xB2, 0x0B, '2' }, // time
	{ 0x70, 0xB3, 0x7D, 0xBB, 0x0C, '3' }, // cash
	{ 0x80, 0xB3, 0x8F, 0xBB, 0x0D, '4' }, // constitution
};

void writeImhHeader(byte *buf, int16 dx, int16 dy, uint16 packedW, uint16 h) {
	WRITE_LE_UINT16(buf + 0, (uint16)dx);
	WRITE_LE_UINT16(buf + 2, (uint16)dy);
	WRITE_LE_UINT16(buf + 4, packedW);
	WRITE_LE_UINT16(buf + 6, h);
}

// Character walk-cycle frame offsets inside SPRITES.IMH. Transcribed from
// Reuromancer/NeuromancerWin64/character_control.c:18-26 (the g_*_frames
// tables). Each entry is a byte offset to an IMH frame (header + pixels)
// inside the decompressed spritesheet.
static const uint16 kUpFrames[8]    = {
	0x0000, 0x037A, 0x06F4, 0x0A7C, 0x0DE8, 0x1162, 0x14DC, 0x1856
};
static const uint16 kRightFrames[8] = {
	0x1B46, 0x1EA4, 0x20A4, 0x2394, 0x277C, 0x2AE8, 0x2CE8, 0x2FD8
};
static const uint16 kDownFrames[8]  = {
	0x33C0, 0x36B0, 0x3A2A, 0x3DA4, 0x411E, 0x448A, 0x4812, 0x4B8C
};
static const uint16 kLeftFrames[8]  = {
	0x4F06, 0x5272, 0x5472, 0x5762, 0x5B4A, 0x5EB6, 0x6134, 0x6424
};

static uint16 frameOffsetFor(int dir, int frame) {
	frame &= 7;
	switch (dir) {
	case 0: return kUpFrames[frame];
	case 1: return kRightFrames[frame];
	case 2: return kDownFrames[frame];
	case 3: return kLeftFrames[frame];
	default: return kDownFrames[0];
	}
}

enum {
	kCharSpeedHort = 5,
	kCharSpeedVert = 2,
	kCharFrameCapMs = 100
};

// Per-level exit destinations. Transcribed verbatim from DOS data.c:233-291
// (g_3f85.level_info[58].level_transitions). 58 levels, 4 directions each
// (N, E, S, W). 0xFF = no exit in that direction. Indices match our
// 0-based level enum (level 0 = Chatsubo / R1.BIH).
static const uint8 kLevelTransitions[58][4] = {
	{ 0xFF, 0xFF, 0x01, 0xFF }, { 0xFF, 0x04, 0xFF, 0xFF },
	{ 0x01, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0x04, 0xFF },
	{ 0x03, 0x0C, 0x05, 0x01 }, { 0x04, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0x0E, 0xFF, 0xFF }, { 0xFF, 0x0F, 0xFF, 0xFF },
	{ 0xFF, 0x10, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0x0C, 0xFF },
	{ 0x0B, 0x17, 0x0D, 0x04 }, { 0x0C, 0x18, 0x0E, 0xFF },
	{ 0x0D, 0x19, 0x0F, 0x06 }, { 0x0E, 0xFF, 0x10, 0x07 },
	{ 0x0F, 0x1A, 0x11, 0xFF }, { 0x10, 0xFF, 0xFF, 0x12 },
	{ 0xFF, 0x11, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF },
	{ 0x15, 0x1D, 0xFF, 0xFF }, { 0xFF, 0xFF, 0x14, 0xFF },
	{ 0xFF, 0xFF, 0x0B, 0xFF }, { 0xFF, 0xFF, 0xFF, 0x0C },
	{ 0xFF, 0x0D, 0xFF, 0xFF }, { 0xFF, 0x1E, 0xFF, 0x0E },
	{ 0xFF, 0xFF, 0xFF, 0x10 }, { 0xFF, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0x1D, 0xFF }, { 0x1C, 0x20, 0xFF, 0x14 },
	{ 0x1F, 0x26, 0xFF, 0x19 }, { 0xFF, 0xFF, 0x1E, 0xFF },
	{ 0x21, 0x29, 0xFF, 0x1D }, { 0xFF, 0xFF, 0x20, 0xFF },
	{ 0xFF, 0xFF, 0x21, 0xFF }, { 0xFF, 0xFF, 0x24, 0xFF },
	{ 0x23, 0xFF, 0x25, 0xFF }, { 0x24, 0x2B, 0x26, 0xFF },
	{ 0x25, 0x2C, 0x27, 0x1E }, { 0x26, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0x29, 0xFF }, { 0x28, 0xFF, 0xFF, 0x20 },
	{ 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0x25 },
	{ 0xFF, 0x30, 0x2D, 0x26 }, { 0x2C, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0xFF, 0x28 }, { 0xFF, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0x33, 0xFF, 0x2C }, { 0xFF, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0x35, 0xFF, 0xFF }, { 0xFF, 0x36, 0xFF, 0x30 },
	{ 0xFF, 0xFF, 0x35, 0xFF }, { 0xFF, 0xFF, 0x36, 0xFF },
	{ 0x35, 0xFF, 0xFF, 0x33 }, { 0x36, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0xFF, 0x35 }, { 0xFF, 0xFF, 0xFF, 0x36 }
};

} // anonymous namespace

RealWorldScene::RealWorldScene(NeuromancerEngine *engine)
	: Scene(engine),
	  _next(kSceneRealWorld),
	  _textVisible(false),
	  _introPending(false),
	  _scrollerActive(false),
	  _lastScrollerState(TextScroller::kIdle),
	  _lastScrollerLines(-1),
	  _currentPage(0),
	  _activeWidget(kWidgetScroll),
	  _dialogOpen(false),
	  _dialogCurrentReply(0),
	  _dialogFirst(0),
	  _dialogTotal(0),
	  _dialogTextInput(false),
	  _charX(160),
	  _charY(117),
	  _charDir(kDirUp),
	  _charFrame(0),
	  _charMoving(false),
	  _lmbHeld(false),
	  _charLastStepMs(0),
	  _walkL(8), _walkR(312), _walkT(8), _walkB(120),
	  _lastExitDir(-1),
	  _statusMode(kStatusDate),
	  _cash(6),
	  _constitution(2000),
	  _timeH(0),
	  _timeM(0),
	  _dateDay(0),
	  _bankAccount(2000),
	  _bankTxIndex(0),
	  _playerName("Case"),
	  _lastClockTickMs(0),
	  _pax(engine, this),
	  _inventory(engine, this),
	  _skillsMenu(engine, this),
	  _rom(engine, this),
	  _bodyPartsShop(engine, this) {
	// DOS save-slot defaults for the 4-entry bank-transaction ring
	// buffer (data.c:472-475). First 3 records are uploads of 120/56/75
	// credits, last is a 1000-credit fined/withdrawn marker. The PAX
	// banking screen reads these to display Case's transaction history.
	static const struct { uint8 op; uint32 amount; } kInitialTx[4] = {
		{ 0x40, 120 }, { 0x40, 56 }, { 0x40, 75 }, { 0xC0, 1000 }
	};
	for (int i = 0; i < 4; i++) {
		_bankTx[i].op = kInitialTx[i].op;
		_bankTx[i].amount = kInitialTx[i].amount;
	}

	// Empty all inventory slots, then seed the DOS save-slot defaults
	// (data.c:293-313). Each slot is 4 bytes: [code, version, flag, aux].
	// 0xFF in the code byte means the slot is unused.
	memset(_exitZones,   0,    sizeof(_exitZones));
	memset(_invItems,    0xFF, sizeof(_invItems));
	memset(_invSoftware, 0xFF, sizeof(_invSoftware));
	// DOS save-slot default (data.c:331-334 g_3f85.skills): Case starts
	// with only Bargaining (idx 0) and Debug (idx 3) at level 0; every
	// other skill is 0xFF (not acquired). The player picks up chips
	// through gameplay. Matching DOS exactly so the skills picker shows
	// the intended initial list.
	static const uint8 kStartSkills[16] = {
		0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};
	memcpy(_skills, kStartSkills, sizeof(_skills));
	_gasMaskOn = false;
	for (int i = 0; i < 32; i++) {
		_invItems[i * 4 + 1] = _invItems[i * 4 + 2] = _invItems[i * 4 + 3] = 0;
		_invSoftware[i * 4 + 1] = _invSoftware[i * 4 + 2] = _invSoftware[i * 4 + 3] = 0;
	}
	// items[0] = pawn ticket (0x5F), items[1] = CyberEyes (0x53)
	_invItems[0 * 4 + 0] = 0x5F;
	_invItems[1 * 4 + 0] = 0x53;
	// software[0] = Mimic v1 (code 0x00, version 1)
	_invSoftware[0 * 4 + 0] = 0x00;
	_invSoftware[0 * 4 + 1] = 0x01;
}

RealWorldScene::~RealWorldScene() = default;

void RealWorldScene::init() {
	ResourceManager *res = _engine->resources();

	_neuroImh.resize(64000);
	uint32 neuroSize = res->load("NEURO.IMH", _neuroImh.data());
	debugC(1, kDebugResource, "RealWorldScene: NEURO.IMH -> %u bytes", neuroSize);

	_picSprite.resize(sizeof(ImhHeader) + kPicBytes);
	writeImhHeader(_picSprite.data(), 0, 0, kPicPackedW, kPicHeight);

	_scrollSprite.resize(sizeof(ImhHeader) + kScrollBytes);
	writeImhHeader(_scrollSprite.data(), 0, 0, kScrollPackedW, kScrollHeightPx);

	_bubbleSprite.resize(sizeof(ImhHeader) + kBubbleBytes);
	writeImhHeader(_bubbleSprite.data(), 0, 0, kBubblePackedW, kBubbleHeightPx);

	_statusSprite.resize(sizeof(ImhHeader) + kStatusBytes);
	writeImhHeader(_statusSprite.data(), 0, 0, kStatusPackedW, kStatusHeightPx);

	_bihData.resize(64000);

	SpriteChain *chain = _engine->spriteChain();
	chain->addSprite(kLayerBackground, 0, 0, _neuroImh.data(), true);

	loadLevel();

	// Seed the DSEG mirror of the cash / CON / bank / name fields so
	// BIH scripts that query them on the opening level see the real
	// starting values instead of the zero-initialised BSS the VM gave
	// them. Must happen AFTER loadLevel() since loadLevel attaches the
	// BIH buffer (which fills the opening ~40K of DSEG). The save-load
	// path uses the same setters from syncGame to keep this symmetric.
	setCash(_cash);
	setConstitution(_constitution);
	setBankAccount(_bankAccount);
	setBankTxIndex(_bankTxIndex);
	setPlayerName(_playerName);
	setGasMaskOn(_gasMaskOn);
	mirrorClockToDseg();
	mirrorRoomposToDseg();
	mirrorInventoryToDseg();
	updateStatusWidget();
}

void RealWorldScene::deinit() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerBackground);
	chain->clearSprite(kLayerLevelBg);
	chain->clearSprite(kLayerDialogBubble);
	chain->clearSprite(kLayerNeuroMenu);
	chain->clearSprite(kLayerCharacter);
	chain->clearSprite(kLayerStatusWidget);
	chain->clearSprite(kLayerDebugOverlay);
}

// Called when the real-world scene is resumed after a cyberspace
// round-trip. The scene instance was preserved across the swap so all
// member state (level, inventory, VM counters, character pose) is still
// intact -- we only need to re-push the sprite layers that deinit()
// cleared. Deliberately does NOT call loadLevel() / init(): those would
// reset the player to the spawn point and re-queue the level intro.
void RealWorldScene::resume() {
	SpriteChain *chain = _engine->spriteChain();

	// Re-install the persistent UI sprites. Buffer contents were kept
	// in memory across the pause; we just re-register them with the
	// chain that was cleared by deinit().
	if (!_neuroImh.empty())
		chain->addSprite(kLayerBackground, 0, 0, _neuroImh.data(), true);

	// The level PIC (kLayerLevelBg) was pushed by the most recent
	// loadLevel() but deinit cleared it. Push it back using the still-
	// cached sprite buffer so the player sees the level art on return
	// from cyberspace, not a blank background.
	if (!_picSprite.empty())
		chain->addSprite(kLayerLevelBg, 8, 8, _picSprite.data(), true);

	// Re-render current-level artwork, character, and status widget
	// from the preserved state.
	renderCurrentPage();
	renderCharacterFrame();
	updateStatusWidget();
}

// Save / load hook. Persists the bits of player-facing state the scene
// owns directly. The engine is responsible for currentLevel / visited
// bits; the VM serializes its own DSEG + threads.
// Cash and constitution both have mirrors in the DSEG that the VM /
// BIH scripts can read via readVar*. Mirror both ways so a direct
// setter call updates the HUD *and* any script branching on the DSEG
// view. DSEG offsets from DOS data.h:
//   cash          = 0x4C78 (uint32)
//   constitution  = 0x4C9F (uint16)
void RealWorldScene::setCash(int32 v) {
	_cash = v;
	if (NeuroVM *vm = _engine->vm()) {
		uint32 u = (uint32)v;
		vm->writeVar16(0x4C78, (uint16)(u & 0xFFFF));
		vm->writeVar16(0x4C7A, (uint16)(u >> 16));
	}
	updateStatusWidget();
}

void RealWorldScene::setConstitution(int16 v) {
	_constitution = v;
	if (NeuroVM *vm = _engine->vm())
		vm->writeVar16(0x4C9F, (uint16)v);
	updateStatusWidget();
}

// DSEG-mirrored bank / name setters. Same DSEG layout as init():
//   bank_account    = 0x4C89 (uint32)
//   bank_tx_index   = 0x4C8D (uint8, low 2 bits active)
//   name[13]        = 0x4C92..0x4C9E (null-padded 13-byte buffer)
void RealWorldScene::setBankAccount(int32 v) {
	_bankAccount = v;
	if (NeuroVM *vm = _engine->vm()) {
		uint32 u = (uint32)v;
		vm->writeVar16(0x4C89, (uint16)(u & 0xFFFF));
		vm->writeVar16(0x4C8B, (uint16)(u >> 16));
	}
}

void RealWorldScene::setBankTxIndex(uint8 v) {
	_bankTxIndex = v & 3;
	if (NeuroVM *vm = _engine->vm())
		vm->writeVar8(0x4C8D, _bankTxIndex);
}

Common::String RealWorldScene::dateStringForDay(int dateDay) {
	int day = 16, month = 11, year = 58;
	if (dateDay > 14) {
		if (day + dateDay > 61) {
			year = 59; month = 1; day = day + dateDay - 61;
		} else {
			month = 12; day = day + dateDay - 30;
		}
	} else {
		day += dateDay;
	}
	return Common::String::format("%02d/%02d/%02d", month, day, year);
}

void RealWorldScene::setGasMaskOn(bool on) {
	_gasMaskOn = on;
	if (NeuroVM *vm = _engine->vm())
		vm->writeVar8(0x4C19, on ? 1 : 0);
}

void RealWorldScene::setPlayerName(const Common::String &v) {
	_playerName = v;
	if (NeuroVM *vm = _engine->vm()) {
		// DOS keeps name[0..1] = "{@" as decoration; displayable text
		// starts at name+2 (see Reuromancer scene_main_menu.c:39 and
		// rw_state_pax.c:435 -- every reader uses `g_4bae.name + 2`).
		// Mirror that layout so dialog text splicing `@name` picks up
		// the right characters.
		char buf[13] = { 0 };
		buf[0] = '{';
		buf[1] = '@';
		Common::strlcpy(buf + 2, _playerName.c_str(), sizeof(buf) - 2);
		for (int i = 0; i < 13; ++i)
			vm->writeVar8((uint16)(0x4C92 + i), (uint8)buf[i]);
	}
}

void RealWorldScene::syncGame(Common::Serializer &s) {
	// Player name. DOS stores this as a fixed 13-char field; we use a
	// Common::String backed by a fixed-size buffer for portability.
	char name[16] = { 0 };
	Common::strlcpy(name, _playerName.c_str(), sizeof(name));
	s.syncBytes((byte *)name, sizeof(name));
	if (s.isLoading())
		_playerName = name;

	// Character pose + movement state.
	s.syncAsSint32LE(_charX);
	s.syncAsSint32LE(_charY);
	{
		int8 dir = (int8)_charDir;
		s.syncAsSByte(dir);
		if (s.isLoading()) _charDir = (CharDir)dir;
	}
	s.syncAsSint32LE(_charFrame);
	{
		byte b = _charMoving ? 1 : 0;
		s.syncAsByte(b);
		if (s.isLoading()) _charMoving = (b != 0);
	}
	s.syncAsSint32LE(_lastExitDir);

	// Status + game-clock state.
	{
		byte mode = (byte)_statusMode;
		s.syncAsByte(mode);
		if (s.isLoading()) _statusMode = (StatusMode)mode;
	}
	s.syncAsSint32LE(_cash);
	s.syncAsSint16LE(_constitution);
	s.syncAsSint16LE(_timeH);
	s.syncAsSint16LE(_timeM);
	s.syncAsSint16LE(_dateDay);

	// Bank state.
	s.syncAsSint32LE(_bankAccount);
	s.syncAsByte(_bankTxIndex);
	for (int i = 0; i < 4; i++) {
		s.syncAsByte(_bankTx[i].op);
		s.syncAsUint32LE(_bankTx[i].amount);
	}

	// Inventory + skills + gas-mask.
	s.syncBytes(_invItems,    sizeof(_invItems));
	s.syncBytes(_invSoftware, sizeof(_invSoftware));
	s.syncBytes(_skills,      sizeof(_skills));
	{
		byte gas = _gasMaskOn ? 1 : 0;
		s.syncAsByte(gas);
		if (s.isLoading()) _gasMaskOn = (gas != 0);
	}

	// On load: push cash / constitution / bank / name into the VM's
	// DSEG mirror so any BIH script that reads them directly sees the
	// restored values. Symmetrical with init()'s first-run seeding.
	if (s.isLoading()) {
		setCash(_cash);
		setConstitution(_constitution);
		setBankAccount(_bankAccount);
		setBankTxIndex(_bankTxIndex);
		setPlayerName(_playerName);
		setGasMaskOn(_gasMaskOn);
		mirrorClockToDseg();
		mirrorRoomposToDseg();
		mirrorInventoryToDseg();
	}
}

// Direct Skills entry point. DOS rom_software_analysis
// (FUN_1000_b679 at neuro.exe 1000:b679) is literally a tail-call into
// the skill picker -- we reproduce that by closing any blocking widget
// and opening the Skills sub-module immediately.
void RealWorldScene::openSkillsMenu() {
	clearTextWidgets();
	_textVisible  = false;
	_introPending = false;
	_dialogOpen   = false;
	_skillsMenu.open();
}

void RealWorldScene::enterCyberspace() {
	clearTextWidgets();
	_textVisible  = false;
	_introPending = false;
	_dialogOpen   = false;
	_next = kSceneCyberspace;
}

void RealWorldScene::openPax() {
	clearTextWidgets();
	_textVisible  = false;
	_introPending = false;
	_dialogOpen   = false;
	// DOS sub_189AE writes g_a61a = 2 ("in comlink") on open; mirror
	// that to DSEG so scripts see the comlink-active state.
	if (NeuroVM *vm = _engine->vm())
		vm->writeVar8(0xA61A, 2);
	_pax.open();
}

// After a save-file load the engine re-activates this scene. We need to
// (a) re-run loadLevel() for the current level to reload the PIC / BIH /
// sprite assets, then (b) override the default spawn position + direction
// with whatever the save file provided. Also hides any text widgets left
// over in the loaded VM state -- we re-draw them as the VM runs.
void RealWorldScene::reinitializeAfterLoad() {
	// Cache restored character pose: loadLevel -> applyRoomposForCurrentLevel
	// would otherwise clobber it.
	int   savedX     = _charX;
	int   savedY     = _charY;
	CharDir savedDir = _charDir;

	// Reload the level assets + walk bounds + dialog widgets.
	loadLevel();
	// Suppress the intro text that loadLevel re-queues; the player has
	// already seen it in the saved session.
	_introPending = false;
	_textVisible  = false;
	clearTextWidgets();

	// Restore the saved pose, stop any movement, re-render.
	_charX        = savedX;
	_charY        = savedY;
	_charDir      = savedDir;
	_charFrame    = 0;
	_charMoving   = false;
	_lmbHeld      = false;
	renderCharacterFrame();
	updateStatusWidget();
}

SceneId RealWorldScene::update() {
	uint32 nowMs = g_system->getMillis();

	// PAX has exclusive control while active: freeze the VM, character
	// controller, and in-scene text widgets. Matches the DOS RWS_PAX sub-
	// state which tail-returns from update_pax() without running update_normal.
	if (_pax.isActive()) {
		_pax.update();
		tickGameClock(nowMs);
		_engine->render();
		if (!_pax.isActive()) {
			// PAX just closed this frame (e.g. via 'X' button): restore
			// the character sprite and re-acknowledge the scene.
			restoreCharacterAfterPax();
		}
		return _next;
	}

	// Inventory uses the same exclusive-control pattern as PAX. Same
	// rationale: freeze the VM + character while the window is up; on
	// close, let the VM resume so level scripts can react to any
	// cash_withdrawal / active_item change the player just made.
	if (_inventory.isActive()) {
		_inventory.update();
		tickGameClock(nowMs);
		_engine->render();
		return _next;
	}

	// Skills: same exclusive-control pattern.
	if (_skillsMenu.isActive()) {
		_skillsMenu.update();
		tickGameClock(nowMs);
		_engine->render();
		return _next;
	}

	// ROM construct: same pattern. Matches DOS rom_main_loop which
	// freezes the character controller while the panel is up.
	if (_rom.isActive()) {
		_rom.update();
		tickGameClock(nowMs);
		_engine->render();
		return _next;
	}

	// Body-parts shop: exclusive focus while active (matches DOS
	// RWS_BODY_PARTS_SHOP state which returns CPU_STOPPED until the
	// player exits).
	if (_bodyPartsShop.isActive()) {
		_bodyPartsShop.update();
		tickGameClock(nowMs);
		_engine->render();
		return _next;
	}

	// Advance the scroll widget's teletype reveal. Re-compose the sprite
	// when either the revealed-line count or the scroller state changed
	// so we don't thrash the sprite-chain every tick.
	if (_scrollerActive) {
		TextScroller::State st = _scroller.tick(nowMs);
		int lines = _scroller.visibleLines();
		if (st != _lastScrollerState || lines != _lastScrollerLines) {
			_lastScrollerState = st;
			_lastScrollerLines = lines;
			renderScrollerWidget();
		}
	}

	if (!_textVisible && !_introPending)
		advanceVmOnce();

	// Character walks while the player holds LMB. Only advance motion
	// when no blocking widget is up -- matches the DOS game freezing the
	// character while modal dialogs / bubbles are visible.
	if (!_textVisible && !_introPending && !_dialogOpen)
		updateCharacter(nowMs);

	tickGameClock(nowMs);

	_engine->render();
	return _next;
}

// Advance the active text widget on player input. Returns true if the
// input was consumed (the event handler should stop further processing).
bool RealWorldScene::advanceActiveText() {
	// Scroll widget: route through the scroller state machine.
	if (_scrollerActive && _activeWidget == kWidgetScroll) {
		TextScroller::State st = _scroller.state();
		if (st == TextScroller::kRunning) {
			// Player is impatient: skip the reveal. Fast-forward by
			// bumping the frame cap to zero so tick catches up.
			_scroller.tick(g_system->getMillis() + 10000);
			renderScrollerWidget();
			return true;
		}
		if (st == TextScroller::kWaitingForInput) {
			_scroller.acknowledge();
			renderScrollerWidget();
			return true;
		}
		if (st == TextScroller::kComplete) {
			_scrollerActive = false;
			clearTextWidgets();
			_textVisible = false;
			if (_introPending) {
				_introPending = false;
				startVmForCurrentLevel();
			} else {
				_engine->vm()->resume();
			}
			return true;
		}
		return false;
	}

	// Bubble widget: page through as before.
	if (_textVisible && _activeWidget == kWidgetBubble) {
		if (pageTextForward())
			return true;
		clearTextWidgets();
		_textVisible = false;
		if (_introPending) {
			_introPending = false;
			startVmForCurrentLevel();
		} else {
			_engine->vm()->resume();
		}
		return true;
	}
	return false;
}

// After the PAX panel closes, re-install the player sprite at its last
// position. The level PIC is still on kLayerLevelBg underneath, so we don't
// have to reload it -- only the character layer was cleared on open().
void RealWorldScene::restoreCharacterAfterPax() {
	renderCharacterFrame();
}

// Advance the in-game clock by one minute every real second, matching the
// DOS ui_panel_update (Reuromancer/NeuromancerWin64/scene_real_world.c:
// 601-620). Wraps at 60 -> hour, 24 -> day. Re-renders the status widget
// only when the visible mode's displayed value has changed.
void RealWorldScene::tickGameClock(uint32 nowMs) {
	if (_lastClockTickMs == 0) {
		_lastClockTickMs = nowMs;
		// First tick -- seed DSEG so scripts reading the clock on level
		// 1 see the starting 07:15, 11/16/58 values instead of zeros.
		mirrorClockToDseg();
		return;
	}
	if (nowMs - _lastClockTickMs < 1000)
		return;

	// Catch up however many seconds passed (e.g. after a debugger break).
	while (nowMs - _lastClockTickMs >= 1000) {
		_lastClockTickMs += 1000;
		if (++_timeM >= 60) {
			_timeM = 0;
			if (++_timeH >= 24) {
				_timeH = 0;
				_dateDay++;
			}
		}
	}
	mirrorClockToDseg();

	// Only redraw the status widget when it's showing a field that just
	// changed; saves a sprite-unpack per second.
	if (_statusMode == kStatusTime ||
	    (_statusMode == kStatusDate && _timeH == 0 && _timeM == 0)) {
		updateStatusWidget();
	}
}

// Mirror the current clock values into the DSEG slots the DOS binary
// uses (data.h:221-223). Scripts that query the time (e.g. to gate
// evening-only dialog) read from these offsets directly.
void RealWorldScene::mirrorClockToDseg() {
	NeuroVM *vm = _engine->vm();
	if (!vm) return;
	vm->writeVar16(0x4BC6, (uint16)_timeM);   // time_m
	vm->writeVar8 (0x4BC8, (uint8)_timeH);    // time_h
	vm->writeVar8 (0x4BC9, (uint8)_dateDay);  // date_day
}

// Mirror the player's current level and (charX, charY) into the DSEG
// slots BIH scripts read for geometric gates. DOS data.h:296-298:
//   level_n   = 0x4CA1 (uint16)
//   roompos_x = 0x4CA3 (uint16)
//   roompos_y = 0x4CA5 (uint16)
void RealWorldScene::mirrorRoomposToDseg() {
	NeuroVM *vm = _engine->vm();
	if (!vm) return;
	vm->writeVar16(0x4CA1, (uint16)_engine->currentLevel());
	vm->writeVar16(0x4CA3, (uint16)_charX);
	vm->writeVar16(0x4CA5, (uint16)_charY);
}

// Mirror the 128-byte items + 128-byte software + 16-byte skills
// arrays into DSEG. DOS data.h:190-197:
//   items[128]    = 0x41D7  (32 slots x 4 bytes -- code/version/flag/aux)
//   software[128] = 0x4257
//   skills[16]    = 0x42D7
// BIH scripts read these when gating on "has Case sold the sky-level
// cyberdeck?" etc.
void RealWorldScene::mirrorInventoryToDseg() {
	NeuroVM *vm = _engine->vm();
	if (!vm) return;
	for (int i = 0; i < 128; ++i) {
		vm->writeVar8((uint16)(0x41D7 + i), _invItems[i]);
		vm->writeVar8((uint16)(0x4257 + i), _invSoftware[i]);
	}
	for (int i = 0; i < 16; ++i)
		vm->writeVar8((uint16)(0x42D7 + i), _skills[i]);
}

void RealWorldScene::handleEvent(const Common::Event &event) {
	// PAX owns all input while open -- any unhandled events fall through
	// silently so they don't drive the level's character or UI.
	if (_pax.isActive()) {
		(void)_pax.handleEvent(event);
		if (!_pax.isActive())
			restoreCharacterAfterPax();
		return;
	}

	if (_inventory.isActive()) {
		(void)_inventory.handleEvent(event);
		return;
	}

	if (_skillsMenu.isActive()) {
		(void)_skillsMenu.handleEvent(event);
		return;
	}

	if (_rom.isActive()) {
		(void)_rom.handleEvent(event);
		return;
	}

	if (_bodyPartsShop.isActive()) {
		(void)_bodyPartsShop.handleEvent(event);
		return;
	}

	// Dialog picker takes absolute priority. Two sub-modes:
	//   - normal "cycle then accept": Enter accepts, other keys cycle.
	//   - text-input: the accepted reply was '@'-prefixed; printable
	//     keys append to _dialogTyped (16-char cap), backspace shrinks,
	//     Enter commits to _dialogInput and finalises the accept.
	if (_dialogOpen && event.type == Common::EVENT_KEYDOWN) {
		if (_dialogTextInput) {
			if (event.kbd.keycode == Common::KEYCODE_RETURN ||
			    event.kbd.keycode == Common::KEYCODE_KP_ENTER) {
				setDialogInput(_dialogTyped);
				_dialogTextInput = false;
				_dialogTyped.clear();
				acceptDialogReply();
			} else if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				_dialogTextInput = false;
				_dialogTyped.clear();
				_dialogOpen = false;
				clearTextWidgets();
			} else if (event.kbd.keycode == Common::KEYCODE_BACKSPACE) {
				if (!_dialogTyped.empty())
					_dialogTyped.deleteLastChar();
				renderDialogPicker();
			} else if (event.kbd.ascii >= 0x20 && event.kbd.ascii < 0x7F &&
			           _dialogTyped.size() < 16) {
				_dialogTyped += (char)event.kbd.ascii;
				renderDialogPicker();
			}
			return;
		}
		if (event.kbd.keycode == Common::KEYCODE_RETURN ||
		    event.kbd.keycode == Common::KEYCODE_KP_ENTER) {
			acceptDialogReply();
		} else if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			// Cancel: close picker, don't write game state.
			_dialogOpen = false;
			clearTextWidgets();
		} else if (event.kbd.keycode == Common::KEYCODE_UP ||
		           event.kbd.keycode == Common::KEYCODE_LEFT) {
			// Bidirectional cycle: step backwards through the reply
			// list. DOS advances only forward, but the reverse step
			// is a free quality-of-life improvement since the picker
			// state is entirely local.
			if (_dialogTotal > 0) {
				_dialogCurrentReply = (_dialogCurrentReply - 1 +
				                       (int)_dialogTotal) % (int)_dialogTotal;
				renderDialogPicker();
			}
		} else if (event.kbd.keycode == Common::KEYCODE_DOWN ||
		           event.kbd.keycode == Common::KEYCODE_RIGHT) {
			advanceDialogReply();
		} else {
			advanceDialogReply();
		}
		return;
	}
	if (_dialogOpen && event.type == Common::EVENT_LBUTTONDOWN) {
		// While typing into an '@'-prefix reply, mouse clicks are inert
		// so the player can type uninterrupted. Matches DOS which does
		// not cycle replies after text-input mode is armed.
		if (_dialogTextInput)
			return;
		advanceDialogReply();
		return;
	}
	if (_dialogOpen && event.type == Common::EVENT_RBUTTONDOWN) {
		if (_dialogTextInput)
			return;
		acceptDialogReply();
		return;
	}

	// UI buttons (both keyboard shortcuts and sidebar clicks) must fire
	// BEFORE the "dismiss active text" path. In the DOS game you can click
	// the talk / inventory / PAX icons while a bubble is on-screen and
	// that opens the corresponding sub-scene without dismissing the
	// bubble's underlying VM state.
	if (event.type == Common::EVENT_KEYDOWN) {
		int uiAction = keyToUiAction(event.kbd.ascii);
		if (uiAction >= 0) {
			onUiAction(uiAction);
			return;
		}
	}
	if (event.type == Common::EVENT_LBUTTONDOWN) {
		int uiAction = hitTestUiButton(event.mouse.x, event.mouse.y);
		if (uiAction >= 0) {
			onUiAction(uiAction);
			return;
		}
	}

	if (event.type == Common::EVENT_KEYDOWN) {
		// Dismiss / advance the blocking text widget. advanceActiveText
		// handles both the teletype scroller (scroll widget) and the
		// page-at-a-time bubble, and calls startVmForCurrentLevel() or
		// vm()->resume() itself on completion.
		if ((_introPending || _textVisible) && advanceActiveText())
			return;

		switch (event.kbd.keycode) {
		case Common::KEYCODE_ESCAPE:
			_next = kSceneMainMenu;
			break;
		case Common::KEYCODE_q:
			_engine->requestQuit();
			break;

		// Arrow keys drive the player character, same as the DOS
		// character_control_handle_kboard (character_control.c:199-232).
		// Level transitions happen only when the character walks into
		// an exit rectangle, not from a keyboard shortcut.
		case Common::KEYCODE_LEFT:  _charDir = kDirLeft;  _charMoving = true; _charLastStepMs = 0; _charFrame = 1; renderCharacterFrame(); break;
		case Common::KEYCODE_RIGHT: _charDir = kDirRight; _charMoving = true; _charLastStepMs = 0; _charFrame = 1; renderCharacterFrame(); break;
		case Common::KEYCODE_UP:    _charDir = kDirUp;    _charMoving = true; _charLastStepMs = 0; _charFrame = 1; renderCharacterFrame(); break;
		case Common::KEYCODE_DOWN:  _charDir = kDirDown;  _charMoving = true; _charLastStepMs = 0; _charFrame = 1; renderCharacterFrame(); break;

		default:
			break;
		}
		return;
	}

	if (event.type == Common::EVENT_KEYUP) {
		// Stop walking when the arrow key is released, matching DOS
		// which polls sfKeyboard_isKeyPressed each tick.
		switch (event.kbd.keycode) {
		case Common::KEYCODE_LEFT:
		case Common::KEYCODE_RIGHT:
		case Common::KEYCODE_UP:
		case Common::KEYCODE_DOWN:
			_charMoving = false;
			break;
		default:
			break;
		}
		return;
	}

	if (event.type == Common::EVENT_LBUTTONDOWN) {
		// A click also advances blocking text (unless it already landed
		// on a UI button, which returned early earlier).
		if ((_introPending || _textVisible) && advanceActiveText())
			return;
		// PIC area click: start walking in the clicked direction and
		// latch LMB-held so MOUSEMOVE keeps the walk direction updated.
		if (event.mouse.x >= 8 && event.mouse.x < 8 + 304 &&
		    event.mouse.y >= 8 && event.mouse.y < 8 + 112) {
			_lmbHeld = true;
			setCharDirFromCursor(event.mouse.x, event.mouse.y);
		}
		return;
	}

	if (event.type == Common::EVENT_LBUTTONUP) {
		_lmbHeld = false;
		_charMoving = false;
		return;
	}

	if (event.type == Common::EVENT_MOUSEMOVE && _lmbHeld &&
	    !_textVisible && !_introPending && !_dialogOpen) {
		// Re-evaluate direction while the button is held and the cursor
		// moves -- matches DOS character_control_handle_mouse reacting
		// to sfEvtMouseMoved.
		if (event.mouse.x >= 8 && event.mouse.x < 8 + 304 &&
		    event.mouse.y >= 8 && event.mouse.y < 8 + 112) {
			setCharDirFromCursor(event.mouse.x, event.mouse.y);
		} else {
			// Cursor left the PIC area while held -- treat as release.
			_charMoving = false;
		}
	}
}

// Placeholder click-to-move: split the PIC area into three zones
// (left-edge / middle / right-edge). Edge clicks page through levels
// exactly like the arrow keys, so the player gets visible feedback.
// Once ROOMPOS.BIH parsing lands we'll dispatch to level-specific
// exits instead; for now the raw roompos bytes for this level are
// also logged so we can see them against screenshots.
void RealWorldScene::handlePicClick(int x, int y) {
	int relX = x - 8;   // 0..303 inside the PIC
	int relY = y - 8;   // 0..111

	debugC(1, kDebugGeneral,
	       "RealWorldScene: PIC click at screen (%d, %d) -> relative (%d, %d)",
	       x, y, relX, relY);

	// Dump the level's ROOMPOS entry for future use.
	if (const byte *rp = _engine->roompos()) {
		uint8 lvl = _engine->currentLevel();
		uint32 off = (uint32)lvl * 20;
		if (off + 20 <= _engine->roomposSize()) {
			debugC(1, kDebugGeneral,
			       "  roompos[level=%u]: floor=%02X %02X %02X %02X"
			       "  exits=[%02X %02X %02X %02X] [%02X %02X %02X %02X]"
			       " [%02X %02X %02X %02X] [%02X %02X %02X %02X]",
			       lvl,
			       rp[off+0],  rp[off+1],  rp[off+2],  rp[off+3],
			       rp[off+4],  rp[off+5],  rp[off+6],  rp[off+7],
			       rp[off+8],  rp[off+9],  rp[off+10], rp[off+11],
			       rp[off+12], rp[off+13], rp[off+14], rp[off+15],
			       rp[off+16], rp[off+17], rp[off+18], rp[off+19]);
		}
	}

	// No edge-zone level hack here: the DOS game changes levels only
	// when the character walks onto an exit rectangle, not from
	// arbitrary clicks. The mouse-held walk path (setCharDirFromCursor)
	// above already handles motion toward the click target.
}

// Determine the walk direction from a cursor-vs-character comparison, per
// Reuromancer/NeuromancerWin64/character_control.c:148-188. The sprite's
// "width" hit-area is 2x the IMH header's packedWidth, but since we don't
// re-parse the header each frame we use a conservative 32 px fallback
// (character frames are consistent at ~16-32 px wide).
void RealWorldScene::setCharDirFromCursor(int cursorX, int cursorY) {
	const int spriteW = 32;
	const int spriteH = 40;

	CharDir prev = _charDir;
	if (cursorX < _charX) {
		_charDir = kDirLeft;
		_charMoving = true;
	} else if (cursorX > _charX + spriteW) {
		_charDir = kDirRight;
		_charMoving = true;
	} else if (cursorY < _charY) {
		_charDir = kDirUp;
		_charMoving = true;
	} else if (cursorY > _charY + spriteH) {
		_charDir = kDirDown;
		_charMoving = true;
	} else {
		_charMoving = false;
	}

	// DOS character_control_handle_input snaps to frame 1 and renders
	// immediately when the direction changes, so the player gets instant
	// visual feedback instead of waiting for the 100ms throttle.
	if (_charMoving && _charDir != prev) {
		_charFrame = 1;
		_charLastStepMs = 0;    // allow an immediate step on next update
		renderCharacterFrame();
	}
}

// Per-tick update: advance the character one step in its current direction
// if enough wall time has elapsed since the last step. Frames cycle 0..7
// per direction; horizontal motion moves 5px/step, vertical 2px/step --
// matching the DOS character_control_update() constants.
void RealWorldScene::updateCharacter(uint32 nowMs) {
	if (!_charMoving) {
		// Lock to idle pose (frame 0) if we've stopped moving.
		if (_charFrame != 0) {
			_charFrame = 0;
			renderCharacterFrame();
		}
		return;
	}

	if (nowMs - _charLastStepMs < kCharFrameCapMs)
		return;
	_charLastStepMs = nowMs;

	// Clamp to the level's walkable region, derived from ROOMPOS exit
	// rectangles in applyRoomposForCurrentLevel(). Matches DOS bounds
	// in character_control_update (scene_real_world.c:82-85).
	switch (_charDir) {
	case kDirLeft:  if (_charX - kCharSpeedHort >  _walkL) _charX -= kCharSpeedHort; break;
	case kDirRight: if (_charX + kCharSpeedHort <  _walkR) _charX += kCharSpeedHort; break;
	case kDirUp:    if (_charY - kCharSpeedVert >  _walkT) _charY -= kCharSpeedVert; break;
	case kDirDown:  if (_charY + kCharSpeedVert <  _walkB) _charY += kCharSpeedVert; break;
	default: break;
	}

	_charFrame = (_charFrame + 1) & 7;
	renderCharacterFrame();

	// Mirror player pixel position + current level into DSEG so BIH
	// scripts that geometry-gate on "is the player at the bar?" or
	// "did Case walk past the curtain?" see the live values.
	mirrorRoomposToDseg();

	debugC(3, kDebugGeneral,
	       "char: dir=%d pos=(%d, %d) frame=%d",
	       (int)_charDir, _charX, _charY, _charFrame);

	// After moving, see whether the character has crossed into an exit
	// zone. hitExitZone + checkForLevelExit together drive level changes
	// triggered by player movement (as opposed to VM opcode 0x10).
	checkForLevelExit();
}

void RealWorldScene::renderCharacterFrame() {
	const byte *sheet = _engine->spritesheet();
	if (!sheet)
		return;
	uint16 off = frameOffsetFor((int)_charDir, _charFrame);
	_engine->spriteChain()->addSprite(kLayerCharacter, _charX, _charY,
	                                  sheet + off,
	                                  /*opaque=*/false, /*transKey=*/0);
}

// Port of Reuromancer/NeuromancerWin64/scene_real_world.c:657-698
// (roompos_init). Reads the level's 20-byte record from ROOMPOS.BIH:
//   floor a8ae[0..3] at offset +0
//   exits[dir][0..3] at offset +4 + dir*4  for dir 0=N, 1=E, 2=S, 3=W
//
// Entry side is derived from `_lastExitDir`: entry = (_lastExitDir + 2) & 3,
// so exiting east on the old level spawns the character at the west edge
// of the new one. A fresh start (_lastExitDir == -1) defaults to south,
// matching DOS roompos_init's `transition = 2`.
//
// If ROOMPOS data isn't available (engine init failed) or the level index
// is out of range, keep the default conservative PIC-sized bounds.
void RealWorldScene::applyRoomposForCurrentLevel() {
	const byte *rp = _engine->roompos();
	uint8 lvl = _engine->currentLevel();
	if (!rp)
		return;
	uint32 off = (uint32)lvl * 20;
	if (off + 20 > _engine->roomposSize())
		return;

	const byte *floor = rp + off + 0;
	const byte *exitN = rp + off + 4;
	const byte *exitE = rp + off + 8;
	const byte *exitS = rp + off + 12;
	const byte *exitW = rp + off + 16;

	// Cache the four exit rectangles for later hit-tests.
	memcpy(_exitZones[0], exitN, 4);
	memcpy(_exitZones[1], exitE, 4);
	memcpy(_exitZones[2], exitS, 4);
	memcpy(_exitZones[3], exitW, 4);

	// Pick entry zone: opposite of the direction the player took to leave
	// the previous level. Default to south on a fresh start.
	int transition = 2;
	if (_lastExitDir >= 0 && _lastExitDir <= 3)
		transition = (_lastExitDir + 2) & 3;

	const byte *entry = _exitZones[transition];
	// If the chosen entry has no rectangle (dest 0xFF in the transition
	// table), DOS falls back to the floor-center default. That should
	// rarely fire in practice.
	if (lvl < 58 && kLevelTransitions[lvl][transition] == 0xFF) {
		_charX = (int)floor[1] + (int)floor[3];
		_charY = ((int)floor[0] + (int)floor[2]) / 2;
	} else {
		_charY = ((int)entry[3] >> 1) + (int)entry[1];
		_charX = ((int)entry[0] << 1) + (int)entry[2];
	}

	// Face AWAY from the entry (into the room). DOS: dir = (transition+2)&3.
	_charDir   = (CharDir)((transition + 2) & 3);
	_charFrame = 0;

	// Walkable bounds derive from the exit rectangles regardless of which
	// side we entered from -- they represent the room interior.
	_walkL = (int)exitW[0] << 1;
	_walkR = ((int)exitE[0] + (int)exitE[2]) << 1;
	_walkT = (int)exitN[1];
	_walkB = (int)exitS[1] + (int)exitS[3];

	debugC(1, kDebugLevel,
	       "RealWorldScene: roompos[%u] entry=%d start=(%d, %d) "
	       "bounds L/R/T/B=%d/%d/%d/%d floor={%d,%d,%d,%d}",
	       lvl, transition, _charX, _charY, _walkL, _walkR, _walkT, _walkB,
	       floor[0], floor[1], floor[2], floor[3]);
}

// Test whether the character is currently inside the exit rectangle for
// direction `dir` (0=N, 1=E, 2=S, 3=W) AND that exit leads somewhere on
// this level. Returns the destination level index, or -1. Mirrors DOS
// roompos_hit_exit_zone (scene_real_world.c:926).
int RealWorldScene::hitExitZone(int dir) const {
	if (dir < 0 || dir > 3) return -1;
	uint8 lvl = _engine->currentLevel();
	if (lvl >= 58) return -1;
	uint8 dest = kLevelTransitions[lvl][dir];
	if (dest == 0xFF) return -1;

	const uint8 *z = _exitZones[dir];
	int l = ((int)z[0]) << 1;            // pixel x
	int t = (int)z[1];
	int r = l + (((int)z[2]) << 1);      // right edge in pixels
	int b = t + (int)z[3];

	if (dir & 1) {
		// RIGHT (1) / LEFT (3): check vertical range + cross the edge.
		if (_charY < t || _charY > b) return -1;
		if (dir == 1) {
			if (_charX < l) return -1;   // not yet at the zone's left edge
		} else {
			if (_charX > r) return -1;   // not yet at the zone's right edge
		}
	} else {
		// UP (0) / DOWN (2): check horizontal range + cross the edge.
		if (_charX < l || _charX > r) return -1;
		if (dir == 2) {
			if (_charY < t) return -1;
		} else {
			if (_charY > b) return -1;
		}
	}
	return (int)dest;
}

void RealWorldScene::checkForLevelExit() {
	// Only react while the character is actively walking; avoids re-firing
	// when the player stands in an exit zone without new input.
	if (!_charMoving) return;
	if (_charDir < 0 || _charDir > 3) return;

	int dest = hitExitZone((int)_charDir);
	if (dest < 0) return;

	debugC(1, kDebugLevel,
	       "RealWorldScene: exit %d -> level %d (from %d)",
	       (int)_charDir, dest, (int)_engine->currentLevel());

	// Remember the direction we left by so the new level spawns us at the
	// opposite edge. Then stop moving so the new level's roompos_init
	// controls the initial pose.
	_lastExitDir = (int)_charDir;
	_charMoving  = false;
	_lmbHeld     = false;

	_engine->setCurrentLevel((uint8)dest);
	loadLevel();
}

int RealWorldScene::keyToUiAction(uint16 ascii) const {
	char lower = (char)tolower((byte)(ascii & 0x7F));
	for (uint i = 0; i < sizeof(kUiButtons) / sizeof(kUiButtons[0]); i++) {
		char label = (char)tolower((byte)kUiButtons[i].label);
		if (label == lower)
			return kUiButtons[i].code;
	}
	return -1;
}

int RealWorldScene::hitTestUiButton(int x, int y) const {
	for (uint i = 0; i < sizeof(kUiButtons) / sizeof(kUiButtons[0]); i++) {
		const UiButtonRect &b = kUiButtons[i];
		if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom)
			return b.code;
	}
	return -1;
}

void RealWorldScene::onUiAction(int code) {
	switch (code) {
	case kUiDate: _statusMode = kStatusDate; updateStatusWidget(); break;
	case kUiTime: _statusMode = kStatusTime; updateStatusWidget(); break;
	case kUiCash: _statusMode = kStatusCash; updateStatusWidget(); break;
	case kUiConstitution: _statusMode = kStatusCon; updateStatusWidget(); break;

	// The six navigation buttons open auxiliary scenes that aren't ported
	// yet. Fall back to a scroll-widget message so the interaction is
	// still visibly acknowledged.
	case kUiInventory: {
		// Dismiss any blocking text widget so the VM isn't left mid-yield
		// while the player edits the inventory -- same pattern as PAX.
		clearTextWidgets();
		_textVisible  = false;
		_introPending = false;
		_dialogOpen   = false;
		_inventory.open();
		break;
	}
	case kUiPax: {
		// Only open the PAX panel if the current level actually hosts
		// a terminal. DOS setup_ui_buttons hides the PAX icon otherwise
		// (scene_real_world.c:549-551). We don't gate the icon itself
		// yet, but the Operate path respects the capability list.
		if (!_engine->vm()->bih().hasCapability(/*pax=*/1)) {
			showText("No PAX terminal here.", kWidgetScroll);
			break;
		}
		// Dismiss any blocking text widget first so the VM isn't stuck
		// mid-yield while the player is in the PAX panel.
		clearTextWidgets();
		_textVisible  = false;
		_introPending = false;
		_dialogOpen   = false;
		_pax.open();
		break;
	}
	case kUiDialog:    openDialogPicker(); break;
	case kUiSkills: {
		// Same pattern as PAX / Inventory: dismiss blocking text widgets
		// so the VM isn't left mid-yield while the skills window is up.
		clearTextWidgets();
		_textVisible  = false;
		_introPending = false;
		_dialogOpen   = false;
		_skillsMenu.open();
		break;
	}
	case kUiChip: {
		// Same pattern as PAX / Inventory / Skills: dismiss blocking
		// text widgets so the VM isn't left mid-yield while the ROM
		// panel is up. Matches DOS rom_panel_open's control flow
		// (FUN_1000_881d at neuro.exe 1000:881d) which takes exclusive
		// focus until the player picks "X. Exit Rom Construct".
		clearTextWidgets();
		_textVisible  = false;
		_introPending = false;
		_dialogOpen   = false;
		_rom.open();
		break;
	}
	case kUiDisk:
		// "Disk Options" in DOS opened a Save / Load / Quit menu. The
		// ScummVM equivalent is the main-menu dialog (same one reachable
		// via F5): it hosts save, load, quit, and preferences in one
		// place. We open it here so the in-game Disk button offers the
		// expected functionality without us re-implementing a custom UI.
		clearTextWidgets();
		_textVisible  = false;
		_introPending = false;
		_dialogOpen   = false;
		_engine->openMainMenuDialog();
		break;

	default:
		debugC(1, kDebugGeneral, "RealWorldScene: unknown UI code 0x%02X", code);
		break;
	}
}

void RealWorldScene::gotoLevel(int delta) {
	int next = (int)_engine->currentLevel() + delta;
	for (int attempts = 0; attempts <= kMaxLevel; attempts++) {
		if (next < 0)         next = kMaxLevel;
		if (next > kMaxLevel) next = 0;

		_engine->setCurrentLevel((uint8)next);
		if (loadLevel()) {
			return;
		}
		next += (delta >= 0 ? +1 : -1);
	}
	warning("RealWorldScene: no playable level found in a full scan");
}

bool RealWorldScene::loadLevel() {
	int lvl = (int)_engine->currentLevel();
	ResourceManager *res = _engine->resources();

	Common::String picName = Common::String::format("R%d.PIC", lvl + 1);
	uint32 picSize = res->load(picName, _picSprite.data() + sizeof(ImhHeader));
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", picName.c_str(), picSize);
	if (picSize == 0)
		return false;

	_engine->spriteChain()->addSprite(kLayerLevelBg, 8, 8, _picSprite.data(), true);
	clearTextWidgets();
	_textVisible = false;
	_introPending = false;
	_dialogOpen = false;
	_dialogTextInput = false;
	_dialogTyped.clear();
	_dialogInput.clear();
	_dialogCurrentReply = 0;
	_pages.clear();
	_currentPage = 0;

	// Reset character controller to the level's default entry pose.
	// Direction = CD_UP matches the DOS roompos_init choice for the
	// default transition (= 2) -- see scene_real_world.c:696.
	_charDir        = kDirUp;
	_charFrame      = 0;
	_charMoving     = false;
	_lmbHeld        = false;
	_charLastStepMs = 0;

	applyRoomposForCurrentLevel();

	// New level = new player position + new level id. Push both to DSEG
	// right away so the level's init/update BIH script sees the right
	// pose on its first tick (before updateCharacter runs).
	mirrorRoomposToDseg();

	// Log the character sprite's header so we can see what dx/dy offsets
	// the artwork was authored with. Mismatched dy is the typical cause
	// of "floating" character sprites.
	if (const byte *sheet = _engine->spritesheet()) {
		const byte *frame = sheet + 0x0000; // CD_UP frame 0
		int16 dx = (int16)READ_LE_UINT16(frame + 0);
		int16 dy = (int16)READ_LE_UINT16(frame + 2);
		uint16 pw = READ_LE_UINT16(frame + 4);
		uint16 h  = READ_LE_UINT16(frame + 6);
		debugC(1, kDebugGeneral,
		       "RealWorldScene: char UP frame 0: dx=%d dy=%d w=%d h=%d",
		       dx, dy, pw * 2, h);
	}

	renderCharacterFrame();

	Common::String bihName = Common::String::format("R%d.BIH", lvl + 1);
	uint32 bihSize = res->load(bihName, _bihData.data());
	debugC(1, kDebugResource, "RealWorldScene: %s -> %u bytes", bihName.c_str(), bihSize);

	NeuroVM *vm = _engine->vm();
	vm->resetThreads();
	if (bihSize > 0) {
		vm->attach(_bihData.data(), bihSize);
		showLevelIntro();
	}
	updateStatusWidget();
	return true;
}

void RealWorldScene::showLevelIntro() {
	uint8 level = _engine->currentLevel();
	uint16 stringNum = _engine->isLevelVisited(level) ? 1 : 0;
	_engine->markLevelVisited(level);

	const char *s = _engine->vm()->bih().textString(stringNum);
	debugC(1, kDebugScript, "RealWorldScene: intro text[%u] = \"%s\"", stringNum, s);

	if (s && *s) {
		showText(s, kWidgetScroll);
		_introPending = true;
	} else {
		startVmForCurrentLevel();
	}
}

void RealWorldScene::startVmForCurrentLevel() {
	_engine->vm()->startDefaultThread(0, 0);
}

void RealWorldScene::clearTextWidgets() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerNeuroMenu);
	chain->clearSprite(kLayerDialogBubble);
}

// Paint a bordered frame into `pixels` matching build_text_frame from
// LibNeuroRoutines/drawing.c: black top/bottom rows; white interior with a
// single-pixel black column on each side.
static void buildBorderFrame(byte *pixels, int widthPx, int heightPx) {
	int packedW = widthPx / 2;
	for (int row = 0; row < heightPx; row++) {
		byte *line = pixels + row * packedW;
		if (row == 0 || row == heightPx - 1) {
			memset(line, 0x00, packedW);
		} else {
			memset(line, 0xFF, packedW);
			line[0]           = 0x0F;
			line[packedW - 1] = 0xF0;
		}
	}
}

// Split a word-wrapped (newline-separated) body into pages of `linesPerPage`
// lines each. Trailing empty lines are preserved only in the last page.
static Common::Array<Common::String> paginate(const Common::String &wrapped, int linesPerPage) {
	Common::Array<Common::String> pages;
	if (linesPerPage <= 0 || wrapped.empty()) {
		pages.push_back(wrapped);
		return pages;
	}

	Common::Array<Common::String> lines;
	Common::String cur;
	for (uint i = 0; i < wrapped.size(); i++) {
		char c = wrapped[i];
		if (c == '\n') {
			lines.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	if (!cur.empty())
		lines.push_back(cur);

	for (uint i = 0; i < lines.size(); i += linesPerPage) {
		Common::String page;
		for (uint j = i; j < i + linesPerPage && j < lines.size(); j++) {
			if (j > i) page += '\n';
			page += lines[j];
		}
		pages.push_back(page);
	}
	if (pages.empty())
		pages.push_back(Common::String());
	return pages;
}

void RealWorldScene::showText(const char *text, TextWidget widget) {
	clearTextWidgets();

	// Expand DOS control codes (0x01 name, 0x02 date, \r -> \n). DOS
	// splices these at text-fetch time -- we pass the live player name
	// and the formatted date so NPCs correctly address Case by name.
	const Common::String dateStr = dateString();
	Common::String expanded = expandText(text, _playerName.c_str(), dateStr.c_str());

	_activeWidget = widget;
	_textVisible  = true;

	if (widget == kWidgetScroll) {
		// Long-form text (level intro, VM op 0x02 output): feed the
		// scroller and let update() drive the teletype reveal. Frame cap
		// of ~100 ms matches the DOS text_scrolling_data_t default in
		// rw_state_pax.c:47 and gives a readable, non-frantic feel.
		_scroller.start(expanded.c_str(), kScrollColumns,
		                kScrollRows, /*frameCapMs=*/100);
		_scrollerActive    = true;
		_lastScrollerLines = -1;
		_lastScrollerState = TextScroller::kIdle;
		_pages.clear();
		_currentPage = 0;
		renderScrollerWidget();
		return;
	}

	// Bubble widget: pre-wrap + paginate as before. Bubble bodies are
	// short; the scroller would just add latency here.
	Common::String wrapped = wrapText(expanded.c_str(), kBubbleColumns);
	_pages        = paginate(wrapped, kBubbleRows);
	_currentPage  = 0;
	_scrollerActive = false;

	debugC(1, kDebugScript, "RealWorldScene: text -> %u page(s) in widget %d",
	       (uint)_pages.size(), (int)widget);

	renderCurrentPage();
}

// Compose the scroll widget sprite from the TextScroller's current
// revealed lines. Background stays at the sentinel transparent key (14)
// everywhere except inside glyph cells, so the NEURO.IMH chrome below
// the widget continues to show through exactly as in the DOS build.
void RealWorldScene::renderScrollerWidget() {
	byte *pixels = _scrollSprite.data() + sizeof(ImhHeader);
	memset(pixels, 0xEE, kScrollBytes);

	// Compose the visible lines into a single '\n'-joined string and
	// let drawString lay them out. visibleLines() never exceeds the
	// scroll widget's row capacity.
	Common::String body;
	for (int i = 0; i < _scroller.visibleLines(); i++) {
		if (i > 0) body += '\n';
		body += _scroller.lineAt(i);
	}
	if (!body.empty())
		drawString(body.c_str(), kScrollWidthPx, kScrollHeightPx, 0, 0, pixels);

	_engine->spriteChain()->addSprite(kLayerNeuroMenu, kScrollX, kScrollY,
	                                  _scrollSprite.data(),
	                                  /*opaque=*/false, /*transKey=*/14);
}

void RealWorldScene::renderCurrentPage() {
	if (_pages.empty() || _currentPage >= (int)_pages.size())
		return;
	const Common::String &page = _pages[_currentPage];

	// Count lines in the page (1 + number of '\n'). Used to size the
	// bubble widget so its frame fits the content exactly, matching the
	// DOS formula bottom = lines*8 + 19 (neuro_window_control.c:96).
	int lines = 1;
	for (uint i = 0; i < page.size(); i++)
		if (page[i] == '\n')
			lines++;

	// Bubble widget: size dynamically to the current page's line count.
	// height = lines*8 + 16 (inner text) + 2 border rows; clamp to the
	// pre-allocated buffer so we never overflow.
	int desiredH = lines * 8 + 16;
	if (desiredH > (int)kBubbleHeightPx) desiredH = (int)kBubbleHeightPx;
	int packedW = kBubbleWidthPx / 2;
	int usedBytes = packedW * desiredH;

	// Update the IMH header in the sprite buffer to reflect the tighter
	// size. width stays at packedW; height is the per-page height.
	WRITE_LE_UINT16(_bubbleSprite.data() + 6, (uint16)desiredH);

	byte *pixels = _bubbleSprite.data() + sizeof(ImhHeader);
	buildBorderFrame(pixels, kBubbleWidthPx, desiredH);
	(void)usedBytes;

	// drawString writes black ink on a white background (kFontPixels in
	// font.cpp encodes on-pixels as 0x00 and off-pixels as 0xFF), so we
	// can render straight on top of the white bubble interior without any
	// XOR gymnastics. The previous XOR pass inverted the colours and
	// turned the bubble body black -- matching what the user reported.
	drawString(page.c_str(), kBubbleWidthPx, desiredH,
	           kBubbleInnerLeft, kBubbleInnerTop, pixels);

	_engine->spriteChain()->addSprite(kLayerDialogBubble, kBubbleX, kBubbleY,
	                                  _bubbleSprite.data(), /*opaque=*/true);
}

// Advance to the next page. Returns true if there was more to show (caller
// should not dismiss the widget), false if the final page was already
// visible (caller should clear + resume VM).
bool RealWorldScene::pageTextForward() {
	if (_currentPage + 1 >= (int)_pages.size())
		return false;
	_currentPage++;
	renderCurrentPage();
	return true;
}

// ---- Dialog picker ------------------------------------------------------

// Open the dialog-picker widget for the current level. Reads the per-level
// dialog control (set by VM opcode 0x13) to know which reply strings in the
// BIH text section are this level's options. If no dialog is registered
// for the level, shows a placeholder message instead.
void RealWorldScene::openDialogPicker() {
	NeuroVM *vm = _engine->vm();
	uint8 level = _engine->currentLevel();
	_dialogFirst = vm->dialogFirstReply(level);
	_dialogTotal = vm->dialogTotalReplies(level);

	if (_dialogTotal == 0) {
		showText("No one to talk to here.", kWidgetScroll);
		return;
	}

	_dialogOpen = true;
	_dialogCurrentReply = 0;
	debugC(1, kDebugScript,
	       "RealWorldScene: dialog opened (level=%u first=%u total=%u)",
	       level, _dialogFirst, _dialogTotal);
	renderDialogPicker();
}

// Render the currently-highlighted reply into the bubble widget. In
// cycle mode this shows "[n/total] <reply text>"; in text-input mode
// it shows the prompt plus the typed buffer with a trailing cursor.
void RealWorldScene::renderDialogPicker() {
	const char *raw = _engine->vm()->bih().textString(_dialogFirst + _dialogCurrentReply);
	// DOS strips the '@' marker before storing in g_4bae but keeps it
	// visible in the BIH text table -- skip it here so the player sees
	// just the prompt text.
	if (raw && raw[0] == '@') ++raw;
	const Common::String dateStr = dateString();
	Common::String expanded = expandText(raw, _playerName.c_str(), dateStr.c_str());
	Common::String header;

	if (_dialogTextInput) {
		header = expanded;
		header += "\n< ";
		header += _dialogTyped;
		header += "_";
	} else {
		header = Common::String::format("[%d/%d] ",
		                                _dialogCurrentReply + 1,
		                                (int)_dialogTotal);
		header += expanded;
	}

	// Pack into a single-page bubble and render immediately.
	Common::String wrapped = wrapText(header.c_str(), kBubbleColumns);
	_pages.clear();
	_pages.push_back(wrapped);
	_currentPage  = 0;
	_activeWidget = kWidgetBubble;
	renderCurrentPage();
}

void RealWorldScene::advanceDialogReply() {
	_dialogCurrentReply = (_dialogCurrentReply + 1) % (int)_dialogTotal;
	debugC(2, kDebugScript, "RealWorldScene: dialog cycle -> %d", _dialogCurrentReply);
	renderDialogPicker();
}

// Accept the currently-highlighted reply. Writes the reply id into
// active_dialog_reply (var[16], DSEG 0x4BBE) and clears the wait flag at
// var[0], matching the DOS on_dialog_accept_reply end state. Then closes
// the picker and resumes the VM so it picks up the new state.
void RealWorldScene::acceptDialogReply() {
	// If the accepted reply starts with '@' and we're not already in
	// text-input mode, flip modes and wait for the player's keyboard
	// entry instead of closing the picker. Matches DOS rw_state_dialog.c
	// which gates on g_dlg_with_user_input (set when the reply carries
	// a leading '@' per utilities.c:180-185).
	if (!_dialogTextInput) {
		const char *raw =
			_engine->vm()->bih().textString(_dialogFirst + _dialogCurrentReply);
		if (raw && raw[0] == '@') {
			_dialogTextInput = true;
			_dialogTyped.clear();
			setDialogInput(Common::String()); // reset the previous answer
			renderDialogPicker();
			return;
		}
	}

	uint8 replyId = (uint8)(_dialogFirst + _dialogCurrentReply);
	NeuroVM *vm   = _engine->vm();
	vm->writeVar8(NeuroVM::kVarActiveDialogReply, replyId);
	vm->writeVar8(NeuroVM::kVarDialogWaitFlag, 0);

	debugC(1, kDebugScript,
	       "RealWorldScene: dialog accept reply %u -> var[%u]=%u input='%s'",
	       replyId, NeuroVM::kVarActiveDialogReply, replyId,
	       _dialogInput.c_str());

	_dialogOpen = false;
	_textVisible = false;
	clearTextWidgets();
	vm->resume();
}

// Renders the current status-panel string in a small 64x8 sprite placed at
// (96, 149). Follows the DOS formatting (scene_real_world.c:630-654):
//   UI_PM_CASH -> "$%7d"         (e.g. "$      0")
//   UI_PM_CON  -> "%8d"          (e.g. "    2000")
//   UI_PM_TIME -> "   %02d:%02d" (e.g. "   07:15")
//   UI_PM_DATE -> "mm/dd/yy"     (via build_date_string)
void RealWorldScene::updateStatusWidget() {
	byte *pixels = _statusSprite.data() + sizeof(ImhHeader);
	// Sentinel fill (colour 14 = 0xEE packed): drawString overwrites the
	// character cells we actually need with black-on-white, and the rest
	// of the buffer stays transparent via transBlitFrom(key=14). This
	// prevents the status box from drawing a solid black rectangle over
	// the Inventory window, which opens in the same screen region.
	memset(pixels, 0xEE, kStatusBytes);

	char buf[9] = { 0 };
	switch (_statusMode) {
	case kStatusCash:
		snprintf(buf, sizeof(buf), "$%7d", _cash);
		break;
	case kStatusCon:
		snprintf(buf, sizeof(buf), "%8d", _constitution);
		break;
	case kStatusTime:
		snprintf(buf, sizeof(buf), "   %02d:%02d", _timeH, _timeM);
		break;
	case kStatusDate:
		snprintf(buf, sizeof(buf), "%s", dateString().c_str());
		break;
	}

	drawString(buf, kStatusWidthPx, kStatusHeightPx, 0, 0, pixels);

	// Lives on kLayerStatusWidget (= 8) -- lower in the composition stack
	// than widget popups (Inventory, PAX), so the Inventory window at
	// (56, 128)..(231, 191), which overlaps the status area at (96, 149),
	// correctly draws on top of it. Non-text cells are the 0xEE sentinel
	// so they blit transparent via the transBlitFrom key path.
	_engine->spriteChain()->addSprite(kLayerStatusWidget, kStatusX, kStatusY,
	                                  _statusSprite.data(),
	                                  /*opaque=*/false, /*transKey=*/14);
}

void RealWorldScene::advanceVmOnce() {
	NeuroVM *vm = _engine->vm();
	NeuroVM::TickResult r = vm->tick();

	switch (r.action) {
	case NeuroVM::Action::kIdle:
		break;

	case NeuroVM::Action::kTextOutput: {
		const char *s = vm->bih().textString(r.stringNum);
		debugC(1, kDebugScript, "RealWorldScene: scroll text[%u] = \"%s\"", r.stringNum, s);
		showText(s, kWidgetScroll);
		break;
	}

	case NeuroVM::Action::kDialogReply: {
		const char *s = vm->bih().textString(r.stringNum);
		debugC(1, kDebugScript,
		       "RealWorldScene: bubble text[%u] at (%u, %u) = \"%s\"",
		       r.stringNum, r.var1, r.var2, s);
		showText(s, kWidgetBubble);
		break;
	}

	case NeuroVM::Action::kEnterDialog:
		debugC(1, kDebugScript, "RealWorldScene: VM op 0x17 enter dialog");
		openDialogPicker();
		break;

	case NeuroVM::Action::kChangeLevel:
		debugC(1, kDebugLevel, "RealWorldScene: VM requested level %u", r.levelN);
		// VM-driven level changes spawn from the south edge, matching DOS
		// which resets g_exit_point = -1 (transition defaults to 2) when
		// g_load_level_vm != 0 (scene_real_world.c:667-670).
		_lastExitDir = -1;
		_engine->setCurrentLevel(r.levelN);
		loadLevel();
		break;
	}
}

} // End of namespace Neuromancer
