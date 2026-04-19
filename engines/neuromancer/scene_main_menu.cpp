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

#include "neuromancer/scene_main_menu.h"

#include "neuromancer/detection.h"
#include "neuromancer/gfx.h"
#include "neuromancer/menu.h"
#include "neuromancer/music_player.h"
#include "neuromancer/neuromancer.h"
#include "neuromancer/resource.h"

#include "common/debug.h"
#include "common/keyboard.h"
#include "common/textconsole.h"

namespace Neuromancer {

MainMenuScene::MainMenuScene(NeuromancerEngine *engine)
	: Scene(engine), _menu(nullptr), _next(kSceneMainMenu) {}

MainMenuScene::~MainMenuScene() {
	delete _menu;
}

// Mirror of scene_main_menu.c:init() -- TITLE.IMH as background, a single
// "New/Load" menu with two keyboard-hit items.
void MainMenuScene::init() {
	_titleImh.resize(64000);
	uint32 sz = _engine->resources()->load("TITLE.IMH", _titleImh.data());
	debugC(1, kDebugResource, "MainMenuScene: TITLE.IMH -> %u bytes", sz);

	// Reuromancer calls neuro_menu_create(6, 5, 20, 10, 1, NULL). cellL=5,
	// cellT=20, cellW=10, cellH=1. Resulting pixel rect: (32, 152, 128, 176).
	delete _menu;
	_menu = new NeuroMenu();
	_menu->create(5, 20, 10, 1);
	_menu->drawText("New/Load", 1, 0);
	_menu->addItem(1, 0, 3, 0, 'n');
	_menu->addItem(5, 0, 4, 1, 'l');

	SpriteChain *chain = _engine->spriteChain();
	chain->addSprite(kLayerBackground, 0, 0, _titleImh.data(), true);
	chain->addSprite(kLayerNeuroMenu, _menu->left(), _menu->top(), _menu->imhBuffer(), true);

	// Title theme -- track 1 is the menu/intro music in the DOS build.
	if (MusicPlayer *m = _engine->music())
		m->setTrack(1);
}

void MainMenuScene::deinit() {
	SpriteChain *chain = _engine->spriteChain();
	chain->clearSprite(kLayerBackground);
	chain->clearSprite(kLayerNeuroMenu);
}

SceneId MainMenuScene::update() {
	_engine->render();
	return _next;
}

void MainMenuScene::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_KEYDOWN) {
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE ||
		    event.kbd.keycode == Common::KEYCODE_q) {
			_engine->requestQuit();
			return;
		}
		const MenuButton *hit = _menu ? _menu->hitTestKey((char)event.kbd.ascii) : nullptr;
		if (hit) {
			onButton(hit->code);
		}
	} else if (event.type == Common::EVENT_LBUTTONDOWN) {
		const MenuButton *hit = _menu ? _menu->hitTestPoint(event.mouse.x, event.mouse.y) : nullptr;
		if (hit) {
			onButton(hit->code);
		}
	}
}

void MainMenuScene::onButton(int code) {
	switch (code) {
	case 0: // New
		debugC(1, kDebugGeneral, "MainMenuScene: New");
		_next = kSceneRealWorld;
		break;
	case 1: // Load
		debugC(1, kDebugGeneral, "MainMenuScene: Load (not implemented)");
		break;
	default:
		break;
	}
}

} // End of namespace Neuromancer
