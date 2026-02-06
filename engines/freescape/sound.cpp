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

#include "common/file.h"
#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "audio/softsynth/ay8912.h"

#include "freescape/freescape.h"
#include "freescape/games/eclipse/eclipse.h"

namespace Freescape {

void FreescapeEngine::loadSpeakerFxZX(Common::SeekableReadStream *file, int sfxTable, int sfxData) {
	debugC(1, kFreescapeDebugParser, "Reading sound table for ZX");
	int numberSounds = 25;

	if (isDark())
		numberSounds = 34;

	if (isEclipse() && (_variant & GF_ZX_DEMO_MICROHOBBY))
		numberSounds = 21;

	for (int i = 1; i < numberSounds; i++) {
		debugC(1, kFreescapeDebugParser, "Reading sound table entry: %d ", i);
		_soundsSpeakerFxZX[i] = new Common::Array<soundUnitZX>();
		int soundIdx = (i - 1) * 4;
		file->seek(sfxTable + soundIdx);

		byte SFXtempStruct[8] = {};

		uint8 dataIndex = file->readByte();
		uint16 soundValue = file->readUint16LE();
		SFXtempStruct[0] = file->readByte();

		file->seek(sfxData + dataIndex * 4);
		uint8 soundType = file->readByte();
		int original_sound_ptr = sfxData + dataIndex * 4 + 1;
		int sound_ptr = original_sound_ptr;
		uint8 soundSize = 0;
		int16 repetitions = 0;
		debugC(1, kFreescapeDebugParser, "dataIndex: %x, value: %x, SFXtempStruct[0]: %x, type: %x", dataIndex, soundValue, SFXtempStruct[0], soundType);
		if (soundType == 0xff)
			break;

		if ((soundType & 0x80) == 0) {
			SFXtempStruct[6] = 0;
			SFXtempStruct[4] = soundType;

			while (true) {
				while (true) {
					file->seek(sound_ptr);
					//debug("start sound ptr: %x", sound_ptr);
					soundSize = file->readByte();
					SFXtempStruct[1] = soundSize;
					SFXtempStruct[2] = file->readByte();
					SFXtempStruct[3] = file->readByte();

					for (int j = 0; j <= 7; j++)
						debugC(1, kFreescapeDebugParser, "SFXtempStruct[%d]: %x", j, SFXtempStruct[j]);

					do {
						uint32 var9 = 0xffffff & (SFXtempStruct[3] * 0xd0);
						uint32 var10 = var9 / soundValue;

						var9 = 0xffffff & (7 * soundValue);
						uint16 var5 = (0xffff & var9) - 0x1e;
						if ((short)var5 < 0)
							var5 = 1;

						soundUnitZX soundUnit;
						soundUnit.isRaw = false;
						soundUnit.freqTimesSeconds = (var10 & 0xffff) + 1;
						soundUnit.tStates = var5;
						soundUnit.multiplier = 10;
						//debug("playSFX(%x, %x)", soundUnit.freqTimesSeconds, soundUnit.tStates);
						_soundsSpeakerFxZX[i]->push_back(soundUnit);
						int16 var4 = 0;

						if ((SFXtempStruct[2] & 0x80) != 0) {
							var4 = 0xff;
						}
						//debug("var4: %d", var4);
						//debug("soundValue delta: %d", int16(((var4 << 8) | SFXtempStruct[2])));
						soundValue = soundValue + int16(((var4 << 8) | SFXtempStruct[2]));
						//debug("soundValue: %x", soundValue);
						soundSize = soundSize - 1;
					} while (soundSize != 0);
					SFXtempStruct[5] = SFXtempStruct[5] + 1;
					if (SFXtempStruct[5] == SFXtempStruct[4])
						break;

					sound_ptr = original_sound_ptr + SFXtempStruct[5] * 3;
					//debug("sound ptr: %x", sound_ptr);
				}

				soundSize = SFXtempStruct[0];
				SFXtempStruct[0] = soundSize - 1;
				sound_ptr = original_sound_ptr;
				if ((soundSize - 1) == 0)
					break;
				SFXtempStruct[5] = 0;
			}
		} else if (soundType & 0x80) {
			file->seek(sound_ptr);
			for (int j = 1; j <= 7; j++) {
				SFXtempStruct[j] = file->readByte();
				debugC(1, kFreescapeDebugParser, "SFXtempStruct[%d]: %x", j, SFXtempStruct[j]);
			}
			soundSize = SFXtempStruct[0];
			repetitions = SFXtempStruct[1] | (SFXtempStruct[2] << 8);
			uint16 var5 = soundValue;
			//debug("Repetitions: %x", repetitions);
			if ((soundType & 0x7f) == 1) {
				do  {
					do {
						soundUnitZX soundUnit;
						soundUnit.isRaw = false;
						soundUnit.tStates = var5;
						soundUnit.freqTimesSeconds = SFXtempStruct[3] | (SFXtempStruct[4] << 8);
						soundUnit.multiplier = 1.8f;
						//debug("playSFX(%x, %x)", soundUnit.freqTimesSeconds, soundUnit.tStates);
						_soundsSpeakerFxZX[i]->push_back(soundUnit);
						repetitions = repetitions - 1;
						var5 = var5 + (SFXtempStruct[5] | (SFXtempStruct[6] << 8));

					} while ((byte)((byte)repetitions | (byte)((uint16)repetitions >> 8)) != 0);
					soundSize = soundSize - 1;
					repetitions = SFXtempStruct[1] | (SFXtempStruct[2] << 8);
					var5 = soundValue;
				} while (soundSize != 0);
			} else if ((soundType & 0x7f) == 2) {
				repetitions = SFXtempStruct[1] | (SFXtempStruct[0] << 8);
				debugC(1, kFreescapeDebugParser, "Raw sound, repetitions: %x", repetitions);
				uint16 sVar7 = SFXtempStruct[3];
				soundType = 0;
				soundSize = SFXtempStruct[2];
				uint16 silenceSize = SFXtempStruct[4];
				bool cond1 = (SFXtempStruct[4] != 0 && SFXtempStruct[4] != 2);
				bool cond2 = SFXtempStruct[4] == 2;
				bool cond3 = SFXtempStruct[4] == 0;

				assert(cond1 || cond2 || cond3);
				do {
					soundUnitZX soundUnit;
					soundUnit.isRaw = true;
					int totalSize = soundSize + sVar7;
					soundUnit.rawFreq = 0.1f;
					soundUnit.rawLengthus = totalSize;
					_soundsSpeakerFxZX[i]->push_back(soundUnit);
					//debugN("%x ", silenceSize);
					soundUnit.rawFreq = 0;
					soundUnit.rawLengthus = silenceSize;
					_soundsSpeakerFxZX[i]->push_back(soundUnit);
					repetitions = repetitions + -1;
					soundSize = SFXtempStruct[5] + soundSize;

					if (cond1)
						silenceSize = (repetitions & 0xff) | (repetitions >> 8);
					else if (cond2)
						silenceSize = (repetitions & 0xff);
					else
						silenceSize = soundSize;

					//debug("soundSize: %x", soundSize);
					//sVar7 = (uint16)bVar9 << 8;
				} while (repetitions != 0);
				//debug("\n");
				//if (i == 15)
				//	assert(0);
			} else {
				debugC(1, kFreescapeDebugParser, "Sound type: %x", soundType);
				bool beep = false;
				do {
					soundType = 0;
					uint16 uVar2 = SFXtempStruct[1] | (SFXtempStruct[2] << 8);
					uint8 cVar3 = 0;
					do {
						//debug("start cycle %d:", cVar3);
						//ULA_PORT = bVar4;
						//bVar4 = bVar4 ^ 0x10;
						beep = !beep;
						repetitions = (((uint16)soundType * 0x100 + (uint16)soundType * -2) -
						               (uint16)((uint16)soundType * 0x100 < (uint16)soundType)) + (uVar2 & 0xff);
						uint8 bVar9 = (byte)repetitions;
						uint8 bVar8 = (byte)((uint16)repetitions >> 8);
						uint8 bVar1 = bVar9 - bVar8;
						soundType = bVar1;
						if (bVar8 <= bVar9) {
							bVar1 = bVar1 - 1;
							soundType = bVar1;
						}
						//debug("wait %d", bVar1);
						assert(bVar1 > 0);
						soundUnitZX soundUnit;
						soundUnit.isRaw = false;
						soundUnit.freqTimesSeconds = beep ? 1000 : 0;
						soundUnit.tStates = beep ? 437500 / 1000 - 30.125 : 0;
						soundUnit.multiplier = float(bVar1) / 500;
						_soundsSpeakerFxZX[i]->push_back(soundUnit);

						// No need to wait
						//do {
						//	bVar1 = bVar1 - 1;
						//} while (bVar1 != 0);
						cVar3 = (char)(uVar2 >> 8) + -1;
						uVar2 = (((uint16)cVar3) << 8) | (uint8)uVar2;
					} while (cVar3 != '\0');
					soundSize = soundSize + -1;
				} while (soundSize != '\0');
			}
		}
	}
	//assert(0);
}

void FreescapeEngine::loadSpeakerFxDOS(Common::SeekableReadStream *file, int offsetFreq, int offsetTable, int numberSounds) {
	debugC(1, kFreescapeDebugParser, "Reading PC speaker sound table for DOS");
	for (int i = 1; i <= numberSounds; i++) {
		debugC(1, kFreescapeDebugParser, "Reading sound table entry: %d ", i);
		int soundIdx = (i - 1) * 4;
		file->seek(offsetFreq + soundIdx);
		uint16 index = file->readByte();
		if (index == 0xff)
			continue;
		uint iVar = index * 5;

		uint16 frequencyStart = file->readUint16LE();
		uint8 repetitions = file->readByte();
		debugC(1, kFreescapeDebugParser, "Frequency start: %d ", frequencyStart);
		debugC(1, kFreescapeDebugParser, "Repetitions: %d ", repetitions);

		uint8 frequencyStepsNumber = 0;
		uint16 frequencyStep = 0;

		file->seek(offsetTable + iVar);
		uint8 lastIndex = file->readByte();
		debugC(1, kFreescapeDebugParser, "0x%x %d (lastIndex)", offsetTable - 0x200, lastIndex);

		frequencyStepsNumber = file->readByte();
		debugC(1, kFreescapeDebugParser, "0x%x %d (frequency steps)", offsetTable + 1 - 0x200, frequencyStepsNumber);

		int basePtr = offsetTable + iVar + 1;
		debugC(1, kFreescapeDebugParser, "0x%x (basePtr)", basePtr - 0x200);

		frequencyStep = file->readUint16LE();
		debugC(1, kFreescapeDebugParser, "0x%x %d (steps number)", offsetTable + 2 - 0x200, (int16)frequencyStep);

		uint8 frequencyDuration = file->readByte();
		debugC(1, kFreescapeDebugParser, "0x%x %d (frequency duration)", offsetTable + 4 - 0x200, frequencyDuration);

		soundSpeakerFx *speakerFxInfo = new soundSpeakerFx();
		_soundsSpeakerFx[i] = speakerFxInfo;

		speakerFxInfo->frequencyStart = frequencyStart;
		speakerFxInfo->repetitions = repetitions;
		speakerFxInfo->frequencyStepsNumber = frequencyStepsNumber;
		speakerFxInfo->frequencyStep = frequencyStep;
		speakerFxInfo->frequencyDuration = frequencyDuration;

		for (int j = 1; j < lastIndex; j++) {

			soundSpeakerFx *speakerFxInfoAdditionalStep = new soundSpeakerFx();
			speakerFxInfoAdditionalStep->frequencyStart = 0;
			speakerFxInfoAdditionalStep->repetitions = 0;

			file->seek(basePtr + 4 * j);
			debugC(1, kFreescapeDebugParser, "Reading at %x", basePtr + 4 * j - 0x200);
			frequencyStepsNumber = file->readByte();
			debugC(1, kFreescapeDebugParser, "%d (steps number)", frequencyStepsNumber);
			frequencyStep = file->readUint16LE();
			debugC(1, kFreescapeDebugParser, "%d (frequency step)", (int16)frequencyStep);
			frequencyDuration = file->readByte();
			debugC(1, kFreescapeDebugParser, "%d (frequency duration)", frequencyDuration);

			speakerFxInfoAdditionalStep->frequencyStepsNumber = frequencyStepsNumber;
			speakerFxInfoAdditionalStep->frequencyStep = frequencyStep;
			speakerFxInfoAdditionalStep->frequencyDuration = frequencyDuration;
			speakerFxInfo->additionalSteps.push_back(speakerFxInfoAdditionalStep);
		}
		debugC(1, kFreescapeDebugParser, "\n");
	}
}

void FreescapeEngine::playSound(int index, bool sync, Audio::SoundHandle &handle) {
	if (index < 0) {
		debugC(1, kFreescapeDebugMedia, "Sound not specified");
		return;
	}

	if (_syncSound)
		waitForSounds();

	_syncSound = sync;

	debugC(1, kFreescapeDebugMedia, "Playing sound %d with sync: %d", index, sync);
	if (isAmiga() || isAtariST()) {
		playSoundFx(index, sync);
		return;
	}

	if (isDOS()) {
		soundSpeakerFx *speakerFxInfo = _soundsSpeakerFx[index];
		if (speakerFxInfo)
			playSoundDOS(speakerFxInfo, sync, handle);
		else
			debugC(1, kFreescapeDebugMedia, "WARNING: Sound %d is not available", index);

		return;
	} else if (isSpectrum()) {
		if (isDriller())
			playSoundDrillerZX(index, handle);
		else
			playSoundZX(_soundsSpeakerFxZX[index], handle);
		return;
	} else if (isCPC()) {
		playSoundCPC(index, handle);
		return;
	}

	Common::Path filename;
	filename = Common::String::format("%s-%d.wav", _targetName.c_str(), index);
	debugC(1,  kFreescapeDebugMedia, "Playing sound %s", filename.toString().c_str());
	playWav(filename);
	_syncSound = sync;
}
void FreescapeEngine::playWav(const Common::Path &filename) {

	Common::SeekableReadStream *s = _dataBundle->createReadStreamForMember(filename);
	if (!s) {
		debugC(1, kFreescapeDebugMedia, "WARNING: Sound %s not found", filename.toString().c_str());
		return;
	}
	Audio::AudioStream *stream = Audio::makeWAVStream(s, DisposeAfterUse::YES);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &_soundFxHandle, stream);
}

void FreescapeEngine::playMusic(const Common::Path &filename) {
	Audio::SeekableAudioStream *stream = nullptr;
	stream = Audio::SeekableAudioStream::openStreamFile(filename);
	if (stream) {
		_mixer->stopHandle(_musicHandle);
		Audio::LoopingAudioStream *loop = new Audio::LoopingAudioStream(stream, 0);
		_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle, loop);
	}
}

void FreescapeEngine::playSoundFx(int index, bool sync) {
	if (_soundsFx.size() == 0) {
		debugC(1, kFreescapeDebugMedia, "WARNING: Sounds are not loaded");
		return;
	}

	if (index < 0 || index >= int(_soundsFx.size())) {
		debugC(1, kFreescapeDebugMedia, "WARNING: Sound %d not available", index);
		return;
	}

	int size = _soundsFx[index]->size;
	int sampleRate = _soundsFx[index]->sampleRate;
	int repetitions = _soundsFx[index]->repetitions;
	byte *data = _soundsFx[index]->data;

	if (size > 4) {
		Audio::SeekableAudioStream *s = Audio::makeRawStream(data, size, sampleRate, Audio::FLAG_16BITS, DisposeAfterUse::NO);
		Audio::AudioStream *stream = new Audio::LoopingAudioStream(s, repetitions);
		_mixer->playStream(Audio::Mixer::kSFXSoundType, &_soundFxHandle, stream);
	} else
		debugC(1, kFreescapeDebugMedia, "WARNING: Sound %d is empty", index);
}

void FreescapeEngine::stopAllSounds(Audio::SoundHandle &handle) {
	debugC(1, kFreescapeDebugMedia, "Stopping sound");
	_mixer->stopHandle(handle);
}

void FreescapeEngine::waitForSounds() {
	if (_usePrerecordedSounds || isAmiga() || isAtariST() || isCPC())
		while (_mixer->isSoundHandleActive(_soundFxHandle))
			waitInLoop(10);
	else {
		while (!_speaker->endOfStream())
			waitInLoop(10);
	}
}

bool FreescapeEngine::isPlayingSound() {
	if (_usePrerecordedSounds || isAmiga() || isAtariST() || isCPC())
		return _mixer->isSoundHandleActive(_soundFxHandle);

	return (!_speaker->endOfStream());
}

void FreescapeEngine::playSilence(int duration, bool sync) {
	_speaker->playQueue(Audio::PCSpeaker::kWaveFormSilence, 0, 1000 * 10 * duration);
	_mixer->stopHandle(_soundFxHandle);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &_soundFxHandle, _speaker, -1, Audio::Mixer::kMaxChannelVolume, 0, DisposeAfterUse::NO);
}

void FreescapeEngine::queueSoundConst(double hzFreq, int duration) {
	_speaker->playQueue(Audio::PCSpeaker::kWaveFormSquare, hzFreq, 1000 * 10 * duration);
}

uint16 FreescapeEngine::playSoundDOSSpeaker(uint16 frequencyStart, soundSpeakerFx *speakerFxInfo) {
	uint8 frequencyStepsNumber = speakerFxInfo->frequencyStepsNumber;
	int16 frequencyStep = speakerFxInfo->frequencyStep;
	uint8 frequencyDuration = speakerFxInfo->frequencyDuration;

	int16 freq = frequencyStart;
	int waveDurationMultipler = 1800;
	int waveDuration = waveDurationMultipler * (frequencyDuration + 1);

	while (true) {
		if (freq > 0) {
			float hzFreq = 1193180.0 / freq;
			debugC(1, kFreescapeDebugMedia, "raw %d, hz: %f, duration: %d", freq, hzFreq, waveDuration);
			_speaker->playQueue(Audio::PCSpeaker::kWaveFormSquare, hzFreq, waveDuration);
		}
		if (frequencyStepsNumber > 0) {
			// Ascending initial portions of cycle
			freq += frequencyStep;
			frequencyStepsNumber--;
		} else
			break;
	}

	return freq;
}

void FreescapeEngine::playSoundZX(Common::Array<soundUnitZX> *data, Audio::SoundHandle &handle) {
	for (auto &it : *data) {
		soundUnitZX value = it;

		if (value.isRaw) {
			debugC(1, kFreescapeDebugMedia, "raw hz: %f, duration: %d", value.rawFreq, value.rawLengthus);
			if (value.rawFreq == 0) {
				_speaker->playQueue(Audio::PCSpeaker::kWaveFormSilence, 1, 5 * value.rawLengthus);
				continue;
			}
			_speaker->playQueue(Audio::PCSpeaker::kWaveFormSquare, value.rawFreq, 5 * value.rawLengthus);
		} else {
			if (value.freqTimesSeconds == 0 && value.tStates == 0) {
				_speaker->playQueue(Audio::PCSpeaker::kWaveFormSilence, 1, 1000 * value.multiplier);
				continue;
			}

			float hzFreq = 1 / ((value.tStates + 30.125) / 437500.0);
			float waveDuration = value.freqTimesSeconds / hzFreq;
			waveDuration = value.multiplier * 1000 * (waveDuration + 1);
			debugC(1, kFreescapeDebugMedia, "non raw hz: %f, duration: %f", hzFreq, waveDuration);
			_speaker->playQueue(Audio::PCSpeaker::kWaveFormSquare, hzFreq, waveDuration);
		}
	}

	_mixer->stopHandle(_soundFxHandle);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, _speaker, -1, kFreescapeDefaultVolume, 0, DisposeAfterUse::NO);
}

void FreescapeEngine::playSoundDOS(soundSpeakerFx *speakerFxInfo, bool sync, Audio::SoundHandle &handle) {
	uint freq = speakerFxInfo->frequencyStart;

	for (int i = 0; i < speakerFxInfo->repetitions; i++) {
		freq = playSoundDOSSpeaker(freq, speakerFxInfo);

		for (auto &it : speakerFxInfo->additionalSteps) {
			assert(it);
			freq = playSoundDOSSpeaker(freq, it);
		}
	}

	_mixer->stopHandle(handle);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, _speaker, -1, kFreescapeDefaultVolume / 2, 0, DisposeAfterUse::NO);
}

void FreescapeEngine::loadSoundsFx(Common::SeekableReadStream *file, int offset, int number) {
	file->seek(offset);
	soundFx *sound = nullptr;
	_soundsFx[0] = sound;
	for (int i = 1; i < number + 1; i++) {
		sound = (soundFx *)malloc(sizeof(soundFx));
		int zero = file->readUint16BE();
		assert(zero == 0);
		int size = file->readUint16BE();
		float sampleRate = float(file->readUint16BE()) / 2;
		debugC(1, kFreescapeDebugParser, "Loading sound: %d (size: %d, sample rate: %f) at %" PRIx64, i, size, sampleRate, file->pos());
		byte *data = (byte *)malloc(size * sizeof(byte));
		file->read(data, size);
		sound->sampleRate = sampleRate;
		sound->size = size;
		sound->data = (byte *)data;
		sound->repetitions = 1;
		_soundsFx[i] = sound;
	}
}

/**
 * CPC Sound Implementation (shared by Driller, Dark Side, and other Freescape CPC games)
 *
 * Based on reverse engineering of DRILL.BIN and DARKCODE.BIN (both load at 0x1C62).
 * The sound engine is identical across games; only table contents and sizes differ.
 *
 * All sounds use the sub_4760h system:
 *   - Sound initialization loads 7-byte entry from the sound definition table
 *   - Volume envelope from "Tone" Table
 *   - Pitch sweep from "Envelope" Table
 *   - 300Hz interrupt-driven update
 *
 * AY-3-8912 PSG with 1MHz clock:
 *   Port 0xF4 = register select, Port 0xF6 = data
 *
 * ---- Sound Definition Table ----
 * N entries, 7 bytes each. Loaded with 1-based sound number.
 *   Byte 0: flags
 *     - Bits 0-1: channel number (1=A, 2=B, 3=C)
 *     - Bit 2: tone disable (0 = enable tone, 1 = disable)
 *     - Bit 3: noise disable (0 = enable noise, 1 = disable)
 *   Byte 1: "tone" table index (volume envelope)
 *   Byte 2: "envelope" table index (pitch sweep)
 *   Bytes 3-4: initial AY tone period (little-endian, 12-bit)
 *   Byte 5: initial AY volume (0-15)
 *   Byte 6: duration (repeat count; 0 = single play)
 *
 * ---- "Tone" Table - Volume Envelope ----
 * Despite the name, this table controls VOLUME modulation, not pitch.
 * Indexed by 4-byte stride: base = index * 4.
 *   Byte 0: number of triplets (N)
 *   Then N triplets of 3 bytes each:
 *     Byte 0: counter - how many times to apply the delta
 *     Byte 1: delta (signed) - added to volume each step
 *     Byte 2: limit - ticks between each application
 *
 * ---- "Envelope" Table - Pitch Sweep ----
 * Despite the name, this table controls PITCH modulation, not envelope.
 * Indexed by 4-byte stride: base = index * 4.
 *   Byte 0: number of triplets (N)
 *   Then N triplets of 3 bytes each:
 *     Byte 0: counter - how many times to apply the delta
 *     Byte 1: delta (signed) - added to period each step
 *     Byte 2: limit - ticks between each application
 */

class CPCSfxStream : public Audio::AudioStream {
public:
	CPCSfxStream(int index, const byte *soundDefTable, int soundDefTableSize,
	             const byte *toneTable, const byte *envelopeTable, int rate = 44100)
		: _ay(rate, 1000000), _rate(rate),
		  _soundDefTable(soundDefTable), _soundDefTableSize(soundDefTableSize),
		  _toneTable(toneTable), _envelopeTable(envelopeTable) {
		_finished = false;
		_tickSampleCount = 0;

		// Reset all AY registers to match CPC init state
		for (int r = 0; r < 14; r++)
			_ay.setReg(r, 0);
		// Noise period from CPC init table (verified in binary)
		_ay.setReg(6, 0x07);

		memset(&_ch, 0, sizeof(_ch));
		setupSound(index);
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		if (_finished)
			return 0;

		int samplesGenerated = 0;
		// AY8912Stream is stereo: readBuffer counts int16 values (2 per frame).
		// CPC interrupts fire at 300Hz (6 per frame). The update routine is called
		// unconditionally at every interrupt, NOT inside the 50Hz divider.
		int samplesPerTick = (_rate / 300) * 2;

		while (samplesGenerated < numSamples && !_finished) {
			// Generate samples until next tick
			int remaining = samplesPerTick - _tickSampleCount;
			int toGenerate = MIN(numSamples - samplesGenerated, remaining);

			if (toGenerate > 0) {
				_ay.readBuffer(buffer + samplesGenerated, toGenerate);
				samplesGenerated += toGenerate;
				_tickSampleCount += toGenerate;
			}

			// Run interrupt handler at 300Hz tick boundary
			if (_tickSampleCount >= samplesPerTick) {
				_tickSampleCount -= samplesPerTick;
				tickUpdate();
			}
		}

		return samplesGenerated;
	}

	bool isStereo() const override { return true; }
	bool endOfData() const override { return _finished; }
	bool endOfStream() const override { return _finished; }
	int getRate() const override { return _rate; }

private:
	Audio::AY8912Stream _ay;
	int _rate;
	bool _finished;
	int _tickSampleCount; // Samples generated in current tick

	// Pointers to table data loaded from game binary (owned by FreescapeEngine)
	const byte *_soundDefTable;
	int _soundDefTableSize;      // Size in bytes (numSounds * 7)
	const byte *_toneTable;      // Volume envelope data
	const byte *_envelopeTable;  // Pitch sweep data

	/**
	 * Channel state - mirrors the 23-byte per-channel structure
	 * as populated by the init routine and updated at 300Hz.
	 *
	 * "vol" fields come from the "tone" table - controls volume envelope
	 * "pitch" fields come from the "envelope" table - controls pitch sweep
	 */
	struct ChannelState {
		// Volume modulation (from "tone" table)
		byte volCounter;        // ix+000h: initial counter value
		int8 volDelta;          // ix+001h: signed delta added to volume
		byte volLimit;          // ix+002h: initial limit value
		byte volCounterCur;     // ix+003h: current counter (decremented)
		byte volLimitCur;       // ix+004h: current limit countdown
		byte volume;            // ix+005h: current AY volume (0-15)
		byte volTripletTotal;   // ix+006h: total number of volume triplets
		byte volCurrentStep;    // ix+007h: current triplet index
		byte duration;          // ix+008h: repeat count
		byte volToneIdx;        // tone table index (to recompute data pointer)

		// Pitch modulation (from "envelope" table)
		byte pitchCounter;      // ix+00Bh: initial counter value
		int8 pitchDelta;        // ix+00Ch: signed delta added to period
		byte pitchLimit;        // ix+00Dh: initial limit value
		byte pitchCounterCur;   // ix+00Eh: current counter (decremented)
		byte pitchLimitCur;     // ix+00Fh: current limit countdown
		uint16 period;          // ix+010h-011h: current 16-bit AY tone period
		byte pitchTripletTotal; // ix+012h: total number of pitch triplets
		byte pitchCurrentStep;  // ix+013h: current triplet index
		byte pitchEnvIdx;       // envelope table index (to recompute data pointer)

		byte finishedFlag;      // ix+016h: set when volume envelope exhausted

		// AY register mapping for this channel
		byte channelNum;        // 1=A, 2=B, 3=C
		byte toneRegLo;         // AY register for tone fine
		byte toneRegHi;         // AY register for tone coarse
		byte volReg;            // AY register for volume
		bool active;             // Channel is producing sound
	} _ch;

	void writeReg(int reg, byte val) {
		_ay.setReg(reg, val);
	}

	void setupSound(int index) {
		int maxSounds = _soundDefTableSize / 7;
		if (index >= 1 && index <= maxSounds) {
			setupSub4760h(index);
		} else {
			_finished = true;
		}
	}

	/**
	 * Sound initialization - loads 7-byte entry and configures AY registers.
	 */
	void setupSub4760h(int soundNum) {
		int maxSounds = _soundDefTableSize / 7;
		if (soundNum < 1 || soundNum > maxSounds) {
			_finished = true;
			return;
		}

		const byte *entry = &_soundDefTable[(soundNum - 1) * 7];
		byte flags = entry[0];
		byte toneIdx = entry[1];
		byte envIdx = entry[2];
		uint16 period = entry[3] | (entry[4] << 8);
		byte volume = entry[5];
		byte duration = entry[6];

		// Channel number (1-based): 1=A, 2=B, 3=C
		byte channelNum = flags & 0x03;
		if (channelNum < 1 || channelNum > 3) {
			_finished = true;
			return;
		}

		// AY register mapping
		_ch.channelNum = channelNum;
		_ch.toneRegLo = (channelNum - 1) * 2;       // A=0, B=2, C=4
		_ch.toneRegHi = (channelNum - 1) * 2 + 1;   // A=1, B=3, C=5
		_ch.volReg = channelNum + 7;                 // A=8, B=9, C=10

		// Configure mixer (register 7)
		// Start with all disabled (0xFF), selectively enable per flags
		// Bit 2 set in flags = DISABLE tone, Bit 3 set = DISABLE noise
		byte mixer = 0xFF;
		if (!(flags & 0x04))
			mixer &= ~(1 << (channelNum - 1));        // Enable tone
		if (!(flags & 0x08))
			mixer &= ~(1 << (channelNum - 1 + 3));    // Enable noise
		writeReg(7, mixer);

		// Set AY tone period from entry[3-4]
		_ch.period = period;
		writeReg(_ch.toneRegLo, period & 0xFF);
		writeReg(_ch.toneRegHi, period >> 8);

		// Set AY volume from entry[5]
		_ch.volume = volume;
		writeReg(_ch.volReg, volume);

		// Duration from entry[6]
		_ch.duration = duration;

		// Load volume envelope from "tone" table
		// index * 4 stride, byte[0]=triplet_count, then {counter, delta, limit}
		int toneBase = toneIdx * 4;
		_ch.volTripletTotal = _toneTable[toneBase];
		_ch.volCurrentStep = 0;
		_ch.volToneIdx = toneIdx;

		// Load first volume triplet
		int volOff = toneBase + 1;
		_ch.volCounter = _toneTable[volOff];
		_ch.volDelta = static_cast<int8>(_toneTable[volOff + 1]);
		_ch.volLimit = _toneTable[volOff + 2];
		_ch.volCounterCur = _ch.volCounter;
		_ch.volLimitCur = _ch.volLimit;

		// Load pitch sweep from "envelope" table
		// index * 4 stride, byte[0]=triplet_count, then {counter, delta, limit}
		int envBase = envIdx * 4;
		_ch.pitchTripletTotal = _envelopeTable[envBase];
		_ch.pitchCurrentStep = 0;
		_ch.pitchEnvIdx = envIdx;

		// Load first pitch triplet
		int pitchOff = envBase + 1;
		_ch.pitchCounter = _envelopeTable[pitchOff];
		_ch.pitchDelta = static_cast<int8>(_envelopeTable[pitchOff + 1]);
		_ch.pitchLimit = _envelopeTable[pitchOff + 2];
		_ch.pitchCounterCur = _ch.pitchCounter;
		_ch.pitchLimitCur = _ch.pitchLimit;

		_ch.finishedFlag = 0;
		_ch.active = true;

		debugC(1, kFreescapeDebugMedia, "CPC sound init: sound %d ch=%d mixer=0x%02x period=%d vol=%d dur=%d tone[%d] env[%d]",
			soundNum, channelNum, mixer, period, volume, duration, toneIdx, envIdx);
		debugC(1, kFreescapeDebugMedia, "  vol envelope: triplets=%d counter=%d delta=%d limit=%d",
			_ch.volTripletTotal, _ch.volCounter, _ch.volDelta, _ch.volLimit);
		debugC(1, kFreescapeDebugMedia, "  pitch sweep:  triplets=%d counter=%d delta=%d limit=%d",
			_ch.pitchTripletTotal, _ch.pitchCounter, _ch.pitchDelta, _ch.pitchLimit);
	}

	/**
	 * 300Hz interrupt-driven update. Updates pitch first, then volume.
	 */
	void tickUpdate() {
		if (!_ch.active) {
			_finished = true;
			return;
		}

		const byte *toneRaw = _toneTable;
		const byte *envRaw = _envelopeTable;

		// === PITCH UPDATE ===
		_ch.pitchLimitCur--;
		if (_ch.pitchLimitCur == 0) {
			// Reload limit countdown
			_ch.pitchLimitCur = _ch.pitchLimit;

			// period += sign_extend(pitchDelta) with natural 16-bit wrapping
			_ch.period += static_cast<int8>(_ch.pitchDelta);

			// Write period to AY tone registers (AY masks coarse to 4 bits)
			writeReg(_ch.toneRegLo, _ch.period & 0xFF);
			writeReg(_ch.toneRegHi, _ch.period >> 8);

			// Decrement pitch counter
			_ch.pitchCounterCur--;
			if (_ch.pitchCounterCur == 0) {
				// Advance to next pitch triplet
				_ch.pitchCurrentStep++;
				if (_ch.pitchCurrentStep >= _ch.pitchTripletTotal) {
					// All pitch triplets exhausted -> check duration
					_ch.duration--;
					if (_ch.duration == 0) {
						// SHUTDOWN: silence and deactivate
						writeReg(_ch.volReg, 0);
						_ch.active = false;
						_finished = true;
						return;
					}
					// Duration > 0: restart BOTH volume and pitch from beginning

					// Reload first volume triplet (from tone table)
					int volOff = _ch.volToneIdx * 4 + 1;
					_ch.volCounter = toneRaw[volOff];
					_ch.volDelta = static_cast<int8>(toneRaw[volOff + 1]);
					_ch.volLimit = toneRaw[volOff + 2];
					_ch.volCounterCur = _ch.volCounter;
					_ch.volLimitCur = _ch.volLimit;

					// Reset both position indices and done flag
					_ch.volCurrentStep = 0;
					_ch.pitchCurrentStep = 0;
					_ch.finishedFlag = 0;

					// Reload first pitch triplet (from envelope table)
					int off = _ch.pitchEnvIdx * 4 + 1;
					_ch.pitchCounter = envRaw[off];
					_ch.pitchDelta = static_cast<int8>(envRaw[off + 1]);
					_ch.pitchLimit = envRaw[off + 2];
					_ch.pitchCounterCur = _ch.pitchCounter;
					_ch.pitchLimitCur = _ch.pitchLimit;
				} else {
					// Load next pitch triplet
					int off = _ch.pitchEnvIdx * 4 + 1 + _ch.pitchCurrentStep * 3;
					_ch.pitchCounter = envRaw[off];
					_ch.pitchDelta = static_cast<int8>(envRaw[off + 1]);
					_ch.pitchLimit = envRaw[off + 2];
					_ch.pitchCounterCur = _ch.pitchCounter;
					_ch.pitchLimitCur = _ch.pitchLimit;
				}
			}
		}

		// === VOLUME UPDATE ===
		if (!_ch.finishedFlag) {
			_ch.volLimitCur--;
			if (_ch.volLimitCur == 0) {
				// Reload limit countdown
				_ch.volLimitCur = _ch.volLimit;

				// volume = (volume + volDelta) & 0x0F
				_ch.volume = (_ch.volume + _ch.volDelta) & 0x0F;
				writeReg(_ch.volReg, _ch.volume);

				// Decrement volume counter
				_ch.volCounterCur--;
				if (_ch.volCounterCur == 0) {
					// Advance to next volume triplet
					_ch.volCurrentStep++;
					if (_ch.volCurrentStep >= _ch.volTripletTotal) {
						// All volume triplets exhausted -> set finished flag
						// NOTE: Does NOT shutdown channel - pitch continues
						_ch.finishedFlag = 1;
					} else {
						// Load next volume triplet
						int off = _ch.volToneIdx * 4 + 1 + _ch.volCurrentStep * 3;
						_ch.volCounter = toneRaw[off];
						_ch.volDelta = static_cast<int8>(toneRaw[off + 1]);
						_ch.volLimit = toneRaw[off + 2];
						_ch.volCounterCur = _ch.volCounter;
						_ch.volLimitCur = _ch.volLimit;
					}
				}
			}
		}
	}
};

void FreescapeEngine::playSoundCPC(int index, Audio::SoundHandle &handle) {
	if (_soundsCPCSoundDefTable.empty()) {
		debugC(1, kFreescapeDebugMedia, "CPC sound tables not loaded");
		return;
	}
	debugC(1, kFreescapeDebugMedia, "Playing CPC sound %d", index);
	CPCSfxStream *stream = new CPCSfxStream(index,
		_soundsCPCSoundDefTable.data(), _soundsCPCSoundDefTable.size(),
		_soundsCPCToneTable.data(), _soundsCPCEnvelopeTable.data());
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream, -1, kFreescapeDefaultVolume, 0, DisposeAfterUse::YES);
}

void FreescapeEngine::playSoundDrillerZX(int index, Audio::SoundHandle &handle) {
	debugC(1, kFreescapeDebugMedia, "Playing Driller ZX sound %d", index);
	Common::Array<soundUnitZX> soundUnits;

	auto addTone = [&](uint16 hl, uint16 de, float multiplier) {
		soundUnitZX s;
		s.isRaw = false;
		s.tStates = hl; // HL determines period
		s.freqTimesSeconds = de; // DE determines duration (number of cycles)
		s.multiplier = multiplier;
		soundUnits.push_back(s);
	};

	// Linear Sweep: Period increases -> Pitch decreases
	auto addSweep = [&](uint16 startHl, uint16 endHl, uint16 step, uint16 duration) {
		for (uint16 hl = startHl; hl < endHl; hl += step) {
			addTone(hl, duration, 10.0f);
		}
	};

	// Zap effect: Decreasing Period (E decrements) -> Pitch increases
	auto addZap = [&](uint16 startE, uint16 endE, uint16 duration) {
		for (uint16 e = startE; e > endE; e--) {
			// Map E (delay loops) to HL (tStates)
			// Small E -> Short Period -> High Freq
			uint16 hl = (24 + e) * 4;
			addTone(hl, duration, 10.0f);
		}
	};

	// Sweep Down: Increasing Period (E increments) -> Pitch decreases
	auto addSweepDown = [&](uint16 startE, uint16 endE, uint16 step, uint16 duration, float multiplier) {
		for (uint16 e = startE; e < endE; e += step) {
			uint16 hl = (24 + e) * 4;
			addTone(hl, duration, multiplier);
		}
	};

	switch (index) {
	case 1: // Shoot (FUN_95A1 -> 95AF)
		// Laser: High Pitch -> Low Pitch
		// Adjusted pitch to be even lower (0x200-0x600 is approx 850Hz-280Hz)
		addSweepDown(0x200, 0x600, 20, 1, 2.0f);
		break;
	case 2: // Collide/Bump (FUN_95DE)
		// Low tone sequence
		addTone(0x93c, 0x40, 10.0f); // 64 cycles ~340ms
		addTone(0x7a6, 0x30, 10.0f); // 48 cycles
		break;
	case 3: // Step (FUN_95E5)
		// Short blip
		// Increased duration significantly again (0xC0 = 192 cycles)
		addTone(0x7a6, 0xC0, 10.0f);
		break;
	case 4: // Silence (FUN_95F7)
		break;
	case 5: // Area Change? (FUN_95F8)
		addTone(0x1f0, 0x60, 10.0f); // High pitch, longer
		break;
	case 6: // Menu (Silence?) (FUN_9601)
		break;
	case 7: // Hit? (Sweep FUN_9605)
		// Sweep down (Period increases)
		addSweep(0x200, 0xC00, 64, 2);
		break;
	case 8: // Zap (FUN_961F)
		// Zap: Low -> High
		addZap(0xFF, 0x10, 2);
		break;
	case 9: // Sweep (FUN_9673)
		addSweep(0x100, 0x600, 16, 4);
		break;
	case 10: // Area Change (FUN_9696)
		addSweep(0x100, 0x500, 16, 4);
		break;
	case 11: // Explosion (FUN_96B9)
		{
			soundUnitZX s;
			s.isRaw = true;
			s.rawFreq = 0.0f; // Noise
			s.rawLengthus = 100000; // 100ms noise
			soundUnits.push_back(s);
		}
		break;
	case 12: // Sweep Down (FUN_96E4)
		addSweepDown(0x01, 0xFF, 1, 2, 10.0f);
		break;
	case 13: // Fall? (FUN_96FD)
		addSweep(300, 800, 16, 2);
		break;
	default:
		debugC(1, kFreescapeDebugMedia, "Unknown Driller ZX sound %d", index);
		break;
	}

	playSoundZX(&soundUnits, handle);
}


} // namespace Freescape
