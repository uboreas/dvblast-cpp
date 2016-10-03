/*
 * cLdvbasi.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * asi.c: support for Computer Modules ASI cards
 *****************************************************************************
 * Copyright (C) 2004, 2009, 2015 VideoLAN
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

#include <cLdvbev.h>
#include <cLdvbdemux.h>
#include <cLdvbasi.h>

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

namespace libcLdvbasi {

   /*****************************************************************************
    * Local declarations
    *****************************************************************************/
#define ASI_DEVICE "/dev/asirx%u"
#define ASI_TIMESTAMPS_FILE "/sys/class/asi/asirx%u/timestamps"
#define ASI_BUFSIZE_FILE "/sys/class/asi/asirx%u/bufsize"
#define ASI_LOCK_TIMEOUT 5000000 /* 5 s */

   static int i_handle;
   static struct cLev_io asi_watcher;
   static struct cLev_timer mute_watcher;
   static int i_bufsize;
   static uint8_t p_pid_filter[8192 / 8];
   static bool b_sync = false;

   /*****************************************************************************
    * Local prototypes
    *****************************************************************************/
   static void asi_Read(void *loop, void *w, int revents);
   static void asi_MuteCb(void *loop, void *w, int revents);

   /*****************************************************************************
    * Local helpers
    *****************************************************************************/
#define MAXLEN 256

   static int ReadULSysfs( const char *psz_fmt, unsigned int i_link )
   {
      char psz_file[MAXLEN], psz_data[MAXLEN];
      char *psz_tmp;
      int i_fd;
      ssize_t i_ret;
      unsigned int i_data;

      snprintf( psz_file, sizeof(psz_file), psz_fmt, i_link );
      psz_file[sizeof(psz_file) - 1] = '\0';

      if ( (i_fd = open( psz_file, O_RDONLY )) < 0 )
         return i_fd;

      i_ret = read( i_fd, psz_data, sizeof(psz_data) );
      close( i_fd );

      if ( i_ret < 0 )
         return i_ret;

      i_data = strtoul( psz_data, &psz_tmp, 0 );
      if ( *psz_tmp != '\n' )
         return -1;

      return i_data;
   }

   static ssize_t WriteULSysfs( const char *psz_fmt, unsigned int i_link,
         unsigned int i_buf )
   {
      char psz_file[MAXLEN], psz_data[MAXLEN];
      int i_fd;
      ssize_t i_ret;

      snprintf( psz_file, sizeof(psz_file), psz_fmt, i_link );
      psz_file[sizeof(psz_file) - 1] = '\0';

      snprintf( psz_data, sizeof(psz_data), "%u\n", i_buf );
      psz_file[sizeof(psz_data) - 1] = '\0';

      if ( (i_fd = open( psz_file, O_WRONLY )) < 0 )
         return i_fd;

      i_ret = write( i_fd, psz_data, strlen(psz_data) + 1 );
      close( i_fd );
      return i_ret;
   }

   /*****************************************************************************
    * asi_Open
    *****************************************************************************/
   void asi_Open( void )
   {
      char psz_dev[MAXLEN];

      /* No timestamp - we wouldn't know what to do with them */
      if ( WriteULSysfs( ASI_TIMESTAMPS_FILE, libcLdvb::i_asi_adapter, 0 ) < 0 ) {
         cLbugf(cL::dbg_dvb, "couldn't write file " ASI_TIMESTAMPS_FILE "\n", libcLdvb::i_asi_adapter);
         exit(EXIT_FAILURE);
      }

      if ( (i_bufsize = ReadULSysfs( ASI_BUFSIZE_FILE, libcLdvb::i_asi_adapter )) < 0 ) {
         cLbugf(cL::dbg_dvb, "couldn't read file " ASI_BUFSIZE_FILE "\n", libcLdvb::i_asi_adapter );
         exit(EXIT_FAILURE);
      }

      if ( i_bufsize % TS_SIZE ) {
         cLbugf(cL::dbg_dvb, ASI_BUFSIZE_FILE " must be a multiple of 188\n", libcLdvb::i_asi_adapter );
         exit(EXIT_FAILURE);
      }

      snprintf( psz_dev, sizeof(psz_dev), ASI_DEVICE, libcLdvb::i_asi_adapter );
      psz_dev[sizeof(psz_dev) - 1] = '\0';
      if ( (i_handle = open( psz_dev, O_RDONLY, 0 )) < 0 ) {
         cLbugf(cL::dbg_dvb, "couldn't open device " ASI_DEVICE " (%s)\n", libcLdvb::i_asi_adapter, strerror(errno) );
         exit(EXIT_FAILURE);
      }

#ifdef USE_ASI_HARDWARE_FILTERING
      memset( p_pid_filter, 0x0, sizeof(p_pid_filter) );
#else
      memset( p_pid_filter, 0xff, sizeof(p_pid_filter) );
#endif
      if ( ioctl( i_handle, ASI_IOC_RXSETPF, p_pid_filter ) < 0 ) {
         cLbug(cL::dbg_dvb, "couldn't filter padding\n" );
      }

      fsync( i_handle );

      cLev_io_init(&asi_watcher, asi_Read, i_handle, 1); //EV_READ
      cLev_io_start(libcLdvb::event_loop, &asi_watcher);

      cLev_timer_init(&mute_watcher, asi_MuteCb,
            ASI_LOCK_TIMEOUT / 1000000., ASI_LOCK_TIMEOUT / 1000000.);
   }

   /*****************************************************************************
    * ASI events, struct ev_io *
    *****************************************************************************/
   static void asi_Read(void *loop, void *w, int revents)
   {
      unsigned int i_val;

      if ( ioctl(i_handle, ASI_IOC_RXGETEVENTS, &i_val) == 0 )
      {
         if ( i_val & ASI_EVENT_RX_BUFFER )
            cLbug(cL::dbg_dvb, "driver receive buffer queue overrun\n" );
         if ( i_val & ASI_EVENT_RX_FIFO )
            cLbug(cL::dbg_dvb, "onboard receive FIFO overrun\n" );
         if ( i_val & ASI_EVENT_RX_CARRIER )
            cLbug(cL::dbg_dvb, "carrier status change\n" );
         if ( i_val & ASI_EVENT_RX_LOS )
            cLbug(cL::dbg_dvb, "loss of packet synchronization\n" );
         if ( i_val & ASI_EVENT_RX_AOS )
            cLbug(cL::dbg_dvb, "acquisition of packet synchronization\n" );
         if ( i_val & ASI_EVENT_RX_DATA )
            cLbug(cL::dbg_dvb, "receive data status change\n" );
      }

      struct iovec p_iov[i_bufsize / TS_SIZE];
      libcLdvboutput::block_t *p_ts, **pp_current = &p_ts;
      int i, i_len;

      for ( i = 0; i < i_bufsize / TS_SIZE; i++ )
      {
         *pp_current = libcLdvboutput::block_New();
         p_iov[i].iov_base = (*pp_current)->p_ts;
         p_iov[i].iov_len = TS_SIZE;
         pp_current = &(*pp_current)->p_next;
      }

      if ( (i_len = readv(i_handle, p_iov, i_bufsize / TS_SIZE)) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't read from device " ASI_DEVICE " (%s)\n",
               libcLdvb::i_asi_adapter, strerror(errno) );
         i_len = 0;
      }
      i_len /= TS_SIZE;

      if ( i_len ) {
         if ( !b_sync ) {
            cLbug(cL::dbg_dvb, "frontend has acquired lock\n");
            b_sync = true;
         }
         cLev_timer_again(loop, &mute_watcher);
      }

      pp_current = &p_ts;
      while ( i_len && *pp_current ) {
         pp_current = &(*pp_current)->p_next;
         i_len--;
      }

      if ( *pp_current )
         cLbug(cL::dbg_dvb, "partial buffer received\n" );
      libcLdvboutput::block_DeleteChain( *pp_current );
      *pp_current = NULL;

      libcLdvbdemux::demux_Run( p_ts );
   }

   static void asi_MuteCb(void *loop, void *w, int revents)
   {
      cLbug(cL::dbg_dvb, "frontend has lost lock\n");
      cLev_timer_stop(loop, w);
   }

   /*****************************************************************************
    * asi_SetFilter
    *****************************************************************************/
   int asi_SetFilter( uint16_t i_pid )
   {
#ifdef USE_ASI_HARDWARE_FILTERING
      p_pid_filter[ i_pid / 8 ] |= (0x01 << (i_pid % 8));
      if ( ioctl( i_handle, ASI_IOC_RXSETPF, p_pid_filter ) < 0 )
         cLbugf(cL::dbg_dvb, "couldn't add filter on PID %u\n", i_pid );

      return 1;
#else
      return -1;
#endif
   }

   /*****************************************************************************
    * asi_UnsetFilter: normally never called
    *****************************************************************************/
   void asi_UnsetFilter( int i_fd, uint16_t i_pid )
   {
#ifdef USE_ASI_HARDWARE_FILTERING
      p_pid_filter[ i_pid / 8 ] &= ~(0x01 << (i_pid % 8));
      if ( ioctl( i_handle, ASI_IOC_RXSETPF, p_pid_filter ) < 0 )
         cLbugf(cL::dbg_dvb, "couldn't remove filter on PID %u\n", i_pid );
#endif
   }

   /*****************************************************************************
    * asi_Reset
    *****************************************************************************/
   void asi_Reset( void )
   {
      cLbug(cL::dbg_dvb, "asi_Reset() do nothing\n" );
   }

} /* namespace libcLdvbasi */

