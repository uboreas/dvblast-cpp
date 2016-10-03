/*
 * cLdvbcore.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * dvblast.c, util.c, demux.c
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
#include <bitstream/mpeg/psi.h>
#include <time.h>
#include <errno.h>

#ifdef HAVE_CLMACOS
#include <stdarg.h>
#include <sys/time.h>
#endif

namespace libcLdvb {

   /*static*/ const char *psz_conf_file = (const char *) 0;
   /*static*/ const char *psz_dvb_charset = "UTF-8";
#ifdef HAVE_CLICONV
   /*static*/ iconv_t conf_iconv = (iconv_t)-1;
   /*static*/ iconv_t iconv_handle = (iconv_t)-1;
#endif

   const char *psz_native_charset = "UTF-8";
   void *event_loop = (void *) 0;
   int i_frequency = 0;


   uint16_t pi_newpids[CLDVB_N_MAP_PIDS];  /* pmt, audio, video, spu */
   mtime_t i_print_period = 0;

   bool b_enable_emm = false;
   bool b_enable_ecm = false;
   mtime_t i_es_timeout = 0;
   int b_budget_mode = 0;
   int b_select_pmts = 0;
   int b_any_type = 0;

   int i_adapter = 0;
   int i_fenum = 0;
   int i_canum = 0;

   mtime_t i_frontend_timeout_duration = DEFAULT_FRONTEND_TIMEOUT;
   mtime_t i_quit_timeout_duration = 0;

   int i_voltage = 13;
   int b_tone = 0;
   int i_bandwidth = 8;
   int i_inversion = -1;
   int i_srate = 27500000;
   int i_fec = 999;
   int i_rolloff = 35;
   int i_satnum = 0;
   int i_uncommitted = 0;
   char *psz_modulation = (char *) 0;

   char *psz_delsys = (char *) 0;
   int i_pilot = -1;
   int i_mis = 0;
   int i_fec_lp = 999;
   int i_guard = -1;
   int i_transmission = -1;
   int i_hierarchy = -1;

   int i_asi_adapter = 0;


   void (*pf_Open)( void ) = 0;
   void (*pf_Reset)( void ) = 0;
   int (*pf_SetFilter)( uint16_t i_pid ) = 0;
   void (*pf_UnsetFilter)( int i_fd, uint16_t i_pid ) = 0;
   void (*pf_ResendCAPMTs)(void) = 0;
   bool (*pf_PIDIsSelected)(uint16_t i_pid) = 0;

   inline bool streq(char *a, char *b)
   {
      if (!a && b)
         return false;
      if (!b && a)
         return false;
      if (a == b)
         return true;
      return strcmp(a, b) == 0 ? true : false;
   }

   inline char *xstrdup(char *str)
   {
      return str ? strdup(str) : (char *) 0;
   }

   mtime_t mdate(void)
   {
      mtime_t rc = 0;
#if defined (HAVE_CLOCK_NANOSLEEP)
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
      gettimeofday(&tv_date, (void *) 0);
      rc = (mtime_t)((tv_date.tv_sec * 1000000) + tv_date.tv_usec);
#endif
      return rc;
   }

   void msleep(mtime_t delay)
   {
      struct timespec ts;
      ts.tv_sec = delay / 1000000;
      ts.tv_nsec = (delay % 1000000) * 1000;

#if defined( HAVE_CLOCK_NANOSLEEP )
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

   void hexDump(uint8_t *p_data, uint32_t i_len)
   {
      char *p_outline = cLmalloc(char, 69);
      char *p_hrdata = cLmalloc(char, 17);
      for (uint16_t i = 0; i < i_len; i += 16) {
         sprintf(p_outline, "%03x: ", i);
         for(uint16_t j = 0; j < 16; j++) {
            if (i + j < i_len) {
               sprintf(&p_outline[5 + (3 * j)], "%02x ", p_data[i + j]);
               if (p_data[i + j] >= 32 && p_data[i + j] <= 136) {
                  sprintf( &p_hrdata[j], "%c", p_data[i + j]);
               } else {
                  sprintf( &p_hrdata[j], ".");
               }
            } else {
               sprintf(&p_outline[5 + (3 * j)], "   ");
               sprintf(&p_hrdata[j], " ");
            }
         }
         sprintf(&p_outline[53], "%16s", p_hrdata);
         cLbugf(cL::dbg_dvb, "%s\n", p_outline);
      }
      free(p_hrdata);
      free(p_outline);
   }

   /*****************************************************************************
    * psi_pack_section: return psi section
    *  Note: Allocates the return value. The caller must free it.
    *****************************************************************************/
   uint8_t *psi_pack_section(uint8_t *p_section, unsigned int *pi_size)
   {
      uint8_t *p_flat_section;
      uint16_t psi_length = psi_get_length(p_section) + PSI_HEADER_SIZE;
      *pi_size = 0;

      p_flat_section = cLmalloc(uint8_t, psi_length);
      if (!p_flat_section)
         return (uint8_t *) 0;

      *pi_size = psi_length;
      memcpy(p_flat_section, p_section, psi_length);

      return p_flat_section;
   }

   /*****************************************************************************
    * psi_pack_sections: return psi sections as array
    *  Note: Allocates the return value. The caller must free it.
    *****************************************************************************/
   uint8_t *psi_pack_sections(uint8_t **pp_sections, unsigned int *pi_size)
   {
      uint8_t i_last_section;
      uint8_t *p_flat_section;
      unsigned int i, i_pos = 0;

      if (!psi_table_validate(pp_sections))
         return (uint8_t *) 0;

      i_last_section = psi_table_get_lastsection(pp_sections);

      /* Calculate total size */
      *pi_size = 0;
      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_section = psi_table_get_section(pp_sections, i);
         *pi_size += psi_get_length(p_section) + PSI_HEADER_SIZE;
      }

      p_flat_section = cLmalloc(uint8_t, *pi_size);
      if (!p_flat_section)
         return (uint8_t *) 0;

      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_section = psi_table_get_section(pp_sections, i);
         uint16_t psi_length = psi_get_length(p_section) + PSI_HEADER_SIZE;
         memcpy(p_flat_section + i_pos, p_section, psi_length);
         i_pos += psi_length;
      }

      return p_flat_section;
   }

   /*****************************************************************************
    * psi_unpack_sections: return psi sections
    *  Note: Allocates psi_table, the result must be psi_table_free()'ed
    *****************************************************************************/
   uint8_t **psi_unpack_sections(uint8_t *p_flat_sections, unsigned int i_size)
   {
      uint8_t **pp_sections;
      unsigned int i, i_offset = 0;

      pp_sections = psi_table_allocate();
      if (!pp_sections) {
         cLbugf(cL::dbg_dvb, "%s: cannot allocate PSI table\n", __func__);
         return (uint8_t **) 0;
      }

      psi_table_init(pp_sections);

      for (i = 0; i < PSI_TABLE_MAX_SECTIONS; i++) {
         uint8_t *p_section = p_flat_sections + i_offset;
         uint16_t i_section_len = psi_get_length(p_section) + PSI_HEADER_SIZE;
         if (!psi_validate(p_section)) {
            cLbugf(cL::dbg_dvb, "%s: Invalid section %d\n", __func__, i);
            psi_table_free(pp_sections);
            return (uint8_t **) 0;
         }

         /* Must use allocated section not p_flat_section + offset directly! */
         uint8_t *p_section_local = psi_private_allocate();
         if (!p_section_local) {
            cLbugf(cL::dbg_dvb, "%s: cannot allocate PSI private\n", __func__);
            psi_table_free(pp_sections);
            return (uint8_t **) 0;
         }
         memcpy(p_section_local, p_section, i_section_len);

         /* We ignore the return value of psi_table_section(), because it is useless
           in this case. We are building the table section by section and when we have
           more than one section in a table, psi_table_section() returns false when section
           0 is added.  */
         psi_table_section(pp_sections, p_section_local);

         i_offset += i_section_len;
         if (i_offset >= i_size - 1)
            break;
      }

      return pp_sections;
   }

   static char *iconv_append_null(const char *p_string, size_t i_length)
   {
      char *psz_string = cLmalloc(char, i_length + 1);
      memcpy(psz_string, p_string, i_length);
      psz_string[i_length] = '\0';
      return psz_string;
   }

   char *str_iv(void *_unused, const char *psz_encoding, char *p_string, size_t i_length)
   {
#ifdef HAVE_CLICONV
      static const char *psz_current_encoding = "";

      char *psz_string, *p;
      size_t i_out_length;

      if (!strcmp(psz_encoding, psz_native_charset))
         return iconv_append_null(p_string, i_length);

      if (iconv_handle != (iconv_t)-1 &&
            strcmp(psz_encoding, psz_current_encoding)) {
         iconv_close(iconv_handle);
         iconv_handle = (iconv_t)-1;
      }

      if (iconv_handle == (iconv_t)-1)
         iconv_handle = iconv_open(psz_native_charset, psz_encoding);
      if (iconv_handle == (iconv_t)-1) {
         cLbugf(cL::dbg_dvb, "couldn't open converter from %s to %s\n", psz_encoding,
               psz_native_charset);
         return iconv_append_null(p_string, i_length);
      }
      psz_current_encoding = psz_encoding;

      /* converted strings can be up to six times larger */
      i_out_length = i_length * 6;
      p = psz_string = cLmalloc(char, i_out_length);
      if ((long)iconv(iconv_handle, &p_string, &i_length, &p, &i_out_length) == -1) {
         cLbugf(cL::dbg_dvb, "couldn't convert from %s to %s\n", psz_encoding,
               psz_native_charset);
         free(psz_string);
         return iconv_append_null(p_string, i_length);
      }
      if (i_length)
         cLbugf(cL::dbg_dvb, "partial conversion from %s to %s\n", psz_encoding,
               psz_native_charset);

      *p = '\0';
      return psz_string;
#else
      return iconv_append_null(p_string, i_length);
#endif
   }

   void debug_cb(void *p, const char *fmt, ...)
   {
      cLpf(fmt, tbuf, nump);
      cLbugf(cL::dbg_dvb, "%s\n", tbuf);
      ::free(tbuf);
   }

   void begin()
   {
   }

   void end()
   {
#ifdef HAVE_CLICONV
      if (iconv_handle != (iconv_t)-1) {
         iconv_close(iconv_handle);
         iconv_handle = (iconv_t)-1;
      }
      if (conf_iconv != (iconv_t)-1 ) {
         iconv_close( libcLdvb::conf_iconv );
         conf_iconv = (iconv_t)-1;
      }
#endif
   }


} /* namespace libcLdvb */
