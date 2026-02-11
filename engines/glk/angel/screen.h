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

#ifndef GLK_ANGEL_SCREEN_H
#define GLK_ANGEL_SCREEN_H

#include "glk/screen.h"

namespace Graphics {
class MacFontManager;
}

namespace Glk {
namespace Angel {

/**
 * Angel subengine specific screen class
 */
class AngelScreen : public Screen {
public:
	AngelScreen();
	~AngelScreen() override;

	/**
	 * Initialize the screen
	 */
	void initialize() override;

	/**
	 * Draws a string using the specified font at the given co-ordinates
	 */
	int drawString(const Point &pos, int fontIdx, uint color, const Common::String &text, int spw = 0) override;

	/**
	 * Draws a unicode string using the specified font at the given co-ordinates
	 */
	int drawStringUni(const Point &pos, int fontIdx, uint color, const Common::U32String &text, int spw = 0) override;

	/**
	 * Get the width in pixels of a string
	 */
	size_t stringWidth(int fontIdx, const Common::String &text, int spw = 0) override;

	/**
	 * Get the width in pixels of a unicode string
	 */
	size_t stringWidthUni(int fontIdx, const Common::U32String &text, int spw = 0) override;

protected:
	/**
	 * Load a single font
	 */
	const Graphics::Font *loadFont(FACES face, Common::Archive *archive,
		double size, double aspect, int style) override;

private:
	Graphics::MacFontManager *_macFontManager;
	Common::Array<bool> _isFontOwned;
};

} // End of namespace Angel
} // End of namespace Glk

#endif
