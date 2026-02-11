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

#include "glk/angel/screen.h"
#include "glk/conf.h"
#include "graphics/macgui/macfontmanager.h"
#include "common/file.h"

namespace Glk {
namespace Angel {

AngelScreen::AngelScreen() : Screen() {
	_macFontManager = nullptr;
}

AngelScreen::~AngelScreen() {
	for (int idx = 0; idx < (int)_fonts.size(); ++idx) {
		if (_isFontOwned[idx])
			delete _fonts[idx];
		_fonts[idx] = nullptr;
	}
	delete _macFontManager;
}

void AngelScreen::initialize() {
	_isFontOwned.resize(FONTS_TOTAL);
	for (int i = 0; i < FONTS_TOTAL; ++i)
		_isFontOwned[i] = true;

	Screen::initialize();
}

const Graphics::Font *AngelScreen::loadFont(FACES face, Common::Archive *archive, double size, double aspect, int style) {
	if (!_macFontManager)
		_macFontManager = new Graphics::MacFontManager(0, Common::UNK_LANG);

	// Retro Quality: Monaco fonts for the Angel subengine.
	const Graphics::Font *font = _macFontManager->getFont(Graphics::MacFont(Graphics::kMacFontMonaco, (int)size, style));
	if (font) {
		_isFontOwned[face] = false;
		return font;
	}

	// Fallback to base class loading if Monaco isn't found
	return Screen::loadFont(face, archive, size, aspect, style);
}

int AngelScreen::drawString(const Point &pos, int fontIdx, uint color, const Common::String &text, int spw) {
	int baseLine = (fontIdx >= PROPR) ? g_conf->_propInfo._baseLine : g_conf->_monoInfo._baseLine;
	const Graphics::Font *font = _fonts[fontIdx];
	int x = pos.x;
	uint32 lastChar = 0;

	for (uint i = 0; i < text.size(); ++i) {
		uint32 c = (byte)text[i];
		int charWidth = font->getCharWidth(c);
		int kerning = font->getKerningOffset(lastChar, c);

		x += kerning * GLI_SUBPIX;
		font->drawChar(this, c, x / GLI_SUBPIX, pos.y - baseLine, color);
		x += charWidth * GLI_SUBPIX;

		if (c == ' ')
			x += spw;
		lastChar = c;
	}

	return MIN(x, (int)w * GLI_SUBPIX);
}

int AngelScreen::drawStringUni(const Point &pos, int fontIdx, uint color, const Common::U32String &text, int spw) {
	int baseLine = (fontIdx >= PROPR) ? g_conf->_propInfo._baseLine : g_conf->_monoInfo._baseLine;
	const Graphics::Font *font = _fonts[fontIdx];
	int x = pos.x;
	uint32 lastChar = 0;

	for (uint i = 0; i < text.size(); ++i) {
		uint32 c = text[i];
		int charWidth = font->getCharWidth(c);
		int kerning = font->getKerningOffset(lastChar, c);

		x += kerning * GLI_SUBPIX;
		font->drawChar(this, c, x / GLI_SUBPIX, pos.y - baseLine, color);
		x += charWidth * GLI_SUBPIX;

		if (c == ' ')
			x += spw;
		lastChar = c;
	}

	return MIN(x, (int)w * GLI_SUBPIX);
}

size_t AngelScreen::stringWidth(int fontIdx, const Common::String &text, int spw) {
	const Graphics::Font *font = _fonts[fontIdx];
	int x = 0;
	uint32 lastChar = 0;

	for (uint i = 0; i < text.size(); ++i) {
		uint32 c = (byte)text[i];
		x += font->getKerningOffset(lastChar, c) * GLI_SUBPIX;
		x += font->getCharWidth(c) * GLI_SUBPIX;

		if (c == ' ')
			x += spw;
		lastChar = c;
	}

	return x;
}

size_t AngelScreen::stringWidthUni(int fontIdx, const Common::U32String &text, int spw) {
	const Graphics::Font *font = _fonts[fontIdx];
	int x = 0;
	uint32 lastChar = 0;

	for (uint i = 0; i < text.size(); ++i) {
		uint32 c = text[i];
		x += font->getKerningOffset(lastChar, c) * GLI_SUBPIX;
		x += font->getCharWidth(c) * GLI_SUBPIX;

		if (c == ' ')
			x += spw;
		lastChar = c;
	}

	return x;
}

} // End of namespace Angel
} // End of namespace Glk
