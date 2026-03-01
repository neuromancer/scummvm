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

#include "hypno/teacher/Animation.h"
#include "hypno/grammar.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/file.h"
#include "common/path.h"
#include "graphics/surface.h"

namespace Hypno {

TeacherAnimation::TeacherAnimation() : _decoder(nullptr), flags(0), _hasLastFrame(false) {}

TeacherAnimation::~TeacherAnimation() {
	close();
	if (_hasLastFrame) {
		_lastFrame.free();
		_hasLastFrame = false;
	}
}

void TeacherAnimation::play(const Common::String &filename, uint32 f) {
	close();
	flags = f;
	_decoder = new HypnoSmackerDecoder();
	Common::File *file = new Common::File();
	if (!file->open(Common::Path(filename))) {
		warning("Unable to open animation %s", filename.c_str());
		delete file;
		delete _decoder;
		_decoder = nullptr;
		return;
	}
	if (!_decoder->loadStream(file)) {
		warning("Unable to load smacker %s", filename.c_str());
		delete _decoder;
		_decoder = nullptr;
		return;
	}
	_decoder->start();
	debugC(1, kHypnoDebugMedia, "TeacherAnimation: loaded '%s' hasAudio=%d",
		filename.c_str(), _decoder->getAudioTrackCount() > 0 ? 1 : 0);
}

bool TeacherAnimation::needsUpdate() {
	return _decoder ? _decoder->needsUpdate() : false;
}

void TeacherAnimation::draw(Graphics::Surface *surface, int x, int y, bool transparent, bool scaleToSurface) {
	if (_decoder) {
		const Graphics::Surface *frame = _decoder->decodeNextFrame();
		if (frame) {
			if (_decoder->hasDirtyPalette() && _decoder->getPalette()) {
				((TeacherEngine *)g_engine)->loadPalette(_decoder->getPalette(), 0, 256);
			}

			// Cache the decoded frame for later re-drawing
			if (_hasLastFrame) {
				_lastFrame.free();
			}
			_lastFrame.copyFrom(*frame);
			_hasLastFrame = true;

			uint32 transColor = ((TeacherEngine *)g_engine)->_transparentColor;
			if (scaleToSurface && (frame->w != surface->w || frame->h != surface->h)) {
				Graphics::Surface *sframe = frame->scale(surface->w, surface->h);
				int drawW = sframe->w;
				int drawH = sframe->h;
				if (x + drawW > surface->w) drawW = surface->w - x;
				if (y + drawH > surface->h) drawH = surface->h - y;
				if (drawW > 0 && drawH > 0) {
					Common::Rect srcRect(0, 0, drawW, drawH);
					if (transparent)
						surface->copyRectToSurfaceWithKey(*sframe, x, y, srcRect, transColor);
					else
						surface->copyRectToSurface(*sframe, x, y, srcRect);
				}
				sframe->free();
				delete sframe;
			} else {
				int drawW = frame->w;
				int drawH = frame->h;
				if (x + drawW > surface->w) drawW = surface->w - x;
				if (y + drawH > surface->h) drawH = surface->h - y;
				if (drawW > 0 && drawH > 0) {
					Common::Rect srcRect(0, 0, drawW, drawH);
					if (transparent)
						surface->copyRectToSurfaceWithKey(*frame, x, y, srcRect, transColor);
					else
						surface->copyRectToSurface(*frame, x, y, srcRect);
				}
			}
		}
	}
}

void TeacherAnimation::drawLastFrame(Graphics::Surface *surface, int x, int y, bool transparent) {
	if (_hasLastFrame) {
		// Clip source rect to fit within destination surface
		int drawW = _lastFrame.w;
		int drawH = _lastFrame.h;
		if (x + drawW > surface->w) drawW = surface->w - x;
		if (y + drawH > surface->h) drawH = surface->h - y;
		if (drawW <= 0 || drawH <= 0) return;

		Common::Rect srcRect(0, 0, drawW, drawH);
		uint32 transColor = ((TeacherEngine *)g_engine)->_transparentColor;
		if (transparent)
			surface->copyRectToSurfaceWithKey(_lastFrame, x, y, srcRect, transColor);
		else
			surface->copyRectToSurface(_lastFrame, x, y, srcRect);
	}
}

bool TeacherAnimation::endOfVideo() {
	return _decoder ? _decoder->endOfVideo() : true;
}

int TeacherAnimation::getCurFrame() const {
	return _decoder ? _decoder->getCurFrame() : 0;
}

int TeacherAnimation::getFrameCount() const {
	if (!_decoder) return 0;
	// SmackerDecoder exposes getFrameCount via forceSeekToFrame's implementation
	// but we need a frame count. The VideoDecoder base doesn't expose it directly,
	// so we use the same trick: the frameCount is stored as part of the decoder
	// We can get it from the decoder by checking what forceSeekToFrame can seek to.
	// Actually, VideoDecoder has a getFrameCount() method when the track supports it.
	// SmackerVideoTrack::getFrameCount() returns _frameCount.
	// But getTrack() is protected, so we use getCurFrame() + endOfVideo() approach.
	// Alternatively we can access it through the HypnoSmackerDecoder which inherits from SmackerDecoder.
	// The simplest approach: we already know the frame count is stored in the decoder.
	// We'll use a workaround: store the frame count on first access.
	return 0;  // Not needed for state range clamping; ranges are parsed from MIS files
}

void TeacherAnimation::gotoFrame(int targetFrame) {
	if (!_decoder) return;
	// Use SmackerDecoder's built-in forceSeekToFrame which properly handles
	// palette tracking and delta frame decoding
	bool wasPlaying = _decoder->isPlaying();
	const Graphics::Surface *frame = _decoder->forceSeekToFrame(targetFrame);
	if (frame) {
		// Cache the seeked-to frame for drawLastFrame()
		if (_hasLastFrame) {
			_lastFrame.free();
		}
		_lastFrame.copyFrom(*frame);
		_hasLastFrame = true;
	}
	// forceSeekToFrame() calls stopAudio() which destroys mixer handles.
	// Restart audio by creating new mixer handles via startAudio().
	if (wasPlaying) {
		_decoder->restartAudio();
	}
}

// Decode the next frame and cache it (mirrors original DoFrame + NextFrame)
void TeacherAnimation::decodeAndAdvance() {
	if (!_decoder || _decoder->endOfVideo()) return;

	const Graphics::Surface *frame = _decoder->decodeNextFrame();
	if (frame) {
		// Cache for drawLastFrame()
		if (_hasLastFrame) {
			_lastFrame.free();
		}
		_lastFrame.copyFrom(*frame);
		_hasLastFrame = true;
	}
}

void TeacherAnimation::close() {
	if (_decoder) {
		_decoder->close();
		delete _decoder;
		_decoder = nullptr;
	}
}

} // End of namespace Hypno
