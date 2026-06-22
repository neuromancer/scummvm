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

#ifndef INTERSPECTIVE_MOVIE_H
#define INTERSPECTIVE_MOVIE_H

#include "common/ptr.h"
#include "common/path.h"
#include "common/stream.h"

#include "interspective/resources.h"

namespace Interspective {
//
class Movie {
public:
	virtual ~Movie();
	static Common::ScopedPtr<Movie> fromFile(const Common::Path &name);
	void setFrameDelay(uint delay);
	bool play();

protected:
	virtual void showFrame();
	virtual void setPalette();
	virtual void delay();

	Surface _s;
	byte _pal[0x300];

private:
	Movie();
	Movie(Common::ScopedPtr<Common::ReadStream> stream);
	Movie(const Movie &);
	Movie &operator=(const Movie &);

	bool findKeyFrame();
	bool loadKeyFrame();
	bool loadIFrame();

	int _delay;
	int _iFrames;
	Common::ScopedPtr<Common::ReadStream> _f;
};

} // End of namespace Interspective

#endif // INTERSPECTIVE_MOVIE_H
