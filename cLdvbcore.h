/*
 * cLdvbcore.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * config.h, dvblast.h, util.c
 *****************************************************************************
 * Copyright (C) 2004, 2008-2011, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Andy Gatward <a.j.gatward@reading.ac.uk>
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

#ifndef CLDVB_H_
#define CLDVB_H_

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <cLcommon.h>

//#define HAVE_CLDVBHW

#define DVB_VERSION                 "3.0"
#define DVB_VERSION_MAJOR           3
#define DVB_VERSION_MINOR           0
#define DVB_VERSION_EXTRA           "cLrelease"
#define DEFAULT_PORT                3001
#define TS_SIZE                     188
#define MAX_PIDS                    8192
#define UNUSED_PID                  (MAX_PIDS + 1)
#define DEFAULT_IPV4_MTU            1500
#define DEFAULT_IPV6_MTU            1280
#define PADDING_PID                 8191
#define WATCHDOG_WAIT               10000000LL
#define WATCHDOG_REFRACTORY_PERIOD  60000000LL
#define MAX_ERRORS                  1000
#define DEFAULT_VERBOSITY           4
#define MAX_POLL_TIMEOUT            100000 /* 100 ms */
#define MIN_POLL_TIMEOUT            100 /* 100 us */
#define DEFAULT_OUTPUT_LATENCY      200000 /* 200 ms */
#define DEFAULT_MAX_RETENTION       40000 /* 40 ms */
#define MAX_EIT_RETENTION           500000 /* 500 ms */
#define DEFAULT_FRONTEND_TIMEOUT    30000000 /* 30 s */
#define EXIT_STAT_FRONTEND_TIMEOUT  100
/*
 * The problem with hardware filtering is that on startup, when you only
 * set a filter on PID 0, it can take a very long time for a large buffer
 * (typically ~100 TS packets) to fill up. And the buffer size cannot be
 * adjusted afer startup. --Meuuh
 */
//#define USE_ASI_HARDWARE_FILTERING

#define CLDVB_COMM_HEADER_SIZE      8
#define CLDVB_COMM_BUFFER_SIZE      (CLDVB_COMM_HEADER_SIZE + ((PSI_PRIVATE_MAX_SIZE + PSI_HEADER_SIZE) * (PSI_TABLE_MAX_SECTIONS / 2)))
#define CLDVB_COMM_HEADER_MAGIC     0x49
#define CLDVB_COMM_MAX_MSG_CHUNK    4096
#define CLDVB_MAX_BLOCKS            500
#define CLDVB_N_MAP_PIDS            4

#define CLDVB_OUTPUT_MAX_PACKETS    100

// Define the dump period in seconds
#define CLDVB_MRTG_INTERVAL   1
#define CLDVB_MRTG_PIDS       0x2000


#ifdef HAVE_CLLINUX
#define HAVE_CLOCK_NANOSLEEP
#endif

#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({const typeof(((type *)0)->member)*_mptr = (ptr); (type *)((char *)_mptr - offsetof(type, member));})
#endif

class cLdvbobj {
   public:
      typedef int64_t mtime_t;

   protected:
      const char *psz_native_charset;
      char *psz_mrtg_file;
      mtime_t i_print_period;
      int i_priority, i_adapter;

   public:
      void *event_loop;

      inline void set_charset(const char *s) {
         this->psz_native_charset = s;
      }
      inline void set_mrtg_file(char *s) {
         this->psz_mrtg_file = s;
      }
      inline void set_print_period(mtime_t i) {
         this->i_print_period = i;
      }
      inline void set_priority(int i) {
         this->i_priority = i;
      }
      inline void set_adapter(int a) {
         this->i_adapter = a;
      }

      static mtime_t mdate(void);
      static void msleep(mtime_t delay);
      cLdvbobj();
      ~cLdvbobj();
};

#endif /*CLDVB_H_*/

