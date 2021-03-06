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
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "ags/plugins/core/core.h"

namespace AGS3 {
namespace Plugins {
namespace Core {

void EngineExports::AGS_EngineStartup(IAGSEngine *engine) {
	_audioChannel.AGS_EngineStartup(engine);
	_audioClip.AGS_EngineStartup(engine);
	_button.AGS_EngineStartup(engine);
	_character.AGS_EngineStartup(engine);
	_dateTime.AGS_EngineStartup(engine);
	_dialog.AGS_EngineStartup(engine);
	_dialogOptionsRenderingInfo.AGS_EngineStartup(engine);
	_drawingSurface.AGS_EngineStartup(engine);
	_dynamicSprite.AGS_EngineStartup(engine);
	_file.AGS_EngineStartup(engine);
	_game.AGS_EngineStartup(engine);
	_globalAPI.AGS_EngineStartup(engine);
}

} // namespace Core
} // namespace Plugins
} // namespace AGS3
