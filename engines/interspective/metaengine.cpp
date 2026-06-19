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

#include "common/system.h"
#include "common/translation.h"
#include "graphics/surface.h"

#include "interspective/detection.h"
#include "interspective/innocent.h"
#include "interspective/logic.h"

namespace Interspective {

static const ADExtraGuiOptionsMap optionsList[] = {
	{
		GAMEOPTION_SHOW_HOVER_LABELS,
		{
			_s("Show object labels"),
			_s("Shows the name of the object under the cursor next to the cursor."),
			"show_hover_labels",
			false,
			0,
			0
		}
	},
	AD_EXTRA_GUI_OPTIONS_TERMINATOR
};

class InterspectiveMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override {
		return "interspective";
	}

	const ADExtraGuiOptionsMap *getAdvancedExtraGuiOptions() const override {
		return Interspective::optionsList;
	}

	Common::Error createInstance(OSystem *syst, ::Engine **engine, const ADGameDescription *gd) const override {
		*engine = new Interspective::Engine(syst);
		return Common::kNoError;
	}

	bool hasFeature(MetaEngineFeature f) const override {
		return checkExtendedSaves(f) || f == kSupportsLoadingDuringStartup;
	}

	void getSavegameThumbnail(::Graphics::Surface &thumb) override {
		Engine *engine = static_cast<Engine *>(g_engine);
		if (engine && engine->logic() && engine->logic()->inStatusMode() && engine->statusSaveThumbnail()) {
			thumb.copyFrom(*engine->statusSaveThumbnail());
			return;
		}

		MetaEngine::getSavegameThumbnail(thumb);
	}
};

} // End of namespace Interspective

#if PLUGIN_ENABLED_DYNAMIC(INTERSPECTIVE)
REGISTER_PLUGIN_DYNAMIC(INTERSPECTIVE, PLUGIN_TYPE_ENGINE, Interspective::InterspectiveMetaEngine);
#else
REGISTER_PLUGIN_STATIC(INTERSPECTIVE, PLUGIN_TYPE_ENGINE, Interspective::InterspectiveMetaEngine);
#endif // PLUGIN_ENABLED_DYNAMIC(INTERSPECTIVE)
