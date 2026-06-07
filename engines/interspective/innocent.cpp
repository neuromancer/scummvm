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

#include "interspective/innocent.h"

#include "audio/mididrv.h"
#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/file.h"
#include "common/language.h"
#include "common/savefile.h"
#include "common/scummsys.h"
#include "common/serializer.h"
#include "common/system.h"
#include "engines/metaengine.h"
#include "engines/util.h"
#include "graphics/thumbnail.h"
#include "gui/message.h"

#include "interspective/debug.h"
#include "interspective/debugger.h"
#include "interspective/eventmanager.h"
#include "interspective/graphics.h"
#include "interspective/logic.h"
#include "interspective/musicparser.h"
#include "interspective/program.h"
#include "interspective/resources.h"
#include "interspective/sound.h"

using namespace Common;

namespace Interspective {

Engine *Engine::me;

namespace {

struct VerbButtonRegion {
	int16 left;
	int16 top;
	int16 right;
	int16 bottom;
	uint16 region;
};

static const VerbButtonRegion kVerbButtonRegions[] = {
	{65, 153, 79, 169, 3},
	{83, 153, 99, 169, 4},
	{101, 153, 117, 169, 5},
	{65, 172, 81, 188, 6},
	{83, 172, 99, 188, 7},
	{101, 172, 117, 188, 8}};

static bool applyVerbButtonClick(Logic *logic, const Common::Point &pos) {
	if (!logic || logic->noStep() || logic->cursorMode() == 0x20)
		return false;

	for (uint i = 0; i < ARRAYSIZE(kVerbButtonRegions); ++i) {
		const VerbButtonRegion &r = kVerbButtonRegions[i];
		if (pos.x < r.left || pos.x >= r.right || pos.y < r.top || pos.y >= r.bottom)
			continue;

		logic->setVerbModeFromHitRegion(r.region);
		logic->setHitTarget(r.region);
		return true;
	}

	return false;
}

static uint16 verbHitRegionForKey(const Common::Event &event) {
	switch (event.kbd.ascii) {
	case ' ':
		return 2;
	case 'h':
	case 'H':
		return 3;
	case 'u':
	case 'U':
		return 4;
	case 'm':
	case 'M':
		return 5;
	case 'l':
	case 'L':
		return 6;
	case 's':
	case 'S':
		return 7;
	case 't':
	case 'T':
		return 8;
	default:
		return 0xffff;
	}
}

static bool applyVerbHotkey(Logic *logic, const Common::Event &event) {
	if (!logic || event.kbdRepeat)
		return false;

	const uint16 hitRegion = verbHitRegionForKey(event);
	if (hitRegion == 0xffff)
		return false;
	if (hitRegion == 2) {
		logic->activateStatusButtonHotkey();
		return true;
	}
	return logic->setVerbModeFromHitRegion(hitRegion);
}

static uint8 keyboardCursorDirectionBit(Common::KeyCode keycode) {
	switch (keycode) {
	case Common::KEYCODE_UP:
	case Common::KEYCODE_KP8:
		return 0x01;
	case Common::KEYCODE_DOWN:
	case Common::KEYCODE_KP2:
		return 0x02;
	case Common::KEYCODE_LEFT:
	case Common::KEYCODE_KP4:
		return 0x04;
	case Common::KEYCODE_RIGHT:
	case Common::KEYCODE_KP6:
		return 0x08;
	case Common::KEYCODE_HOME:
	case Common::KEYCODE_KP7:
		return 0x01 | 0x04;
	case Common::KEYCODE_PAGEUP:
	case Common::KEYCODE_KP9:
		return 0x01 | 0x08;
	case Common::KEYCODE_END:
	case Common::KEYCODE_KP1:
		return 0x02 | 0x04;
	case Common::KEYCODE_PAGEDOWN:
	case Common::KEYCODE_KP3:
		return 0x02 | 0x08;
	default:
		return 0;
	}
}

static uint8 keyboardCursorButtonBit(Common::KeyCode keycode) {
	switch (keycode) {
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER:
	case Common::KEYCODE_KP5:
		return 0x10;
	case Common::KEYCODE_TAB:
	case Common::KEYCODE_KP_PLUS:
		return 0x20;
	default:
		return 0;
	}
}

struct MusicStateSyncResult {
	MusicStateSyncResult() : disableAllSound(false), disableSfx(false), stopSfx(false) {}
	bool disableAllSound;
	bool disableSfx;
	bool stopSfx;
};

static MusicStateSyncResult synchronizeMusicState(Common::Serializer &s, Resources *resources,
												  uint8 currentMusicMode, uint8 currentSfxMode) {
	MusicStateSyncResult result;
	uint8 active = Music.isActive() ? 1 : 0;
	uint16 currentTune = Music.currentTuneWord();
	uint16 scriptOffset = 0xffff;
	uint16 beat = Music.currentBeat();
	uint32 beatTicks = Music.currentBeatTicks();
	uint8 commandByte = Music.driverCommandByte();
	uint8 modeFlag = Music.driverModeFlag();
	uint8 savedMusicMode = currentMusicMode;
	uint8 savedSfxMode = currentSfxMode;

	byte *mainBase = resources ? resources->mainBase() : 0;
	const uint32 mainSize = (resources && resources->mainDat()) ? resources->mainDat()->dataSize() : 0;
	const byte *scriptBase = Music.currentScriptBase();
	if (scriptBase && mainBase) {
		const uintptr mainAddr = reinterpret_cast<uintptr>(mainBase);
		const uintptr scriptAddr = reinterpret_cast<uintptr>(scriptBase);
		if (scriptAddr >= mainAddr && scriptAddr < mainAddr + mainSize)
			scriptOffset = uint16(scriptAddr - mainAddr);
	}

	s.syncAsByte(active);
	s.syncAsUint16LE(currentTune);
	s.syncAsUint16LE(scriptOffset);
	s.syncAsUint16LE(beat);
	s.syncAsUint32LE(beatTicks);
	s.syncAsByte(commandByte);
	s.syncAsByte(modeFlag);
	s.syncAsByte(savedMusicMode);
	s.syncAsByte(savedSfxMode);

	if (s.isLoading()) {
		if (savedSfxMode != currentSfxMode)
			result.disableSfx = true;

		if (currentMusicMode == 0) {
			Music.stopMusic();
			return result;
		}

		// RestoreMusicState @ 1000:5cd4 enters through Op_f8, so any
		// currently playing sample is stopped before the music-driver image
		// is validated/restored.
		result.stopSfx = true;

		if (savedMusicMode != currentMusicMode) {
			Music.stopMusic();
			result.disableAllSound = true;
			return result;
		}

		const byte *script = 0;
		if (scriptOffset != 0xffff && mainBase && scriptOffset < mainSize)
			script = mainBase + scriptOffset;
		Music.restoreSavedState(script, currentTune, active, commandByte, modeFlag, beat, beatTicks);
	}
	return result;
}

} // End of anonymous namespace

Engine::Engine(OSystem *syst) : ::Engine(syst) {
	_resources = &Res;
	_resources->setEngine(this);
	_graphics = &Graphics::instance();
	_graphics->setEngine(this);
	_logic = &Logic::instance();
	_logic->setEngine(this);
	_sound = new Sound(this);
	_statusSaveThumbnail = nullptr;
	_copyProtection = false;
	me = this;
	_lastTicks = 0;
	_escapeHeld = false;
	_keyboardCursorDirs = 0;
	_keyboardCursorDirsPrev = 0;
	_keyboardCursorRepeat = 0;
	_dosMusicEnabled = 0;
	_dosSfxEnabled = 0;
	// Single-language defaults; resolveDataFilenames() (in run(), once the game
	// path is in SearchMan) overrides these for the multilingual CD.
	_language = Common::UNK_LANG;
	_mainDatName = "iuc_main.dat";
	_progDatName = "iuc_prog.dat";

	// Debug channels are now registered via the MetaEngine
	// (InterspectiveMetaEngineDetection::getDebugChannels) so that
	// --debugflags=name works. The legacy addDebugChannel() calls here
	// were silently ignored, leaving "Engine does not support debug
	// level 'script'" warnings whenever the flags were used.
	_rnd = new Common::RandomSource("interspective");
}

Engine::~Engine() {
	MusicParser::destroy();
	if (_statusSaveThumbnail) {
		_statusSaveThumbnail->free();
		delete _statusSaveThumbnail;
	}
	delete _sound;
	delete _rnd;
}

bool Engine::hasFeature(EngineFeature f) const {
	return f == kSupportsLoadingDuringRuntime || f == kSupportsSavingDuringRuntime;
}

bool Engine::canLoadGameStateCurrently(Common::U32String *msg) {
	return _logic != nullptr && _resources != nullptr && _resources->mainDat() != nullptr;
}

bool Engine::canSaveGameStateCurrently(Common::U32String *msg) {
	return _logic != nullptr && _resources != nullptr && _resources->mainDat() != nullptr;
}

void Engine::captureStatusSaveThumbnail() {
	if (_statusSaveThumbnail) {
		_statusSaveThumbnail->free();
		delete _statusSaveThumbnail;
		_statusSaveThumbnail = nullptr;
	}

	_statusSaveThumbnail = new ::Graphics::Surface;
	if (!::Graphics::createThumbnail(*_statusSaveThumbnail)) {
		_statusSaveThumbnail->free();
		delete _statusSaveThumbnail;
		_statusSaveThumbnail = nullptr;
	}
}

Common::Error Engine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	enum {
		kSaveVersion = 1
	};

	if (!stream || !_logic || !_resources || !_resources->mainDat())
		return Common::kWritingFailed;

	const bool statusSaveSnapshot = _logic->beginStatusSaveSnapshot();
	Common::Error result = Common::kNoError;
	Common::Serializer s(nullptr, stream);
	if (!s.matchBytes("IUCS", 4)) {
		result = Common::kWritingFailed;
	} else {
		s.syncVersion(kSaveVersion);

		MainDat *main = _resources->mainDat();
		uint16 mainSize = main->dataSize();
		s.syncAsUint16LE(mainSize);
		s.syncBytes(main->_data, mainSize);

		uint16 blockId = _logic->currentBlock();
		uint16 blockSize = _logic->blockProgram() ? _logic->blockProgram()->codeSize() : 0;
		s.syncAsUint16LE(blockId);
		s.syncAsUint16LE(blockSize);
		if (blockSize != 0)
			s.syncBytes(_logic->blockProgram()->base(), blockSize);

		_logic->synchronize(s);
		if (_sound)
			_sound->synchronize(s);
		synchronizeMusicState(s, _resources, _dosMusicEnabled, _dosSfxEnabled);
		result = s.err() ? Common::kWritingFailed : Common::kNoError;
	}
	if (statusSaveSnapshot)
		_logic->endStatusSaveSnapshot();
	return result;
}

Common::Error Engine::loadGameStream(Common::SeekableReadStream *stream) {
	enum {
		kSaveVersion = 1
	};

	if (!stream || !_logic || !_resources || !_resources->mainDat())
		return Common::kReadingFailed;

	Common::Serializer s(stream, nullptr);
	if (!s.matchBytes("IUCS", 4))
		return Common::kReadingFailed;
	if (!s.syncVersion(kSaveVersion))
		return Common::kReadingFailed;

	MainDat *main = _resources->mainDat();
	uint16 mainSize = 0;
	s.syncAsUint16LE(mainSize);
	if (mainSize != main->dataSize())
		return Common::kReadingFailed;
	s.syncBytes(main->_data, mainSize);

	uint16 blockId = 0xffff;
	uint16 blockSize = 0;
	s.syncAsUint16LE(blockId);
	s.syncAsUint16LE(blockSize);
	Common::Array<byte> blockData;
	if (blockSize != 0) {
		blockData.resize(blockSize);
		s.syncBytes(&blockData[0], blockSize);
		_logic->setLoadBlockImageOverride(blockId, blockData);
	}

	_logic->synchronize(s);
	if (_sound)
		_sound->synchronize(s);
	const MusicStateSyncResult musicResult =
		synchronizeMusicState(s, _resources, _dosMusicEnabled, _dosSfxEnabled);
	if (musicResult.stopSfx && _sound && _sound->isSfxPlaying())
		_sound->stopAll();
	// NOTE: DOS RestoreMusicState @ 1000:5cd4 disabled audio (and showed
	// "Unable to restore sound.") when a save's stored device byte no longer
	// matched the .ini-selected device. We no longer model that device byte —
	// audio is ScummVM-managed and always advertises MIDI music + digital SFX
	// (see initDosSoundConfig). So we deliberately ignore musicResult's
	// disableAllSound/disableSfx flags; honoring them would re-silence digital
	// SFX after loading an older save. The music tune itself is restored above.

	if (blockSize != 0) {
		// LoadGame_ReadFromDisk copies the saved resource-segment image
		// before RestoreGameStateFromBuffer/RestoreRoomFromBackup. Logic
		// consumes the override during doChangeRoom(); a second copy here
		// would erase room-restore side effects that DOS keeps.
		Program *block = _logic->blockProgram();
		if (!block || block->codeSize() != blockSize || _logic->currentBlock() != blockId)
			return Common::kReadingFailed;
	}

	// LoadGame_ReadFromDisk @ 1000:82fd clears the type-5 fullscreen
	// graphic slot (DS:0x677b) after a successful restore.
	_logic->setGraphicSlot(4, 0);

	return s.err() ? Common::kReadingFailed : Common::kNoError;
}

Common::Error Engine::loadStartupSaveSlot(int slot) {
	Common::InSaveFile *saveFile = _saveFileMan->openForLoading(getSaveStateName(slot));
	if (!saveFile)
		return Common::kReadingFailed;

	Common::Error result = loadGameStream(saveFile);
	if (result.getCode() == Common::kNoError) {
		ExtendedSavegameHeader header;
		if (MetaEngine::readSavegameHeader(saveFile, &header))
			setTotalPlayTime(header.playtime);
		_logic->restoreRoomFromBackup();
	}

	delete saveFile;
	return result;
}

Common::Error Engine::run() {
	initGraphics(320, 200);

	_copyProtection = ConfMan.getBool("copy_protection");
	_startRoom = ConfMan.getInt("boot_param");
	_debugger = &Debug;
	Debug.setEngine(this);
	initDosSoundConfig();
	resolveDataFilenames();
	_resources->init();
	_graphics->init();
	// music is initialized in the singleton
	_logic->init();

	_resources->loadActors();
	bool loadedStartupSave = false;
	const int startupSaveSlot = ConfMan.getInt("save_slot");
	if (startupSaveSlot >= 0) {
		const Common::Error loadError = loadStartupSaveSlot(startupSaveSlot);
		if (loadError.getCode() == Common::kNoError) {
			loadedStartupSave = true;
			debugC(1, kDebugLevelFlow, "loaded startup save slot %d", startupSaveSlot);
		} else {
			warning("Interspective: failed to load startup save slot %d (%s); starting a new game",
					startupSaveSlot, loadError.getDesc().c_str());
		}
	}
	if (!loadedStartupSave)
		_logic->initCode();
	_graphics->hideCursor();
	while (!shouldQuit()) {
		handleEvents();
		_logic->tick();
		_logic->callAnimations();
		_logic->runPostAnimationScripts();
		_graphics->paint();
		_graphics->syncCursorVisibility();
		//		_graphics->paintAnimations();
		const bool paused = _logic->paused();
		if (!paused)
			_graphics->updateScreen();
		else
			debugC(3, kDebugLevelGraphics, "skipping screen update while paused");
		if (paused)
			_logic->setPaused(false);
		_debugger->onFrame();
		delay(40);
	}

	return kNoError;
}

bool Engine::consumeEscapePress(const Common::Event &event) const {
	if (event.type == Common::EVENT_KEYUP && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
		_escapeHeld = false;
		return false;
	}
	if (event.type != Common::EVENT_KEYDOWN || event.kbd.keycode != Common::KEYCODE_ESCAPE)
		return false;
	if (event.kbdRepeat || _escapeHeld)
		return false;
	_escapeHeld = true;
	return true;
}

bool Engine::applyKeyboardCursorButton(const Common::Event &event) {
	if (event.kbdRepeat)
		return false;

	const uint8 bit = keyboardCursorButtonBit(event.kbd.keycode);
	if (bit == 0 || !_logic || !_graphics)
		return false;

	const Common::Point pos = _graphics->cursorPosition();
	if (bit == 0x10) {
		_logic->lockCursorAndButtons(pos, 1);
		if (!applyVerbButtonClick(_logic, pos))
			EventManager::instance().clicked(pos);
		return true;
	}

	_logic->lockCursorAndButtons(pos, 2);
	_logic->setSpeechSkipInput(true);
	_logic->cycleCursorModeByRightClick();
	return true;
}

void Engine::updateKeyboardCursorDirection(Common::KeyCode keycode, bool pressed) {
	const uint8 bit = keyboardCursorDirectionBit(keycode);
	if (bit == 0)
		return;

	if (pressed)
		_keyboardCursorDirs |= bit;
	else
		_keyboardCursorDirs &= ~bit;

	if (_keyboardCursorDirs != _keyboardCursorDirsPrev) {
		_keyboardCursorDirsPrev = _keyboardCursorDirs;
		_keyboardCursorRepeat = 0;
	}
}

void Engine::applyKeyboardCursorMovement() {
	// ProcessKeyboardCursor @ 1000:0a0a accelerates held keyboard
	// directions through 1, 2, 4, then 8-pixel steps. The DOS keyboard ISR
	// also maps Enter/KP5 and Tab/KP+ into left/right button bits; those
	// are consumed in applyKeyboardCursorButton().
	if (_keyboardCursorDirs == 0 || !_graphics)
		return;

	uint16 step = 8;
	if (_keyboardCursorRepeat < 0x38) {
		step = 4;
		if (_keyboardCursorRepeat <= 0x18) {
			step = 2;
			if (_keyboardCursorRepeat <= 0x08)
				step = 1;
		}
		++_keyboardCursorRepeat;
	}

	Common::Point pos = _graphics->cursorPosition();
	if (_keyboardCursorDirs & 0x01)
		pos.y -= step;
	if (_keyboardCursorDirs & 0x02)
		pos.y += step;
	if (_keyboardCursorDirs & 0x04)
		pos.x -= step;
	if (_keyboardCursorDirs & 0x08)
		pos.x += step;
	_graphics->setCursorPosition(pos);
}

void Engine::handleEvents() {
	Common::Event event;
	while (_eventMan->pollEvent(event)) {
		switch (event.type) {

		case Common::EVENT_KEYDOWN: {
			// DOS HandleSpecialKey @ 1000:b7d4. Alt+letter and Fn keys reach
			// the DOS code as 0x100|scancode; Ctrl combos as ASCII control
			// codes. These hidden hotkeys are live every tick in normal play,
			// on the status screen, and inside verb modals.
			const Common::KeyCode kc = event.kbd.keycode;
			const byte mods = event.kbd.flags & (Common::KBD_CTRL | Common::KBD_ALT);
			if (!event.kbdRepeat && kc == Common::KEYCODE_v && (mods & Common::KBD_ALT)) {
				// Hidden Alt-V version/credits screen (DOS @ 1000:b87b,
				// RunModalLoop with the credits text at DS:0x8889; ungated).
				GUI::MessageDialog dlg(
					Common::U32String(
						"INNOCENT - until caught\n\n"
						"English\n"
						"Interspective PC System\n\n"
						"(c) 1993 Divide By Zero"));
				dlg.runModal();
				break;
			}
			if (!event.kbdRepeat && kc == Common::KEYCODE_F1) {
				// F1 "Game paused" overlay (DOS @ 1000:b862, RunModalLoop with
				// the pause text at DS:0x884e, no menu items; ungated). The
				// blocking modal is the pause itself.
				GUI::MessageDialog dlg(
					Common::U32String("Game paused.\n\nPress a button or ENTER to continue."));
				dlg.runModal();
				break;
			}
			const bool menuKey = kc == Common::KEYCODE_F8 ||
								 ((mods & Common::KBD_CTRL) && (kc == Common::KEYCODE_c || kc == Common::KEYCODE_q)) ||
								 ((mods & Common::KBD_ALT) && (kc == Common::KEYCODE_q || kc == Common::KEYCODE_x));
			if (!event.kbdRepeat && menuKey) {
				// F8 / Ctrl-C / Ctrl-Q / Alt-Q / Alt-X open the Continue/
				// Restart/Exit system menu (DOS @ 1000:b82d), but only while
				// the fullscreen gate is clear. Routed to the same host menu
				// as Op_fc (openMainMenuDialog); a faithful in-engine 3-item
				// Continue/Restart/Exit modal honoring restart is a follow-up.
				if (!_logic || !_logic->fullscreenGateActive())
					openMainMenuDialog();
				break;
			}
			if (consumeEscapePress(event) && _logic) {
				// DOS: ESC (0x1b) is NOT a verb/status hotkey — CheckVerbHotkey
				// @1000:b9bc maps only Space→status (region 2) and H/U/M/L/S/T
				// to verbs; ESC maps to no region. ESC's ONLY job is to skip a
				// cutscene when an escape break point is armed (Op_3d /
				// HandleEscDuringScript @1000:2bd9). When nothing is armed DOS
				// does nothing — it must NOT open the status menu (that is
				// Space's role, handled via applyVerbHotkey below).
				_logic->requestSkipCutscene();
			} else if (applyKeyboardCursorButton(event)) {
				// Consumed as a DOS keyboard-button event.
			} else {
				applyVerbHotkey(_logic, event);
			}
			updateKeyboardCursorDirection(event.kbd.keycode, true);
			break;
		}

		case Common::EVENT_KEYUP:
			consumeEscapePress(event);
			if (_logic && keyboardCursorButtonBit(event.kbd.keycode) == 0x20)
				_logic->setSpeechSkipInput(false);
			updateKeyboardCursorDirection(event.kbd.keycode, false);
			if (event.kbd.keycode == Common::KEYCODE_BACKQUOTE)
				_debugger->attach();
			break;

		case Common::EVENT_MOUSEMOVE:
		case Common::EVENT_LBUTTONDOWN:
		case Common::EVENT_LBUTTONUP:
		case Common::EVENT_RBUTTONDOWN:
		case Common::EVENT_RBUTTONUP:
			_graphics->setCursorPosition(event.mouse);
			if (_logic && (event.type == Common::EVENT_RBUTTONDOWN || event.type == Common::EVENT_RBUTTONUP))
				_logic->setSpeechSkipInput(event.type == Common::EVENT_RBUTTONDOWN);
			if (_logic && event.type == Common::EVENT_RBUTTONDOWN) {
				_logic->lockCursorAndButtons(event.mouse, 2);
				_logic->cycleCursorModeByRightClick();
			} else if (_logic && (event.type == Common::EVENT_LBUTTONUP || event.type == Common::EVENT_RBUTTONUP)) {
				_logic->lockCursorAndButtons(event.mouse, 0);
			}
			if (event.type == Common::EVENT_LBUTTONDOWN) {
				if (_logic)
					_logic->lockCursorAndButtons(event.mouse, 1);
				if (applyVerbButtonClick(_logic, event.mouse))
					break;
				EventManager::instance().clicked(event.mouse);
			}
			break;

		default:
			break;
		}
	}
	applyKeyboardCursorMovement();
}

bool Engine::escapePressed() const {
	Common::Event event;
	while (_eventMan->pollEvent(event)) {
		switch (event.type) {

		case Common::EVENT_KEYDOWN:
			if (consumeEscapePress(event))
				return true;
			break;

		case Common::EVENT_KEYUP:
			consumeEscapePress(event);
			if (event.kbd.keycode == Common::KEYCODE_BACKQUOTE)
				_debugger->attach();
			break;
		default:
			break;
		}
	}
	return false;
}

void Engine::initDosSoundConfig() {
	// The DOS game read a single device switch from innocent.ini (/r Roland,
	// /a Adlib, /b SoundBlaster) and could drive only ONE audio device at a
	// time — so e.g. the bundled "/a+" config gave Adlib music with NO digital
	// sound effects. ScummVM owns the audio device selection plus the
	// music/sfx volume and mute controls through its own options, so we do NOT
	// model the .ini here.
	//
	// Instead we expose both audio paths at once: the MIDI music path
	// (dosMusic==1, the Adlib-config tune-bank mapping the music code expects)
	// and the digital SFX path (dosSfx==2, the SoundBlaster sample banks). This
	// lets MIDI music and digital sound effects play simultaneously — something
	// the original game could not do. These bytes only advertise "device
	// present" to the script logic and the SFX gate; actual audibility (volume
	// and muting) is handled entirely by ScummVM's mixer.
	_dosMusicEnabled = 1;
	_dosSfxEnabled = 2;

	debugC(1, kDebugLevelMusic, "DOS sound config: music=%u sfx=%u mask=0x%02x (ScummVM-managed; innocent.ini ignored)",
		   _dosMusicEnabled, _dosSfxEnabled, dosSoundDeviceMask());
}

// Maps a ScummVM language to the multilingual CD's IUC_MAIN/IUC_PROG file
// extension. From IUC.BAT: english->ENG, german->DTL (set_dtl), french->FRN,
// spanish->ESP, italian->ITL. English is the fallback for UNK/unknown.
static const char *langExtForLanguage(Common::Language lang) {
	switch (lang) {
	case Common::DE_DEU:
		return "DTL";
	case Common::FR_FRA:
		return "FRN";
	case Common::ES_ESP:
		return "ESP";
	case Common::IT_ITA:
		return "ITL";
	default:
		return "ENG";
	}
}

void Engine::resolveDataFilenames() {
	// The single-language release ships lowercase iuc_main.dat / iuc_prog.dat.
	// The multilingual CD ships per-language IUC_MAIN.<ext> / IUC_PROG.<ext>
	// (ENG, DTL=German, FRN, ESP, ITL); the data is detected as UNK_LANG so the
	// launcher offers a Language dropdown (detection.cpp), and the choice
	// arrives here through ConfMan "language". Map it to the extension, falling
	// back to English when the chosen language's files are absent, and finally
	// to the single-language .dat names. Called from run() once the game path
	// is in SearchMan, so Common::File::exists() is reliable.
	_language = Common::parseLanguage(ConfMan.get("language"));

	const Common::String mainMulti = Common::String::format("IUC_MAIN.%s", langExtForLanguage(_language));
	if (Common::File::exists(Common::Path(mainMulti))) {
		_mainDatName = mainMulti;
		_progDatName = Common::String::format("IUC_PROG.%s", langExtForLanguage(_language));
	} else if (Common::File::exists(Common::Path("IUC_MAIN.ENG"))) {
		// Multilingual set present but the chosen language's data is missing:
		// fall back to English rather than the (absent) single-language files.
		_mainDatName = "IUC_MAIN.ENG";
		_progDatName = "IUC_PROG.ENG";
	} else {
		_mainDatName = "iuc_main.dat";
		_progDatName = "iuc_prog.dat";
	}

	debugC(1, kDebugLevelFiles, "Interspective data files: main=%s prog=%s (language=%s)",
		   _mainDatName.c_str(), _progDatName.c_str(), Common::getLanguageCode(_language));
}

uint16 Engine::getRandom(uint16 max) const {
	return _rnd->getRandomNumber(max);
}

void Engine::delay(int millis) const {
	int target = _lastTicks + millis;
	while ((_lastTicks = _system->getMillis()) < target) {
		_system->delayMillis(target - _lastTicks);
	}
}

} // End of namespace Interspective
