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

#ifndef HYPNO_TEACHER_WEAPON_H
#define HYPNO_TEACHER_WEAPON_H

#include "hypno/teacher/Parser.h"
#include "hypno/teacher/Sprite.h"
#include "common/array.h"

namespace Hypno {

class Projectile;

// ============================================================================
// Weapon - Base class for weapons
// Original: Weapon (RockThrower.h, size 0xa8)
// ============================================================================
class Weapon : public Parser {
public:
	int m_posX;          // 0x88 - X position (default 0x64)
	int m_posY;          // 0x8c - Y position (default 0xdc)
	int m_posZ;          // 0x90 - Z position (default 0xc7)
	int field_0x94;
	int m_crosshairX;    // 0x98 - crosshair X
	int m_crosshairY;    // 0x9c - crosshair Y
	int field_0xa0;      // click state
	int field_0xa4;      // weapon sound

	Weapon();
	~Weapon() override;
	virtual void OnHit();
	virtual void DrawCrosshairs();
	void DrawExplosion();
};

// ============================================================================
// Projectile - Combat projectile
// Original: Projectile (Projectile.h, size 0x108)
// Extends Sprite with velocity and collision
// ============================================================================
class Projectile : public Sprite {
public:
	int startX;        // 0xD8
	int startY;        // 0xDC
	int currentX;      // 0xE0
	int currentY;      // 0xE4
	int nextX;         // 0xE8
	int nextY;         // 0xEC
	int halfWidth;     // 0xF0
	int halfHeight;    // 0xF4
	float velocityX;   // 0xF8
	float velocityY;   // 0xFC
	int active;        // 0x100
	int field_0x104;   // 0x104

	Projectile();
	void Launch();
	void Update();
	int CheckCollision();
};

// ============================================================================
// RockThrower - Rock throwing weapon
// Original: RockThrower (RockThrower.h, size 0xb8)
// ============================================================================
class RockThrower : public Weapon {
public:
	int m_itemCount;        // 0xa8 - number of projectiles
	Projectile **m_items;   // 0xac - array of projectile pointers
	int field_0xb0;         // hit counter
	int field_0xb4;

	RockThrower();
	~RockThrower() override;

	void UpdateProjectiles();
	void DrawCrosshairs() override;
	int lblParse(const Common::String &line) override;
};

// Global projectile hit counter (original: g_ProjectileHits_0043d150)
extern int g_projectileHits;

} // End of namespace Hypno

#endif
