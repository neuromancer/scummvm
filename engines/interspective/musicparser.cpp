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

#include "interspective/musicparser.h"

#include "audio/mididrv.h"
#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/system.h"

#include "interspective/innocent.h"
#include "interspective/resources.h"
#include "interspective/util.h"

namespace Common {
DECLARE_SINGLETON(Interspective::MusicParser);
}

namespace Interspective {

static uint16 midiTuneIndexForScriptTune(uint16 tuneIdx) {
	MainDat *main = Res.mainDat();
	// The iuc_main tunes directory is ordered Adlib-first: physical tune
	// indices 1..count/2 are the Adlib tunes (iuc_a*.dat, type 1) and
	// count/2+1..count are the Roland tunes (iuc_r*.dat, type 4). Script tune
	// words use the low (Adlib-half) indices. IMPORTANT: this is NOT a simple
	// "match the bank to the DOS device byte" — the C++ plays tunes through
	// ScummVM's MIDI device, so which physical tune sounds correct depends on
	// that device, not on the DOS hardware. The +rolandBase remap is applied
	// in Adlib-config mode (dosMusicEnabled==1): this is the empirically tuned
	// behavior from commit "music sound correctly in adlib mode" (43f0391e4d8)
	// and is the configuration used by the SoundBlaster/Adlib default and by
	// the MIDI-music + digital-SFX coexistence path (which both run with
	// dosMusicEnabled==1). Do NOT flip this to ==4 without re-verifying actual
	// audio — doing so regresses music in those default/coexistence setups.
	if (!main || Engine::instance().dosMusicEnabled() != 1)
		return tuneIdx;

	const uint16 count = main->tunesCount();
	const uint16 rolandBase = count / 2;
	if (rolandBase == 0 || tuneIdx == 0 || tuneIdx > rolandBase || tuneIdx + rolandBase > count)
		return tuneIdx;

	return tuneIdx + rolandBase;
}

MusicParser::MusicParser() : MidiParser(), _sfxPendingBeat(-1), _musicType(MT_INVALID), _active(true),
							 _currentTuneWord(0), _currentScriptMainOffset(0xffff), _driverCommandByte(0), _driverModeFlag(0),
							 _sfxDataSize(0), _sfxBeatCount(0), _sfxCurrentBeat(-1), _sfxBeatTicks(0),
							 _sfxTime(0), _sfxLastTick(0), _sfxPsecPerTick(500000 * 0x19 / 120), _sfxTick(0),
							 _sfxTunePlaying(false), _time(0), _lastTick(0), _tick(0) {
	memset(_sfxData, 0, sizeof(_sfxData));
	clearSfxState();
	const uint32 devTypes = MDT_MIDI | MDT_ADLIB | MDT_PREFER_GM;
	MidiDriver::DeviceHandle dev = MidiDriver::detectDevice(devTypes);
	_musicType = MidiDriver::getMusicType(dev);
	const Common::String devId = MidiDriver::getDeviceString(dev, MidiDriver::kDeviceId);
	debugC(1, kDebugLevelMusic, "Interspective music init: detected device id='%s' musicType=%d",
		   devId.c_str(), int(_musicType));

	_midiDriver.reset(MidiDriver::createMidi(dev));
	if (!_midiDriver) {
		warning("Interspective music init: MidiDriver::createMidi returned NULL — music will be silent");
		return;
	}

	int openResult = _midiDriver->open();
	if (openResult != 0) {
		warning("Interspective music init: MidiDriver::open failed (%d) — music will be silent", openResult);
		return;
	}
	debugC(1, kDebugLevelMusic, "Interspective music init: MIDI driver opened OK; baseTempo=%u",
		   (uint)_midiDriver->getBaseTempo());

	// Report current mixer volume so the user can confirm the music isn't being
	// silenced upstream. Default in ScummVM is typically 192/256 (75%) but can
	// be 0 if the user has muted music in the Audio panel — that would silence
	// the OPL output entirely regardless of how many NoteOns we send.
	if (g_system && g_system->getMixer()) {
		const int musicVol = g_system->getMixer()->getVolumeForSoundType(Audio::Mixer::kMusicSoundType);
		const int sfxVol = g_system->getMixer()->getVolumeForSoundType(Audio::Mixer::kSFXSoundType);
		debugC(1, kDebugLevelMusic | kDebugLevelSound,
			   "Interspective music init: mixer volumes — music=%d/%d sfx=%d/%d (max=%d)",
			   musicVol, Audio::Mixer::kMaxMixerVolume,
			   sfxVol, Audio::Mixer::kMaxMixerVolume,
			   Audio::Mixer::kMaxMixerVolume);
		if (musicVol == 0)
			warning("Interspective music init: ★ MUSIC VOLUME IS 0 — that's why you hear nothing. "
					"Adjust in ScummVM's Audio settings (or `music_volume` in scummvm.ini).");
	} else {
		debugC(1, kDebugLevelMusic, "Interspective music init: g_system / mixer unavailable — can't read volume");
	}

	// MidiParser::setMidiDriver only sets _driver — doesn't touch the driver's
	// timer callback. We override it ourselves below to point at our own tick
	// (the base MidiParser::onTimer drives the standard event-stream model that
	// our parseNextEvent() override no-ops; we run our own beat/channel/note
	// state machine in MusicParser::tick instead).
	setMidiDriver(_midiDriver.get());
	setTimerRate(_midiDriver->getBaseTempo());
	_midiDriver->setTimerCallback(this, &MusicParser::timerCallback);
	debugC(1, kDebugLevelMusic, "Interspective music init: timer callback registered (timerRate=%u µs)",
		   (uint)_timerRate);
}

MusicParser::~MusicParser() {
	// Quiesce the MIDI timer callback BEFORE freeing _tune/_script: the timer
	// thread runs tick() -> _tune->tick(), so freeing _tune while the callback
	// is still registered can fire the timer on freed memory during shutdown.
	if (_midiDriver)
		_midiDriver->setTimerCallback(0, 0);
	// Timer is unregistered; take the lock to wait out any tick() already in
	// flight on the audio thread before we free _tune/_script.
	Common::StackLock lock(_mutex);
	stopSfxNotes();
	silence();
	unloadMusic();
	_tune.reset();
	_script.reset();
	if (_midiDriver) {
		_midiDriver->close();
		setMidiDriver(0);
		_midiDriver.reset();
	}
}

bool MusicParser::loadMusic(const byte *data, uint32 size) {
	return loadMusic(Common::Span<const byte>(data, size), 0xffff);
}

bool MusicParser::loadMusic(Common::Span<const byte> data, uint16 mainOffset) {
	Common::StackLock lock(_mutex);
	if (!_midiDriver) {
		warning("Interspective music: loadMusic skipped — no MIDI driver");
		return false;
	}

	// Idempotency guard: scripts can re-emit Op_f4 with the same data
	// pointer many times per second. Rebuilding while the MIDI timer
	// thread is ticking can free _tune mid-iteration, so skip if data
	// matches what's already loaded.
	if (_script && hasCurrentTune() &&
		((mainOffset != 0xffff && _currentScriptMainOffset == mainOffset) ||
		 (mainOffset == 0xffff && _script->matches(data))))
		return true;

	static int loadMusicCallCount = 0;
	loadMusicCallCount++;
	debugC(2, kDebugLevelMusic, "Interspective music: loadMusic called (#%d) data=%p size=%u",
		   loadMusicCallCount, (const void *)data.data(), (unsigned)data.size());

	unloadMusic();
	silence();
	_script.reset();
	_tune.reset();
	_script.reset(new MusicScript(data));
	_currentScriptMainOffset = mainOffset;

	// Reset our custom music clock so tunes always start from tick 0. Without
	// this, _tick keeps growing across loadMusic calls, and Note::reset (which
	// schedules notes against `_tick + 1`) would queue them against an
	// ever-growing target — but _lastTick is also stale, which throws the
	// pacing of the timer-gated tick loop. Restart cleanly.
	_tick = 0;
	_lastTick = 0;
	_time = 0;

	uint16 tuneIdx = _script->getTune();
	_currentTuneWord = tuneIdx;
	const uint16 midiTuneIdx = midiTuneIndexForScriptTune(tuneIdx);
	debugC(1, kDebugLevelMusic, "Interspective music: loadMusic tune index = %u (data tune %u)", tuneIdx, midiTuneIdx);
	_tune.reset(new Tune(midiTuneIdx));

	_numTracks = 1;
	_ppqn = 120;
	//	_clocksPerTick = 0x19;
	setTempo(500000 * 0x19);
	setTrack(0);
	debugC(2, kDebugLevelMusic, "Interspective music: loadMusic complete — _psecPerTick=%u _ppqn=%u",
		   (uint)_psecPerTick, (uint)_ppqn);
	return true;
}

void MusicParser::tick() {
	Common::StackLock lock(_mutex);
	if (_driverCommandByte == 1) {
		stopMusic();
		return;
	}

	_time += _timerRate;
	if (_sfxTunePlaying) {
		_sfxTime += _timerRate;
		if (!_sfxLastTick || _sfxTime >= _sfxLastTick + _sfxPsecPerTick) {
			_sfxLastTick = _sfxTime;
			tickSfxTune();
			if (_sfxTunePlaying)
				_sfxTick++;
		}
	}
	if (_lastTick && _time < _lastTick + _psecPerTick)
		return;

	_lastTick = _time;

	static bool reportedFirstTick = false;
	if (!reportedFirstTick) {
		reportedFirstTick = true;
		debugC(1, kDebugLevelMusic, "Interspective music: first MusicParser::tick fired (timerRate=%u psecPerTick=%u tune=%p)",
			   (uint)_timerRate, (uint)_psecPerTick, (const void *)_tune.get());
	}

	if (_tune && _tune->isPlaying()) {
		static bool reportedFirstTuneTick = false;
		if (!reportedFirstTuneTick) {
			reportedFirstTuneTick = true;
			debugC(1, kDebugLevelMusic, "Interspective music: first Tune::tick about to fire (Music.getTick=%u)",
				   (uint)_tick);
		}
		_tune->tick();
		if (!_tune->isPlaying()) {
			// DOS CheckMusicPlaying @ 1000:5c78 tests the resident driver's
			// current-tune word, not whether any translated MIDI note is
			// still active. Clear the modeled word as soon as the tune state
			// reaches its terminal beat so Op_f4/Op_f5 waits can resume.
			_currentTuneWord = 0;
			_driverCommandByte = 0;
			silence();
			unloadMusic();
		}
	}
	_tick++;
}

static byte notes[8][4];

enum {
	kMidiNoteOff = 0x80,
	kMidiNoteOn = 0x90,
	kMidiChannelControl = 0xb0,
	kMidiSetProgram = 0xc0
};

enum {
	kMidiCtrlExpression = 0xb,
	kMidiCtrlAllNotesOff = 0x7b
};

enum {
	kSetTempo = 0x81,
	kSetProgram = 0x82,
	kCmdSetBeat = 0x85,
	kSetExpression = 0x89,
	kCmdNoteOff = 0x8b,
	kCmdCallScript = 0x8c,
	kHangNote = 0xfe
};

static uint8 clampMidiControllerValue(uint8 value) {
	return MIN<uint8>(value, 127);
}

void MusicParser::silence() {
	debugC(2, kDebugLevelMusic, "turning off all notes");
	if (!_driver) {
		memset(notes, 0, sizeof(notes));
		return;
	}

	for (int channel = 2; channel < 10; channel++)
		for (int i = 0; i < 4; i++)
			if (notes[channel - 2][i])
				Music._driver->send(channel | kMidiNoteOff, notes[channel - 2][i], 0);

	memset(notes, 0, sizeof(notes));
}

void MusicParser::stopMusicNotesNotInSlots(const bool activeSlots[8][4]) {
	for (int channel = 0; channel < 8; channel++) {
		for (int slot = 0; slot < 4; slot++) {
			if (!notes[channel][slot] || activeSlots[channel][slot])
				continue;

			debugC(2, kDebugLevelMusic, "turning off orphaned note %d on channel %d slot %d",
				   (uint)notes[channel][slot], channel + 2, slot);
			if (_driver)
				_driver->send((channel + 2) | kMidiNoteOff, notes[channel][slot], 0);
			notes[channel][slot] = 0;
		}
	}
}

bool MusicParser::playSfxTune(Common::Span<const byte> data) {
	Common::StackLock lock(_mutex);
	enum {
		kTuneBeatCountOffset = 0x21,
		kTuneHeaderSize = 0x25
	};

	if (!_driver || !data || data.size() < kTuneHeaderSize || data.size() > kSfxTuneBufferSize)
		return false;

	stopSfxNotes();

	const uint16 beatCount = data.getUint16LEAt(kTuneBeatCountOffset);
	const uint32 channelBase = kTuneHeaderSize + uint32(beatCount) * 8;
	if (beatCount == 0 || channelBase >= data.size() || channelBase + 16 > data.size())
		return false;

	memcpy(_sfxData, data.data(), data.size());
	_sfxDataSize = data.size();
	_sfxBeatCount = beatCount;
	_sfxTime = 0;
	_sfxLastTick = 0;
	_sfxPsecPerTick = 500000 * 0x19 / 120;
	_sfxTick = 0;
	_sfxTunePlaying = true;
	if (!setSfxBeat(0)) {
		stopSfxNotes();
		return false;
	}

	debugC(1, kDebugLevelSound, "Interspective music: Roland SFX tune bytes=%u beats=%u",
		   (uint)data.size(), (uint)beatCount);
	return true;
}

void MusicParser::stopSfxNotes() {
	Common::StackLock lock(_mutex);
	if (_driver) {
		for (uint8 channel = 0; channel < ARRAYSIZE(_sfxChannels); ++channel) {
			for (uint8 noteIndex = 0; noteIndex < ARRAYSIZE(_sfxChannels[channel].notes); ++noteIndex) {
				SfxNote &note = _sfxChannels[channel].notes[noteIndex];
				if (note.playing && note.note != 0)
					_driver->send(_sfxChannels[channel].midiChannel | kMidiNoteOff, note.note, 0);
			}
		}
	}
	clearSfxState();
}

void MusicParser::clearSfxState() {
	memset(_sfxChannels, 0, sizeof(_sfxChannels));
	for (uint8 i = 0; i < ARRAYSIZE(_sfxChannels); ++i)
		_sfxChannels[i].midiChannel = i + 2;
	_sfxDataSize = 0;
	_sfxBeatCount = 0;
	_sfxCurrentBeat = -1;
	_sfxPendingBeat = -1;
	_sfxBeatTicks = 0;
	_sfxTime = 0;
	_sfxLastTick = 0;
	_sfxPsecPerTick = 500000 * 0x19 / 120;
	_sfxTick = 0;
	_sfxTunePlaying = false;
}

bool MusicParser::setSfxBeat(uint16 beat) {
	enum {
		kTuneHeaderSize = 0x25
	};

	if (!_sfxTunePlaying || beat >= _sfxBeatCount) {
		stopSfxNotes();
		return false;
	}

	uint8 heldNotes[8][4];
	bool heldPlaying[8][4];
	memset(heldNotes, 0, sizeof(heldNotes));
	memset(heldPlaying, 0, sizeof(heldPlaying));
	for (uint8 channel = 0; channel < ARRAYSIZE(_sfxChannels); ++channel) {
		for (uint8 noteIndex = 0; noteIndex < ARRAYSIZE(_sfxChannels[channel].notes); ++noteIndex) {
			const SfxNote &note = _sfxChannels[channel].notes[noteIndex];
			heldNotes[channel][noteIndex] = note.note;
			heldPlaying[channel][noteIndex] = note.playing;
		}
	}

	memset(_sfxChannels, 0, sizeof(_sfxChannels));
	for (uint8 i = 0; i < ARRAYSIZE(_sfxChannels); ++i) {
		_sfxChannels[i].midiChannel = i + 2;
		for (uint8 noteIndex = 0; noteIndex < ARRAYSIZE(_sfxChannels[i].notes); ++noteIndex) {
			_sfxChannels[i].notes[noteIndex].note = heldNotes[i][noteIndex];
			_sfxChannels[i].notes[noteIndex].playing = heldPlaying[i][noteIndex];
		}
	}

	const uint32 beatOffset = kTuneHeaderSize + uint32(beat) * 8;
	const uint32 channelBase = kTuneHeaderSize + uint32(_sfxBeatCount) * 8;
	if (beatOffset + 8 > _sfxDataSize || channelBase >= _sfxDataSize) {
		stopSfxNotes();
		return false;
	}

	const Common::Span<const byte> sfxData(_sfxData, _sfxDataSize);
	for (uint8 channel = 0; channel < ARRAYSIZE(_sfxChannels); ++channel) {
		const uint8 channelIndex = sfxData.getUint8At(beatOffset + channel);
		if (channelIndex == 0)
			continue;
		const uint32 channelDef = channelBase + uint32(channelIndex) * 16;
		if (channelDef + 16 > _sfxDataSize)
			continue;

		SfxChannel &state = _sfxChannels[channel];
		state.active = true;
		state.notInitialized = true;
		state.midiChannel = channel + 2;
		state.initPos = uint16(channelDef + 8);
		for (uint8 noteIndex = 0; noteIndex < ARRAYSIZE(state.notes); ++noteIndex) {
			const uint16 pos = sfxData.getUint16LEAt(channelDef + noteIndex * 2);
			if (pos != 0 && pos + 1 < _sfxDataSize) {
				state.notes[noteIndex].pos = pos;
				state.notes[noteIndex].tick = _sfxTick + 1;
				state.notes[noteIndex].active = true;
			}
		}
	}

	_sfxCurrentBeat = beat;
	_sfxBeatTicks = 0;
	return true;
}

void MusicParser::tickSfxTune() {
	if (!_sfxTunePlaying)
		return;

	// Apply any beat switch requested on the previous tick (in-stream
	// kCmdSetBeat or the 64-tick auto-advance). setSfxBeat() rebuilds
	// _sfxChannels, so it must run HERE — before the channel/note iteration —
	// never from inside it (the old code called it mid-loop, memset-ing the
	// array the loop was still walking, and started the new beat at beatTicks 1
	// instead of 0, a ~1/64 timing skew).
	if (_sfxPendingBeat >= 0) {
		const uint16 b = uint16(_sfxPendingBeat);
		_sfxPendingBeat = -1;
		if (!setSfxBeat(b))
			return;
	}

	const Common::Span<const byte> sfxData(_sfxData, _sfxDataSize);
	for (uint8 channel = 0; channel < ARRAYSIZE(_sfxChannels); ++channel) {
		SfxChannel &state = _sfxChannels[channel];
		if (!state.active)
			continue;

		if (state.notInitialized) {
			for (uint8 i = 0; i < 4; ++i) {
				const uint16 pos = uint16(state.initPos + i * 2);
				if (pos + 1 < _sfxDataSize)
					execSfxCommand(sfxData.getUint8At(pos), sfxData.getUint8At(pos + 1), state.midiChannel, 0);
			}
			state.notInitialized = false;
		}

		for (uint8 noteIndex = 0; noteIndex < ARRAYSIZE(state.notes); ++noteIndex)
			tickSfxNote(state.notes[noteIndex], state.midiChannel);
	}

	if (!_sfxTunePlaying)
		return;

	_sfxBeatTicks++;
	if (_sfxBeatTicks == 64) {
		// Defer to the next tick's top (see above). Don't clobber an explicit
		// in-stream kCmdSetBeat already queued this tick — it takes precedence.
		if (_sfxPendingBeat < 0)
			_sfxPendingBeat = _sfxCurrentBeat + 1;
		_sfxBeatTicks = 0;
	}
}

void MusicParser::tickSfxNote(SfxNote &note, uint8 channel) {
	if (!note.active || note.tick != _sfxTick)
		return;
	if (note.pos + 1 >= _sfxDataSize) {
		if (note.playing && note.note != 0)
			_driver->send(channel | kMidiNoteOff, note.note, 0);
		note = SfxNote();
		return;
	}

	const Common::Span<const byte> sfxData(_sfxData, _sfxDataSize);
	const uint8 command = sfxData.getUint8At(note.pos);
	const uint8 parameter = sfxData.getUint8At(note.pos + 1);
	note.pos += 2;

	if (command == kHangNote) {
		note.tick += parameter;
		return;
	}
	if (command == kCmdSetBeat) {
		// Defer: setSfxBeat() rebuilds _sfxChannels and we are iterating it.
		_sfxPendingBeat = parameter;
		return;
	}

	execSfxCommand(command, parameter, channel, &note);
	note.tick++;
}

void MusicParser::execSfxCommand(uint8 command, uint8 parameter, uint8 channel, SfxNote *note) {
	if (command == 0)
		return;

	switch (command) {
	case kSetProgram:
		_driver->send(channel | kMidiSetProgram, MidiDriver::_mt32ToGm[parameter], 0);
		break;
	case kSetExpression:
		if (_musicType == MT_ADLIB)
			_driver->send(channel | kMidiChannelControl, MidiDriver::MIDI_CONTROLLER_VOLUME, parameter / 2);
		else
			_driver->send(channel | kMidiChannelControl, kMidiCtrlExpression, clampMidiControllerValue(parameter));
		break;
	case kCmdNoteOff:
		if (note && note->playing && note->note != 0)
			_driver->send(channel | kMidiNoteOff, note->note, 0);
		if (note) {
			note->note = 0;
			note->playing = false;
		}
		break;
	case kCmdSetBeat:
		// Defer: never rebuild _sfxChannels from inside tickSfxTune's loop.
		_sfxPendingBeat = parameter;
		break;
	case kSetTempo:
		if (parameter != 0)
			_sfxPsecPerTick = MAX<uint32>(1, (500000u * parameter) / 120u);
		debugC(2, kDebugLevelSound, "Roland SFX tempo command %u psecPerTick=%u",
			   (uint)parameter, (uint)_sfxPsecPerTick);
		break;
	case kCmdCallScript:
		// In the SFX records command 0x8c is embedded in the note stream,
		// but there is no external music script attached to driver command
		// 0x0e. The resident driver consumes it as part of the effect stream;
		// keep it as a no-op here so the effect reaches its beat boundary.
		break;
	default:
		if (command < 0x80 && note) {
			if (note->playing && note->note != 0)
				_driver->send(channel | kMidiNoteOff, note->note, 0);
			_driver->send(channel | kMidiNoteOn, command, parameter);
			note->note = command;
			note->playing = true;
		} else {
			debugC(1, kDebugLevelSound, "Roland SFX unhandled command 0x%02x", (uint)command);
		}
		break;
	}
}

bool MusicParser::isPlaying() const {
	return _tune && _tune->isPlaying();
}

uint8 MusicParser::currentTempoParameter() const {
	if (!_tempo)
		return 0;

	const uint32 parameter = (_tempo + 250000) / 500000;
	return (uint8)MAX<uint32>(1, MIN<uint32>(255, parameter));
}

void MusicParser::stopMusic() {
	Common::StackLock lock(_mutex);
	_currentTuneWord = 0;
	_currentScriptMainOffset = 0xffff;
	_driverCommandByte = 0;
	silence();
	unloadMusic();
	if (_tune)
		_tune->stop();
}

void MusicParser::requestStopCurrent() {
	Common::StackLock lock(_mutex);
	_driverCommandByte = 1;
}

void MusicParser::restoreSavedState(Common::Span<const byte> script, uint16 scriptMainOffset,
									uint16 currentTuneWord, uint8 active,
									uint8 driverCommandByte, uint8 driverModeFlag, uint16 beat, uint32 beatTicks) {
	Common::StackLock lock(_mutex);
	const uint32 savedBeatTicks = beatTicks & 0x3f;
	const uint8 savedTempoParameter = (beatTicks >> 8) & 0xff;
	const uint16 savedScriptOffset = beatTicks >> 16;
	_active = active != 0;
	_driverCommandByte = driverCommandByte;
	_driverModeFlag = driverModeFlag;

	if (!script.data() || currentTuneWord == 0 || !_midiDriver) {
		stopMusic();
		_currentTuneWord = 0;
		return;
	}

	stopMusic();
	if (!loadMusic(script, scriptMainOffset)) {
		_currentTuneWord = 0;
		return;
	}

	_currentTuneWord = currentTuneWord;
	_currentScriptMainOffset = scriptMainOffset;
	_driverCommandByte = driverCommandByte;
	_driverModeFlag = driverModeFlag;
	if (_tune)
		_tune->restorePosition(beat, savedBeatTicks);
	if (_script && savedScriptOffset != 0)
		_script->setOffset(savedScriptOffset);
	if (savedTempoParameter != 0)
		setTempo(500000 * savedTempoParameter);
	debugC(1, kDebugLevelMusic,
		   "Interspective music: restored saved state tune=%u beat=%u beatTicks=%u scriptOffset=0x%04x tempo=%u psecPerTick=%u",
		   (uint)_currentTuneWord, (uint)beat, (uint)savedBeatTicks,
		   (uint)(_script ? _script->offset() : 0),
		   (uint)currentTempoParameter(), (uint)_psecPerTick);
}

bool MusicParser::restartCurrent() {
	Common::StackLock lock(_mutex);
	if (!_midiDriver || !_active || !_script || !hasCurrentTune())
		return false;

	// RestartCurrentMusic @ 1000:5d6a calls QueueAndStartTune, but the
	// underlying StartMusicTune @ 1000:5d9d returns without reloading when
	// the requested tune id already matches g_current_tune. ScummVM keeps
	// the active tune resident, so there is no audible work to do here.
	debugC(2, kDebugLevelMusic,
		   "Interspective music: block-change current tune reasserted (no reload)");
	return true;
}

void MusicParser::setMaxVolume(uint8 dosMusicMode) {
	Common::StackLock lock(_mutex);
	debugC(2, kDebugLevelMusic, "setting music channel volume to maximum");
	_driverCommandByte = 0xff;
	_driverModeFlag = (dosMusicMode == 4) ? 0 : 0x3f;
	if (!_driver)
		return;
	for (int channel = 2; channel < 10; ++channel)
		_driver->send(channel | kMidiChannelControl, MidiDriver::MIDI_CONTROLLER_VOLUME, 127);
}

MusicScript::MusicScript() : _offset(0) {}

MusicScript::MusicScript(Common::Span<const byte> data) : _code(data), _offset(2) {}

bool MusicScript::matches(Common::Span<const byte> data) const {
	return _code.data() == data.data() && _code.size() == data.size();
}

bool MusicScript::canRead(uint32 offset, uint32 count) const {
	if (!_code)
		return false;
	return offset <= _code.size() && count <= _code.size() - offset;
}

bool MusicScript::readByteAt(uint32 offset, byte &value) const {
	if (!canRead(offset, 1))
		return false;
	value = _code.getUint8At(offset);
	return true;
}

bool MusicScript::readUint16LEAt(uint32 offset, uint16 &value) const {
	if (!canRead(offset, 2))
		return false;
	value = _code.getUint16LEAt(offset);
	return true;
}

uint16 MusicScript::getTune() const {
	uint16 tune = 0;
	readUint16LEAt(0, tune);
	return tune;
}

enum {
	kControl94 = 0x94,
	kControl95 = 0x95,
	kJump = 0x96,
	kControl97 = 0x97,
	kSetBeat = 0x9a,
	kStop = 0x9b
};

void MusicScript::tick() {
	// Bounded: jumps and control opcodes continue the loop; beat/stop opcodes
	// return. A cyclic/self-referential jump (in malformed data) would
	// otherwise spin forever on the MIDI timer thread. Cap the chain and bail
	// to stopMusic.
	for (int guard = 0; guard < 256; ++guard) {
		byte opcode = 0;
		if (!readByteAt(_offset, opcode)) {
			warning("Interspective music: truncated script at offset 0x%x — stopping music",
					(uint)_offset);
			Music.stopMusic();
			return;
		}

		switch (opcode) {

		case kJump: {
			uint16 target = 0;
			if (!readUint16LEAt(_offset + 2, target)) {
				warning("Interspective music: truncated jump at offset 0x%x — stopping music",
						(uint)_offset);
				Music.stopMusic();
				return;
			}
			debugC(2, kDebugLevelMusic, "will jump to music script at 0x%x", target);
			if (target == _offset) {
				warning("Interspective music: script kJump to self at offset 0x%x — stopping music",
						(uint)_offset);
				Music.stopMusic();
				return;
			}
			_offset = target;
			break;
		}

		case kControl94:
		case kControl95: {
			if (!canRead(_offset, 6)) {
				warning("Interspective music: truncated control 0x%02x at offset 0x%x — stopping music",
						(uint)opcode, (uint)_offset);
				Music.stopMusic();
				return;
			}
			const Common::Span<const byte> control(_code.subspan(_offset, 6));
			// Op_f4 @ 1000:57aa only seeds the resident music driver's script
			// pointer and tune word; these control opcodes are consumed by that
			// driver. They do not emit a beat directly, so continue to the next
			// script command instead of treating valid shipped data as fatal.
			debugC(2, kDebugLevelMusic,
				   "will consume music script control 0x%02x at 0x%x args=%02x %02x %02x %02x %02x",
				   (uint)opcode, (uint)_offset,
				   (uint)control.getUint8At(1), (uint)control.getUint8At(2),
				   (uint)control.getUint8At(3), (uint)control.getUint8At(4),
				   (uint)control.getUint8At(5));
			_offset += 6;
			break;
		}

		case kControl97: {
			if (!canRead(_offset, 4)) {
				warning("Interspective music: truncated control 0x97 at offset 0x%x — stopping music",
						(uint)_offset);
				Music.stopMusic();
				return;
			}
			const Common::Span<const byte> control(_code.subspan(_offset, 4));
			debugC(2, kDebugLevelMusic,
				   "will consume music script control 0x97 at 0x%x args=%02x %02x %02x",
				   (uint)_offset, (uint)control.getUint8At(1),
				   (uint)control.getUint8At(2), (uint)control.getUint8At(3));
			_offset += 4;
			break;
		}

		case kSetBeat: {
			byte beat = 0;
			if (!readByteAt(_offset + 1, beat)) {
				warning("Interspective music: truncated set-beat at offset 0x%x — stopping music",
						(uint)_offset);
				Music.stopMusic();
				return;
			}
			debugC(2, kDebugLevelMusic, "will set beat to %d", beat);
			Music.setBeat(beat);
			_offset += 2;
			return;
		}

		case kStop:
			debugC(2, kDebugLevelMusic, "will stop playing");
			Music.stopMusic();
			return;

		default: {
			// Unhandled music opcodes show up in real game scripts.
			// Stopping music is a safer fallback than `error()` which
			// kills the engine — the user loses music for that scene
			// rather than the whole session.
			static bool reportedOnce = false;
			if (!reportedOnce) {
				reportedOnce = true;
				warning("Interspective music: unhandled script opcode 0x%02x at offset 0x%x — stopping music",
						(uint)opcode, (uint)_offset);
			}
			Music.stopMusic();
			return;
		}
		}
	}

	// Fell through 256 jumps without a terminating opcode — malformed/cyclic
	// script. Stop rather than risk spinning the timer thread.
	warning("Interspective music: script exceeded 256 jumps at offset 0x%x — stopping music",
			(uint)_offset);
	Music.stopMusic();
}

Tune::Tune() : _currentBeat(-1) {}

enum {
	kTuneBeatCountOffset = 0x21,
	kTuneHeaderSize = 0x25
};

Tune::Tune(uint16 index) {
	memset(_data, 0, sizeof(_data));
	Res.loadTune(index, _data);

	const Common::Span<const byte> tuneData(_data, sizeof(_data));
	uint16 nbeats = tuneData.getUint16LEAt(kTuneBeatCountOffset);
	// Sanity: header + nbeats*8 (beat array) must fit in the buffer with room for at least one
	// 16-byte channel entry. Otherwise treat as empty and skip — happens if loadTune got a bogus
	// index and the 32 KB buffer is full of zeros / garbage.
	const uint maxBeats = (sizeof(_data) - kTuneHeaderSize - 16) / 8;
	if (nbeats > maxBeats) {
		warning("Tune %u has implausible nbeats=%u (max %u); skipping", index, nbeats, maxBeats);
		nbeats = 0;
	}
	_beats.resize(nbeats);

	const Common::Span<const byte> beats = tuneData.subspan(kTuneHeaderSize, uint32(nbeats) * 8);
	const Common::Span<const byte> channels = tuneData.subspan(kTuneHeaderSize + uint32(nbeats) * 8);

	for (uint i = 0; i < _beats.size(); i++) {
		debugC(2, kDebugLevelMusic, "found beat at offset 0x%x", (uint)(kTuneHeaderSize + i * 8));
		_beats[i] = Beat(beats.subspan(i * 8, 8), channels,
						 kTuneHeaderSize + uint32(nbeats) * 8, tuneData);
	}

	_currentBeat = 0;
	_beatticks = 0;
	// Note::tick fires only when Note::_tick == MusicParser::getTick(). Notes are
	// constructed with _tick = 0; without an explicit reset on the initial beat,
	// MusicParser::_tick increments past 0 before any note can match — so the
	// player runs but no NoteOn ever reaches the driver. The DOS engine called
	// the equivalent of setBeat() implicitly via the script's first kSetBeat
	// command, but our scripts may rely on the initial-beat path. Sync note
	// ticks against the music clock so notes fire from frame one.
	if (!_beats.empty())
		_beats[0].reset();

	uint activeChannels = _beats.empty() ? 0 : _beats[0].activeChannels();
	debugC(1, kDebugLevelMusic, "Tune %u: %u beats, %u active channels in beat 0",
		   index, (uint)_beats.size(), activeChannels);
	if (_beats.empty() || activeChannels == 0)
		warning("Music tune %u loaded but has no playable content (beats=%u channels=%u)",
				index, (uint)_beats.size(), activeChannels);
}

void Tune::setBeat(uint16 index) {
	if (index >= _beats.size()) {
		// Script asked to seek past the last beat — common at tune end. Stop
		// the modeled DOS current-tune word too; CheckMusicPlaying polls that
		// word and should report "not playing" once the terminal beat is
		// reached.
		debugC(2, kDebugLevelMusic, "Interspective music: Tune::setBeat(%u) >= beats=%u — stopping tune",
			   (uint)index, (uint)_beats.size());
		Music.stopMusic();
		return;
	}
	bool activeSlots[8][4];
	memset(activeSlots, 0, sizeof(activeSlots));
	for (int channel = 0; channel < 8; channel++)
		for (int slot = 0; slot < 4; slot++)
			activeSlots[channel][slot] = _beats[index].hasNoteSlot(channel, slot);
	Music.stopMusicNotesNotInSlots(activeSlots);
	_currentBeat = index;
	_beats[_currentBeat].reset();
	_beatticks = 0;
}

void Tune::restorePosition(uint16 beat, uint32 beatTicks) {
	if (beat >= _beats.size()) {
		stop();
		return;
	}

	const uint32 savedBeatTicks = beatTicks % 64;
	_currentBeat = beat;
	_beats[_currentBeat].reset();
	_beatticks = savedBeatTicks;
}

void Tune::stop() {
	_currentBeat = -1;
	_beatticks = 0;
}

bool Tune::isPlaying() const {
	return _currentBeat >= 0 && _currentBeat < int32(_beats.size());
}

void Tune::tick() {
	if (!isPlaying()) {
		// Out-of-range beat index would smash the heap. Default-constructed
		// Tune has _currentBeat=-1 (never initialised). After the last beat
		// finishes, setBeat(_currentBeat+1) can advance past _beats.size().
		// Either way: silently do nothing rather than crash.
		static bool reportedBadBeat = false;
		if (!reportedBadBeat) {
			reportedBadBeat = true;
			warning("Interspective music: Tune::tick guarded out-of-range beat (currentBeat=%d, beats=%u)",
					(int)_currentBeat, (uint)_beats.size());
		}
		return;
	}
	_beats[_currentBeat].tick();
	if (!isPlaying())
		return;
	_beatticks++;
	if (_beatticks == 64)
		setBeat(_currentBeat + 1);
	_beatticks %= 64;
}

Beat::Beat() {}

Beat::Beat(Common::Span<const byte> def, Common::Span<const byte> channels, uint32 channelsOffset,
		   Common::Span<const byte> tune) {
	for (int i = 0; i < 8; i++) {
		const uint8 channelIndex = def.getUint8At(i);
		if (channelIndex) {
			uint16 off = 16 * channelIndex;
			if (off + 16 > channels.size()) {
				warning("Interspective music: channel definition %u outside tune data", (uint)channelIndex);
				continue;
			}
			debugC(2, kDebugLevelMusic, "found channel at offset 0x%x",
				   uint(channelsOffset + off));
			_channels[i] = Channel(channels.subspan(off, 16), tune, i + 2);
		}
	}
}

bool Beat::hasNoteSlot(byte channel, byte note) const {
	return channel < 8 && _channels[channel].hasNoteSlot(note);
}

void Beat::reset(uint32 start) {
	for (int i = 0; i < 8; i++)
		_channels[i].reset();
}

void Beat::tick() {
	for (int i = 0; i < 8; i++)
		_channels[i].tick();
}

Channel::Channel() : _active(false), _not_initialized(false), _initnote(0), _chanidx(0) {}

Channel::Channel(Common::Span<const byte> def, Common::Span<const byte> tune, byte chanidx) {
	for (int i = 0; i < 4; i++) {
		const uint16 off = def.getUint16LEAt(i * 2);
		if (off) {
			if (off + 2 > tune.size()) {
				warning("Interspective music: note stream offset 0x%x outside tune data", off);
				continue;
			}
			debugC(2, kDebugLevelMusic, "found note at offset 0x%x", off);
			_notes[i] = Note(tune.subspan(off), i);
		}
	}

	for (int i = 0; i < 4; i++) {
		_init[i] = MusicCommand(def.subspan(8 + i * 2, 2));
	}
	_active = true;
	_not_initialized = true;
	_initnote = 0;
	_chanidx = chanidx;
}

bool Channel::hasNoteSlot(byte index) const {
	return _active && index < 4 && _notes[index].isActive();
}

void Channel::reset() {
	unless(_active) return;

	_not_initialized = true;
	_initnote = 0;

	for (int i = 0; i < 4; i++)
		_notes[i].reset();
}

void Channel::tick() {
	unless(_active) return;

	if (_not_initialized) {
		for (byte i = 0; i < 4; i++)
			_init[i].exec(_chanidx);
		_not_initialized = false;
	}

	for (byte i = 0; i < 4; i++)
		_notes[i].tick(_chanidx);
}

Note::Note() : _pos(0), _tick(0), _index(0), _channel(0) {}

Note::Note(Common::Span<const byte> data, byte index) : _data(data), _pos(0), _tick(0), _index(index), _channel(0) {}

void Note::setNote(byte n) {
	notes[_channel - 2][_index] = n;
}

byte Note::note() const {
	return notes[_channel - 2][_index];
}

void Note::reset() {
	unless(_data) return;

	_tick = Music.getTick() + 1;
	_pos = 0;
}

void Note::tick(byte channel) {
	_channel = channel;
	unless(_data && Music.getTick() == _tick) return;
	if (_pos + 1 >= _data.size()) {
		if (note() != 0) {
			Music._driver->send(channel | kMidiNoteOff, note(), 0);
			setNote(0);
		}
		_data = Common::Span<const byte>();
		return;
	}

	if (_data.getUint8At(_pos) == kHangNote) {
		_tick += _data.getUint8At(_pos + 1);
		_pos += 2;
		return;
	}

	MusicCommand cmd(_data.subspan(_pos, 2));
	cmd.exec(channel, this);

	_pos += 2;
	_tick++;
}

MusicCommand::MusicCommand() : _command(0) {}

bool MusicCommand::empty() const {
	return _command == 0;
}

MusicCommand::MusicCommand(Common::Span<const byte> def) : _command(0),
														   _parameter(0) {
	if (def.data() && def.size() >= 2) {
		_command = def.getUint8At(0);
		_parameter = def.getUint8At(1);
	}
}

void MusicCommand::exec(byte channel, Note *note) {
	unless(_command) return;

	switch (_command) {

	case kSetProgram:
		debugC(2, kDebugLevelMusic, "set program on channel %d to %d", channel, _parameter);
		Music._driver->send(channel | kMidiSetProgram, MidiDriver::_mt32ToGm[_parameter], 0);
		break;

	case kSetExpression:
		if (Music._musicType == MT_ADLIB) {
			debugC(2, kDebugLevelMusic, "set expression on channel %d to %d (AdLib volume %d)",
				   channel, _parameter, _parameter / 2);
			Music._driver->send(channel | kMidiChannelControl, MidiDriver::MIDI_CONTROLLER_VOLUME, _parameter / 2);
		} else {
			const uint8 value = clampMidiControllerValue(_parameter);
			debugC(2, kDebugLevelMusic, "set expression on channel %d to %d (MIDI expression %d)",
				   channel, _parameter, value);
			Music._driver->send(channel | kMidiChannelControl, kMidiCtrlExpression, value);
		}
		break;

	case kCmdNoteOff:
		debugC(2, kDebugLevelMusic, "turn off note %d on channel %d", _parameter, channel);

		// note is null for a channel's 4 init-slot commands (Channel::tick runs
		// _init[i].exec(_chanidx) with the default note=0). A note-off there is
		// unexpected data; skip it rather than null-deref in release (NDEBUG)
		// builds — matches execSfxCommand's null-safe handling.
		if (!note) {
			warning("Interspective music: note-off opcode in channel %d init slot ignored", channel);
			break;
		}
		Music._driver->send(channel | kMidiNoteOff, note->note(), 0);
		note->setNote(0);
		break;

	case kCmdCallScript:
		debugC(2, kDebugLevelMusic, "will call script");
		Music._script->tick();
		break;

	case kSetTempo:
		debugC(2, kDebugLevelMusic, "setting tempo to %d", _parameter);
		Music.setTempo(500000 * _parameter);
		break;

	case kCmdSetBeat:
		debugC(2, kDebugLevelMusic, "setting beat to %d", _parameter);
		Music.setBeat(_parameter);
		break;

	default:
		if (_command < 0x80) {
			// As above: a note-on in a channel init slot has note==0. Skip
			// instead of dereferencing null in release builds.
			if (!note) {
				warning("Interspective music: note-on opcode 0x%02x in channel %d init slot ignored",
						_command, channel);
				break;
			}
			debugC(2, kDebugLevelMusic, "play note %d at volume %d on %d", _command, _parameter, channel);

			static bool reportedFirstNote = false;
			if (!reportedFirstNote) {
				reportedFirstNote = true;
				debugC(1, kDebugLevelMusic,
					   "Interspective music: first NoteOn reached driver "
					   "(channel=%u pitch=%u velocity=%u)",
					   (uint)channel, (uint)_command, (uint)_parameter);
			}

			if (note->note()) {
				debugC(2, kDebugLevelMusic, "[first turn off note %d]", note->note());

				Music._driver->send(channel | kMidiNoteOff, note->note(), 0);
			}

			Music._driver->send(channel | kMidiNoteOn, _command, _parameter);
			note->setNote(_command);
			break;
		}

		error("unhandled music command %x", _command);
	}
}

} // End of namespace Interspective
