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

#ifndef NEUROMANCER_NEUROMANCER_H
#define NEUROMANCER_NEUROMANCER_H

#include "common/array.h"
#include "common/random.h"
#include "common/serializer.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"

namespace Audio { class PCSpeaker; }

namespace Neuromancer {

class LevelHandlers;
class MusicPlayer;
class NeuroVM;
class ResourceManager;
class Scene;
class SpriteChain;

class NeuromancerEngine : public Engine {
public:
	NeuromancerEngine(OSystem *syst, const ADGameDescription *gd);
	~NeuromancerEngine() override;

	Common::Error run() override;

	// ---- Save / load plumbing (Engine overrides) ----
	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsLoadingDuringRuntime ||
		       f == kSupportsSavingDuringRuntime;
	}
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave) override;

	// Bidirectional state sync. Called by both saveGameStream and
	// loadGameStream through a Common::Serializer. On load it populates
	// the scene + VM state; on save it snapshots them.
	Common::Error syncGame(Common::Serializer &s);

	// Subsystem accessors used by scenes and level handlers.
	ResourceManager *resources() { return _resources; }
	NeuroVM *vm() { return _vm; }
	LevelHandlers *levelHandlers() { return _levelHandlers; }
	SpriteChain *spriteChain() { return _spriteChain; }
	MusicPlayer *music() { return _musicPlayer; }

	uint8 currentLevel() const { return _currentLevel; }
	void setCurrentLevel(uint8 level) { _currentLevel = level; }

	// Read-only accessor for the decompressed ROOMPOS.BIH payload, loaded
	// once at engine startup. Layout: 58 levels * 5 "roompos" entries of
	// 4 bytes each = 1160 bytes. Entry 0 is the floor bounds of a level,
	// entries 1-4 are exit zones (one per cardinal direction). Exact byte
	// semantics are still partially reverse-engineered -- callers inspect
	// the raw bytes for now.
	const byte *roompos() const {
		return _roomposData.empty() ? nullptr : _roomposData.data();
	}
	uint32 roomposSize() const { return _roomposData.size(); }

	// Read-only accessor for the decompressed SPRITES.IMH payload. Used
	// by the real-world scene to render the player character: frame
	// offsets for UP/RIGHT/DOWN/LEFT animations are hardcoded at the
	// scene level (see g_*_frames[] tables in character_control.c).
	const byte *spritesheet() const {
		return _spritesImh.empty() ? nullptr : _spritesImh.data();
	}

	// Persistent "has the player been here before" bit per level, used by
	// setup_intro() to choose long (first-visit) or short (revisit) text.
	// Mirrors g_visited_levels_bitstring in the DOS build.
	bool isLevelVisited(uint8 level) const;
	void markLevelVisited(uint8 level);

	// Scenes call this to exit the engine (e.g. the user selects "Quit").
	void requestQuit() { _exitGame = true; }

	// Access the scene that was temporarily pushed aside for a
	// cyberspace round-trip. Returns nullptr when no scene is paused.
	// Used by CyberspaceScene to read live real-world state (cash,
	// bank, player name) for the Role / Monitor readout.
	Scene *pausedScene() const { return _pausedScene; }

	// Active scene. BIH-script callbacks (bih_script.cpp) use this to
	// reach the RealWorldScene inventory / skills state when handling
	// neuro_cb commands (CB_CMD_HAS_ITEM, CB_CMD_REMOVE_ITEM, ...).
	Scene *scene() const { return _scene; }

	// The ADGameDescription record that detection selected. Engines
	// can inspect this for language / platform-specific behaviour.
	const ADGameDescription *gameDescription() const { return _gameDescription; }

	// Composite cursor on top of the current scene and push to the screen.
	// Scenes should call this instead of SpriteChain::renderToScreen() so
	// the cursor stays consistent across scene transitions.
	void render();

private:
	const ADGameDescription *_gameDescription;
	Common::RandomSource _rnd;

	ResourceManager *_resources;
	NeuroVM *_vm;
	LevelHandlers *_levelHandlers;
	SpriteChain *_spriteChain;
	Audio::PCSpeaker *_speaker;
	MusicPlayer *_musicPlayer;
	Scene *_scene;
	// Optional "paused" scene held across a temporary scene swap. Used by
	// the cyberspace round-trip: the real-world scene is kept alive (not
	// deinit'd) so its state -- cash, inventory, VM program counter,
	// level contents -- survives the jack-in. On return the engine
	// reinstates _pausedScene as _scene instead of creating a fresh one.
	Scene *_pausedScene;
	uint32 _lastMusicTickMs;

	Common::Array<byte> _cursorsImh;  // decompressed CURSORS.IMH
	Common::Array<byte> _roomposData; // decompressed ROOMPOS.BIH (1160 bytes)
	Common::Array<byte> _spritesImh;  // decompressed SPRITES.IMH (character frames)
	int _mouseX, _mouseY;

	uint8 _currentLevel;
	uint8 _visitedLevels[8]; // 64 bits; bit(level) = visited
	bool _exitGame;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_NEUROMANCER_H
