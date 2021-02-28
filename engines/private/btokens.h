/* A Bison parser, made by GNU Bison 3.7.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2020 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_PRIVATEB_ENGINES_PRIVATE_BTOKENS_H_INCLUDED
# define YY_PRIVATEB_ENGINES_PRIVATE_BTOKENS_H_INCLUDED
/* Debug traces.  */
#ifndef PRIVATEB_DEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define PRIVATEB_DEBUG 1
#  else
#   define PRIVATEB_DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define PRIVATEB_DEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined PRIVATEB_DEBUG */
#if PRIVATEB_DEBUG
extern int PRIVATEB_debug;
#endif

/* Token kinds.  */
#ifndef PRIVATEB_TOKENTYPE
# define PRIVATEB_TOKENTYPE
  enum PRIVATEB_tokentype
  {
    PRIVATEB_EMPTY = -2,
    PRIVATEB_EOF = 0,              /* "end of file"  */
    PRIVATEB_error = 256,          /* error  */
    PRIVATEB_UNDEF = 257,          /* "invalid token"  */
    SNAME = 258,                   /* SNAME  */
    SETTINGSTOKS = 259,            /* SETTINGSTOKS  */
    DEBUGTOK = 260,                /* DEBUGTOK  */
    DEFINESETTOK = 261,            /* DEFINESETTOK  */
    NL = 262,                      /* NL  */
    NUL = 263,                     /* NUL  */
    END_SECTION = 264,             /* END_SECTION  */
    START_SET_NO_CODE = 265,       /* START_SET_NO_CODE  */
    END_SET_NO_CODE = 266,         /* END_SET_NO_CODE  */
    SEP_SET_NO_CODE = 267          /* SEP_SET_NO_CODE  */
  };
  typedef enum PRIVATEB_tokentype PRIVATEB_token_kind_t;
#endif

/* Value type.  */
#if ! defined PRIVATEB_STYPE && ! defined PRIVATEB_STYPE_IS_DECLARED
union PRIVATEB_STYPE
{
#line 82 "engines/private/bgrammar.y"

        Private::Symbol *sym; /* symbol table pointer */
        int (**inst)();       /* machine instruction */
        char *s;              /* string value */
        int *i;               /* integer value */
        int narg;             /* auxiliary value to count function arguments */

#line 92 "engines/private/btokens.h"

};
typedef union PRIVATEB_STYPE PRIVATEB_STYPE;
# define PRIVATEB_STYPE_IS_TRIVIAL 1
# define PRIVATEB_STYPE_IS_DECLARED 1
#endif


extern PRIVATEB_STYPE PRIVATEB_lval;

int PRIVATEB_parse (void);

#endif /* !YY_PRIVATEB_ENGINES_PRIVATE_BTOKENS_H_INCLUDED  */
