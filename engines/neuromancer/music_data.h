/* ScummVM - Graphic Adventure Engine
 *
 * Generated from Reuromancer/LibNeuroRoutines/asm_seg7.asm by
 * engines/neuromancer/tools/extract_asm_seg7.py. Do not edit manually.
 */

#ifndef NEUROMANCER_MUSIC_DATA_H
#define NEUROMANCER_MUSIC_DATA_H

#include "common/scummsys.h"

namespace Neuromancer {

extern const uint32 kMusicDataSize;
extern const byte   kMusicData[];

// Offsets of named symbols in kMusicData, transcribed from the MASM
// labels in asm_seg7.asm so the player can reference them by name.
enum MusicDataOffset : uint16 {
	kMd_byte_26F20 = 0x0000,
	kMd_byte_26F21 = 0x0001,
	kMd_byte_26F22 = 0x0002,
	kMd_mark_0003h = 0x0003,
	kMd_track_num = 0x0004,
	kMd_mark_0006h = 0x0006,
	kMd_mark_0008h = 0x0008,
	kMd_mark_000Ah = 0x000A,
	kMd_mark_000Ch = 0x000C,
	kMd_mark_01C5h = 0x01C5,
};

} // End of namespace Neuromancer

#endif // NEUROMANCER_MUSIC_DATA_H
