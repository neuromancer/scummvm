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
#include "common/str.h"

namespace Neuromancer {

// Real-world scene: UI frame (NEURO.IMH) + per-level PIC + neuro-VM tick.
//
// Two text widgets mirror the DOS engine (scene_real_world.c / window_animation.c):
//   - Scroll widget: small 136x56 box at (88, 134). Used by setup_intro()
//     and by VM opcode 0x02 (text output).
//   - Dialog bubble: overlay near the character position. Used by VM
//     opcodes 0x01 / 0x18 (NPC replies). For now a simple rectangle at a
//     fixed location -- full BUBBLES.IMH-based framing lands later.
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
	enum TextWidget {
		kWidgetScroll,  // lower 136x56 box -- intro + opcode 0x02
		kWidgetBubble   // overlay dialog bubble -- opcode 0x01 / 0x18
	};

	bool loadLevel();
	void showLevelIndicator();
	void gotoLevel(int delta);
	void advanceVmOnce();
	void showLevelIntro();
	void startVmForCurrentLevel();

	// Render `text` into the requested widget and mark that widget as the
	// active (blocking) overlay. Any previously active widget is cleared.
	void showText(const char *text, TextWidget widget);
	void clearTextWidgets();

	Common::Array<byte> _neuroImh;
	Common::Array<byte> _picSprite;
	Common::Array<byte> _bihData;
	Common::Array<byte> _scrollSprite;     // 136x56 scroll text box
	Common::Array<byte> _bubbleSprite;     // dialog bubble
	Common::Array<byte> _indicatorSprite;

	SceneId _next;
	bool _textVisible;
	bool _introPending;
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_REAL_WORLD_H
