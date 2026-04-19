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

namespace Neuromancer {

// Minimal port of scene_real_world.c:init(): loads NEURO.IMH as the UI
// background and R{N+1}.PIC as the level image, composited at (8, 8).
// Full scene (movement, animations, VM) will be added in later phases.
// Esc returns to the main menu.
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
	Common::Array<byte> _neuroImh;  // full decompressed NEURO.IMH
	Common::Array<byte> _picSprite; // synthesized IMH buffer (header + PIC pixels)

	SceneId _next;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_REAL_WORLD_H
