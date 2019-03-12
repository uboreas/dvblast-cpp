/*
 * cLdvbcore.cpp
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
 *****************************************************************************
 * util.c
 *****************************************************************************
 * Copyright (C) 2004, 2008-2011, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Andy Gatward <a.j.gatward@reading.ac.uk>
 *          Marian Ďurkovič <md@bts.sk>
 *
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

#include <cLdvbcore.h>
#include <time.h>
#include <errno.h>

#ifdef HAVE_CLMACOS
#include <sys/time.h>
#endif
#include <unistd.h>

cLdvbobj::cLdvbobj()
{
   this->psz_native_charset = "UTF-8//IGNORE";
   this->psz_mrtg_file = (char *) 0;
   this->event_loop = (void *) 0;
   this->i_print_period = (mtime_t) 0;
   this->i_priority = -1;
   this->i_adapter = 0;
   this->i_delsysatsc = 0;
   srand((unsigned int)(time(0) * getpid()));
   cLbug(cL::dbg_high, "cLdvbobj created\n");
}

cLdvbobj::~cLdvbobj()
{
   cLbug(cL::dbg_high, "cLdvbobj deleted\n");
}

cLdvbobj::mtime_t cLdvbobj::mdate(void)
{
   mtime_t rc = 0;
#ifdef HAVE_CLOCK_NANOSLEEP
   struct timespec ts;
   /* Try to use POSIX monotonic clock if available */
   if( clock_gettime(CLOCK_MONOTONIC, &ts ) == EINVAL)
      /* Run-time fallback to real-time clock (always available) */
      (void)clock_gettime( CLOCK_REALTIME, &ts);
   rc = (mtime_t)((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000));
#else
   struct timeval tv_date;
   /* gettimeofday() could return an error, and should be tested. However, the
    * only possible error, according to 'man', is EFAULT, which can not happen
    * here, since tv is a local variable. */
   gettimeofday(&tv_date, 0);
   rc = (mtime_t)((tv_date.tv_sec * 1000000) + tv_date.tv_usec);
#endif
   return rc;
}

void cLdvbobj::msleep(mtime_t delay)
{
   struct timespec ts;
   ts.tv_sec = delay / 1000000;
   ts.tv_nsec = (delay % 1000000) * 1000;
#ifdef HAVE_CLOCK_NANOSLEEP
   int val;
   while ((val = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts)) == EINTR);
   if (val == EINVAL) {
      ts.tv_sec = delay / 1000000;
      ts.tv_nsec = (delay % 1000000) * 1000;
      while (clock_nanosleep(CLOCK_REALTIME, 0, &ts, &ts) == EINTR);
   }
#else
   while (nanosleep(&ts, &ts) && errno == EINTR);
#endif
}
