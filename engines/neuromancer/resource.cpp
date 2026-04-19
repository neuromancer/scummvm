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
 * Derived from reverse-engineering work in the Reuromancer project
 *   https://github.com/hhrhhr/Reuromancer
 * Copyright (C) 1988, Interplay Productions
 */

#include "neuromancer/resource.h"

#include "neuromancer/decompress.h"
#include "neuromancer/detection.h"

#include "common/debug.h"
#include "common/textconsole.h"

namespace Neuromancer {

// Resource offset tables transcribed from Reuromancer/LibNeuroRoutines/resources_lists.c.
// These are absolute offsets within the retail NEURO1.DAT / NEURO2.DAT files.
// File index 0 = neuro1.dat, 1 = neuro2.dat. Terminator is file=0xFF.

const ResourceEntry kImhResources[] = {
	{ 0, "BUBBLES.IMH",  0x363F1, 0x00FD },
	{ 0, "CURSORS.IMH",  0x364EE, 0x00F3 },
	{ 0, "NEURO.IMH",    0x365E1, 0x0A8B },
	{ 0, "SPRITES.IMH",  0x3706C, 0x25C3 },
	{ 0, "TITLE.IMH",    0x395A8, 0x2BAB },
	{ 1, "AIP0.IMH",     0x1873B, 0x0334 },
	{ 1, "AIP1.IMH",     0x18A6F, 0x032A },
	{ 1, "AIP2.IMH",     0x18D99, 0x0308 },
	{ 1, "AIP3.IMH",     0x190A1, 0x02D2 },
	{ 1, "AIP4.IMH",     0x19373, 0x0335 },
	{ 1, "AIP5.IMH",     0x196A8, 0x0342 },
	{ 1, "AIP6.IMH",     0x199EA, 0x0373 },
	{ 1, "AIP7.IMH",     0x19D5D, 0x02EA },
	{ 1, "AIP8.IMH",     0x1A047, 0x0316 },
	{ 1, "AIP9.IMH",     0x1A35D, 0x02F8 },
	{ 1, "AIP10.IMH",    0x1A655, 0x03D2 },
	{ 1, "AIP11.IMH",    0x1AA27, 0x0335 },
	{ 1, "CSDB.IMH",     0x1AD5C, 0x15BD },
	{ 1, "CSPACE.IMH",   0x1C319, 0x106D },
	{ 1, "CSPANEL.IMH",  0x1D386, 0x0733 },
	{ 1, "DBSPR.IMH",    0x1DAB9, 0x026B },
	{ 1, "ENDGAME.IMH",  0x1DD24, 0x3E92 },
	{ 1, "GRIDBASE.IMH", 0x21BB6, 0x109B },
	{ 1, "GRIDS.IMH",    0x22C51, 0x6084 },
	{ 1, "ICE.IMH",      0x28CD5, 0x22E1 },
	{ 1, "SHOTS.IMH",    0x2AFB6, 0x055A },
	{ 1, "VIRUSICE.IMH", 0x2B510, 0x467F },
	{ 1, "VIRUSROT.IMH", 0x2FB8F, 0x223F },
	{ 0xFF, nullptr, 0, 0 }
};

const ResourceEntry kPicResources[] = {
	{ 0, "R1.PIC",  0x005EE, 0x1346 },
	{ 0, "R2.PIC",  0x01CE0, 0x1CB3 },
	{ 0, "R3.PIC",  0x045CA, 0x1B06 },
	{ 0, "R4.PIC",  0x0654A, 0x2646 },
	{ 0, "R5.PIC",  0x0914C, 0x13FF },
	{ 0, "R6.PIC",  0x0AD36, 0x0FDC },
	{ 0, "R7.PIC",  0x0CD55, 0x1AD2 },
	{ 0, "R8.PIC",  0x0F1CB, 0x187B },
	{ 0, "R9.PIC",  0x110A6, 0x20F8 },
	{ 0, "R10.PIC", 0x13561, 0x1051 },
	{ 0, "R11.PIC", 0x14D4E, 0x11DC },
	{ 0, "R12.PIC", 0x16BE8, 0x1A35 },
	{ 0, "R13.PIC", 0x191B2, 0x1A9F },
	{ 0, "R14.PIC", 0x1AC7E, 0x1DFE },
	{ 0, "R15.PIC", 0x1CAA9, 0x1D0B },
	{ 0, "R16.PIC", 0x1E7E1, 0x1CC0 },
	{ 0, "R17.PIC", 0x2055A, 0x1B13 },
	{ 0, "R18.PIC", 0x2209A, 0x1730 },
	{ 0, "R19.PIC", 0x23C63, 0x1336 },
	{ 0, "R20.PIC", 0x27416, 0x164C },
	{ 0, "R21.PIC", 0x28A88, 0x0A53 },
	{ 0, "R22.PIC", 0x2976E, 0x0690 },
	{ 0, "R23.PIC", 0x2A6BE, 0x1A98 },
	{ 0, "R24.PIC", 0x2C6D9, 0x1DDD },
	{ 0, "R25.PIC", 0x2F436, 0x0B59 },
	{ 0, "R26.PIC", 0x307A2, 0x1DD3 },
	{ 0, "R27.PIC", 0x338EB, 0x133E },
	{ 1, "R29.PIC", 0x33010, 0x1160 },
	{ 1, "R30.PIC", 0x342A0, 0x0CB9 },
	{ 1, "R31.PIC", 0x34F86, 0x133B },
	{ 1, "R32.PIC", 0x3694C, 0x0C29 },
	{ 1, "R33.PIC", 0x3759B, 0x0A3A },
	{ 1, "R34.PIC", 0x3846D, 0x05CF },
	{ 1, "R35.PIC", 0x38F24, 0x0A3E },
	{ 1, "R36.PIC", 0x39EEB, 0x1C8F },
	{ 1, "R37.PIC", 0x3C10D, 0x1253 },
	{ 1, "R38.PIC", 0x3D38D, 0x1570 },
	{ 1, "R39.PIC", 0x3E92A, 0x13FD },
	{ 1, "R40.PIC", 0x40259, 0x0D39 },
	{ 1, "R41.PIC", 0x41216, 0x0707 },
	{ 1, "R42.PIC", 0x41B89, 0x096D },
	{ 1, "R44.PIC", 0x42E0E, 0x12F3 },
	{ 1, "R45.PIC", 0x445A7, 0x16AA },
	{ 1, "R46.PIC", 0x468D1, 0x0AE2 },
	{ 1, "R47.PIC", 0x47462, 0x139C },
	{ 1, "R49.PIC", 0x4882B, 0x14B7 },
	{ 1, "R50.PIC", 0x4A2C9, 0x216D },
	{ 1, "R51.PIC", 0x4CBC9, 0x0AC3 },
	{ 1, "R52.PIC", 0x4DAF2, 0x2151 },
	{ 1, "R53.PIC", 0x5046E, 0x0876 },
	{ 1, "R54.PIC", 0x51455, 0x1396 },
	{ 1, "R55.PIC", 0x52811, 0x13B1 },
	{ 1, "R56.PIC", 0x53F4A, 0x1612 },
	{ 1, "R57.PIC", 0x55765, 0x15F7 },
	{ 1, "R58.PIC", 0x56E0F, 0x170C },
	{ 0xFF, nullptr, 0, 0 }
};

const ResourceEntry kBihResources[] = {
	{ 0, "R1.BIH",       0x00000, 0x05EE },
	{ 0, "R2.BIH",       0x01C19, 0x00C7 },
	{ 0, "R3.BIH",       0x03CD3, 0x087F },
	{ 0, "R4.BIH",       0x060D0, 0x047A },
	{ 0, "R5.BIH",       0x0911F, 0x002D },
	{ 0, "R6.BIH",       0x0A54B, 0x07EB },
	{ 0, "R7.BIH",       0x0CAAE, 0x02A7 },
	{ 0, "R8.BIH",       0x0E827, 0x09A4 },
	{ 0, "R9.BIH",       0x10C02, 0x04A4 },
	{ 0, "R10.BIH",      0x1319E, 0x03C3 },
	{ 0, "R11.BIH",      0x145B2, 0x079C },
	{ 0, "R12.BIH",      0x1629C, 0x094C },
	{ 0, "R13.BIH",      0x19185, 0x002D },
	{ 0, "R14.BIH",      0x1AC51, 0x002D },
	{ 0, "R15.BIH",      0x1CA7C, 0x002D },
	{ 0, "R16.BIH",      0x1E7B4, 0x002D },
	{ 0, "R17.BIH",      0x204A1, 0x00B9 },
	{ 0, "R18.BIH",      0x2206D, 0x002D },
	{ 0, "R19.BIH",      0x237CA, 0x0499 },
	{ 0, "R20.BIH",      0x26DCC, 0x064A },
	{ 0, "R21.BIH",      0x28A62, 0x0026 },
	{ 0, "R22.BIH",      0x294DB, 0x0293 },
	{ 0, "R23.BIH",      0x29EA7, 0x0817 },
	{ 0, "R24.BIH",      0x2C156, 0x0583 },
	{ 0, "R25.BIH",      0x2F052, 0x03E4 },
	{ 0, "R26.BIH",      0x2FF8F, 0x0813 },
	{ 0, "R27.BIH",      0x33100, 0x07EB },
	{ 0, "R28.BIH",      0x34E5B, 0x0412 },
	{ 0, "CORNERS.BIH",  0x3526D, 0x0021 },
	{ 0, "ROOMPOS.BIH",  0x3528E, 0x0336 },
	{ 1, "NEWS.BIH",     0x154D1, 0x146E },
	{ 1, "PAXBBS.BIH",   0x1693F, 0x0C6F },
	{ 1, "R29.BIH",      0x32BB3, 0x045D },
	{ 1, "R30.BIH",      0x3427A, 0x0026 },
	{ 1, "R31.BIH",      0x34F59, 0x002D },
	{ 1, "R32.BIH",      0x362C1, 0x068B },
	{ 1, "R33.BIH",      0x37575, 0x0026 },
	{ 1, "R34.BIH",      0x37FD5, 0x0498 },
	{ 1, "R35.BIH",      0x38E5C, 0x00C8 },
	{ 1, "R36.BIH",      0x39962, 0x0589 },
	{ 1, "R37.BIH",      0x3C0E0, 0x002D },
	{ 1, "R38.BIH",      0x3D360, 0x002D },
	{ 1, "R39.BIH",      0x3E8FD, 0x002D },
	{ 1, "R40.BIH",      0x3FD27, 0x0532 },
	{ 1, "R41.BIH",      0x40F92, 0x0284 },
	{ 1, "R42.BIH",      0x41A5B, 0x012E },
	{ 1, "R44.BIH",      0x424F6, 0x0918 },
	{ 1, "R45.BIH",      0x444CF, 0x00D8 },
	{ 1, "R46.BIH",      0x460DA, 0x07F7 },
	{ 1, "R47.BIH",      0x473B3, 0x00AF },
	{ 1, "R49.BIH",      0x487FE, 0x002D },
	{ 1, "R50.BIH",      0x49CE2, 0x05E7 },
	{ 1, "R51.BIH",      0x4CB0A, 0x00BF },
	{ 1, "R52.BIH",      0x4D68C, 0x0466 },
	{ 1, "R53.BIH",      0x4FF5E, 0x0510 },
	{ 1, "R54.BIH",      0x5142F, 0x0026 },
	{ 1, "R55.BIH",      0x527EB, 0x0026 },
	{ 1, "R56.BIH",      0x53BC2, 0x0388 },
	{ 1, "R57.BIH",      0x5555C, 0x0209 },
	{ 1, "R58.BIH",      0x56D5C, 0x00B3 },
	{ 0xFF, nullptr, 0, 0 }
};

const ResourceEntry kAnhResources[] = {
	{ 0, "R1.ANH",  0x01934, 0x02E5 },
	{ 0, "R2.ANH",  0x03993, 0x0340 },
	{ 0, "R4.ANH",  0x08B90, 0x058F },
	{ 0, "R6.ANH",  0x0BD12, 0x0D9C },
	{ 0, "R8.ANH",  0x10A46, 0x01BC },
	{ 0, "R11.ANH", 0x15F2A, 0x0372 },
	{ 0, "R12.ANH", 0x1861D, 0x0B68 },
	{ 0, "R19.ANH", 0x24F99, 0x1E33 },
	{ 0, "R22.ANH", 0x29DFE, 0x00A9 },
	{ 0, "R24.ANH", 0x2E4B6, 0x0B9C },
	{ 0, "R26.ANH", 0x32575, 0x0B8B },
	{ 0, "R27.ANH", 0x34C29, 0x0232 },
	{ 1, "R29.ANH", 0x34170, 0x010A },
	{ 1, "R34.ANH", 0x38A3C, 0x0420 },
	{ 1, "R36.ANH", 0x3BB7A, 0x0566 },
	{ 1, "R41.ANH", 0x4191D, 0x013E },
	{ 1, "R44.ANH", 0x44101, 0x03CE },
	{ 1, "R45.ANH", 0x45C51, 0x0489 },
	{ 1, "R50.ANH", 0x4C436, 0x06D4 },
	{ 1, "R52.ANH", 0x4FC43, 0x031B },
	{ 1, "R53.ANH", 0x50CE4, 0x074B },
	{ 0xFF, nullptr, 0, 0 }
};

const ResourceEntry kTxhResources[] = {
	{ 0, "FTUSER.TXH", 0x3C2E3, 0x0362 },
	{ 0xFF, nullptr, 0, 0 }
};

const ResourceEntry kSavegameResource = { 0, "SAVEGAME.SAV", 0x3C96E, 0x2EE0 };

ResourceManager::ResourceManager() : _opened(false) {}

ResourceManager::~ResourceManager() {
	close();
}

bool ResourceManager::open() {
	if (_opened)
		return true;
	if (!_neuro1.open("neuro1.dat")) {
		warning("Neuromancer: could not open neuro1.dat");
		return false;
	}
	if (!_neuro2.open("neuro2.dat")) {
		warning("Neuromancer: could not open neuro2.dat");
		_neuro1.close();
		return false;
	}
	_opened = true;
	return true;
}

void ResourceManager::close() {
	if (!_opened)
		return;
	_neuro1.close();
	_neuro2.close();
	_opened = false;
}

const ResourceEntry *ResourceManager::findEntry(const Common::String &name) const {
	struct TableRef { const ResourceEntry *table; const char *ext; };
	const TableRef tables[] = {
		{ kImhResources, ".IMH" },
		{ kPicResources, ".PIC" },
		{ kBihResources, ".BIH" },
		{ kAnhResources, ".ANH" },
		{ kTxhResources, ".TXH" },
	};

	for (const TableRef &t : tables) {
		if (!name.hasSuffix(t.ext))
			continue;
		for (const ResourceEntry *e = t.table; e->file != 0xFF; e++) {
			if (name.equalsIgnoreCase(e->name))
				return e;
		}
	}

	if (name.equalsIgnoreCase(kSavegameResource.name))
		return &kSavegameResource;

	return nullptr;
}

Common::File *ResourceManager::fileFor(const ResourceEntry *entry) {
	return entry->file == 0 ? &_neuro1 : &_neuro2;
}

uint32 ResourceManager::load(const Common::String &name, byte *dst) {
	if (!_opened && !open())
		return 0;

	const ResourceEntry *entry = findEntry(name);
	if (!entry) {
		debugC(1, kDebugResource, "Neuromancer: resource %s not found", name.c_str());
		return 0;
	}

	Common::File *f = fileFor(entry);
	// IMH/PIC entries in the DOS layout include a 32-byte preamble before
	// the Huffman payload; BIH/ANH/TXH/SAV do not.
	bool hasPreamble = name.hasSuffix(".IMH") || name.hasSuffix(".PIC");
	uint32 payloadOffset = entry->offset + (hasPreamble ? 32 : 0);

	Common::Array<byte> compressed;
	compressed.resize(entry->size);
	f->seek(payloadOffset, SEEK_SET);
	if (f->read(compressed.data(), entry->size) != entry->size) {
		warning("Neuromancer: short read on %s", name.c_str());
		return 0;
	}

	if (name.hasSuffix(".IMH"))
		return decompressIMH(compressed.data(), dst);
	if (name.hasSuffix(".PIC"))
		return decompressPIC(compressed.data(), dst);
	if (name.hasSuffix(".BIH"))
		return decompressBIH(compressed.data(), dst);
	if (name.hasSuffix(".ANH"))
		return decompressANH(compressed.data(), dst);
	if (name.hasSuffix(".TXH"))
		return decompressTXH(compressed.data(), dst);

	// SAVEGAME.SAV is raw — copy as-is.
	memcpy(dst, compressed.data(), entry->size);
	return entry->size;
}

} // End of namespace Neuromancer
