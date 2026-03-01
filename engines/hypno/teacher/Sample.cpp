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

#include "hypno/teacher/Sample.h"
#include "hypno/teacher/teacher.h"
#include "audio/decoders/wave.h"
#include "audio/audiostream.h"
#include "common/file.h"

namespace Hypno {

Sample::Sample() : _filename("") {
}

Sample::~Sample() {
	Unload();
}

void Sample::Init(int volume) {
	// Not strictly needed with ScummVM API but kept for compatibility
}

void Sample::Fade(int volume, unsigned int duration) {
	// Simple volume set for now
	if (g_engine->_mixer->isSoundHandleActive(_handle)) {
		g_engine->_mixer->setChannelVolume(_handle, (volume * Audio::Mixer::kMaxChannelVolume) / 100);
	}
}

void Sample::Stop() {
	g_engine->_mixer->stopHandle(_handle);
}

int Sample::Play(int volume, int loopCount) {
	debugC(1, kHypnoDebugMedia, "Sample::Play: %s (vol=%d, loops=%d)", _filename.c_str(), volume, loopCount);
	Stop();
	
	Common::File *file = new Common::File();
	if (!file->open(((TeacherEngine *)g_engine)->convertPath(_filename))) {
		debugC(1, kHypnoDebugMedia, "Sample::Play: Failed to open %s", _filename.c_str());
		delete file;
		return 0;
	}

	Audio::RewindableAudioStream *stream = Audio::makeWAVStream(file, DisposeAfterUse::YES);
	if (!stream)
		return 0;

	// In Miles: loopCount 1 = play once, loopCount 0 = loop forever
	// In ScummVM: looping (bool), 0 = loop forever
	int loops = (loopCount == 0) ? 0 : loopCount;
	
	Audio::AudioStream *loopStream = new Audio::LoopingAudioStream(stream, loops);
	g_engine->_mixer->playStream(Audio::Mixer::kSFXSoundType, &_handle, loopStream, -1, (volume * Audio::Mixer::kMaxChannelVolume) / 100);
	
	return 1;
}

int Sample::Load(const Common::String &filename) {
	_filename = filename;
	return 1;
}

void Sample::Unload() {
	Stop();
	_filename = "";
}

} // End of namespace Hypno
