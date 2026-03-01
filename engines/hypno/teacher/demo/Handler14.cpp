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

#include "hypno/teacher/demo/Handler14.h"
#include "hypno/teacher/teacher.h"
#include "common/debug.h"

namespace Hypno {

SoundItem::SoundItem(int id) : soundId(id) {
	Common::String filename = Common::String::format("audio/snd%04d.wav", soundId);
	sample = new Sample();
	sample->Load(filename);
}

SoundItem::~SoundItem() {
	delete sample;
}

Handler14::Handler14() : _sourceAddress(0) {
	handlerId = 14;
}

Handler14::~Handler14() {
	for (auto it : _sounds)
		delete it;
	_sounds.clear();
}

void Handler14::init(SC_Message *msg) {
	_sourceAddress = msg->sourceAddress;
}

int Handler14::shutDown(SC_Message *msg) {
	return 0;
}

void Handler14::update(int param1, int param2) {
}

int Handler14::deinit(SC_Message *msg) {
	if (msg->targetAddress != handlerId)
		return 0;

	switch (msg->priority) {
	case 3: {
		SoundItem *item = findOrCreateSound(msg->sourceAddress);
		if (item && item->sample)
			item->sample->Play(100, 1);
		break;
	}
	case 15: {
		for (auto it : _sounds)
			delete it;
		_sounds.clear();
		break;
	}
	case 18: {
		SoundItem *item = findOrCreateSound(msg->sourceAddress);
		if (item && item->sample)
			item->sample->Fade(msg->param1, 0);
		break;
	}
	case 20: {
		for (auto it = _sounds.begin(); it != _sounds.end(); ++it) {
			if ((*it)->soundId == msg->sourceAddress) {
				delete *it;
				_sounds.erase(it);
				break;
			}
		}
		break;
	}
	case 26: {
		SoundItem *item = findOrCreateSound(msg->sourceAddress);
		if (item && item->sample)
			item->sample->Play(100, 0); // Resume/Loop
		break;
	}
	default:
		debugC(1, kHypnoDebugScene, "Handler14: Unknown priority %d", msg->priority);
		break;
	}
	return 1;
}

SoundItem *Handler14::findOrCreateSound(int soundId) {
	for (auto it : _sounds) {
		if (it->soundId == soundId)
			return it;
	}
	SoundItem *newItem = new SoundItem(soundId);
	_sounds.push_back(newItem);
	return newItem;
}

} // End of namespace Hypno
