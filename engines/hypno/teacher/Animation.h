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

#ifndef HYPNO_TEACHER_ANIMATION_H
#define HYPNO_TEACHER_ANIMATION_H

#include "common/str.h"
#include "graphics/surface.h"

namespace Hypno {

class HypnoSmackerDecoder;

class TeacherAnimation {
public:
	TeacherAnimation();
	~TeacherAnimation();
	void play(const Common::String &filename, uint32 flags);
	bool needsUpdate();
	void draw(Graphics::Surface *surface, int x, int y, bool transparent = false, bool scaleToSurface = false);
	void drawLastFrame(Graphics::Surface *surface, int x, int y, bool transparent = false);
	bool hasLastFrame() const { return _hasLastFrame; }
	bool endOfVideo();
	void close();
	void gotoFrame(int targetFrame);
	void decodeAndAdvance();  // Decode next frame and cache it (original: DoFrame + NextFrame)
	int getCurFrame() const;
	int getFrameCount() const;

	HypnoSmackerDecoder *_decoder;
	uint32 flags;

	Graphics::Surface _lastFrame;  // Cached last decoded frame
	bool _hasLastFrame;
};

} // End of namespace Hypno

#endif
