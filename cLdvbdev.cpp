/*
 * cLdvbdev.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * dvb.c: linux-dvb input for DVBlast
 *****************************************************************************
 * Copyright (C) 2008-2010, 2015 VideoLAN
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
#include <cLdvben50221.h>
#include <cLdvbdemux.h>
#include <cLdvbdev.h>

#include <unistd.h>
#include <inttypes.h>
#include <errno.h>

#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/ca.h>

#define DVBAPI_VERSION ((DVB_API_VERSION)*100+(DVB_API_VERSION_MINOR))

#if DVBAPI_VERSION < 508
#define DTV_STREAM_ID        42
#define FE_CAN_MULTISTREAM   0x4000000
#endif

#include <fcntl.h>
#include <sys/ioctl.h>

namespace libcLdvbdev {

   static int i_frontend, i_dvr;
   static struct cLev_io frontend_watcher, dvr_watcher;
   static struct cLev_timer lock_watcher, mute_watcher, print_watcher;
   static fe_status_t i_last_status;
   static libcLdvboutput::block_t *p_freelist = NULL;

   /*****************************************************************************
    * Local prototypes
    *****************************************************************************/
   static void DVRRead(void *loop, void *w, int revents);
   static void DVRMuteCb(void *loop, void *w, int revents);
   static void FrontendRead(void *loop, void *w, int revents);
   static void FrontendLockCb(void *loop, void *w, int revents);
   static void FrontendSet( bool b_reset );

   /*****************************************************************************
    * dvb_Open
    *****************************************************************************/
   void dvb_Open( void )
   {
      char psz_tmp[128];

      cLbugf(cL::dbg_dvb, "compiled with DVB API version %d.%d\n", DVB_API_VERSION, DVB_API_VERSION_MINOR );

      if ( libcLdvb::i_frequency )
      {
         sprintf( psz_tmp, "/dev/dvb/adapter%d/frontend%d", libcLdvb::i_adapter, libcLdvb::i_fenum );
         if( (i_frontend = open(psz_tmp, O_RDWR | O_NONBLOCK)) < 0 )
         {
            cLbugf(cL::dbg_dvb, "opening device %s failed (%s)\n", psz_tmp,
                  strerror(errno) );
            exit(1);
         }

         FrontendSet(true);
      }
      else
      {
         i_frontend = -1;
      }

      sprintf( psz_tmp, "/dev/dvb/adapter%d/dvr%d", libcLdvb::i_adapter, libcLdvb::i_fenum );

      if( (i_dvr = open(psz_tmp, O_RDONLY | O_NONBLOCK)) < 0 )
      {
         cLbugf(cL::dbg_dvb, "opening device %s failed (%s)\n", psz_tmp,
               strerror(errno) );
         exit(1);
      }

      if ( ioctl( i_dvr, DMX_SET_BUFFER_SIZE, DVB_DVR_BUFFER_SIZE ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't set %s buffer size (%s)\n", psz_tmp,
               strerror(errno) );
      }

      cLev_io_init(&dvr_watcher, DVRRead, i_dvr, 1); // EV_READ
      cLev_io_start(libcLdvb::event_loop, &dvr_watcher);

      if ( i_frontend != -1 )
      {
         cLev_io_init(&frontend_watcher, FrontendRead, i_frontend, 1); //EV_READ
         cLev_io_start(libcLdvb::event_loop, &frontend_watcher);
      }

      cLev_timer_init(&lock_watcher, FrontendLockCb,
            libcLdvb::i_frontend_timeout_duration / 1000000.,
            libcLdvb::i_frontend_timeout_duration / 1000000.);
      cLev_timer_init(&mute_watcher, DVRMuteCb,
            DVB_DVR_READ_TIMEOUT / 1000000.,
            DVB_DVR_READ_TIMEOUT / 1000000.);

      libcLdvben50221::en50221_Init();
   }

   /*****************************************************************************
    * dvb_Reset
    *****************************************************************************/
   void dvb_Reset( void )
   {
      if ( libcLdvb::i_frequency )
         FrontendSet(true);
   }

   /*****************************************************************************
    * DVR events, struct ev_io *
    *****************************************************************************/
   static void DVRRead(void *loop, void *w, int revents)
   {
      int i, i_len;
      libcLdvboutput::block_t *p_ts = p_freelist, **pp_current = &p_ts;
      struct iovec p_iov[DVB_MAX_READ_ONCE];

      for ( i = 0; i < DVB_MAX_READ_ONCE; i++ )
      {
         if ( (*pp_current) == NULL ) *pp_current = libcLdvboutput::block_New();
         p_iov[i].iov_base = (*pp_current)->p_ts;
         p_iov[i].iov_len = TS_SIZE;
         pp_current = &(*pp_current)->p_next;
      }

      if ( (i_len = readv(i_dvr, p_iov, DVB_MAX_READ_ONCE)) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't read from DVR device (%s)\n",
               strerror(errno) );
         i_len = 0;
      }
      i_len /= TS_SIZE;

      if ( i_len )
         cLev_timer_again(loop, &mute_watcher);

      pp_current = &p_ts;
      while ( i_len && *pp_current )
      {
         pp_current = &(*pp_current)->p_next;
         i_len--;
      }

      p_freelist = *pp_current;
      *pp_current = NULL;

      libcLdvbdemux::demux_Run( p_ts );
   }

   //struct ev_timer *
   static void DVRMuteCb(void *loop, void *w, int revents)
   {
      cLbug(cL::dbg_dvb, "no DVR output, resetting\n" );
      cLev_timer_stop(loop, w);
      if ( libcLdvb::i_frequency )
         FrontendSet(false);
      libcLdvben50221::en50221_Reset();
   }

   /*
    * Demux
    */

   /*****************************************************************************
    * dvb_SetFilter : controls the demux to add a filter
    *****************************************************************************/
   int dvb_SetFilter( uint16_t i_pid )
   {
      struct dmx_pes_filter_params s_filter_params;
      char psz_tmp[128];
      int i_fd;

      sprintf( psz_tmp, "/dev/dvb/adapter%d/demux%d", libcLdvb::i_adapter, libcLdvb::i_fenum );
      if( (i_fd = open(psz_tmp, O_RDWR)) < 0 )
      {
         cLbugf(cL::dbg_dvb, "DMXSetFilter: opening device failed (%s)\n",
               strerror(errno) );
         return -1;
      }

      s_filter_params.pid      = i_pid;
      s_filter_params.input    = DMX_IN_FRONTEND;
      s_filter_params.output   = DMX_OUT_TS_TAP;
      s_filter_params.flags    = DMX_IMMEDIATE_START;
      s_filter_params.pes_type = DMX_PES_OTHER;

      if ( ioctl( i_fd, DMX_SET_PES_FILTER, &s_filter_params ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "failed setting filter on %d (%s)\n", i_pid,
               strerror(errno) );
         close( i_fd );
         return -1;
      }

      cLbugf(cL::dbg_dvb, "setting filter on PID %d\n", i_pid );

      return i_fd;
   }

   /*****************************************************************************
    * dvb_UnsetFilter : removes a filter
    *****************************************************************************/
   void dvb_UnsetFilter( int i_fd, uint16_t i_pid )
   {
      if ( ioctl( i_fd, DMX_STOP ) < 0 )
         cLbugf(cL::dbg_dvb, "DMX_STOP failed (%s)\n", strerror(errno) );
      else
         cLbugf(cL::dbg_dvb, "unsetting filter on PID %d\n", i_pid );

      close( i_fd );
   }


   /*
    * Frontend
    */

   /*****************************************************************************
    * Print info, struct ev_timer *
    *****************************************************************************/
   static void PrintCb(void *loop, void *w, int revents )
   {
      uint32_t i_ber = 0;
      uint16_t i_strength = 0, i_snr = 0;
      uint32_t i_uncorrected = 0;

      ioctl(i_frontend, FE_READ_BER, &i_ber);
      ioctl(i_frontend, FE_READ_SIGNAL_STRENGTH, &i_strength);
      ioctl(i_frontend, FE_READ_SNR, &i_snr);
      ioctl(i_frontend, FE_READ_UNCORRECTED_BLOCKS, &i_uncorrected);

      cLbugf(cL::dbg_dvb, "frontend ber: %"PRIu32" strength: %"PRIu16" snr: %"PRIu16" uncorrected: %"PRIu32"\n", i_ber, i_strength, i_snr, i_uncorrected);
   }

   /*****************************************************************************
    * Frontend events, struct ev_io *
    *****************************************************************************/
   static void FrontendRead(void  *loop, void *w, int revents)
   {
      struct dvb_frontend_event event;
      fe_status_t i_status, i_diff;

      for( ;; )
      {
         int i_ret = ioctl( i_frontend, FE_GET_EVENT, &event );

         if( i_ret < 0 )
         {
            if( errno == EWOULDBLOCK )
               return; /* no more events */

            cLbugf(cL::dbg_dvb, "reading frontend event failed (%d) %s\n",
                  i_ret, strerror(errno) );
            return;
         }

         i_status = event.status;
         i_diff = (fe_status_t)(i_status ^ i_last_status);
         i_last_status = i_status;

         {
#define IF_UP( x )                                                          \
      }                                                                   \
if ( i_diff & (x) )                                                 \
{                                                                   \
   if ( i_status & (x) )

            IF_UP( FE_HAS_SIGNAL )
                      { cLbug(cL::dbg_dvb, "frontend has acquired signal\n" );}
            else
               {cLbug(cL::dbg_dvb, "frontend has lost signal\n" );}

            IF_UP( FE_HAS_CARRIER )
            {cLbug(cL::dbg_dvb, "frontend has acquired carrier\n" );}
            else
               {cLbug(cL::dbg_dvb, "frontend has lost carrier\n" );}

         IF_UP( FE_HAS_VITERBI )
         {cLbug(cL::dbg_dvb, "frontend has acquired stable FEC\n" );}
         else
            {cLbug(cL::dbg_dvb, "frontend has lost FEC\n" );}

         IF_UP( FE_HAS_SYNC )
         {cLbug(cL::dbg_dvb, "frontend has acquired sync\n" );}
         else
            {cLbug(cL::dbg_dvb, "frontend has lost sync\n" );}

         IF_UP( FE_HAS_LOCK )
         {
            int32_t i_value = 0;
            cLbug(cL::dbg_dvb, "frontend has acquired lock\n" );

            cLev_timer_stop(loop, &lock_watcher);
            cLev_timer_again(loop, &mute_watcher);

            /* Read some statistics */
            if( ioctl( i_frontend, FE_READ_BER, &i_value ) >= 0 )
               cLbugf(cL::dbg_dvb, "- Bit error rate: %d\n", i_value );
            if( ioctl( i_frontend, FE_READ_SIGNAL_STRENGTH, &i_value ) >= 0 )
               cLbugf(cL::dbg_dvb, "- Signal strength: %d\n", i_value );
            if( ioctl( i_frontend, FE_READ_SNR, &i_value ) >= 0 )
               cLbugf(cL::dbg_dvb, "- SNR: %d\n", i_value );

            if (libcLdvb::i_print_period)
            {
               cLev_timer_init( &print_watcher, PrintCb,
                     libcLdvb::i_print_period / 1000000.,
                     libcLdvb::i_print_period / 1000000. );
               cLev_timer_start( libcLdvb::event_loop, &print_watcher );
            }
         }
         else
         {
            cLbug(cL::dbg_dvb, "frontend has lost lock\n" );


            if (libcLdvb::i_frontend_timeout_duration)
            {
               cLev_timer_stop(libcLdvb::event_loop, &lock_watcher);
               cLev_timer_again(loop, &mute_watcher);
            }

            if (libcLdvb::i_print_period)
               cLev_timer_stop(libcLdvb::event_loop, &print_watcher);
         }

         IF_UP( FE_REINIT )
         {
            /* The frontend was reinited. */
            cLbug(cL::dbg_dvb, "reiniting frontend\n");
            if ( libcLdvb::i_frequency )
               FrontendSet(true);
         }
         }
#undef IF_UP
      }
   }

   //struct ev_timer *
   static void FrontendLockCb(void *loop, void *w, int revents)
   {
      if ( libcLdvb::i_quit_timeout_duration )
      {
         cLbug(cL::dbg_dvb, "no lock\n" );
         cLev_break(loop, 2); //EVBREAK_ALL
         return;
      }

      cLbug(cL::dbg_dvb, "no lock, tuning again\n" );
      cLev_timer_stop(loop, w);

      if ( libcLdvb::i_frequency )
         FrontendSet(false);
   }

   static int FrontendDoDiseqc(void)
   {
      fe_sec_voltage_t fe_voltage;
      fe_sec_tone_mode_t fe_tone;
      int bis_frequency;

      switch ( libcLdvb::i_voltage )
      {
         case 0: fe_voltage = SEC_VOLTAGE_OFF; break;
         default:
         case 13: fe_voltage = SEC_VOLTAGE_13; break;
         case 18: fe_voltage = SEC_VOLTAGE_18; break;
      }

      fe_tone = libcLdvb::b_tone ? SEC_TONE_ON : SEC_TONE_OFF;

      /* Automatic mode. */
      if ( libcLdvb::i_frequency >= 950000 && libcLdvb::i_frequency <= 2150000 )
      {
         cLbugf(cL::dbg_dvb, "frequency %d is in IF-band\n", libcLdvb::i_frequency );
         bis_frequency = libcLdvb::i_frequency;
      }
      else if ( libcLdvb::i_frequency >= 2500000 && libcLdvb::i_frequency <= 2700000 )
      {
         cLbugf(cL::dbg_dvb, "frequency %d is in S-band\n", libcLdvb::i_frequency );
         bis_frequency = 3650000 - libcLdvb::i_frequency;
      }
      else if ( libcLdvb::i_frequency >= 3400000 && libcLdvb::i_frequency <= 4200000 )
      {
         cLbugf(cL::dbg_dvb, "frequency %d is in C-band (lower)\n", libcLdvb::i_frequency );
         bis_frequency = 5150000 - libcLdvb::i_frequency;
      }
      else if ( libcLdvb::i_frequency >= 4500000 && libcLdvb::i_frequency <= 4800000 )
      {
         cLbugf(cL::dbg_dvb, "frequency %d is in C-band (higher)\n", libcLdvb::i_frequency );
         bis_frequency = 5950000 - libcLdvb::i_frequency;
      }
      else if ( libcLdvb::i_frequency >= 10700000 && libcLdvb::i_frequency < 11700000 )
      {
         cLbugf(cL::dbg_dvb, "frequency %d is in Ku-band (lower)\n",
               libcLdvb::i_frequency );
         bis_frequency = libcLdvb::i_frequency - 9750000;
      }
      else if ( libcLdvb::i_frequency >= 11700000 && libcLdvb::i_frequency <= 13250000 )
      {
         cLbugf(cL::dbg_dvb, "frequency %d is in Ku-band (higher)\n",
               libcLdvb::i_frequency );
         bis_frequency = libcLdvb::i_frequency - 10600000;
         fe_tone = SEC_TONE_ON;
      }
      else
      {
         cLbugf(cL::dbg_dvb, "frequency %d is out of any known band\n",
               libcLdvb::i_frequency );
         exit(1);
      }

      /* Switch off continuous tone. */
      if ( ioctl( i_frontend, FE_SET_TONE, SEC_TONE_OFF ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "FE_SET_TONE failed (%s)\n", strerror(errno) );
         exit(1);
      }

      /* Configure LNB voltage. */
      if ( ioctl( i_frontend, FE_SET_VOLTAGE, fe_voltage ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "FE_SET_VOLTAGE failed (%s)\n", strerror(errno) );
         exit(1);
      }

      /* Wait for at least 15 ms. Currently 100 ms because of broken drivers. */
      libcLdvb::msleep(100000);

      /* Diseqc */
      if ( libcLdvb::i_satnum > 0 && libcLdvb::i_satnum < 5 )
      {
         /* digital satellite equipment control,
          * specification is available from http://www.eutelsat.com/
          */

         /* DiSEqC 1.1 */
         struct dvb_diseqc_master_cmd uncmd =
         { {0xe0, 0x10, 0x39, 0xf0, 0x00, 0x00}, 4};

         /* DiSEqC 1.0 */
         struct dvb_diseqc_master_cmd cmd =
         { {0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4};

         cmd.msg[3] = 0xf0 /* reset bits */
               | ((libcLdvb::i_satnum - 1) << 2)
               | (fe_voltage == SEC_VOLTAGE_13 ? 0 : 2)
               | (fe_tone == SEC_TONE_ON ? 1 : 0);

         if ( libcLdvb::i_uncommitted > 0 && libcLdvb::i_uncommitted < 5 )
         {
            uncmd.msg[3] = 0xf0 /* reset bits */
                  | ((libcLdvb::i_uncommitted - 1) << 2)
                  | (fe_voltage == SEC_VOLTAGE_13 ? 0 : 2)
                  | (fe_tone == SEC_TONE_ON ? 1 : 0);
            if( ioctl( i_frontend, FE_DISEQC_SEND_MASTER_CMD, &uncmd ) < 0 )
            {
               cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n",
                     strerror(errno) );
               exit(1);
            }
            /* Repeat uncommitted command */
            uncmd.msg[0] = 0xe1; /* framing: master, no reply, repeated TX */
            if( ioctl( i_frontend, FE_DISEQC_SEND_MASTER_CMD, &uncmd ) < 0 )
            {
               cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n",
                     strerror(errno) );
               exit(1);
            }
            /* Pause 125 ms between uncommitted & committed diseqc commands. */
            libcLdvb::msleep(125000);
         }

         if( ioctl( i_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd ) < 0 )
         {
            cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n",
                  strerror(errno) );
            exit(1);
         }
         libcLdvb::msleep(100000); /* Should be 15 ms. */

         /* Do it again just to be sure. */
         cmd.msg[0] = 0xe1; /* framing: master, no reply, repeated TX */
         if( ioctl( i_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd ) < 0 )
         {
            cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n",
                  strerror(errno) );
            exit(1);
         }
         libcLdvb::msleep(100000); /* Again, should be 15 ms */
      }
      else if ( libcLdvb::i_satnum == 0xA || libcLdvb::i_satnum == 0xB )
      {
         /* A or B simple diseqc ("diseqc-compatible") */
         if( ioctl( i_frontend, FE_DISEQC_SEND_BURST,
               libcLdvb::i_satnum == 0xB ? SEC_MINI_B : SEC_MINI_A ) < 0 )
         {
            cLbugf(cL::dbg_dvb, "ioctl FE_SEND_BURST failed (%s)\n", strerror(errno) );
            exit(1);
         }
         libcLdvb::msleep(100000); /* ... */
      }

      if ( ioctl( i_frontend, FE_SET_TONE, fe_tone ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "FE_SET_TONE failed (%s)\n", strerror(errno) );
         exit(1);
      }

      libcLdvb::msleep(100000); /* ... */

      cLbugf(cL::dbg_dvb, "configuring LNB to v=%d p=%d satnum=%x uncommitted=%x\n", libcLdvb::i_voltage, libcLdvb::b_tone, libcLdvb::i_satnum, libcLdvb::i_uncommitted );
      return bis_frequency;
   }

   //goko

//#define DVB_API_VERSION 6
#if DVB_API_VERSION >= 5

#if DVBAPI_VERSION < 505
#warning Your linux-dvb headers are old, you should consider upgrading your kernel and/or compiling against different kernel headers
#endif

   /*****************************************************************************
    * Helper functions for S2API
    *****************************************************************************/
   static fe_spectral_inversion_t GetInversion(void)
   {
      switch ( libcLdvb::i_inversion )
      {
         case 0:  return INVERSION_OFF;
         case 1:  return INVERSION_ON;
         default:
            cLbugf(cL::dbg_dvb, "invalid inversion %d\n", libcLdvb::i_inversion );
         case -1: return INVERSION_AUTO;
      }
   }

   static fe_code_rate_t GetFEC(fe_caps_t fe_caps, int i_fec_value)
   {
#define GET_FEC_INNER(fec, val)                                             \
      if ( (fe_caps & FE_CAN_##fec) && (i_fec_value == val) )                 \
      return fec;

      GET_FEC_INNER(FEC_AUTO, 999);
      GET_FEC_INNER(FEC_AUTO, -1);
      if (i_fec_value == 0)
         return FEC_NONE;
      GET_FEC_INNER(FEC_1_2, 12);
      GET_FEC_INNER(FEC_2_3, 23);
      GET_FEC_INNER(FEC_3_4, 34);
      if (i_fec_value == 35)
         return FEC_3_5;
      GET_FEC_INNER(FEC_4_5, 45);
      GET_FEC_INNER(FEC_5_6, 56);
      GET_FEC_INNER(FEC_6_7, 67);
      GET_FEC_INNER(FEC_7_8, 78);
      GET_FEC_INNER(FEC_8_9, 89);
      if (i_fec_value == 910)
         return FEC_9_10;

#undef GET_FEC_INNER
      cLbugf(cL::dbg_dvb, "invalid FEC %d\n", i_fec_value );
      return FEC_AUTO;
   }

#define GetFECInner(caps) GetFEC(caps, libcLdvb::i_fec)
#define GetFECLP(caps) GetFEC(caps, libcLdvb::i_fec_lp)

   static fe_modulation_t GetModulation(void)
   {
#define GET_MODULATION( mod )                                               \
      if ( !strcasecmp( libcLdvb::psz_modulation, #mod ) )                              \
      return mod;

      GET_MODULATION(QPSK);
      GET_MODULATION(QAM_16);
      GET_MODULATION(QAM_32);
      GET_MODULATION(QAM_64);
      GET_MODULATION(QAM_128);
      GET_MODULATION(QAM_256);
      GET_MODULATION(QAM_AUTO);
      GET_MODULATION(VSB_8);
      GET_MODULATION(VSB_16);
      GET_MODULATION(PSK_8);
      GET_MODULATION(APSK_16);
      GET_MODULATION(APSK_32);
      GET_MODULATION(DQPSK);

#undef GET_MODULATION
      cLbugf(cL::dbg_dvb, "invalid modulation %s\n", libcLdvb::psz_modulation );
      exit(1);
   }

   static fe_pilot_t GetPilot(void)
   {
      switch ( libcLdvb::i_pilot )
      {
         case 0:  return PILOT_OFF;
         case 1:  return PILOT_ON;
         default:
            cLbugf(cL::dbg_dvb, "invalid pilot %d\n", libcLdvb::i_pilot );
         case -1: return PILOT_AUTO;
      }
   }

   static fe_rolloff_t GetRollOff(void)
   {
      switch ( libcLdvb::i_rolloff )
      {
         case -1:
         case  0: return ROLLOFF_AUTO;
         case 20: return ROLLOFF_20;
         case 25: return ROLLOFF_25;
         default:
            cLbugf(cL::dbg_dvb, "invalid rolloff %d\n", libcLdvb::i_rolloff );
         case 35: return ROLLOFF_35;
      }
   }

   static fe_guard_interval_t GetGuard(void)
   {
      switch ( libcLdvb::i_guard )
      {
         case 32: return GUARD_INTERVAL_1_32;
         case 16: return GUARD_INTERVAL_1_16;
         case  8: return GUARD_INTERVAL_1_8;
         case  4: return GUARD_INTERVAL_1_4;
         default:
            cLbugf(cL::dbg_dvb, "invalid guard interval %d\n", libcLdvb::i_guard );
         case -1:
         case  0: return GUARD_INTERVAL_AUTO;
      }
   }

   static fe_transmit_mode_t GetTransmission(void)
   {
      switch ( libcLdvb::i_transmission )
      {
         case 2: return TRANSMISSION_MODE_2K;
         case 8: return TRANSMISSION_MODE_8K;
#ifdef TRANSMISSION_MODE_4K
         case 4: return TRANSMISSION_MODE_4K;
#endif
         default:
            cLbugf(cL::dbg_dvb, "invalid tranmission mode %d\n", libcLdvb::i_transmission );
         case -1:
         case 0: return TRANSMISSION_MODE_AUTO;
      }
   }

   static fe_hierarchy_t GetHierarchy(void)
   {
      switch ( libcLdvb::i_hierarchy )
      {
         case 0: return HIERARCHY_NONE;
         case 1: return HIERARCHY_1;
         case 2: return HIERARCHY_2;
         case 4: return HIERARCHY_4;
         default:
            cLbugf(cL::dbg_dvb, "invalid intramission mode %d\n", libcLdvb::i_transmission );
         case -1: return HIERARCHY_AUTO;
      }
   }

   /*****************************************************************************
    * FrontendInfo : Print frontend info
    *****************************************************************************/
   static void FrontendInfo( struct dvb_frontend_info *info, uint32_t version,
         fe_delivery_system_t *p_systems, int i_systems )
   {
      cLbugf(cL::dbg_dvb, "using DVB API version %d.%d\n", version / 256, version % 256 );
      cLbugf(cL::dbg_dvb, "Frontend \"%s\" supports:\n", info->name );
      cLbugf(cL::dbg_dvb, " frequency min: %d, max: %d, stepsize: %d, tolerance: %d\n", info->frequency_min, info->frequency_max, info->frequency_stepsize, info->frequency_tolerance );
      cLbugf(cL::dbg_dvb, " symbolrate min: %d, max: %d, tolerance: %d\n", info->symbol_rate_min, info->symbol_rate_max, info->symbol_rate_tolerance);
      cLbug(cL::dbg_dvb, " capabilities:\n" );

#define FRONTEND_INFO(caps,val,msg)                                         \
      if ( caps & val )                                                       \
      cLbugf(cL::dbg_dvb, "  %s\n", msg );

      FRONTEND_INFO( info->caps, FE_IS_STUPID, "FE_IS_STUPID" )
      FRONTEND_INFO( info->caps, FE_CAN_INVERSION_AUTO, "INVERSION_AUTO" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_1_2, "FEC_1_2" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_2_3, "FEC_2_3" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_3_4, "FEC_3_4" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_4_5, "FEC_4_5" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_5_6, "FEC_5_6" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_6_7, "FEC_6_7" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_7_8, "FEC_7_8" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_8_9, "FEC_8_9" )
      FRONTEND_INFO( info->caps, FE_CAN_FEC_AUTO,"FEC_AUTO")
      FRONTEND_INFO( info->caps, FE_CAN_QPSK,   "QPSK" )
      FRONTEND_INFO( info->caps, FE_CAN_QAM_16, "QAM_16" )
      FRONTEND_INFO( info->caps, FE_CAN_QAM_32, "QAM_32" )
      FRONTEND_INFO( info->caps, FE_CAN_QAM_64, "QAM_64" )
      FRONTEND_INFO( info->caps, FE_CAN_QAM_128,"QAM_128")
      FRONTEND_INFO( info->caps, FE_CAN_QAM_256,"QAM_256")
      FRONTEND_INFO( info->caps, FE_CAN_QAM_AUTO,"QAM_AUTO" )
      FRONTEND_INFO( info->caps, FE_CAN_TRANSMISSION_MODE_AUTO, "TRANSMISSION_MODE_AUTO" )
      FRONTEND_INFO( info->caps, FE_CAN_BANDWIDTH_AUTO, "BANDWIDTH_AUTO" )
      FRONTEND_INFO( info->caps, FE_CAN_GUARD_INTERVAL_AUTO, "GUARD_INTERVAL_AUTO" )
      FRONTEND_INFO( info->caps, FE_CAN_HIERARCHY_AUTO, "HIERARCHY_AUTO" )
      FRONTEND_INFO( info->caps, FE_CAN_8VSB, "8VSB" )
      FRONTEND_INFO( info->caps, FE_CAN_16VSB,"16VSB" )
      FRONTEND_INFO( info->caps, FE_HAS_EXTENDED_CAPS, "EXTENDED_CAPS" )
#if DVBAPI_VERSION >= 501
      FRONTEND_INFO( info->caps, FE_CAN_2G_MODULATION, "2G_MODULATION" )
#endif
      FRONTEND_INFO( info->caps, FE_CAN_MULTISTREAM, "MULTISTREAM" )
      FRONTEND_INFO( info->caps, FE_NEEDS_BENDING, "NEEDS_BENDING" )
      FRONTEND_INFO( info->caps, FE_CAN_RECOVER, "FE_CAN_RECOVER" )
      FRONTEND_INFO( info->caps, FE_CAN_MUTE_TS, "FE_CAN_MUTE_TS" )
#undef FRONTEND_INFO

      cLbug(cL::dbg_dvb, " delivery systems:\n" );
      int i;
      for ( i = 0; i < i_systems; i++ )
      {
         switch ( p_systems[i] )
         {
#define DELSYS_INFO(delsys, msg)                                            \
      case delsys: cLbugf(cL::dbg_dvb, "  %s\n", msg); break;
            DELSYS_INFO( SYS_ATSC, "ATSC" )
              DELSYS_INFO( SYS_ATSCMH, "ATSCMH" )
              DELSYS_INFO( SYS_CMMB, "CMBB" )
              DELSYS_INFO( SYS_DAB, "DAB" )
              DELSYS_INFO( SYS_DSS, "DSS" )
              DELSYS_INFO( SYS_DVBC_ANNEX_B, "DVBC_ANNEX_B" )
              DELSYS_INFO( SYS_DVBH, "DVBH" )
              DELSYS_INFO( SYS_DVBS, "DVBS" )
              DELSYS_INFO( SYS_DVBS2, "DVBS2" )
              DELSYS_INFO( SYS_DVBT, "DVBT" )
              DELSYS_INFO( SYS_ISDBC, "ISDBC" )
              DELSYS_INFO( SYS_ISDBS, "ISDBS" )
              DELSYS_INFO( SYS_ISDBT, "ISDBT" )
              DELSYS_INFO( SYS_UNDEFINED, "UNDEFINED" )
#if DVBAPI_VERSION >= 505
              DELSYS_INFO( SYS_DVBC_ANNEX_A, "DVBC_ANNEX_A" )
              DELSYS_INFO( SYS_DVBC_ANNEX_C, "DVBC_ANNEX_C" )
              DELSYS_INFO( SYS_DVBT2, "DVBT2" )
              DELSYS_INFO( SYS_TURBO, "TURBO" )
#else
              DELSYS_INFO( SYS_DVBC_ANNEX_AC, "DVBC_ANNEX_AC" )
#endif
#if DVBAPI_VERSION >= 507
              DELSYS_INFO( SYS_DTMB, "DTMB" )
#else
              DELSYS_INFO( SYS_DMBTH, "DMBTH" )
#endif
            default: cLbugf(cL::dbg_dvb, "  Unknown delivery system %u\n", p_systems[i]);
            break;
         }
      }
   }

   /*****************************************************************************
    * FrontendSet
    *****************************************************************************/
   /* S2API */

#include <cLdvbdevc.h>

#define DELSYS 0
#define FREQUENCY 1
#define MODULATION 2
#define INVERSION 3
#define SYMBOL_RATE 4
#define BANDWIDTH 4
#define FEC_INNER 5
#define FEC_LP 6
#define GUARD 7
#define PILOT 7
#define TRANSMISSION 8
#define ROLLOFF 8
#define MIS 9
#define HIERARCHY 9

   static fe_delivery_system_t
   FrontendGuessSystem( fe_delivery_system_t *p_systems, int i_systems )
   {
      if ( libcLdvb::psz_delsys != NULL )
      {
         if ( !strcasecmp( libcLdvb::psz_delsys, "DVBS" ) )
            return SYS_DVBS;
         if ( !strcasecmp( libcLdvb::psz_delsys, "DVBS2" ) )
            return SYS_DVBS2;
         if ( !strcasecmp( libcLdvb::psz_delsys, "DVBC_ANNEX_A" ) )
#if DVBAPI_VERSION >= 505
            return SYS_DVBC_ANNEX_A;
#else
         return SYS_DVBC_ANNEX_AC;
#endif
         if ( !strcasecmp( libcLdvb::psz_delsys, "DVBC_ANNEX_B" ) )
            return SYS_DVBC_ANNEX_B;
         if ( !strcasecmp( libcLdvb::psz_delsys, "DVBT" ) )
            return SYS_DVBT;
         if ( !strcasecmp( libcLdvb::psz_delsys, "ATSC" ) )
            return SYS_ATSC;
         cLbugf(cL::dbg_dvb, "unknown delivery system %s\n", libcLdvb::psz_delsys );
         exit(1);
      }

      if ( i_systems == 1 )
         return p_systems[0];

      int i;
      for ( i = 0; i < i_systems; i++ )
      {
         switch ( p_systems[i] )
         {
            case SYS_DVBS:
               if ( libcLdvb::i_frequency < 50000000 )
                  return SYS_DVBS;
               break;
#if DVBAPI_VERSION >= 505
            case SYS_DVBC_ANNEX_A:
               if ( libcLdvb::i_frequency > 50000000 || libcLdvb::i_srate != 27500000 ||
                     libcLdvb::psz_modulation != NULL )
                  return SYS_DVBC_ANNEX_A;
               break;
#else
            case SYS_DVBC_ANNEX_AC:
               if ( libcLdvb::i_frequency > 50000000 || libcLdvb::i_srate != 27500000 ||
                     libcLdvb::psz_modulation != NULL )
                  return SYS_DVBC_ANNEX_AC;
               break;
#endif
            case SYS_DVBT:
               if ( libcLdvb::i_frequency > 50000000 )
                  return SYS_DVBT;
               break;
            default:
               break;
         }
      }

      cLbug(cL::dbg_dvb, "couldn't guess delivery system, use --delsys\n" );
      return p_systems[0];
   }

   static void FrontendSet( bool b_init )
   {
      struct dvb_frontend_info info;
      struct dtv_properties *p;
      fe_delivery_system_t p_systems[DVB_MAX_DELIVERY_SYSTEMS] = { (fe_delivery_system_t)0 };
      int i_systems = 0;

      if ( ioctl( i_frontend, FE_GET_INFO, &info ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "FE_GET_INFO failed (%s)\n", strerror(errno) );
         exit(1);
      }

      uint32_t version = 0x300;
#if DVBAPI_VERSION >= 505
      if ( ioctl( i_frontend, FE_GET_PROPERTY, &info_cmdseq ) < 0 )
      {
#endif
         /* DVBv3 device */
         switch ( info.type )
         {
            case FE_OFDM:
               p_systems[i_systems++] = SYS_DVBT;
#if DVBAPI_VERSION >= 505
               if ( info.caps & FE_CAN_2G_MODULATION )
                  p_systems[i_systems++] = SYS_DVBT2;
#endif
               break;
            case FE_QAM:
#if DVBAPI_VERSION >= 505
               p_systems[i_systems++] = SYS_DVBC_ANNEX_A;
#else
               p_systems[i_systems++] = SYS_DVBC_ANNEX_AC;
#endif
               break;
            case FE_QPSK:
               p_systems[i_systems++] = SYS_DVBS;
               if ( info.caps & FE_CAN_2G_MODULATION )
                  p_systems[i_systems++] = SYS_DVBS2;
               break;
            case FE_ATSC:
               if ( info.caps & (FE_CAN_8VSB | FE_CAN_16VSB) )
                  p_systems[i_systems++] = SYS_ATSC;
               if ( info.caps & (FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO) )
                  p_systems[i_systems++] = SYS_DVBC_ANNEX_B;
               break;
            default:
               cLbugf(cL::dbg_dvb, "unknown frontend type %d\n", info.type );
               exit(1);
         }
#if DVBAPI_VERSION >= 505
      }
      else
      {
         version = info_cmdargs[0].u.data;
         if ( ioctl( i_frontend, FE_GET_PROPERTY, &enum_cmdseq ) < 0 )
         {
            cLbugf(cL::dbg_dvb, "unable to query frontend\n" );
            exit(1);
         }
         i_systems = enum_cmdargs[0].u.buffer.len;
         if ( i_systems < 1 )
         {
            cLbugf(cL::dbg_dvb, "no available delivery system\n" );
            exit(1);
         }

         int i;
         for ( i = 0; i < i_systems; i++ )
            p_systems[i] = enum_cmdargs[0].u.buffer.data[i];
      }
#endif

      if ( b_init )
         FrontendInfo( &info, version, p_systems, i_systems );

      /* Clear frontend commands */
      if ( ioctl( i_frontend, FE_SET_PROPERTY, &cmdclear ) < 0 )
      {
         cLbug(cL::dbg_dvb, "Unable to clear frontend\n" );
         exit(1);
      }

      fe_delivery_system_t system = FrontendGuessSystem( p_systems, i_systems );
      switch ( system )
      {
         case SYS_DVBT:
            p = &dvbt_cmdseq;
            p->props[DELSYS].u.data = system;
            p->props[FREQUENCY].u.data = libcLdvb::i_frequency;
            p->props[INVERSION].u.data = GetInversion();
            if ( libcLdvb::psz_modulation != NULL )
               p->props[MODULATION].u.data = GetModulation();
            p->props[BANDWIDTH].u.data = libcLdvb::i_bandwidth * 1000000;
            p->props[FEC_INNER].u.data = GetFECInner(info.caps);
            p->props[FEC_LP].u.data = GetFECLP(info.caps);
            p->props[GUARD].u.data = GetGuard();
            p->props[TRANSMISSION].u.data = GetTransmission();
            p->props[HIERARCHY].u.data = GetHierarchy();

            cLbugf(cL::dbg_dvb, "tuning DVB-T frontend to f=%d bandwidth=%d inversion=%d fec_hp=%d fec_lp=%d hierarchy=%d modulation=%s guard=%d transmission=%d\n",
                  libcLdvb::i_frequency, libcLdvb::i_bandwidth, libcLdvb::i_inversion, libcLdvb::i_fec, libcLdvb::i_fec_lp,
                  libcLdvb::i_hierarchy,
                  libcLdvb::psz_modulation == NULL ? "qam_auto" : libcLdvb::psz_modulation,
                        libcLdvb::i_guard, libcLdvb::i_transmission );
            break;

#if DVBAPI_VERSION >= 505
         case SYS_DVBC_ANNEX_A:
#else
         case SYS_DVBC_ANNEX_AC:
#endif
            p = &dvbc_cmdseq;
            p->props[FREQUENCY].u.data = libcLdvb::i_frequency;
            p->props[INVERSION].u.data = GetInversion();
            if ( libcLdvb::psz_modulation != NULL )
               p->props[MODULATION].u.data = GetModulation();
            p->props[SYMBOL_RATE].u.data = libcLdvb::i_srate;

            cLbugf(cL::dbg_dvb, "tuning DVB-C frontend to f=%d srate=%d inversion=%d modulation=%s\n",
                  libcLdvb::i_frequency, libcLdvb::i_srate, libcLdvb::i_inversion,
                  libcLdvb::psz_modulation == NULL ? "qam_auto" : libcLdvb::psz_modulation );
            break;

         case SYS_DVBC_ANNEX_B:
            p = &atsc_cmdseq;
            p->props[DELSYS].u.data = system;
            p->props[FREQUENCY].u.data = libcLdvb::i_frequency;
            p->props[INVERSION].u.data = GetInversion();
            if ( libcLdvb::psz_modulation != NULL )
               p->props[MODULATION].u.data = GetModulation();

            cLbugf(cL::dbg_dvb, "tuning ATSC cable frontend to f=%d inversion=%d modulation=%s\n",
                  libcLdvb::i_frequency, libcLdvb::i_inversion,
                  libcLdvb::psz_modulation == NULL ? "qam_auto" : libcLdvb::psz_modulation );
            break;

         case SYS_DVBS:
         case SYS_DVBS2:
            if ( libcLdvb::psz_modulation != NULL )
            {
               p = &dvbs2_cmdseq;
               p->props[MODULATION].u.data = GetModulation();
               p->props[PILOT].u.data = GetPilot();
               p->props[ROLLOFF].u.data = GetRollOff();
               p->props[MIS].u.data = libcLdvb::i_mis;
            }
            else
               p = &dvbs_cmdseq;

            p->props[INVERSION].u.data = GetInversion();
            p->props[SYMBOL_RATE].u.data = libcLdvb::i_srate;
            p->props[FEC_INNER].u.data = GetFECInner(info.caps);
            p->props[FREQUENCY].u.data = FrontendDoDiseqc();

            cLbugf(cL::dbg_dvb, "tuning DVB-S frontend to f=%d srate=%d inversion=%d fec=%d rolloff=%d modulation=%s pilot=%d mis=%d\n",
                  libcLdvb::i_frequency, libcLdvb::i_srate, libcLdvb::i_inversion, libcLdvb::i_fec, libcLdvb::i_rolloff,
                  libcLdvb::psz_modulation == NULL ? "legacy" : libcLdvb::psz_modulation, libcLdvb::i_pilot,
                        libcLdvb::i_mis );
            break;

         case SYS_ATSC:
            p = &atsc_cmdseq;
            p->props[FREQUENCY].u.data = libcLdvb::i_frequency;
            p->props[INVERSION].u.data = GetInversion();
            if ( libcLdvb::psz_modulation != NULL )
               p->props[MODULATION].u.data = GetModulation();

            cLbugf(cL::dbg_dvb, "tuning ATSC frontend to f=%d inversion=%d modulation=%s\n",
                  libcLdvb::i_frequency, libcLdvb::i_inversion,
                  libcLdvb::psz_modulation == NULL ? "qam_auto" : libcLdvb::psz_modulation );
            break;

         default:
            cLbugf(cL::dbg_dvb, "unknown frontend type %d\n", info.type );
            exit(1);
      }

      /* Empty the event queue */
      for ( ; ; )
      {
         struct dvb_frontend_event event;
         if ( ioctl( i_frontend, FE_GET_EVENT, &event ) < 0
               && errno == EWOULDBLOCK )
            break;
      }

      /* Now send it all to the frontend device */
      if ( ioctl( i_frontend, FE_SET_PROPERTY, p ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "setting frontend failed (%s)\n", strerror(errno) );
         exit(1);
      }

      i_last_status = (fe_status_t) 0;

      if (libcLdvb::i_frontend_timeout_duration)
         cLev_timer_again(libcLdvb::event_loop, &lock_watcher);
   }

#else /* !S2API */

#warning "You are trying to compile DVBlast with an outdated linux-dvb interface."
#warning "DVBlast will be very limited and some options will have no effect."

   static void FrontendSet( bool b_init )
   {
      struct dvb_frontend_info info;
      struct dvb_frontend_parameters fep;

      if ( ioctl( i_frontend, FE_GET_INFO, &info ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "FE_GET_INFO failed (%s)\n", strerror(errno) );
         exit(1);
      }

      switch ( info.type )
      {
         case FE_OFDM:
            fep.frequency = libcLdvb::i_frequency;
            fep.inversion = INVERSION_AUTO;

            switch ( libcLdvb::i_bandwidth )
            {
               case 6: fep.u.ofdm.bandwidth = BANDWIDTH_6_MHZ; break;
               case 7: fep.u.ofdm.bandwidth = BANDWIDTH_7_MHZ; break;
               default:
               case 8: fep.u.ofdm.bandwidth = BANDWIDTH_8_MHZ; break;
            }

            fep.u.ofdm.code_rate_HP = FEC_AUTO;
            fep.u.ofdm.code_rate_LP = FEC_AUTO;
            fep.u.ofdm.constellation = QAM_AUTO;
            fep.u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
            fep.u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
            fep.u.ofdm.hierarchy_information = HIERARCHY_AUTO;

            cLbugf(cL::dbg_dvb, "tuning OFDM frontend to f=%d, bandwidth=%d\n",
                  libcLdvb::i_frequency, libcLdvb::i_bandwidth );
            break;

               case FE_QAM:
                  fep.frequency = libcLdvb::i_frequency;
                  fep.inversion = INVERSION_AUTO;
                  fep.u.qam.symbol_rate = libcLdvb::i_srate;
                  fep.u.qam.fec_inner = FEC_AUTO;
                  fep.u.qam.modulation = QAM_AUTO;

                  cLbugf(cL::dbg_dvb, "tuning QAM frontend to f=%d, srate=%d\n",
                        libcLdvb::i_frequency, libcLdvb::i_srate );
                  break;

               case FE_QPSK:
                  fep.inversion = INVERSION_AUTO;
                  fep.u.qpsk.symbol_rate = libcLdvb::i_srate;
                  fep.u.qpsk.fec_inner = FEC_AUTO;
                  fep.frequency = FrontendDoDiseqc();

                  cLbugf(cL::dbg_dvb, "tuning QPSK frontend to f=%d, srate=%d\n",
                        libcLdvb::i_frequency, libcLdvb::i_srate );
                  break;

#if DVBAPI_VERSION >= 301
               case FE_ATSC:
                  fep.frequency = libcLdvb::i_frequency;

                  fep.u.vsb.modulation = QAM_AUTO;

                  cLbugf(cL::dbg_dvb, "tuning ATSC frontend to f=%d\n", libcLdvb::i_frequency );
                  break;
#endif

               default:
                  cLbugf(cL::dbg_dvb, "unknown frontend type %d\n", info.type );
                  exit(1);
      }

      /* Empty the event queue */
      for ( ; ; )
      {
         struct dvb_frontend_event event;
         if ( ioctl( i_frontend, FE_GET_EVENT, &event ) < 0
               && errno == EWOULDBLOCK )
            break;
      }

      /* Now send it all to the frontend device */
      if ( ioctl( i_frontend, FE_SET_FRONTEND, &fep ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "setting frontend failed (%s)\n", strerror(errno) );
         exit(1);
      }

      i_last_status = 0;

      if (libcLdvb::i_frontend_timeout_duration)
         cLev_timer_again(libcLdvb::event_loop, &lock_watcher);
   }

#endif /* S2API */

   /*****************************************************************************
    * dvb_FrontendStatus
    *****************************************************************************/
   uint8_t dvb_FrontendStatus( uint8_t *p_answer, ssize_t *pi_size )
   {
      struct libcLdvben50221::ret_frontend_status *p_ret = (struct libcLdvben50221::ret_frontend_status *)p_answer;

      if ( ioctl( i_frontend, FE_GET_INFO, &p_ret->info ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "ioctl FE_GET_INFO failed (%s)\n", strerror(errno) );
         return libcLdvben50221::RET_ERR;
      }

      if ( ioctl( i_frontend, FE_READ_STATUS, &p_ret->i_status ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "ioctl FE_READ_STATUS failed (%s)\n", strerror(errno) );
         return libcLdvben50221::RET_ERR;
      }

      if ( p_ret->i_status & FE_HAS_LOCK )
      {
         if ( ioctl( i_frontend, FE_READ_BER, &p_ret->i_ber ) < 0 )
            cLbugf(cL::dbg_dvb, "ioctl FE_READ_BER failed (%s)\n", strerror(errno) );

         if ( ioctl( i_frontend, FE_READ_SIGNAL_STRENGTH, &p_ret->i_strength )
               < 0 )
            cLbugf(cL::dbg_dvb, "ioctl FE_READ_SIGNAL_STRENGTH failed (%s)\n",
                  strerror(errno) );

         if ( ioctl( i_frontend, FE_READ_SNR, &p_ret->i_snr ) < 0 )
            cLbugf(cL::dbg_dvb, "ioctl FE_READ_SNR failed (%s)\n", strerror(errno) );
      }

      *pi_size = sizeof(struct libcLdvben50221::ret_frontend_status);
      return libcLdvben50221::RET_FRONTEND_STATUS;
   }

} /* namespace libcLdvbdev */


