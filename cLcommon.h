/*
 * cLcommon.h
 * Copyright (C) 2016, Kylone
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef CLCOMMON_H_
#define CLCOMMON_H_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_CLWIN32
#define snprintf sprintf_s
#define cLlocaltime(t, s) localtime_s(s, t)
#define getpid _getpid
#define getppid _getpid
#define atoll _atoi64
#define unlink _unlink
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#define fopen(x, y, z) fopen_s(&x, y, z)
#else
#define cLlocaltime(t, s) localtime_r(t, s)
#define fopen(x, y, z) x = fopen(y,z)
#endif

#include <stdint.h>

#ifdef __GNUC__
#  ifndef const
#    define const __const
#  endif
#  ifndef signed
#    define signed __signed
#  endif
#  ifndef volatile
#    define volatile __volatile
#  endif
#else
#  ifdef __STDC__
#    undef  signed
#    define signed
#    undef  volatile
#    define volatile
#  endif
#endif

namespace cL {

   enum debugcode {
      dbg_default                 = 0x0001,
      dbg_low                     = 0x0001,
      dbg_mid                     = 0x0002,
      dbg_high                    = 0x0004,
      dbg_dvb                     = 0x0008,
      dbg_all                     = 0x00ff
   };

   extern int debug;
   extern int severity;
   extern void *xmalloc(size_t sze);
   extern void *xrealloc(void *p, size_t sze);
#ifdef HAVE_CLDEBUG
   extern void xdebug(int sevr, const char *msg);
   extern void xdebugf(int sevr, const char *f, ...);
#endif

} // namespace cL

#define cLmalloc(type, sze) (type*)cL::xmalloc((size_t)((sze)*sizeof(type)))
#define cLrealloc(type, p, sze) (type*)cL::xrealloc(p, (size_t)((sze)*sizeof(type)))

#ifdef HAVE_CLDEBUG
#define cLbug(sev, msg) cL::xdebug(sev, msg)
#define cLbugf(sev, f, ...) cL::xdebugf(sev, f, __VA_ARGS__)
#else
#define cLbug(sev, msg) ;
#define cLbugf(sev, f, ...) ;
#endif

#define cLpf(fmt, tbuf, nump) \
      int nump = 0; \
      char *tbuf; \
      va_list ap, tap; \
      va_start(ap, fmt); \
      tbuf = cLmalloc(char, 1); \
      va_copy(tap, ap); \
      nump = vsnprintf(tbuf, 1, fmt, tap); \
      va_end(tap); \
      if (nump > 0) { \
         tbuf = cLrealloc(char, tbuf, nump+1); \
         va_copy(tap, ap); \
         nump = vsnprintf(tbuf, nump+1, fmt, tap); \
         va_end(tap); \
         if (nump <= 0) { \
            tbuf = cLrealloc(char, tbuf, 1); \
            nump = 0; \
            tbuf[0] = 0; \
         } \
      } else { \
         nump = 0; \
         tbuf[0] = 0; \
      } \
      va_end(ap);

#endif /* CLCOMMON_H_ */

