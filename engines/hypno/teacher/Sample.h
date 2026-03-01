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

#ifndef HYPNO_TEACHER_SAMPLE_H
#define HYPNO_TEACHER_SAMPLE_H

#include "common/str.h"
#include "audio/mixer.h"

namespace Hypno {

class Sample {
public:
	Sample();
	~Sample();
	void Init(int volume);
	void Fade(int volume, unsigned int duration);
	void Stop();
	int Play(int volume, int loopCount);
	int Load(const Common::String &filename);
	void Unload();

	Audio::SoundHandle _handle;
	Common::String _filename;
};

} // End of namespace Hypno

#endif
