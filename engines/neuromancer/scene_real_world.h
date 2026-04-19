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
// Interaction flow:
//   - On level entry, load NEURO.IMH + R{N+1}.PIC + R{N+1}.BIH.
//   - Attach the BIH to the NeuroVM and start thread 0 at the default
//     program of the first bytecode table (the original engine's
//     opcode-0x00 equivalent).
//   - Each frame: tick the VM. If it yields kTextOutput or kDialogReply,
//     render the string on top of the PIC and wait for a keypress to
//     resume. Level-change requests (opcode 0x10) update the engine's
//     current level and re-init the scene.
//
// Navigation keys (Left/Right/PageUp/PageDown) remain active for rapid
// browsing of level backgrounds until the VM takes over.
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
	bool loadLevel(); // loads BIH, PIC, and queues the intro text
	void showLevelIndicator();
	void clearTextPanel();
	void renderTextPanel(const char *rawText);
	void gotoLevel(int delta);
	void advanceVmOnce();
	void showLevelIntro();
	void startVmForCurrentLevel();

	Common::Array<byte> _neuroImh;
	Common::Array<byte> _picSprite;       // [ImhHeader][PIC pixels]
	Common::Array<byte> _bihData;         // decompressed BIH bytes
	Common::Array<byte> _textPanelSprite; // IMH buffer for the text overlay
	Common::Array<byte> _indicatorSprite; // IMH buffer for "Level N" label

	SceneId _next;
	bool _textVisible;
	bool _introPending; // true while the pre-VM intro text is queued/displayed
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_SCENE_REAL_WORLD_H
