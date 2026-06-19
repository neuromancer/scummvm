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

#include "interspective/movie.h"

#include "common/file.h"
#include "common/system.h"
#include "common/util.h"

#include "interspective/debug.h"
#include "interspective/graphics.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"
#include "interspective/resources.h"

namespace Interspective {
//

Common::ScopedPtr<Movie> Movie::fromFile(const char *name) {
	if (!name || !*name) {
		debugC(1, kDebugLevelGraphics, "movie open skipped: empty filename");
		return Common::ScopedPtr<Movie>();
	}

	Common::ScopedPtr<Common::File> file(new Common::File);
	if (!file->open(name)) {
		debugC(1, kDebugLevelGraphics, "movie open failed: %s", name);
		return Common::ScopedPtr<Movie>();
	}

	Common::ScopedPtr<Common::ReadStream> stream(file.release());
	return Common::ScopedPtr<Movie>(new Movie(Common::move(stream)));
}

Movie::Movie(Common::ScopedPtr<Common::ReadStream> stream)
	: _delay(0), _iFrames(0), _f(Common::move(stream)) {}

Movie::~Movie() {}

void Movie::setFrameDelay(uint jiffies) {
	_delay = jiffies;
}

bool Movie::play() {
	if (!_f)
		return false;

	_s.create(320, 200);

	debugC(4, kDebugLevelGraphics, "creating movie");
	while (findKeyFrame()) {
		if (!loadKeyFrame())
			return false;

		setPalette();
		showFrame();

		while (_iFrames) {
			debugC(3, kDebugLevelGraphics, "got %d iframes", _iFrames);
			if (Eng.escapePressed()) {
				debugC(2, kDebugLevelGraphics, "movie interrupted by ESC");
				return false;
			}
			if (Eng.shouldQuit()) {
				debugC(2, kDebugLevelGraphics, "movie interrupted by quit");
				return false;
			}
			if (!loadIFrame())
				return false;
			showFrame();
			delay();
		}
	}
	return true;
}

bool Movie::findKeyFrame() {
	if (!_f || _f->eos() || _f->err())
		return false;

	(void)_f->readUint32LE(); // size of block, we don't want that
	_iFrames = _f->readUint16LE();
	return !_f->eos() && !_f->err();
}

bool Movie::loadKeyFrame() {
	(void)_f->readUint16LE(); // no idea what that is

	uint16 w;
	uint16 h;
	w = _f->readUint16LE();
	h = _f->readUint16LE();
	if (w != 320 || h != 200) {
		debugC(1, kDebugLevelGraphics, "movie key frame has invalid size %ux%u", w, h);
		return false;
	}

	Resources::decodeImage(_f.get(), reinterpret_cast<byte *>(_s.getPixels()), w * h);

	(void)_f->readByte();
	Resources::readPalette(_f.get(), _pal);
	if (_f->eos() || _f->err()) {
		debugC(1, kDebugLevelGraphics, "movie key frame read failed");
		return false;
	}

	_iFrames--;
	return true;
}

bool Movie::loadIFrame() {
	(void)_f->readUint16LE();

	byte skipB;
	byte skipW;
	skipB = _f->readByte();
	skipW = _f->readByte();
	if (_f->eos() || _f->err()) {
		debugC(1, kDebugLevelGraphics, "movie inter frame header read failed");
		return false;
	}

	assert(_s.pitch == 320);

	int left = 320 * 200;
	byte *dest = reinterpret_cast<byte *>(_s.getPixels());

	while (left) {
		byte b = _f->readByte();
		if (_f->eos() || _f->err()) {
			debugC(1, kDebugLevelGraphics, "movie inter frame read failed");
			return false;
		}

		if (b == skipB) {
			const uint16 skip = _f->readByte();
			if (_f->eos() || _f->err()) {
				debugC(1, kDebugLevelGraphics, "movie byte skip read failed");
				return false;
			}
			if (skip != 0) {
				if (skip > left) {
					debugC(1, kDebugLevelGraphics, "movie byte skip overruns frame (%u > %d)", skip, left);
					return false;
				}
				dest += skip;
				left -= skip;
				continue;
			}
		}

		if (b == skipW) {
			const uint16 skip = _f->readUint16LE();
			if (_f->eos() || _f->err()) {
				debugC(1, kDebugLevelGraphics, "movie word skip read failed");
				return false;
			}
			if (skip != 0) {
				if (skip > left) {
					debugC(1, kDebugLevelGraphics, "movie word skip overruns frame (%u > %d)", skip, left);
					return false;
				}
				dest += skip;
				left -= skip;
				continue;
			}
		}

		*dest++ = b;
		left--;
	}

	_iFrames--;
	return true;
}

void Movie::showFrame() {
	Engine::instance()._system->copyRectToScreen(
		reinterpret_cast<byte *>(_s.getPixels()), _s.pitch, 0, 0, _s.w, _s.h);

	Engine::instance()._system->updateScreen();
}

void Movie::setPalette() {
	Graf.setPalette(_pal, 0, 256);
}

void Movie::delay() {
	Engine::instance().delay(40 * _delay);
}

} // End of namespace Interspective
