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

#ifndef INTERSPECTIVE_H
#define INTERSPECTIVE_H

#include "common/keyboard.h"
#include "common/language.h"
#include "common/ptr.h"
#include "common/random.h"
#include "common/str.h"
#include "engines/engine.h"

class MidiDriver;

namespace Common {
//
struct Event;
class EventManager;

} // namespace Common

namespace Graphics {
struct Surface;
}

namespace Interspective {

class Console;
class Interpreter;
class Resources;
class Graphics;
class Logic;
class Debugger;
class Sound;

class Engine : public ::Engine {
public:
	Engine(OSystem *syst);
	~Engine();

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	void delay(int millis) const;

	Logic *logic() { return _logic; }
	Resources *resources() { return _resources; }
	Graphics *graphics() { return _graphics; }
	Sound *sound() { return _sound; }
	Debugger *debugger() { return _debugger; }
	Common::EventManager *eventMan() { return _eventMan; }
	void captureStatusSaveThumbnail();
	const ::Graphics::Surface *statusSaveThumbnail() const { return _statusSaveThumbnail; }
	MidiDriver *musicDriver() const { return _musicDriver.get(); }
	uint16 dosSoundDeviceMask() const { return uint16(_dosMusicEnabled | _dosSfxEnabled); }
	uint8 dosMusicEnabled() const { return _dosMusicEnabled; }
	uint8 dosSfxEnabled() const { return _dosSfxEnabled; }

	// Resolved main/prog data filenames: the single-language release uses
	// iuc_main.dat / iuc_prog.dat; the multilingual CD uses the per-language
	// IUC_MAIN.<ext> / IUC_PROG.<ext> chosen from ConfMan "language".
	const Common::String &mainDatFilename() const { return _mainDatName; }
	const Common::String &progDatFilename() const { return _progDatName; }
	Common::Language language() const { return _language; }

	uint16 getRandom(uint16 max) const;

	friend class Interpreter;
	bool _copyProtection;

	static Engine &instance() { return *me; }
	bool escapePressed() const;

private:
	bool consumeEscapePress(const Common::Event &event) const;
	bool applyKeyboardCursorButton(const Common::Event &event);
	void updateKeyboardCursorDirection(Common::KeyCode keycode, bool pressed);
	void applyKeyboardCursorMovement();
	void initDosSoundConfig();
	void resolveDataFilenames();
	Common::Error loadStartupSaveSlot(int slot);

	Logic *_logic;
	Resources *_resources;
	Graphics *_graphics;
	Sound *_sound;
	Debugger *_debugger;
	::Graphics::Surface *_statusSaveThumbnail;
	Common::SharedPtr<MidiDriver> _musicDriver;

	Common::RandomSource *_rnd;
	mutable int _lastTicks, _startRoom;
	mutable bool _escapeHeld;
	uint8 _keyboardCursorDirs;
	uint8 _keyboardCursorDirsPrev;
	uint8 _keyboardCursorRepeat;
	uint8 _dosMusicEnabled;
	uint8 _dosSfxEnabled;
	Common::Language _language;
	Common::String _mainDatName;
	Common::String _progDatName;

	void handleEvents();
	static Engine *me;
};

#define Eng Engine::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_H
