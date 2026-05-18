/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $URL$
 * $Id$
 *
 */

#include "interspective/musicparser.h"

#include "common/config-manager.h"
#include "common/endian.h"
#include "common/system.h"
#include "audio/mididrv.h"
#include "audio/mixer.h"

#include "interspective/innocent.h"
#include "interspective/resources.h"
#include "interspective/util.h"

namespace Common {
	DECLARE_SINGLETON(Interspective::MusicParser);
}

namespace Interspective {

static uint16 midiTuneIndexForScriptTune(uint16 tuneIdx) {
	MainDat *main = Res.mainDat();
	if (!main || Engine::instance().dosMusicEnabled() != 1)
		return tuneIdx;

	const uint16 count = main->tunesCount();
	const uint16 rolandBase = count / 2;
	if (rolandBase == 0 || tuneIdx == 0 || tuneIdx > rolandBase || tuneIdx + rolandBase > count)
		return tuneIdx;

	return tuneIdx + rolandBase;
}

MusicParser::MusicParser() : MidiParser(), _tune(0), _script(0), _musicType(MT_INVALID), _active(true),
	_currentTuneWord(0), _driverCommandByte(0), _driverModeFlag(0),
	_sfxNoteCount(0), _sfxNoteTicks(0), _time(0), _lastTick(0), _tick(0) {
	memset(_sfxNoteChannels, 0, sizeof(_sfxNoteChannels));
	memset(_sfxNotes, 0, sizeof(_sfxNotes));
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
	stopSfxNotes();
	silence();
	unloadMusic();
	delete _tune; _tune = 0;
	delete _script; _script = 0;
	if (_midiDriver) {
		_midiDriver->setTimerCallback(0, 0);
		_midiDriver->close();
		setMidiDriver(0);
		delete _midiDriver;
	}
}

bool MusicParser::loadMusic(const byte *data, uint32 size) {
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
	delete _tune; _tune = 0;
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
	if (_driverCommandByte == 1) {
		stopMusic();
		return;
	}

	_time += _timerRate;
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
	if (_sfxNoteTicks != 0 && --_sfxNoteTicks == 0)
		stopSfxNotes();
	_tick++;
}

static byte notes[8][4];

enum {
	kMidiNoteOff = 		  0x80,
	kMidiNoteOn = 		  0x90,
	kMidiChannelControl = 0xb0,
	kMidiSetProgram = 	  0xc0
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

bool MusicParser::playSfxNote(uint8 channel, uint8 note, uint8 velocity, uint16 durationTicks) {
	if (!_driver || note == 0)
		return false;

	stopSfxNotes();
	_driver->send(channel | kMidiNoteOn, note, velocity);
	_sfxNoteChannels[0] = channel;
	_sfxNotes[0] = note;
	_sfxNoteCount = 1;
	_sfxNoteTicks = durationTicks ? durationTicks : 32;
	debugC(1, kDebugLevelSound, "Interspective music: Roland SFX note channel=%u note=%u velocity=%u duration=%u",
		(uint)channel, (uint)note, (uint)velocity, (uint)_sfxNoteTicks);
	return true;
}

void MusicParser::stopSfxNotes() {
	if (!_driver) {
		_sfxNoteCount = 0;
		_sfxNoteTicks = 0;
		return;
	}
	for (uint8 i = 0; i < _sfxNoteCount; ++i) {
		if (_sfxNotes[i] != 0)
			_driver->send(_sfxNoteChannels[i] | kMidiNoteOff, _sfxNotes[i], 0);
	}
	_sfxNoteCount = 0;
	_sfxNoteTicks = 0;
	memset(_sfxNoteChannels, 0, sizeof(_sfxNoteChannels));
	memset(_sfxNotes, 0, sizeof(_sfxNotes));
}

bool MusicParser::isPlaying() const {
	return _tune && _tune->isPlaying();
}

void MusicParser::stopMusic() {
	_currentTuneWord = 0;
	_driverCommandByte = 0;
	silence();
	unloadMusic();
	if (_tune)
		_tune->stop();
}

void MusicParser::requestStopCurrent() {
	_driverCommandByte = 1;
}

void MusicParser::restoreSavedState(const byte *script, uint16 currentTuneWord, uint8 active,
		uint8 driverCommandByte, uint8 driverModeFlag, uint16 beat, uint32 beatTicks) {
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

bool MusicParser::restartCurrentLikeDos() {
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
	debugC(2, kDebugLevelMusic, "setting music channel volume to maximum");
	_driverCommandByte = 0xff;
	_driverModeFlag = (dosMusicMode == 4) ? 0 : 0x3f;
	if (!_driver)
		return;
	for (int channel = 2; channel < 10; ++channel)
		_driver->send(channel | kMidiChannelControl, MidiDriver::MIDI_CONTROLLER_VOLUME, 127);
}

MusicScript::MusicScript() : _code(0) {}

MusicScript::MusicScript(const byte *data) :
	_code(data),
	_offset(2) {}

enum {
	kJump = 0x96,
	kSetBeat = 0x9a,
	kStop =	   0x9b
};

void MusicScript::tick() {
	while (true) {
		switch (_code[_offset]) {

		case kJump: {
			uint16 target = READ_LE_UINT16(_code + _offset + 2);
			debugC(2, kDebugLevelMusic, "will jump to music script at 0x%x", target);
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
}

Tune::Tune() : _currentBeat(-1) {}

enum {
	kTuneBeatCountOffset = 0x21,
	kTuneHeaderSize =	   0x25
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
		debugC(2, kDebugLevelMusic, "found beat at offset 0x%x", beat - _data);
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
			debugC(2, kDebugLevelMusic, "found channel at offset 0x%x", off + channels - tune);
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
	unless (_active)
		return;

	_not_initialized = true;
	_initnote = 0;

	for (int i = 0; i < 4; i++)
		_notes[i].reset();
}

enum {
	kMidiCtrlExpression = 	0xb,
	kMidiCtrlAllNotesOff = 0x7b
};

void Channel::tick() {
	unless (_active)
		return;

	if (_not_initialized) {
		for (byte i = 0; i < 4; i++)
			_init[i].exec(_chanidx);
		_not_initialized = false;
	}

	for (byte i = 0; i < 4; i++)
		_notes[i].tick(_chanidx);
}

Note::Note() : _data(0), _begin(0) {}

Note::Note(const byte *data, byte index) :
	_data(data), _tick(0), _begin(data), _index(index) {}

void Note::setNote(byte n) {
	notes[_channel - 2][_index] = n;
}

byte Note::note() const {
	return notes[_channel - 2][_index];
}

void Note::reset() {
	unless (_data)
		return;

	_tick = Music.getTick() + 1;
	_data = _begin;
}

enum {
	kSetTempo =		 0x81,
	kSetProgram = 	 0x82,
	kCmdSetBeat =	 0x85,
	kSetExpression = 0x89,
	kCmdNoteOff =	 0x8b,
	kCmdCallScript = 0x8c,
	kHangNote = 	 0xfe
};

void Note::tick(byte channel) {
	_channel = channel;
	unless (_data && Music.getTick() == _tick)
		return;

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

MusicCommand::MusicCommand(const byte *def) :
	_command(def[0]),
	_parameter(def[1]) {}

void MusicCommand::exec(byte channel, Note *note) {
	unless (_command)
		return;

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

		assert(note);
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
			assert (note);
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

} // End of namespace
