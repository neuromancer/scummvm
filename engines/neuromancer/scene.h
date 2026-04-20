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

#ifndef NEUROMANCER_SCENE_H
#define NEUROMANCER_SCENE_H

#include "common/events.h"
#include "common/scummsys.h"

namespace Neuromancer {

class NeuromancerEngine;

// Identifier for each scene in the original engine (scene_control.h).
enum SceneId {
	kSceneNone       = -1,
	kSceneMainMenu   = 0,
	kSceneRealWorld  = 1,
	kSceneCyberspace = 2,
	// Additional scenes (endgame, ...) will be added as ported.
};

// Base class mirroring `scene_control_t`: the engine runs exactly one scene
// at a time. Subclasses implement per-scene init / update / teardown and
// return the next scene id from `update()`. Control returns here once the
// scene asks to switch by returning a different id.
class Scene {
public:
	explicit Scene(NeuromancerEngine *engine) : _engine(engine) {}
	virtual ~Scene() = default;

	virtual SceneId id() const = 0;

	virtual void init()   = 0;
	virtual void deinit() = 0;

	// Called when the scene is reactivated after being paused by a
	// temporary side-scene (e.g. the real-world scene resuming after a
	// cyberspace round-trip). Default delegates to init(); scenes that
	// hold persistent state across the pause should override to
	// re-acquire sprite layers without resetting member state. Called
	// exactly once, between the pause scene's deinit and the next
	// update() call.
	virtual void resume() { init(); }

	// Called once per frame; return the scene id that should run next.
	// Returning the current id keeps the scene active.
	virtual SceneId update() = 0;

	// Called for every pending event.
	virtual void handleEvent(const Common::Event &event) = 0;

protected:
	NeuromancerEngine *_engine;
};

// Factory: constructs the concrete Scene subclass for a given id. Returns
// nullptr if the id is unknown (e.g. unimplemented scene).
Scene *createScene(SceneId id, NeuromancerEngine *engine);

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_H
