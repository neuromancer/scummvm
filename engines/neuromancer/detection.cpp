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

#include "neuromancer/detection.h"

namespace Neuromancer {

static const DebugChannelDef debugFlagList[] = {
	{ kDebugGeneral, "general", "General debug output" },
	{ kDebugResource, "resource", "Resource loading (NEURO1/2.DAT)" },
	{ kDebugScript, "script", "Neuro-VM script execution" },
	{ kDebugLevel, "level", "Level lifecycle and handlers" },
	DEBUG_CHANNEL_END
};

static const PlainGameDescriptor neuromancerGames[] = {
	{ "neuromancer", "Neuromancer" },
	{ nullptr, nullptr }
};

const ADGameDescription gameDescriptions[] = {
	{
		"neuromancer",
		"",
		AD_ENTRY2s("neuro1.dat", "d29b149e9c8544385094042094d3c08b", 260174,
		           "neuro2.dat", "a82f19c98916fe4b76905bdd9c57e479", 361755),
		Common::EN_ANY,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	AD_TABLE_END_MARKER
};

} // End of namespace Neuromancer

class NeuromancerMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	NeuromancerMetaEngineDetection()
		: AdvancedMetaEngineDetection(Neuromancer::gameDescriptions, Neuromancer::neuromancerGames) {}

	const char *getName() const override {
		return "neuromancer";
	}

	const char *getEngineName() const override {
		return "Neuromancer";
	}

	const char *getOriginalCopyright() const override {
		return "Copyright (C) 1988 Interplay Productions";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return Neuromancer::debugFlagList;
	}
};

REGISTER_PLUGIN_STATIC(NEUROMANCER_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, NeuromancerMetaEngineDetection);
