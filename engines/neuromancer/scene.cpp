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

#include "neuromancer/scene.h"

#include "neuromancer/scene_cyberspace.h"
#include "neuromancer/scene_main_menu.h"
#include "neuromancer/scene_real_world.h"

#include "common/textconsole.h"

namespace Neuromancer {

Scene *createScene(SceneId id, NeuromancerEngine *engine) {
	switch (id) {
	case kSceneMainMenu:
		return new MainMenuScene(engine);
	case kSceneRealWorld:
		return new RealWorldScene(engine);
	case kSceneCyberspace:
		return new CyberspaceScene(engine);
	default:
		warning("Neuromancer: scene id %d not implemented", (int)id);
		return nullptr;
	}
}

} // End of namespace Neuromancer
