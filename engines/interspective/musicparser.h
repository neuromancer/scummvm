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

#ifndef INTERSPECTIVE_MUSIC_H
#define INTERSPECTIVE_MUSIC_H

#include "common/array.h"
#include "common/mutex.h"
#include "common/noncopyable.h"
#include "common/queue.h"
#include "common/singleton.h"

#include "audio/mididrv.h"
#include "audio/midiparser.h"

#include "interspective/value.h"

namespace Interspective {
//

class Note;

class MusicCommand {
public:
	enum Status {
		kThxBye,
		kCallMe,
		kNvm,
		kNextBeat
	};

	MusicCommand();
	MusicCommand(const byte *code);
	bool empty() const;
	void exec(byte channel, Note *note = 0);

private:
	byte _command, _parameter;
};

class Note {
public:
	Note();
	Note(const byte *data, byte index);
	MusicCommand::Status parseNextEvent(EventInfo &info);
	uint32 delta() const;
	void reset();
	void tick(byte channel);
	byte note() const;
	void setNote(byte n);

private:
	void checkDelta() const;
	mutable const byte *_data;
	mutable uint32 _tick;
	const byte *_begin;
	byte _index;
	byte _channel;
};

class Channel {
public:
	bool isActive() const { return _active; }
	Channel();
	Channel(const byte *def, const byte *tune, byte chanidx);
	MusicCommand::Status parseNextEvent(EventInfo &info);
	uint32 delta() const;
	void reset();
	byte index() const { return _chanidx; }
	void tick();

private:
	Note _notes[4];
	MusicCommand _init[4];
	bool _active, _not_initialized;
	byte _initnote, _chanidx;
};

class Beat {
public:
	Beat();
	Beat(const byte *def, const byte *channels, const byte *tune);
	uint activeChannels() const {
		uint n = 0;
		for (int i = 0; i < 8; i++)
			if (_channels[i].isActive())
				n++;
		return n;
	}
	MusicCommand::Status parseNextEvent(EventInfo &info);
	void reset(uint32 start = 0);
	uint32 delta() const;
	void tick();

private:
	Channel _channels[8];
	uint32 _start;
};

class Tune {
public:
	Tune();
	Tune(uint16 index);
	MusicCommand::Status parseNextEvent(EventInfo &info);
	void setBeat(uint16);
	void restorePosition(uint16 beat, uint32 beatTicks);
	void stop();
	bool isPlaying() const;
	uint16 beatId() const { return _currentBeat; }
	uint32 beatTicks() const { return _beatticks; }
	void tick();

	friend class Note;

private:
	Common::Array<Beat> _beats;

	enum { kTuneBufferSize = 0x8000 };
	byte _data[kTuneBufferSize];
	int32 _currentBeat;
	uint32 _beatticks;
};

class MusicScript : public Common::NonCopyable {
public:
	MusicScript();
	MusicScript(const byte *data);
	void parseNextEvent(EventInfo &info);
	void tick();
	uint16 getTune() const { return READ_LE_UINT16(_code); }
	const byte *base() const { return _code; }

	friend class Note;

private:
	const byte *_code;
	uint16 _offset;
};

class MusicParser : public MidiParser, public Common::Singleton<MusicParser> {
public:
	MusicParser();
	~MusicParser();

	bool loadMusic(const byte *data, uint32 size = 0) override;
	static void timerCallback(void *data) { ((MusicParser *)data)->tick(); }
	void tick();
	uint32 getTick() override { return _tick; }
	void setBeat(uint16 beat) { _tune->setBeat(beat); }
	void silence();
	void stopMusic();
	void requestStopCurrent();
	bool restartCurrentLikeDos();
	void setMaxVolume(uint8 dosMusicMode);
	bool isPlaying() const;
	bool hasCurrentTune() const { return _currentTuneWord != 0; }
	uint16 currentTuneWord() const { return _currentTuneWord; }
	const byte *currentScriptBase() const { return _script ? _script->base() : 0; }
	uint16 currentBeat() const { return _tune ? _tune->beatId() : 0xffff; }
	uint32 currentBeatTicks() const { return _tune ? _tune->beatTicks() : 0; }
	uint8 driverCommandByte() const { return _driverCommandByte; }
	uint8 driverModeFlag() const { return _driverModeFlag; }
	bool isActive() const { return _active; }
	void setActive(bool active) { _active = active; }
	void restoreSavedState(const byte *script, uint16 currentTuneWord, uint8 active,
						   uint8 driverCommandByte, uint8 driverModeFlag,
						   uint16 beat, uint32 beatTicks);
	bool playSfxTune(const byte *data, uint32 size);
	void stopSfxNotes();
	bool isSfxNotePlaying() const { return _sfxTunePlaying; }

	friend class Note;
	friend class MusicCommand;

private:
	struct SfxNote {
		uint16 pos;
		uint32 tick;
		uint8 note;
		bool active;
		bool playing;
	};
	struct SfxChannel {
		SfxNote notes[4];
		uint16 initPos;
		uint8 midiChannel;
		bool active;
		bool notInitialized;
	};

	void parseNextEvent(EventInfo &info) override {}
	bool setSfxBeat(uint16 beat);
	void tickSfxTune();
	void tickSfxNote(SfxNote &note, uint8 channel);
	void execSfxCommand(uint8 command, uint8 parameter, uint8 channel, SfxNote *note);
	void clearSfxState();

	// Serializes the MIDI timer thread (tick() -> _tune->tick() / tickSfxTune())
	// against main-thread state mutators (loadMusic/stopMusic/playSfxTune/etc.)
	// that free or rebuild _tune/_script/_sfxChannels. SDL-backed Common::Mutex
	// is recursive, so the in-tick re-entrant calls (stopMusic via the deferred
	// command byte, setBeat/stopMusic via script callbacks) are deadlock-safe.
	Common::Mutex _mutex;
	// Deferred SFX beat switch: setSfxBeat() rebuilds _sfxChannels, so it must
	// never run mid-iteration of tickSfxTune(). In-stream kCmdSetBeat and the
	// 64-tick auto-advance record the target here; tickSfxTune() applies it at
	// the top of the next tick (-1 = none).
	int32 _sfxPendingBeat;

	Tune *_tune;
	MidiDriver *_midiDriver;
	MusicScript *_script;
	MusicType _musicType;
	bool _active;
	uint16 _currentTuneWord;
	uint8 _driverCommandByte;
	uint8 _driverModeFlag;
	enum { kSfxTuneBufferSize = 0x320 };
	byte _sfxData[kSfxTuneBufferSize];
	uint32 _sfxDataSize;
	SfxChannel _sfxChannels[8];
	uint16 _sfxBeatCount;
	int32 _sfxCurrentBeat;
	uint32 _sfxBeatTicks;
	uint32 _sfxTime;
	uint32 _sfxLastTick;
	uint32 _sfxPsecPerTick;
	uint32 _sfxTick;
	bool _sfxTunePlaying;

	uint32 _time, _lastTick, _tick;
};

#define Music MusicParser::instance()

} // End of namespace Interspective

#endif // INTERSPECTIVE_MUSIC_H
