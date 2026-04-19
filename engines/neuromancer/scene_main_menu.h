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

#ifndef NEUROMANCER_SCENE_MAIN_MENU_H
#define NEUROMANCER_SCENE_MAIN_MENU_H

#include "neuromancer/scene.h"

#include "common/array.h"

namespace Neuromancer {

class NeuroMenu;

// Minimal port of scene_main_menu.c: title background with a bordered
// "New/Load" menu. N transitions to the real-world scene, L is a no-op
// stub, Esc/Q exits the engine.
class MainMenuScene : public Scene {
public:
	explicit MainMenuScene(NeuromancerEngine *engine);
	~MainMenuScene() override;

	SceneId id() const override { return kSceneMainMenu; }

	void init() override;
	void deinit() override;
	SceneId update() override;
	void handleEvent(const Common::Event &event) override;

private:
	void onButton(int code);

	Common::Array<byte> _titleImh;
	NeuroMenu *_menu;
	SceneId _next;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_MAIN_MENU_H
