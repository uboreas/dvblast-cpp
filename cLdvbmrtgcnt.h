/*
 * cLdvbmrtgcnt.h
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
 *****************************************************************************
 * mrtg-cnt.h
 *****************************************************************************
 * Copyright Tripleplay service 2004,2005,2011
 *
 * Author:  Andy Lindsay <a.lindsay@tripleplay-services.com>
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

#ifndef CLDVB_MRTG_CNT_H_
#define CLDVB_MRTG_CNT_H_

#include <cLdvboutput.h>

class cLdvbmrtgcnt {
   private:
      FILE *mrtg_fh;
      long long l_mrtg_packets;
      long long l_mrtg_seq_err_packets;
      long long l_mrtg_error_packets;
      long long l_mrtg_scram_packets;
      // Reporting timer
#ifdef HAVE_CLWIN32
      LARGE_INTEGER mrtg_time;
      LARGE_INTEGER mrtg_inc;
#else
      struct timeval mrtg_time;
#endif
      signed char i_pid_seq[CLDVB_MRTG_PIDS];
      void dumpCounts();
   public:
      int mrtgInit(const char *mrtg_file);
      void mrtgClose();
      void mrtgAnalyse(cLdvboutput::block_t *p_ts);
      cLdvbmrtgcnt();
      ~cLdvbmrtgcnt();
};

#endif /*CLDVB_MRTG_CNT_H_*/
