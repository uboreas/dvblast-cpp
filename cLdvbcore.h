/*
 * cLdvbcore.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * config.h, dvblast.h
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

#ifdef HAVE_CLICONV
#include <iconv.h>
#endif

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

namespace libcLdvb {

   typedef int64_t mtime_t;

   extern const char *psz_conf_file;
   extern const char *psz_dvb_charset;

#ifdef HAVE_CLICONV
   extern iconv_t conf_iconv;
   extern iconv_t iconv_handle;
#endif

   extern const char *psz_native_charset;
   /*core*/ extern void *event_loop;
   extern int i_frequency;

   extern uint16_t pi_newpids[CLDVB_N_MAP_PIDS];
   extern mtime_t i_print_period;

   extern bool b_enable_emm;
   extern bool b_enable_ecm;
   extern mtime_t i_es_timeout;
   extern int b_budget_mode;
   extern int b_select_pmts;
   extern int b_any_type;

   extern int i_adapter;
   extern int i_fenum;
   extern int i_canum;

   extern mtime_t i_frontend_timeout_duration;
   extern mtime_t i_quit_timeout_duration;

   extern int i_voltage;
   extern int b_tone;
   extern int i_bandwidth;
   extern int i_inversion;
   extern int i_srate;
   extern int i_fec;
   extern int i_rolloff;
   extern int i_satnum;
   extern int i_uncommitted;
   extern char *psz_modulation;

   extern char *psz_delsys;
   extern int i_pilot;
   extern int i_mis;
   extern int i_fec_lp;
   extern int i_guard;
   extern int i_transmission;
   extern int i_hierarchy;

   extern int i_asi_adapter;

   extern void (*pf_Open)( void );
   extern void (*pf_Reset)( void );
   extern int (*pf_SetFilter)( uint16_t i_pid );
   extern void (*pf_UnsetFilter)( int i_fd, uint16_t i_pid );
   extern void (*pf_ResendCAPMTs)(void);
   extern bool (*pf_PIDIsSelected)(uint16_t i_pid);

   extern bool streq(char *a, char *b);
   extern char *xstrdup(char *str);

   /*out*/ extern mtime_t mdate( void );
   extern void msleep( mtime_t delay );
   extern void hexDump( uint8_t *p_data, uint32_t i_len );

   extern uint8_t *psi_pack_section( uint8_t *p_sections, unsigned int *pi_size );
   extern uint8_t *psi_pack_sections( uint8_t **pp_sections, unsigned int *pi_size );
   extern uint8_t **psi_unpack_sections( uint8_t *p_flat_sections, unsigned int i_size );

   extern void debug_cb(void *p, const char *fmt, ... );
   extern char *str_iv(void *_unused, const char *psz_encoding, char *p_string, size_t i_length);

   extern void begin();
   extern void end();

} /* namespace libcLdvb */

#endif /*CLDVB_H_*/

