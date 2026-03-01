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

#ifndef HYPNO_TEACHER_DEMO_HANDLER14_H
#define HYPNO_TEACHER_DEMO_HANDLER14_H

#include "hypno/teacher/Handler.h"
#include "hypno/teacher/Sample.h"
#include "common/list.h"

namespace Hypno {

struct SoundItem {
	int soundId;
	Sample *sample;

	SoundItem(int id);
	~SoundItem();
};

class Handler14 : public Handler {
public:
	Handler14();
	~Handler14() override;

	void init(SC_Message *msg) override;
	int shutDown(SC_Message *msg) override;
	void update(int param1, int param2) override;
	int deinit(SC_Message *msg) override;

	SoundItem *findOrCreateSound(int soundId);

	Common::List<SoundItem *> _sounds;
	int _sourceAddress;
};

} // End of namespace Hypno

#endif
