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

#include "ags/plugins/ags_fire/ags_fire.h"
#include "ags/ags.h"

namespace AGS3 {
namespace Plugins {
namespace AGSFire {

FireObject *AGSFire::FindObject(int objIdx) {
	for (size_t i = 0; i < _fires.size(); i++) {
		if (_fires[i].objIdx == objIdx)
			return &_fires[i];
	}
	return nullptr;
}

void AGSFire::RemoveObject(int objIdx) {
	for (size_t i = 0; i < _fires.size(); i++) {
		if (_fires[i].objIdx == objIdx) {
			if (_fires[i].dynSlot > 0)
				_engine->DeleteDynamicSprite(_fires[i].dynSlot);
			_fires.remove_at(i);
			return;
		}
	}
}

const char *AGSFire::AGS_GetPluginName() {
	return "Fire Plugin (ags_fire.dll)";
}

void AGSFire::AGS_EngineStartup(IAGSEngine *engine) {
	PluginBase::AGS_EngineStartup(engine);

	SCRIPT_METHOD(FireAddObject, AGSFire::FireAddObject);
	SCRIPT_METHOD(FirePreHeat, AGSFire::FirePreHeat);
	SCRIPT_METHOD(FireDisableSeeding, AGSFire::FireDisableSeeding);
	SCRIPT_METHOD(FireEnableSeeding, AGSFire::FireEnableSeeding);
	SCRIPT_METHOD(FireSetStrength, AGSFire::FireSetStrength);
	SCRIPT_METHOD(FireRemoveObject, AGSFire::FireRemoveObject);
	SCRIPT_METHOD(FireUpdate, AGSFire::FireUpdate);
	SCRIPT_METHOD(FireStop, AGSFire::FireStop);
}

void AGSFire::FireAddObject(ScriptMethodParams &params) {
	PARAMS3(int, object, int, seedSprite, int, paletteSprite);

	// If already registered, remove old entry
	RemoveObject(object);

	// Get the object's current sprite to determine size
	AGSObject *obj = _engine->GetObject(object);
	if (!obj) {
		params._result = 0;
		return;
	}

	int spriteWidth = _engine->GetSpriteWidth(obj->num);
	int spriteHeight = _engine->GetSpriteHeight(obj->num);
	if (spriteWidth <= 0 || spriteHeight <= 0) {
		params._result = 0;
		return;
	}

	FireObject fo;
	fo.objIdx = object;
	fo.seedSlot = seedSprite;
	fo.palSlot = paletteSprite;
	fo.width = spriteWidth;
	fo.height = spriteHeight;
	fo.seeding = true;
	fo.cooling = 4;

	// Allocate heat buffer (extra border for neighbour sampling)
	int bufW = fo.width + 2;
	int bufH = fo.height + 2;
	fo.heat.resize(bufW * bufH, 0);

	// Create dynamic sprite for the fire (same bit depth as the source sprite)
	BITMAP *srcBmp = _engine->GetSpriteGraphic(obj->num);
	int bpp = srcBmp ? srcBmp->format.bytesPerPixel * 8 : 32;
	fo.dynSlot = _engine->CreateDynamicSprite(bpp, spriteWidth, spriteHeight);
	if (fo.dynSlot <= 0) {
		params._result = 0;
		return;
	}

	_fires.push_back(fo);
	params._result = 1;
}

void AGSFire::FirePreHeat(ScriptMethodParams &params) {
	PARAMS1(int, object);

	FireObject *fo = FindObject(object);
	if (!fo) {
		params._result = 0;
		return;
	}

	// Fill heat buffer with max value
	int bufW = fo->width + 2;
	int bufH = fo->height + 2;
	for (int y = 0; y < bufH; y++)
		for (int x = 0; x < bufW; x++)
			fo->heat[y * bufW + x] = 255;

	params._result = 1;
}

void AGSFire::FireDisableSeeding(ScriptMethodParams &params) {
	PARAMS1(int, object);

	FireObject *fo = FindObject(object);
	if (!fo) {
		params._result = 0;
		return;
	}
	fo->seeding = false;
	params._result = 1;
}

void AGSFire::FireEnableSeeding(ScriptMethodParams &params) {
	PARAMS1(int, object);

	FireObject *fo = FindObject(object);
	if (!fo) {
		params._result = 0;
		return;
	}
	fo->seeding = true;
	params._result = 1;
}

void AGSFire::FireSetStrength(ScriptMethodParams &params) {
	PARAMS2(int, object, int, strength);

	FireObject *fo = FindObject(object);
	if (!fo) {
		params._result = 0;
		return;
	}
	// Clamp to 0-255
	fo->cooling = MAX(0, MIN(255, strength));
	params._result = 1;
}

void AGSFire::FireRemoveObject(ScriptMethodParams &params) {
	PARAMS1(int, object);

	RemoveObject(object);
	params._result = 1;
}

static inline uint8 SampleHeat(const Common::Array<uint8> &heat, int bufW, int x, int y) {
	return heat[y * bufW + x];
}

void AGSFire::FireUpdate(ScriptMethodParams &params) {
	if (_fires.empty()) {
		params._result = 0;
		return;
	}

	for (size_t i = 0; i < _fires.size(); i++) {
		FireObject &fo = _fires[i];
		int w = fo.width;
		int h = fo.height;
		int bufW = w + 2;

		// Step 1: Propagate fire upward (cellular automaton)
		// Process from bottom-1 to top
		for (int y = h - 1; y >= 0; y--) {
			int by = y + 1; // buffer y (with 1-pixel border)
			for (int x = 0; x < w; x++) {
				int bx = x + 1; // buffer x (with 1-pixel border)

				// Average four pixels below with slight randomness
				uint8 s1 = SampleHeat(fo.heat, bufW, bx - 1, by + 1);
				uint8 s2 = SampleHeat(fo.heat, bufW, bx,     by + 1);
				uint8 s3 = SampleHeat(fo.heat, bufW, bx + 1, by + 1);
				uint8 s4 = SampleHeat(fo.heat, bufW, bx,     by + 1);

				int avg = (s1 + s2 + s3 + s4) / 4;
				avg = MAX(0, avg - fo.cooling);
				fo.heat[by * bufW + bx] = (uint8)avg;
			}
		}

		// Step 2: Seed the bottom row
		if (fo.seeding) {
			int by = h; // last row of buffer (y = h, index = h in 0..h+1)
			for (int x = 0; x < w; x++) {
				int bx = x + 1;
				// 40% chance to seed a high value, 60% chance of low flame
				int rnd = ::AGS::g_vm->getRandomNumber(0x7fffffff);
				int val = ((rnd % 100) < 40) ? (170 + (rnd % 85)) : (rnd % 60);
				fo.heat[by * bufW + bx] = (uint8)val;
			}
		}

		// Step 3: Map heat buffer to colors using palette sprite
		BITMAP *dynBmp = _engine->GetSpriteGraphic(fo.dynSlot);
		if (!dynBmp)
			continue;

		BITMAP *palBmp = _engine->GetSpriteGraphic(fo.palSlot);
		if (!palBmp) {
			// No palette: render as grayscale
			uint8 *pixels = (uint8 *)_engine->GetRawBitmapSurface(dynBmp);
			if (!pixels)
				continue;

			int pitch = _engine->GetBitmapPitch(dynBmp);
			int bpp = dynBmp->format.bytesPerPixel;
			if (bpp == 1) {
				for (int y = 0; y < h; y++) {
					for (int x = 0; x < w; x++) {
						uint8 heat = fo.heat[(y + 1) * bufW + (x + 1)];
						pixels[y * pitch + x] = heat;
					}
				}
			} else if (bpp == 4) {
				for (int y = 0; y < h; y++) {
					uint32 *row = (uint32 *)(pixels + y * pitch);
					for (int x = 0; x < w; x++) {
						uint8 heat = fo.heat[(y + 1) * bufW + (x + 1)];
						// Grayscale: heat value replicated to R,G,B
						row[x] = dynBmp->format.RGBToColor(heat, heat, heat);
					}
				}
			}
			_engine->ReleaseBitmapSurface(dynBmp);
		} else {
			// Map heat to palette sprite colors
			// The palette sprite is indexed vertically:
			// row y=0 maps to heat=0, row y=palH-1 maps to heat=255
			int palH = palBmp->h;
			int palW = palBmp->w;
			int palBpp = palBmp->format.bytesPerPixel;

			uint8 *palPixels = (uint8 *)_engine->GetRawBitmapSurface(palBmp);
			uint8 *dynPixels = (uint8 *)_engine->GetRawBitmapSurface(dynBmp);
			if (!palPixels || !dynPixels) {
				if (palPixels) _engine->ReleaseBitmapSurface(palBmp);
				if (dynPixels) _engine->ReleaseBitmapSurface(dynBmp);
				continue;
			}

			int palPitch = _engine->GetBitmapPitch(palBmp);
			int dynPitch = _engine->GetBitmapPitch(dynBmp);

			if (palBpp == 1) {
				// 8-bit palette sprite: use palette index directly
				for (int y = 0; y < h; y++) {
					uint8 *dRow = dynPixels + y * dynPitch;
					for (int x = 0; x < w; x++) {
						uint8 heat = fo.heat[(y + 1) * bufW + (x + 1)];
						int palY = CLIP<int>((heat * (palH - 1)) / 255, 0, palH - 1);
						dRow[x] = palPixels[palY * palPitch];
					}
				}
			} else if (palBpp == 4 && dynBmp->format.bytesPerPixel == 4) {
				for (int y = 0; y < h; y++) {
					uint32 *dRow = (uint32 *)(dynPixels + y * dynPitch);
					for (int x = 0; x < w; x++) {
						uint8 heat = fo.heat[(y + 1) * bufW + (x + 1)];
						int palY = CLIP<int>((heat * (palH - 1)) / 255, 0, palH - 1);
						uint32 *palRow = (uint32 *)(palPixels + palY * palPitch);
						int palX = (palW > 0) ? (x % palW) : 0;
						dRow[x] = palRow[palX];
					}
				}
			}

			_engine->ReleaseBitmapSurface(palBmp);
			_engine->ReleaseBitmapSurface(dynBmp);
		}

		// Step 4: Replace the object's sprite with the fire-rendered sprite
		AGSObject *obj = _engine->GetObject(fo.objIdx);
		if (obj) {
			obj->num = fo.dynSlot;
			_engine->NotifySpriteUpdated(fo.dynSlot);
		}
	}

	params._result = 1;
}

void AGSFire::FireStop(ScriptMethodParams &params) {
	// Remove all fire objects
	for (size_t i = 0; i < _fires.size(); i++) {
		if (_fires[i].dynSlot > 0)
			_engine->DeleteDynamicSprite(_fires[i].dynSlot);
	}
	_fires.clear();
	params._result = 1;
}

} // namespace AGSFire
} // namespace Plugins
} // namespace AGS3
