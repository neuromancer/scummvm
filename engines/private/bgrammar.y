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

// Heavily inspired by hoc
// Copyright (C) AT&T 1995
// All Rights Reserved
//
// Permission to use, copy, modify, and distribute this software and
// its documentation for any purpose and without fee is hereby
// granted, provided that the above copyright notice appear in all
// copies and that both that the copyright notice and this
// permission notice and warranty disclaimer appear in supporting
// documentation, and that the name of AT&T or any of its entities
// not be used in advertising or publicity pertaining to
// distribution of the software without specific, written prior
// permission.
//
// AT&T DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
// INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL AT&T OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
// SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
// IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
// ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
// THIS SOFTWARE.

%require "3.6"
%defines "engines/private/btokens.h"
%output "engines/private/bgrammar.cpp"
%define api.prefix {PRIVATEB_}

%{

#include "private/private.h"
#include "private/grammar.h"

#undef yyerror
#define yyerror         PRIVATEB_xerror

#define code1(c1)       code(c1);
#define code2(c1,c2)    code(c1); code(c2)
#define code3(c1,c2,c3) code(c1); code(c2); code(c3)

using namespace Private;
using namespace Gen;
using namespace Settings;

extern int PRIVATEB_lex();
extern int PRIVATEB_parse();

void PRIVATEB_xerror(const char *str) {
    debug(str);
    assert(0);
}

int PRIVATEB_wrap() {
    return 1;
}


%}

%union {
        Private::Symbol *sym; /* symbol table pointer */
        int (**inst)();       /* machine instruction */
        char *s;              /* string value */
        int *i;               /* integer value */
        int narg;             /* auxiliary value to count function arguments */
}

%token<s> SNAME
%token SETTINGSTOKS DEBUGTOK DEFINESETTOK NL NUL 

// sections
%token END_SECTION 

// Settings
%token START_SET_NO_CODE END_SET_NO_CODE SEP_SET_NO_CODE

%start sections

%%

sections: section END_SECTION sections   { debug("another section:"); }
       |  section                        { debug("last section:"); }
       ;

section:  DEBUGTOK debug               { debug("end of debug."); /* Not used in the game */ }
        | DEFINESETTOK dsettings       { debug("end of setting declarations."); }
        | SETTINGSTOKS settings        { debug("end of settings."); }
        ;

dsettings: SNAME NUL NL dsettings  { debug("define setting: %s", $SNAME); }
         | SNAME NUL               { debug("define setting: %s", $SNAME); }
         ;

debug: /* nothing */
        ;

settings: SNAME NUL NL START_SET_NO_CODE SEP_SET_NO_CODE /*END_SET_NO_CODE*/ NL settings { debug("setting %s", $SNAME); }
          |     
          ;