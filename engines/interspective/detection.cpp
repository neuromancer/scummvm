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

#include "interspective/detection.h"

#include "base/plugins.h"
#include "engines/advancedDetector.h"

namespace Interspective {

static const PlainGameDescriptor interspectiveGames[] = {
	{ "innocent", "Innocent Until Caught" },
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	{
		"innocent",
		"",
		AD_ENTRY1s("IUC_MAIN.DAT", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformDOS,
		ADGF_TESTING | ADGF_NO_FLAGS,
		GUIO0()
	},
	AD_TABLE_END_MARKER
};

} // End of namespace Interspective

class InterspectiveMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	InterspectiveMetaEngineDetection() : AdvancedMetaEngineDetection(Interspective::gameDescriptions, Interspective::interspectiveGames) {
	}

	const char *getName() const override {
		return "interspective";
	}

	const char *getEngineName() const override {
		return "Innocent Until Caught";
	}

	const char *getOriginalCopyright() const override {
		return "Copyright (c) 1993 Divide by Zero";
	}
};

REGISTER_PLUGIN_STATIC(INTERSPECTIVE_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, InterspectiveMetaEngineDetection);
