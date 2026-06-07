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
#include "common/endian.h"
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

MusicParser::MusicParser() : MidiParser(), _sfxPendingBeat(-1), _tune(0), _script(0), _musicType(MT_INVALID), _active(true),
							 _currentTuneWord(0), _driverCommandByte(0), _driverModeFlag(0),
							 _sfxDataSize(0), _sfxBeatCount(0), _sfxCurrentBeat(-1), _sfxBeatTicks(0),
							 _sfxTime(0), _sfxLastTick(0), _sfxPsecPerTick(500000 * 0x19 / 120), _sfxTick(0),
							 _sfxTunePlaying(false), _time(0), _lastTick(0), _tick(0) {
	memset(_sfxData, 0, sizeof(_sfxData));
	clearSfxState();
	const uint32 devTypes = MDT_MIDI | MDT_ADLIB | MDT_PREFER_GM;
	MidiDriver::DeviceHandle dev = MidiDriver::detectDevice(devTypes);
	_musicType = MidiDriver::getMusicType(dev);
	const Common::String devId = MidiDriver::getDeviceString(dev, MidiDriver::kDeviceId);
	warning("Interspective music init: detected device id='%s' musicType=%d",
			devId.c_str(), int(_musicType));

	_midiDriver = MidiDriver::createMidi(dev);
	if (!_midiDriver) {
		warning("Interspective music init: MidiDriver::createMidi returned NULL — music will be silent");
		return;
	}

	int openResult = _midiDriver->open();
	if (openResult != 0) {
		warning("Interspective music init: MidiDriver::open failed (%d) — music will be silent", openResult);
		return;
	}
	warning("Interspective music init: MIDI driver opened OK; baseTempo=%u",
			(uint)_midiDriver->getBaseTempo());

	// Report current mixer volume so the user can confirm the music isn't being
	// silenced upstream. Default in ScummVM is typically 192/256 (75%) but can
	// be 0 if the user has muted music in the Audio panel — that would silence
	// the OPL output entirely regardless of how many NoteOns we send.
	if (g_system && g_system->getMixer()) {
		const int musicVol = g_system->getMixer()->getVolumeForSoundType(Audio::Mixer::kMusicSoundType);
		const int sfxVol = g_system->getMixer()->getVolumeForSoundType(Audio::Mixer::kSFXSoundType);
		warning("Interspective music init: mixer volumes — music=%d/%d sfx=%d/%d (max=%d)",
				musicVol, Audio::Mixer::kMaxMixerVolume,
				sfxVol, Audio::Mixer::kMaxMixerVolume,
				Audio::Mixer::kMaxMixerVolume);
		if (musicVol == 0)
			warning("Interspective music init: ★ MUSIC VOLUME IS 0 — that's why you hear nothing. "
					"Adjust in ScummVM's Audio settings (or `music_volume` in scummvm.ini).");
	} else {
		warning("Interspective music init: g_system / mixer unavailable — can't read volume");
	}

	// MidiParser::setMidiDriver only sets _driver — doesn't touch the driver's
	// timer callback. We override it ourselves below to point at our own tick
	// (the base MidiParser::onTimer drives the standard event-stream model that
	// our parseNextEvent() override no-ops; we run our own beat/channel/note
	// state machine in MusicParser::tick instead).
	setMidiDriver(_midiDriver);
	setTimerRate(_midiDriver->getBaseTempo());
	_midiDriver->setTimerCallback(this, &MusicParser::timerCallback);
	warning("Interspective music init: timer callback registered (timerRate=%u µs)",
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
	delete _tune;
	_tune = 0;
	delete _script;
	_script = 0;
	if (_midiDriver) {
		_midiDriver->close();
		setMidiDriver(0);
		delete _midiDriver;
	}
}

bool MusicParser::loadMusic(const byte *data, uint32 size) {
	Common::StackLock lock(_mutex);
	if (!_midiDriver) {
		warning("Interspective music: loadMusic skipped — no MIDI driver");
		return false;
	}

	// Idempotency guard: scripts can re-emit Op_f4 with the same data
	// pointer many times per second (observed iter-29: 100+ calls/sec
	// of loadMusic with the same data, racing with the MIDI timer
	// thread's _tune->tick() and crashing in Channel::tick when the old
	// _tune was freed mid-iteration). Skip if data matches what's
	// already loaded.
	if (_script && _script->base() == data && hasCurrentTune())
		return true;

	static int loadMusicCallCount = 0;
	loadMusicCallCount++;
	debugC(2, kDebugLevelMusic, "Interspective music: loadMusic called (#%d) data=%p size=%u",
		   loadMusicCallCount, (const void *)data, (unsigned)size);

	unloadMusic();
	silence();
	delete _script;
	delete _tune;
	_tune = 0;
	_script = new MusicScript(const_cast<byte *>(data));

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
	warning("Interspective music: loadMusic tune index = %u (data tune %u)", tuneIdx, midiTuneIdx);
	_tune = new Tune(midiTuneIdx);

	_numTracks = 1;
	_ppqn = 120;
	//	_clocksPerTick = 0x19;
	setTempo(500000 * 0x19);
	setTrack(0);
	warning("Interspective music: loadMusic complete — _psecPerTick=%u _ppqn=%u",
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
		warning("Interspective music: first MusicParser::tick fired (timerRate=%u psecPerTick=%u tune=%p)",
				(uint)_timerRate, (uint)_psecPerTick, (const void *)_tune);
	}

	if (_tune && _tune->isPlaying()) {
		static bool reportedFirstTuneTick = false;
		if (!reportedFirstTuneTick) {
			reportedFirstTuneTick = true;
			warning("Interspective music: first Tune::tick about to fire (Music.getTick=%u)",
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

bool MusicParser::playSfxTune(const byte *data, uint32 size) {
	Common::StackLock lock(_mutex);
	enum {
		kTuneBeatCountOffset = 0x21,
		kTuneHeaderSize = 0x25
	};

	if (!_driver || !data || size < kTuneHeaderSize || size > kSfxTuneBufferSize)
		return false;

	stopSfxNotes();

	const uint16 beatCount = READ_LE_UINT16(data + kTuneBeatCountOffset);
	const uint32 channelBase = kTuneHeaderSize + uint32(beatCount) * 8;
	if (beatCount == 0 || channelBase >= size || channelBase + 16 > size)
		return false;

	memcpy(_sfxData, data, size);
	_sfxDataSize = size;
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
		   (uint)size, (uint)beatCount);
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

	for (uint8 channel = 0; channel < ARRAYSIZE(_sfxChannels); ++channel) {
		const uint8 channelIndex = _sfxData[beatOffset + channel];
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
			const uint16 pos = READ_LE_UINT16(_sfxData + channelDef + noteIndex * 2);
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

	for (uint8 channel = 0; channel < ARRAYSIZE(_sfxChannels); ++channel) {
		SfxChannel &state = _sfxChannels[channel];
		if (!state.active)
			continue;

		if (state.notInitialized) {
			for (uint8 i = 0; i < 4; ++i) {
				const uint16 pos = uint16(state.initPos + i * 2);
				if (pos + 1 < _sfxDataSize)
					execSfxCommand(_sfxData[pos], _sfxData[pos + 1], state.midiChannel, 0);
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

	const uint8 command = _sfxData[note.pos];
	const uint8 parameter = _sfxData[note.pos + 1];
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
			_driver->send(channel | kMidiChannelControl, kMidiCtrlExpression, parameter / 2);
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

void MusicParser::stopMusic() {
	Common::StackLock lock(_mutex);
	_currentTuneWord = 0;
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

void MusicParser::restoreSavedState(const byte *script, uint16 currentTuneWord, uint8 active,
									uint8 driverCommandByte, uint8 driverModeFlag, uint16 beat, uint32 beatTicks) {
	Common::StackLock lock(_mutex);
	_active = active != 0;
	_driverCommandByte = driverCommandByte;
	_driverModeFlag = driverModeFlag;

	if (!script || currentTuneWord == 0 || !_midiDriver) {
		stopMusic();
		_currentTuneWord = 0;
		return;
	}

	stopMusic();
	if (!loadMusic(script)) {
		_currentTuneWord = 0;
		return;
	}

	_currentTuneWord = currentTuneWord;
	_driverCommandByte = driverCommandByte;
	_driverModeFlag = driverModeFlag;
	if (_tune)
		_tune->restorePosition(beat, beatTicks);
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

MusicScript::MusicScript() : _code(0) {}

MusicScript::MusicScript(const byte *data) : _code(data),
											 _offset(2) {}

enum {
	kJump = 0x96,
	kSetBeat = 0x9a,
	kStop = 0x9b
};

void MusicScript::tick() {
	// Bounded: only kJump continues the loop; every other opcode returns. A
	// cyclic/self-referential jump (in malformed data) would otherwise spin
	// forever on the MIDI timer thread. Cap the chain and bail to stopMusic.
	for (int guard = 0; guard < 256; ++guard) {
		switch (_code[_offset]) {

		case kJump: {
			uint16 target = READ_LE_UINT16(_code + _offset + 2);
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

		case kSetBeat:
			debugC(2, kDebugLevelMusic, "will set beat to %d", _code[_offset + 1]);
			Music.setBeat(_code[_offset + 1]);
			_offset += 2;
			return;

		case kStop:
			debugC(2, kDebugLevelMusic, "will stop playing");
			Music.stopMusic();
			return;

		default: {
			// Unhandled music opcodes show up in real game scripts (e.g.
			// 0x94 in tune 5 around iter-29). Stopping music is a safer
			// fallback than `error()` which kills the engine — the user
			// loses music for that scene rather than the whole session.
			static bool reportedOnce = false;
			if (!reportedOnce) {
				reportedOnce = true;
				warning("Interspective music: unhandled script opcode 0x%02x at offset 0x%x — stopping music",
						(uint)_code[_offset], (uint)_offset);
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

	uint16 nbeats = READ_LE_UINT16(_data + kTuneBeatCountOffset);
	// Sanity: header + nbeats*8 (beat array) must fit in the buffer with room for at least one
	// 16-byte channel entry. Otherwise treat as empty and skip — happens if loadTune got a bogus
	// index and the 32 KB buffer is full of zeros / garbage.
	const uint maxBeats = (sizeof(_data) - kTuneHeaderSize - 16) / 8;
	if (nbeats > maxBeats) {
		warning("Tune %u has implausible nbeats=%u (max %u); skipping", index, nbeats, maxBeats);
		nbeats = 0;
	}
	_beats.resize(nbeats);

	const byte *beat = _data + kTuneHeaderSize;
	const byte *channels = beat + 8 * nbeats;

	for (uint i = 0; i < _beats.size(); i++) {
		debugC(2, kDebugLevelMusic, "found beat at offset 0x%x", (uint)(beat - _data));
		_beats[i] = Beat(beat, channels, _data);
		beat += 8;
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
		warning("Interspective music: Tune::setBeat(%u) >= beats=%u — stopping tune",
				(uint)index, (uint)_beats.size());
		Music.stopMusic();
		return;
	}
	_currentBeat = index;
	_beats[_currentBeat].reset();
	_beatticks = 0;
}

void Tune::restorePosition(uint16 beat, uint32 beatTicks) {
	if (beat >= _beats.size()) {
		stop();
		return;
	}

	_currentBeat = beat;
	_beats[_currentBeat].reset();
	_beatticks = beatTicks % 64;
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

Beat::Beat(const byte *def, const byte *channels, const byte *tune) {
	for (int i = 0; i < 8; i++)
		if (def[i]) {
			uint16 off = 16 * def[i];
			debugC(2, kDebugLevelMusic, "found channel at offset 0x%x", (uint)(off + channels - tune));
			_channels[i] = Channel(channels + off, tune, i + 2);
		}
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

Channel::Channel(const byte *def, const byte *tune, byte chanidx) {
	for (int i = 0; i < 4; i++) {
		const uint16 off = READ_LE_UINT16(def);
		def += 2;
		if (off) {
			debugC(2, kDebugLevelMusic, "found note at offset 0x%x", off);
			_notes[i] = Note(tune + off, i);
		}
	}

	for (int i = 0; i < 4; i++) {
		_init[i] = MusicCommand(def);
		def += 2;
	}
	_active = true;
	_not_initialized = true;
	_initnote = 0;
	_chanidx = chanidx;
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

Note::Note() : _data(0), _begin(0) {}

Note::Note(const byte *data, byte index) : _data(data), _tick(0), _begin(data), _index(index) {}

void Note::setNote(byte n) {
	notes[_channel - 2][_index] = n;
}

byte Note::note() const {
	return notes[_channel - 2][_index];
}

void Note::reset() {
	unless(_data) return;

	_tick = Music.getTick() + 1;
	_data = _begin;
}

void Note::tick(byte channel) {
	_channel = channel;
	unless(_data && Music.getTick() == _tick) return;

	if (_data[0] == kHangNote) {
		_tick += _data[1];
		_data += 2;
		return;
	}

	MusicCommand cmd(_data);
	cmd.exec(channel, this);

	_data += 2;
	_tick++;
}

MusicCommand::MusicCommand() : _command(0) {}

bool MusicCommand::empty() const {
	return _command == 0;
}

MusicCommand::MusicCommand(const byte *def) : _command(def[0]),
											  _parameter(def[1]) {}

void MusicCommand::exec(byte channel, Note *note) {
	unless(_command) return;

	switch (_command) {

	case kSetProgram:
		debugC(2, kDebugLevelMusic, "set program on channel %d to %d", channel, _parameter);
		Music._driver->send(channel | kMidiSetProgram, MidiDriver::_mt32ToGm[_parameter], 0);
		break;

	case kSetExpression:
		debugC(2, kDebugLevelMusic, "set expression on channel %d to %d", channel, _parameter);
		if (Music._musicType == MT_ADLIB)
			Music._driver->send(channel | kMidiChannelControl, MidiDriver::MIDI_CONTROLLER_VOLUME, _parameter / 2);
		else
			Music._driver->send(channel | kMidiChannelControl, kMidiCtrlExpression, _parameter / 2);
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
				warning("Interspective music: first NoteOn reached driver "
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
