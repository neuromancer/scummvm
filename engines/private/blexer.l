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

%option noyywrap
%option noinput
%option never-interactive

%option outfile="engines/private/blexer.cpp"
%option prefix="PRIVATEB_"

%{
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "private/private.h"
#include "private/grammar.h"
#include "private/btokens.h"

using namespace Private;
using namespace Gen;
using namespace Settings;

%}

%%
\004J                   return END_SECTION;
\.\005                  return START_SET_NO_CODE;
\002\x7f\x7f            return SEP_SET_NO_CODE;
\001\006                return END_SET_NO_CODE;
I\003                   return DEBUGTOK;
L\003                   return DEFINESETTOK;
O\003                   return SETTINGSTOKS;
P.|P\n                  PRIVATEB_lval.s = (char*) calloc(sizeof(char),3) ; memcpy(PRIVATEB_lval.s, PRIVATEB_text, 2) ; return SNAME;
\n                      return NL;
\x7f                    return NUL;
.                       {
    debug("Unrecognized character: 0x%2x at linenumber %d\n", *yytext, yylineno );
    //exit(1);
}
%%

namespace Private {

int bparse(char *code) {
    g_setts->init();
    YY_BUFFER_STATE bp;
    yy_delete_buffer(YY_CURRENT_BUFFER);
    bp = yy_scan_string(code);
    yy_switch_to_buffer(bp);
    PRIVATEB_parse();
    yy_delete_buffer(bp);
    return 0;
}

} // End of namespace Private
