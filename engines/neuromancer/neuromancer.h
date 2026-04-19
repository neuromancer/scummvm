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

	// Subsystem accessors used by scenes and level handlers.
	ResourceManager *resources() { return _resources; }
	NeuroVM *vm() { return _vm; }
	LevelHandlers *levelHandlers() { return _levelHandlers; }
	SpriteChain *spriteChain() { return _spriteChain; }
	MusicPlayer *music() { return _musicPlayer; }

	uint8 currentLevel() const { return _currentLevel; }
	void setCurrentLevel(uint8 level) { _currentLevel = level; }

	// Persistent "has the player been here before" bit per level, used by
	// setup_intro() to choose long (first-visit) or short (revisit) text.
	// Mirrors g_visited_levels_bitstring in the DOS build.
	bool isLevelVisited(uint8 level) const;
	void markLevelVisited(uint8 level);

	// Scenes call this to exit the engine (e.g. the user selects "Quit").
	void requestQuit() { _exitGame = true; }

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
	uint32 _lastMusicTickMs;

	Common::Array<byte> _cursorsImh; // decompressed CURSORS.IMH
	int _mouseX, _mouseY;

	uint8 _currentLevel;
	uint8 _visitedLevels[8]; // 64 bits; bit(level) = visited
	bool _exitGame;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_NEUROMANCER_H
