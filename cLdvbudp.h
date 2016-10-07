/*
 * cLdvbudp.h
 * Copyright (C) 2016, Kylone
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * udp.c: UDP input for DVBlast
 *****************************************************************************
 * Copyright (C) 2009, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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

#ifndef CLDVBUDP_H_
#define CLDVBUDP_H_

#include <cLdvbdemux.h>

class cLdvbudp : public cLdvbdemux {

   private:
      int i_handle;
      struct cLev_io udp_watcher;
      struct cLev_timer mute_watcher;
      bool b_udp;
      int i_block_cnt;
      uint8_t pi_ssrc[4];
      uint16_t i_seqnum;
      bool b_sync;
      char *psz_udp_src;
      static void udp_Read(void *loop, void *w, int revents);
      static void udp_MuteCb(void *loop, void *w, int revents);

   protected:
#ifdef HAVE_CLDVBHW
      virtual int dev_PIDIsSelected(uint16_t i_pid) { return -1; }
      virtual void dev_ResendCAPMTs() {}
#endif
      virtual void dev_Open();
      virtual void dev_Reset();
      virtual int dev_SetFilter(uint16_t i_pid);
      virtual void dev_UnsetFilter(int i_fd, uint16_t i_pid);

   public:
      inline void setsource(char *s) {
         this->psz_udp_src = s;
      }

      cLdvbudp();
      virtual ~cLdvbudp();

};


#endif /*CLDVBUDP_H_*/
