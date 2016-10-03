/*
 * cLcommon.cpp
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

#include <cLcommon.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

namespace cL {

   int debug = 0;
   int severity = 0;
   static unsigned int cLpid = ::getpid();
   static unsigned int cLppid = ::getppid();

   void *xmalloc(size_t sze)
   {
      void *v = malloc(sze);
      if (v != (void *) 0) {
         return v;
      } else {
         fprintf(stderr, "No sufficient memory: xmalloc(%d)\n", (int)sze);
         fflush(stderr);
         errno = ENOMEM;
         return (void *) 0;
      }
   }

   void *xrealloc(void *p, size_t sze)
   {
      void *v = realloc(p, sze);
      if (v != (void *) 0) {
         return v;
      } else {
         fprintf(stderr, "No sufficient memory: xrealloc(%d)\n", (int)sze);
         fflush(stderr);
         errno = ENOMEM;
         return (void *) 0;
      }
   }

   char *newtimestr()
   {
      char *s = cLmalloc(char, 10);
      time_t *t = new time_t;
      time(t);
      struct tm *xtm = new struct tm;
      cLlocaltime(t, xtm);
      snprintf(s, 10, "%02d:%02d:%02d", xtm->tm_hour, xtm->tm_min, xtm->tm_sec);
      delete(xtm);
      delete(t);
      return s;
   }

   void xbughead(char **s)
   {
      *s = cLmalloc(char, 40);
      char *t = newtimestr();
      snprintf(*s, 40, "%06u:%06u:%s ", cLppid, cLpid, t);
      ::free(t);
   }

   void xdebug(int sevr, const char *msg)
   {
      if (debug && ((sevr & severity) == sevr)) {
         if (sevr) {
            char *s = (char *) 0;
            xbughead(&s);
            fprintf(stderr, "%s", s);
            ::free(s);
         }
         fputs(msg, stderr);
      }
   }

   void xdebugf(int sevr, const char *f, ...)
   {
      if (debug && ((sevr & severity) == sevr)) {
         if (sevr) {
            char *s = (char *) 0;
            xbughead(&s);
            fprintf(stderr, "%s", s);
            ::free(s);
         }
         va_list xap;
         va_start(xap, f);
         vfprintf(stderr, f, xap);
         va_end(xap);
      }
   }

} // namespace cL

