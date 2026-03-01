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

#include "hypno/teacher/Weapon.h"
#include "hypno/teacher/Target.h"
#include "hypno/teacher/CombatEngine.h"
#include "hypno/teacher/teacher.h"
#include "hypno/hypno.h"
#include "common/system.h"
#include "graphics/surface.h"

namespace Hypno {

int g_projectileHits = 0;

// ============================================================================
// Weapon
// ============================================================================

Weapon::Weapon() {
	m_crosshairX = 0;
	m_crosshairY = 0;
	field_0x94 = 0;
	field_0xa4 = 0;
	m_posX = 0x64;
	field_0xa0 = 0;
	m_posY = 0xdc;
	m_posZ = 0xc7;
}

Weapon::~Weapon() {}

// Original: 0x415E00
void Weapon::OnHit() {
	if (g_scoreManager)
		g_scoreManager->shotsFired++;
}

// Original: Weapon::DrawCrosshairs (base implementation)
void Weapon::DrawCrosshairs() {
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	Graphics::Surface *surface = engine->_compositeSurface;
	if (!surface)
		return;

	// Draw crosshair circle at cursor position
	// Original uses SetFillColor(0xfa), DrawCircle(6)
	int cx = m_crosshairX;
	int cy = m_crosshairY;
	int r = 6;

	// Simple crosshair: horizontal and vertical lines
	byte color = 0xfa;
	if (cx >= r && cx + r < surface->w && cy >= r && cy + r < surface->h) {
		// Horizontal line
		for (int x = cx - r; x <= cx + r; x++) {
			if (x >= 0 && x < surface->w)
				*((byte *)surface->getBasePtr(x, cy)) = color;
		}
		// Vertical line
		for (int y = cy - r; y <= cy + r; y++) {
			if (y >= 0 && y < surface->h)
				*((byte *)surface->getBasePtr(cx, y)) = color;
		}
	}
}

// Original: 0x415F10
void Weapon::DrawExplosion() {
	OnHit();
	// Original draws explosion lines using SetFillColor(0xfd), DrawLine
	// Simplified: just mark hit happened
}

// ============================================================================
// Projectile
// ============================================================================

// Original: 0x4161B0
Projectile::Projectile() : Sprite() {
	startX = 0;
	startY = 0;
	currentX = 0;
	currentY = 0;
	nextX = 0;
	nextY = 0;
	halfWidth = 0;
	halfHeight = 0;
	velocityX = 0.0f;
	velocityY = 0.0f;
	active = 0;
	field_0x104 = 0;
}

// Original: 0x4162C0
void Projectile::Launch() {
	active = 1;
	setState2(0);

	int frameCount = 1;
	if (current_state >= 0 && current_state < num_states) {
		frameCount = ranges[current_state].end - ranges[current_state].start + 1;
	}
	if (frameCount <= 0) frameCount = 1;

	startX = 0xa0;  // 160
	startY = 0xb4;  // 180

	int mouseX = 0, mouseY = 0;
	if (g_combatEngine) {
		mouseX = g_combatEngine->_mouseX;
		mouseY = g_combatEngine->_mouseY;
	}

	currentX = mouseX;
	currentY = mouseY;

	// Get sprite dimensions for centering
	halfWidth = 0;
	halfHeight = 0;
	if (animation_data && animation_data->hasLastFrame()) {
		halfWidth = animation_data->_lastFrame.w / 2;
		halfHeight = animation_data->_lastFrame.h / 2;
	}

	velocityX = (float)(mouseX - 0xa0) / (float)frameCount;
	velocityY = (float)(mouseY - 0xb4) / (float)frameCount;
}

// Original: 0x4163E0
void Projectile::Update() {
	if (active == 0)
		return;

	bool isExploding = (current_state == 1);

	if (isExploding) {
		nextX = currentX;
		nextY = currentY;
	} else {
		int frameNum = 1;
		if (animation_data && animation_data->_decoder) {
			frameNum = animation_data->getCurFrame() + 1;
		}

		nextX = startX + (int)(velocityX * (float)frameNum);
		nextY = startY + (int)(velocityY * (float)frameNum);

		if (CheckCollision()) {
			currentX = nextX;
			currentY = nextY;
			setState2(1);  // Switch to explosion state
		}
	}

	if (Do(nextX - halfWidth, nextY - halfHeight, 1.0)) {
		if (isExploding) {
			active = 0;
			return;
		}
		g_projectileHits++;
		setState2(1);
	}
}

// Original: 0x416500
int Projectile::CheckCollision() {
	if (!g_targetList)
		return 0;

	// Iterate all active targets and check collision
	for (auto it = g_targetList->activeTargets.begin();
	     it != g_targetList->activeTargets.end(); ++it) {
		Target *target = it->_value;
		if (!target)
			continue;

		if (target->CheckTimeInRangeParam(nextX, nextY)) {
			target->UpdateProgress(1);
			return 1;
		}
	}
	return 0;
}

// ============================================================================
// RockThrower
// ============================================================================

// Original: 0x4165D0
RockThrower::RockThrower() {
	m_itemCount = 0;
	m_items = nullptr;
	field_0xb0 = 0;
	field_0xb4 = 0;

	m_posX = 0xa0;
	m_posY = 0xa0;
	m_posZ = 0xa6;

	m_itemCount = 3;

	// Parse weapon config from combat engine
	if (g_combatEngine)
		Parser::processFile(this, g_combatEngine, "");
}

RockThrower::~RockThrower() {
	if (m_items) {
		for (int i = 0; i < m_itemCount; i++) {
			delete m_items[i];
		}
		delete[] m_items;
		m_items = nullptr;
	}
}

// Original: 0x416880
void RockThrower::UpdateProjectiles() {
	if (g_combatEngine) {
		DrawCrosshairs();

		// Check mouse button release
		int buttonDown = g_combatEngine->_mouseButtons & 1;
		if (buttonDown == 0 && (g_combatEngine->_prevMouseButtons & 1) != 0) {
			field_0xa0 = 1;
		} else {
			field_0xa0 = 0;
		}

		if (field_0xa0 != 0) {
			OnHit();

			// Launch first inactive projectile
			if (m_itemCount > 0 && m_items) {
				for (int i = 0; i < m_itemCount; i++) {
					if (m_items[i] && m_items[i]->active == 0) {
						m_items[i]->Launch();
						break;
					}
				}
			}
		}
	}

	// Update all projectiles
	g_projectileHits = 0;
	field_0xb0 = 0;

	if (m_items) {
		for (int i = 0; i < m_itemCount; i++) {
			if (m_items[i])
				m_items[i]->Update();
		}
	}

	field_0xb0 = g_projectileHits;
}

// Original: 0x416960
void RockThrower::DrawCrosshairs() {
	TeacherEngine *engine = (TeacherEngine *)g_engine;
	Graphics::Surface *surface = engine->_compositeSurface;
	if (!surface)
		return;

	int cx = m_crosshairX;
	int cy = m_crosshairY;
	int r = 7;

	// Draw crosshair circle
	byte color = 0xfa;
	if (cx >= r && cx + r < surface->w && cy >= r && cy + r < surface->h) {
		// Horizontal line
		for (int x = cx - r; x <= cx + r; x++) {
			if (x >= 0 && x < surface->w)
				*((byte *)surface->getBasePtr(x, cy)) = color;
		}
		// Vertical line
		for (int y = cy - r; y <= cy + r; y++) {
			if (y >= 0 && y < surface->h)
				*((byte *)surface->getBasePtr(cx, y)) = color;
		}
	}
}

// Original: 0x4169A0
int RockThrower::lblParse(const Common::String &line) {
	Common::String keyword, rest;
	tokenize(line, keyword, rest);
	if (keyword.empty())
		return 0;

	if (keyword == "MAXROCKS") {
		int count = 0;
		if (sscanf(rest.c_str(), "%d", &count) == 1) {
			m_itemCount = count;
			m_items = new Projectile*[m_itemCount];
			for (int i = 0; i < m_itemCount; i++) {
				m_items[i] = new Projectile();
			}
		}
	} else if (keyword == "SPRITE") {
		saveFilePosition();
		if (m_itemCount > 0 && m_items) {
			for (int i = 0; i < m_itemCount; i++) {
				restoreFilePosition();
				Parser::processFile(m_items[i], this, "");
			}
		}
	} else if (keyword == "END") {
		return 1;
	}

	return 0;
}

} // End of namespace Hypno
