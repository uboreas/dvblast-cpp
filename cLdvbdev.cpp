/*
 * cLdvbdev.cpp
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
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

#include <cLdvbdev.h>

#include <unistd.h>
#include <inttypes.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/ioctl.h>

cLdvbdev::cLdvbdev()
{
   this->i_frontend = 0;
   this->i_dvr = 0;
   this->i_last_status = (enum fe_status) 0;
   this->p_freelist = (block_t *) 0;

   this->i_frequency = 0;
   this->i_fenum = 0;
   this->i_voltage = 13;
   this->b_tone = 0;
   this->i_bandwidth = 8;
   this->dvb_plp_id = 0;
   this->i_inversion = -1;
   this->i_srate = 27500000;
   this->i_fec = 999;
   this->i_rolloff = 35;
   this->i_satnum = 0;
   this->i_uncommitted = 0;
   this->psz_modulation = (char *) 0;
   this->psz_delsys = (char *) 0;
   this->i_pilot = -1;
   this->i_mis = 0;
   this->i_fec_lp = 999;
   this->i_guard = -1;
   this->i_transmission = -1;
   this->i_hierarchy = -1;
   this->i_dvr_buffer_size = DVB_DVR_BUFFER_SIZE;
   this->i_frontend_timeout_duration = DEFAULT_FRONTEND_TIMEOUT;
   this->psz_lnb_type = "universal";
   this->psz_mis_pls_mode = "ROOT";
   this->i_mis_pls_mode = 0;
   this->i_mis_pls_code = 0;
   this->i_mis_is_id = 0;
   cLbug(cL::dbg_high, "cLdvbdev created\n");
}

cLdvbdev::~cLdvbdev()
{
   cLbug(cL::dbg_high, "cLdvbdev deleted\n");
}

int cLdvbdev::dev_PIDIsSelected(uint16_t i_pid)
{
   for (int i = 0; i < this->p_pids[i_pid].i_nb_outputs; i++)
      if (this->p_pids[i_pid].pp_outputs[i] != (output_t *) 0)
         return 1;

   return 0;
}

void cLdvbdev::dev_ResendCAPMTs()
{
   for (int i = 0; i < this->i_nb_sids; i++) {
      if (this->pp_sids[i]->p_current_pmt != NULL && this->SIDIsSelected(this->pp_sids[i]->i_sid) && this->PMTNeedsDescrambling(this->pp_sids[i]->p_current_pmt))
         this->en50221_AddPMT(this->pp_sids[i]->p_current_pmt);
   }
}

void cLdvbdev::dev_Open()
{
   if (this->i_mis_pls_mode || this->i_mis_pls_code || this->i_mis_is_id) {
      this->i_mis = (this->i_mis_pls_mode << 26 | this->i_mis_pls_code << 8 | this->i_mis_is_id);
      cLbugf(cL::dbg_dvb, "multistream-id(%d, 0x%x) using pls-mode(%d), pls-code(%d), is-id(%d)", this->i_mis, this->i_mis, this->i_mis_pls_mode, this->i_mis_pls_code, this->i_mis_is_id);
   } else
   if (this->i_mis) {
      this->i_mis_pls_mode = (this->i_mis >> 26) & 0x03;
      this->i_mis_pls_code = (this->i_mis >> 8) & 0x3ffff;
      this->i_mis_is_id = this->i_mis & 0xff;
      cLbugf(cL::dbg_dvb, "multistream-id(%d, 0x%x) using pls-mode(%d), pls-code(%d), is-id(%d)", this->i_mis, this->i_mis, this->i_mis_pls_mode, this->i_mis_pls_code, this->i_mis_is_id);
   }

   char psz_tmp[128];

   cLbugf(cL::dbg_dvb, "compiled with DVB API version %d.%d\n", DVB_API_VERSION, DVB_API_VERSION_MINOR);

   if (this->i_frequency) {
      sprintf(psz_tmp, "/dev/dvb/adapter%d/frontend%d", this->i_adapter, this->i_fenum);
      if((this->i_frontend = open(psz_tmp, O_RDWR | O_NONBLOCK)) < 0) {
         cLbugf(cL::dbg_dvb, "opening device %s failed (%s)\n", psz_tmp, strerror(errno));
         exit(1);
      }

      this->FrontendSet(true);
   } else {
      this->i_frontend = -1;
   }

   sprintf(psz_tmp, "/dev/dvb/adapter%d/dvr%d", this->i_adapter, this->i_fenum);

   if((this->i_dvr = open(psz_tmp, O_RDONLY | O_NONBLOCK)) < 0) {
      cLbugf(cL::dbg_dvb, "opening device %s failed (%s)\n", psz_tmp, strerror(errno));
      exit(1);
   }

   if (ioctl(this->i_dvr, DMX_SET_BUFFER_SIZE, this->i_dvr_buffer_size) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't set %s buffer size (%s)\n", psz_tmp, strerror(errno));
   }

   this->dvr_watcher.data = this;
   cLev_io_init(&this->dvr_watcher, cLdvbdev::DVRRead, this->i_dvr, 1); // EV_READ
   cLev_io_start(this->event_loop, &this->dvr_watcher);

   if (this->i_frontend != -1) {
      this->frontend_watcher.data = this;
      cLev_io_init(&this->frontend_watcher, cLdvbdev::FrontendRead, this->i_frontend, 1); //EV_READ
      cLev_io_start(this->event_loop, &this->frontend_watcher);
   }

   this->lock_watcher.data = this;
   cLev_timer_init(&this->lock_watcher, cLdvbdev::FrontendLockCb, this->i_frontend_timeout_duration / 1000000., this->i_frontend_timeout_duration / 1000000.);
   this->mute_watcher.data = this;
   cLev_timer_init(&this->mute_watcher, cLdvbdev::DVRMuteCb, DVB_DVR_READ_TIMEOUT / 1000000., DVB_DVR_READ_TIMEOUT / 1000000.);

   this->en50221_Init();
}

void cLdvbdev::dev_Reset()
{
   if (this->i_frequency)
      this->FrontendSet(true);
}

void cLdvbdev::DVRRead(void *loop, void *p, int revents)
{
   struct cLev_io *w = (struct cLev_io *) p;
   cLdvbdev *pobj = (cLdvbdev *) w->data;

   int i, i_len;
   block_t *p_ts = pobj->p_freelist, **pp_current = &p_ts;
   struct iovec p_iov[DVB_MAX_READ_ONCE];

   for (i = 0; i < DVB_MAX_READ_ONCE; i++) {
      if ((*pp_current) == NULL) *pp_current = pobj->block_New();
      p_iov[i].iov_base = (*pp_current)->p_ts;
      p_iov[i].iov_len = TS_SIZE;
      pp_current = &(*pp_current)->p_next;
   }

   if ((i_len = readv(pobj->i_dvr, p_iov, DVB_MAX_READ_ONCE)) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't read from DVR device (%s)\n", strerror(errno));
      i_len = 0;
   }
   i_len /= TS_SIZE;

   if (i_len)
      cLev_timer_again(loop, &pobj->mute_watcher);

   pp_current = &p_ts;
   while (i_len && *pp_current) {
      pp_current = &(*pp_current)->p_next;
      i_len--;
   }

   pobj->p_freelist = *pp_current;
   *pp_current = (block_t *) 0;

   pobj->demux_Run(p_ts);
}

void cLdvbdev::DVRMuteCb(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *) p;
   cLdvbdev *pobj = (cLdvbdev *) w->data;

   cLbug(cL::dbg_dvb, "no DVR output, resetting\n");
   cLev_timer_stop(loop, w);
   if (pobj->i_frequency)
      pobj->FrontendSet(false);
   pobj->en50221_Reset();
}

/*
 * Demux
 */
int cLdvbdev::dev_SetFilter(uint16_t i_pid)
{
   struct dmx_pes_filter_params s_filter_params;
   char psz_tmp[128];
   int i_fd;

   sprintf(psz_tmp, "/dev/dvb/adapter%d/demux%d", this->i_adapter, this->i_fenum);
   if((i_fd = open(psz_tmp, O_RDWR)) < 0) {
      cLbugf(cL::dbg_dvb, "DMXSetFilter: opening device failed (%s)\n", strerror(errno));
      return -1;
   }

   s_filter_params.pid      = i_pid;
   s_filter_params.input    = DMX_IN_FRONTEND;
   s_filter_params.output   = DMX_OUT_TS_TAP;
   s_filter_params.flags    = DMX_IMMEDIATE_START;
   s_filter_params.pes_type = DMX_PES_OTHER;

   if (ioctl(i_fd, DMX_SET_PES_FILTER, &s_filter_params) < 0) {
      cLbugf(cL::dbg_dvb, "failed setting filter on %d (%s)\n", i_pid, strerror(errno));
      close(i_fd);
      return -1;
   }

   cLbugf(cL::dbg_dvb, "setting filter on PID %d\n", i_pid);

   return i_fd;
}

void cLdvbdev::dev_UnsetFilter(int i_fd, uint16_t i_pid)
{
   if (ioctl(i_fd, DMX_STOP) < 0) {
      cLbugf(cL::dbg_dvb, "DMX_STOP failed (%s)\n", strerror(errno));
   } else {
      cLbugf(cL::dbg_dvb, "unsetting filter on PID %d\n", i_pid);
   }
   close(i_fd);
}

/*
 * Frontend
 */
void cLdvbdev::FrontendPrintCb(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *) p;
   cLdvbdev *pobj = (cLdvbdev *) w->data;

   uint32_t i_ber = 0;
   uint16_t i_strength = 0, i_snr = 0;
   uint32_t i_uncorrected = 0;

   ioctl(pobj->i_frontend, FE_READ_BER, &i_ber);
   ioctl(pobj->i_frontend, FE_READ_SIGNAL_STRENGTH, &i_strength);
   ioctl(pobj->i_frontend, FE_READ_SNR, &i_snr);
   ioctl(pobj->i_frontend, FE_READ_UNCORRECTED_BLOCKS, &i_uncorrected);

   cLbugf(cL::dbg_dvb, "frontend ber: %"PRIu32" strength: %"PRIu16" snr: %"PRIu16" uncorrected: %"PRIu32"\n", i_ber, i_strength, i_snr, i_uncorrected);
}

#define IF_UP(x) } if (i_diff & (x)) { if (i_status & (x))

void cLdvbdev::FrontendRead(void  *loop, void *p, int revents)
{
   struct cLev_io *w = (struct cLev_io *) p;
   cLdvbdev *pobj = (cLdvbdev *) w->data;

   struct dvb_frontend_event event;
   fe_status_t i_status, i_diff;

   for(;;) {
      int i_ret = ioctl(pobj->i_frontend, FE_GET_EVENT, &event);

      if(i_ret < 0) {
         if(errno == EWOULDBLOCK)
            return; /* no more events */

         cLbugf(cL::dbg_dvb, "reading frontend event failed (%d) %s\n", i_ret, strerror(errno));
         return;
      }

      i_status = event.status;
      i_diff = (fe_status_t)(i_status ^ pobj->i_last_status);
      pobj->i_last_status = i_status;

      {
         IF_UP(FE_HAS_SIGNAL)
               { cLbug(cL::dbg_dvb, "frontend has acquired signal\n");}
         else
         {cLbug(cL::dbg_dvb, "frontend has lost signal\n");}

         IF_UP(FE_HAS_CARRIER)
         {cLbug(cL::dbg_dvb, "frontend has acquired carrier\n");}
         else
         {cLbug(cL::dbg_dvb, "frontend has lost carrier\n");}

      IF_UP(FE_HAS_VITERBI)
      {cLbug(cL::dbg_dvb, "frontend has acquired stable FEC\n");}
      else
      {cLbug(cL::dbg_dvb, "frontend has lost FEC\n");}

      IF_UP(FE_HAS_SYNC)
      {cLbug(cL::dbg_dvb, "frontend has acquired sync\n");}
      else
      {cLbug(cL::dbg_dvb, "frontend has lost sync\n");}

      IF_UP(FE_HAS_LOCK)
      {
         int32_t i_value = 0;
         cLbug(cL::dbg_dvb, "frontend has acquired lock\n");

         cLev_timer_stop(loop, &pobj->lock_watcher);
         cLev_timer_again(loop, &pobj->mute_watcher);

         /* Read some statistics */
         if(ioctl(pobj->i_frontend, FE_READ_BER, &i_value) >= 0)
            cLbugf(cL::dbg_dvb, "- Bit error rate: %d\n", i_value);
         if(ioctl(pobj->i_frontend, FE_READ_SIGNAL_STRENGTH, &i_value) >= 0)
            cLbugf(cL::dbg_dvb, "- Signal strength: %d\n", i_value);
         if(ioctl(pobj->i_frontend, FE_READ_SNR, &i_value) >= 0)
            cLbugf(cL::dbg_dvb, "- SNR: %d\n", i_value);

         if (pobj->i_print_period) {
            pobj->print_watcher.data = pobj;
            cLev_timer_init(&pobj->print_watcher, cLdvbdev::FrontendPrintCb, pobj->i_print_period / 1000000., pobj->i_print_period / 1000000.);
            cLev_timer_start(pobj->event_loop, &pobj->print_watcher);
         }
      } else {
         cLbug(cL::dbg_dvb, "frontend has lost lock\n");

         if (pobj->i_frontend_timeout_duration) {
            cLev_timer_stop(pobj->event_loop, &pobj->lock_watcher);
            cLev_timer_again(loop, &pobj->mute_watcher);
         }

         if (pobj->i_print_period)
            cLev_timer_stop(pobj->event_loop, &pobj->print_watcher);
      }

      IF_UP(FE_REINIT)
      {
         /* The frontend was reinited. */
         cLbug(cL::dbg_dvb, "reiniting frontend\n");
         if (pobj->i_frequency)
            pobj->FrontendSet(true);
      }
      }
   }
}

#undef IF_UP

void cLdvbdev::FrontendLockCb(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *) p;
   cLdvbdev *pobj = (cLdvbdev *) w->data;

   if (pobj->i_quit_timeout_duration) {
      fprintf(stdout, "error: no lock\n");
      cLev_break(loop, 2); //EVBREAK_ALL
      return;
   }

   cLbug(cL::dbg_dvb, "no lock, tuning again\n");
   cLev_timer_stop(loop, w);

   if (pobj->i_frequency)
      pobj->FrontendSet(false);
}

int cLdvbdev::FrontendDoDiseqc(void)
{
   fe_sec_voltage_t fe_voltage;
   fe_sec_tone_mode_t fe_tone;
   int bis_frequency;

   switch (this->i_voltage) {
      case 0:
         fe_voltage = SEC_VOLTAGE_OFF;
         break;
      default:
      case 13:
         fe_voltage = SEC_VOLTAGE_13;
         break;
      case 18:
         fe_voltage = SEC_VOLTAGE_18;
         break;
   }

   fe_tone = this->b_tone ? SEC_TONE_ON : SEC_TONE_OFF;

   if (strcmp(this->psz_lnb_type, "universal" ) == 0) {
      /* Automatic mode. */
      if (this->i_frequency >= 950000 && this->i_frequency <= 2150000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in IF-band\n", this->i_frequency);
         bis_frequency = this->i_frequency;
      } else
      if (this->i_frequency >= 2500000 && this->i_frequency <= 2700000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in S-band\n", this->i_frequency);
         bis_frequency = 3650000 - this->i_frequency;
      } else
      if (this->i_frequency >= 3400000 && this->i_frequency <= 4200000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in C-band (lower)\n", this->i_frequency);
         bis_frequency = 5150000 - this->i_frequency;
      } else
      if (this->i_frequency >= 4500000 && this->i_frequency <= 4800000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in C-band (higher)\n", this->i_frequency);
         bis_frequency = 5950000 - this->i_frequency;
      } else
      if (this->i_frequency >= 10700000 && this->i_frequency < 11700000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in Ku-band (lower)\n", this->i_frequency);
         bis_frequency = this->i_frequency - 9750000;
      } else
      if (this->i_frequency >= 11700000 && this->i_frequency <= 13250000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in Ku-band (higher)\n", this->i_frequency);
         bis_frequency = this->i_frequency - 10600000;
         fe_tone = SEC_TONE_ON;
      } else {
         cLbugf(cL::dbg_dvb, "frequency %d is out of any known band\n", this->i_frequency);
         exit(1);
      }
   } else
   if (strcmp(this->psz_lnb_type, "old-sky") == 0) {
      if (this->i_frequency >= 11700000 && this->i_frequency <= 13250000) {
         cLbugf(cL::dbg_dvb, "frequency %d is in Ku-band (higher)", this->i_frequency);
         bis_frequency = i_frequency - 11300000;
         fe_tone = SEC_TONE_ON;
      } else {
         cLbugf(cL::dbg_dvb, "frequency %d is out of any known band", this->i_frequency);
         exit(1);
      }
   } else {
      cLbugf(cL::dbg_dvb, "lnb-type '%s' is not known. Valid type: universal old-sky\n", this->psz_lnb_type);
      exit(1);
   }

   /* Switch off continuous tone. */
   if (ioctl(this->i_frontend, FE_SET_TONE, SEC_TONE_OFF) < 0) {
      cLbugf(cL::dbg_dvb, "FE_SET_TONE failed (%s)\n", strerror(errno));
      exit(1);
   }

   /* Configure LNB voltage. */
   if (ioctl(this->i_frontend, FE_SET_VOLTAGE, fe_voltage) < 0) {
      cLbugf(cL::dbg_dvb, "FE_SET_VOLTAGE failed (%s)\n", strerror(errno));
      exit(1);
   }

   /* Wait for at least 15 ms. Currently 100 ms because of broken drivers. */
   this->msleep(100000);

   /* Diseqc */
   if (this->i_satnum > 0 && this->i_satnum < 5) {
      /* digital satellite equipment control,
       * specification is available from http://www.eutelsat.com/
       */

      /* DiSEqC 1.1 */
      struct dvb_diseqc_master_cmd uncmd = { {0xe0, 0x10, 0x39, 0xf0, 0x00, 0x00}, 4};

      /* DiSEqC 1.0 */
      struct dvb_diseqc_master_cmd cmd = { {0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4};

      cmd.msg[3] = 0xf0 /* reset bits */
            | ((this->i_satnum - 1) << 2)
            | (fe_voltage == SEC_VOLTAGE_13 ? 0 : 2)
            | (fe_tone == SEC_TONE_ON ? 1 : 0);

      if (this->i_uncommitted > 0 && this->i_uncommitted < 17) {
         uncmd.msg[3] = 0xf0 /* reset bits */
               | (i_uncommitted - 1);
         if(ioctl(this->i_frontend, FE_DISEQC_SEND_MASTER_CMD, &uncmd) < 0) {
            cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n", strerror(errno));
            exit(1);
         }
         /* Repeat uncommitted command */
         uncmd.msg[0] = 0xe1; /* framing: master, no reply, repeated TX */
         if(ioctl(this->i_frontend, FE_DISEQC_SEND_MASTER_CMD, &uncmd) < 0) {
            cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n", strerror(errno));
            exit(1);
         }
         /* Pause 125 ms between uncommitted & committed diseqc commands. */
         this->msleep(125000);
      }

      if(ioctl(this->i_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0) {
         cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n", strerror(errno));
         exit(1);
      }
      this->msleep(100000); /* Should be 15 ms. */

      /* Do it again just to be sure. */
      cmd.msg[0] = 0xe1; /* framing: master, no reply, repeated TX */
      if(ioctl(this->i_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0) {
         cLbugf(cL::dbg_dvb, "ioctl FE_SEND_MASTER_CMD failed (%s)\n", strerror(errno));
         exit(1);
      }
      this->msleep(100000); /* Again, should be 15 ms */
   } else
   if (this->i_satnum == 0xA || this->i_satnum == 0xB) {
      /* A or B simple diseqc ("diseqc-compatible") */
      if(ioctl(this->i_frontend, FE_DISEQC_SEND_BURST, this->i_satnum == 0xB ? SEC_MINI_B : SEC_MINI_A) < 0) {
         cLbugf(cL::dbg_dvb, "ioctl FE_SEND_BURST failed (%s)\n", strerror(errno));
         exit(1);
      }
      this->msleep(100000); /* ... */
   }

   if (ioctl(this->i_frontend, FE_SET_TONE, fe_tone) < 0) {
      cLbugf(cL::dbg_dvb, "FE_SET_TONE failed (%s)\n", strerror(errno));
      exit(1);
   }

   this->msleep(100000); /* ... */

   cLbugf(cL::dbg_dvb, "configuring LNB to v=%d p=%d satnum=%x uncommitted=%x lnb-type=%s bis_frequency=%d\n",
      this->i_voltage, this->b_tone, this->i_satnum, this->i_uncommitted, this->psz_lnb_type, bis_frequency);
   return bis_frequency;
}

#if DVB_API_VERSION >= 5

#if DVBAPI_VERSION < 505
#warning Your linux-dvb headers are old, you should consider upgrading your kernel and/or compiling against different kernel headers
#endif

/* Helper functions for S2API */
fe_spectral_inversion_t cLdvbdev::GetInversion()
{
   switch (this->i_inversion)
   {
      case 0:
         return INVERSION_OFF;
      case 1:
         return INVERSION_ON;
      default: {
         cLbugf(cL::dbg_dvb, "invalid inversion %d\n", this->i_inversion);
      }
      //no break;
      case -1:
         return INVERSION_AUTO;
   }
}

#define GET_FEC_INNER(fec, val) \
      if ((fe_caps & FE_CAN_##fec) && (i_fec_value == val)) \
      return fec;

fe_code_rate_t cLdvbdev::GetFEC(fe_caps_t fe_caps, int i_fec_value)
{
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

   cLbugf(cL::dbg_dvb, "invalid FEC %d\n", i_fec_value);
   return FEC_AUTO;
}

#undef GET_FEC_INNER
#define GetFECInner(caps) cLdvbdev::GetFEC(caps, this->i_fec)
#define GetFECLP(caps) cLdvbdev::GetFEC(caps, this->i_fec_lp)

#define GET_MODULATION(mod) \
      if (!strcasecmp(this->psz_modulation, #mod)) \
      return mod;

fe_modulation_t cLdvbdev::GetModulation()
{
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

   cLbugf(cL::dbg_dvb, "invalid modulation %s\n", this->psz_modulation);
   exit(1);
}

#undef GET_MODULATION

fe_pilot_t cLdvbdev::GetPilot()
{
   switch (this->i_pilot) {
      case 0:
         return PILOT_OFF;
      case 1:
         return PILOT_ON;
      default: {
         cLbugf(cL::dbg_dvb, "invalid pilot %d\n", this->i_pilot);
      }
      // no break
      case -1:
         return PILOT_AUTO;
   }
}

fe_rolloff_t cLdvbdev::GetRollOff()
{
   switch (this->i_rolloff) {
      case -1:
      case  0:
         return ROLLOFF_AUTO;
      case 20:
         return ROLLOFF_20;
      case 25:
         return ROLLOFF_25;
      default: {
         cLbugf(cL::dbg_dvb, "invalid rolloff %d\n", this->i_rolloff);
      }
      // no break;
      case 35:
         return ROLLOFF_35;
   }
}

fe_guard_interval_t cLdvbdev::GetGuard()
{
   switch (this->i_guard) {
      case 32:
         return GUARD_INTERVAL_1_32;
      case 16:
         return GUARD_INTERVAL_1_16;
      case  8:
         return GUARD_INTERVAL_1_8;
      case  4:
         return GUARD_INTERVAL_1_4;
      default: {
         cLbugf(cL::dbg_dvb, "invalid guard interval %d\n", this->i_guard);
      }
      // no break
      case -1:
      case  0:
         return GUARD_INTERVAL_AUTO;
   }
}

fe_transmit_mode_t cLdvbdev::GetTransmission()
{
   switch (this->i_transmission) {
      case 2:
         return TRANSMISSION_MODE_2K;
      case 8:
         return TRANSMISSION_MODE_8K;
#ifdef TRANSMISSION_MODE_4K
      case 4:
         return TRANSMISSION_MODE_4K;
#endif
      default: {
         cLbugf(cL::dbg_dvb, "invalid tranmission mode %d\n", this->i_transmission);
      }
      // no break
      case -1:
      case 0:
         return TRANSMISSION_MODE_AUTO;
   }
}

fe_hierarchy_t cLdvbdev::GetHierarchy()
{
   switch (this->i_hierarchy) {
      case 0:
         return HIERARCHY_NONE;
      case 1:
         return HIERARCHY_1;
      case 2:
         return HIERARCHY_2;
      case 4:
         return HIERARCHY_4;
      default: {
         cLbugf(cL::dbg_dvb, "invalid intramission mode %d\n", this->i_transmission);
      }
      // no break
      case -1:
         return HIERARCHY_AUTO;
   }
}

#define FRONTEND_INFO(caps,val,msg) if (caps & val) { cLbugf(cL::dbg_dvb, "  %s\n", msg); }
#define DELSYS_INFO(delsys, msg) case delsys: { cLbugf(cL::dbg_dvb, "  %s\n", msg); break; }

/* Print frontend info */
void cLdvbdev::FrontendInfo(struct dvb_frontend_info *info, uint32_t version, fe_delivery_system_t *p_systems, int i_systems)
{
   cLbugf(cL::dbg_dvb, "using DVB API version %d.%d\n", version / 256, version % 256);
   cLbugf(cL::dbg_dvb, "Frontend \"%s\" supports:\n", info->name);
   cLbugf(cL::dbg_dvb, " frequency min: %d, max: %d, stepsize: %d, tolerance: %d\n", info->frequency_min, info->frequency_max, info->frequency_stepsize, info->frequency_tolerance);
   cLbugf(cL::dbg_dvb, " symbolrate min: %d, max: %d, tolerance: %d\n", info->symbol_rate_min, info->symbol_rate_max, info->symbol_rate_tolerance);
   cLbug(cL::dbg_dvb, " capabilities:\n");

   FRONTEND_INFO(info->caps, FE_IS_STUPID, "FE_IS_STUPID")
   FRONTEND_INFO(info->caps, FE_CAN_INVERSION_AUTO, "INVERSION_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_1_2, "FEC_1_2")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_2_3, "FEC_2_3")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_3_4, "FEC_3_4")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_4_5, "FEC_4_5")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_5_6, "FEC_5_6")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_6_7, "FEC_6_7")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_7_8, "FEC_7_8")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_8_9, "FEC_8_9")
   FRONTEND_INFO(info->caps, FE_CAN_FEC_AUTO,"FEC_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_QPSK,   "QPSK")
   FRONTEND_INFO(info->caps, FE_CAN_QAM_16, "QAM_16")
   FRONTEND_INFO(info->caps, FE_CAN_QAM_32, "QAM_32")
   FRONTEND_INFO(info->caps, FE_CAN_QAM_64, "QAM_64")
   FRONTEND_INFO(info->caps, FE_CAN_QAM_128,"QAM_128")
   FRONTEND_INFO(info->caps, FE_CAN_QAM_256,"QAM_256")
   FRONTEND_INFO(info->caps, FE_CAN_QAM_AUTO,"QAM_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_TRANSMISSION_MODE_AUTO, "TRANSMISSION_MODE_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_BANDWIDTH_AUTO, "BANDWIDTH_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_GUARD_INTERVAL_AUTO, "GUARD_INTERVAL_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_HIERARCHY_AUTO, "HIERARCHY_AUTO")
   FRONTEND_INFO(info->caps, FE_CAN_8VSB, "8VSB")
   FRONTEND_INFO(info->caps, FE_CAN_16VSB,"16VSB")
   FRONTEND_INFO(info->caps, FE_HAS_EXTENDED_CAPS, "EXTENDED_CAPS")
#if DVBAPI_VERSION >= 501
   FRONTEND_INFO(info->caps, FE_CAN_2G_MODULATION, "2G_MODULATION")
#endif
   FRONTEND_INFO(info->caps, FE_CAN_MULTISTREAM, "MULTISTREAM")
   FRONTEND_INFO(info->caps, FE_CAN_TURBO_FEC, "TURBO_FEC")
   FRONTEND_INFO(info->caps, FE_NEEDS_BENDING, "NEEDS_BENDING")
   FRONTEND_INFO(info->caps, FE_CAN_RECOVER, "FE_CAN_RECOVER")
   FRONTEND_INFO(info->caps, FE_CAN_MUTE_TS, "FE_CAN_MUTE_TS")

   cLbug(cL::dbg_dvb, " delivery systems:\n");
   for (int i = 0; i < i_systems; i++) {
      switch (p_systems[i]) {
         DELSYS_INFO(SYS_ATSC, "ATSC")
         DELSYS_INFO(SYS_ATSCMH, "ATSCMH")
         DELSYS_INFO(SYS_CMMB, "CMBB")
         DELSYS_INFO(SYS_DAB, "DAB")
         DELSYS_INFO(SYS_DSS, "DSS")
         DELSYS_INFO(SYS_DVBC_ANNEX_B, "DVBC_ANNEX_B")
         DELSYS_INFO(SYS_DVBH, "DVBH")
         DELSYS_INFO(SYS_DVBS, "DVBS")
         DELSYS_INFO(SYS_DVBS2, "DVBS2")
         DELSYS_INFO(SYS_DVBT, "DVBT")
         DELSYS_INFO(SYS_ISDBC, "ISDBC")
         DELSYS_INFO(SYS_ISDBS, "ISDBS")
         DELSYS_INFO(SYS_ISDBT, "ISDBT")
         DELSYS_INFO(SYS_UNDEFINED, "UNDEFINED")
#if DVBAPI_VERSION >= 505
         DELSYS_INFO(SYS_DVBC_ANNEX_A, "DVBC_ANNEX_A")
         DELSYS_INFO(SYS_DVBC_ANNEX_C, "DVBC_ANNEX_C")
         DELSYS_INFO(SYS_DVBT2, "DVBT2")
         DELSYS_INFO(SYS_TURBO, "TURBO")
#else
         DELSYS_INFO(SYS_DVBC_ANNEX_AC, "DVBC_ANNEX_AC")
#endif
#if DVBAPI_VERSION >= 507
         DELSYS_INFO(SYS_DTMB, "DTMB")
#else
         DELSYS_INFO(SYS_DMBTH, "DMBTH")
#endif
         default: {
            cLbugf(cL::dbg_dvb, "  Unknown delivery system %u\n", p_systems[i]);
         }
         break;
      }
   }
}

#undef FRONTEND_INFO
#undef DELSYS_INFO

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
#define TRANSMISSION 8

#define HIERARCHY 9
#define PLP_ID 10

#define IDX_DVBS2_PILOT     6
#define IDX_DVBS2_ROLLOFF   7
#define IDX_DVBS2_STREAM_ID 8

//ISDBT
#define ISDBT_BANDWIDTH 2
#define ISDBT_LAYERA_FEC 4
#define ISDBT_LAYERA_MODULATION 5
#define ISDBT_LAYERA_SEGMENT_COUNT 6
#define ISDBT_LAYERA_TIME_INTERLEAVING 7
#define ISDBT_LAYERB_FEC 8
#define ISDBT_LAYERB_MODULATION 9
#define ISDBT_LAYERB_SEGMENT_COUNT 10
#define ISDBT_LAYERB_TIME_INTERLEAVING 11
#define ISDBT_LAYERC_FEC 12
#define ISDBT_LAYERC_MODULATION 13
#define ISDBT_LAYERC_SEGMENT_COUNT 14
#define ISDBT_LAYERC_TIME_INTERLEAVING 15

fe_delivery_system_t cLdvbdev::FrontendGuessSystem(fe_delivery_system_t *p_systems, int i_systems)
{
   if (this->psz_delsys != (char *) 0) {
      if (!strcasecmp(this->psz_delsys, "DVBS"))
         return SYS_DVBS;
      if (!strcasecmp(this->psz_delsys, "DVBS2"))
         return SYS_DVBS2;
      if (!strcasecmp(this->psz_delsys, "DVBC_ANNEX_A"))
#if DVBAPI_VERSION >= 505
         return SYS_DVBC_ANNEX_A;
#else
      return SYS_DVBC_ANNEX_AC;
#endif
      if (!strcasecmp(this->psz_delsys, "DVBC_ANNEX_B"))
         return SYS_DVBC_ANNEX_B;
      if (!strcasecmp(this->psz_delsys, "DVBT"))
         return SYS_DVBT;
      if (!strcasecmp(this->psz_delsys, "DVBT2"))
         return SYS_DVBT2;
      if (!strcasecmp(this->psz_delsys, "ATSC"))
         return SYS_ATSC;
      if (!strcasecmp(psz_delsys, "ISDBT"))
         return SYS_ISDBT;

      cLbugf(cL::dbg_dvb, "unknown delivery system %s\n", this->psz_delsys);
      exit(1);
   }

   if (i_systems == 1)
      return p_systems[0];

   for (int i = 0; i < i_systems; i++) {
      switch (p_systems[i]) {
         case SYS_DVBS:
            if (this->i_frequency < 50000000)
               return SYS_DVBS;
            break;
#if DVBAPI_VERSION >= 505
         case SYS_DVBC_ANNEX_A:
            if (this->i_frequency > 50000000 || this->i_srate != 27500000 || this->psz_modulation != (char *) 0)
               return SYS_DVBC_ANNEX_A;
            break;
#else
         case SYS_DVBC_ANNEX_AC:
            if (this->i_frequency > 50000000 || this->i_srate != 27500000 || this->psz_modulation != (char *) 0)
               return SYS_DVBC_ANNEX_AC;
            break;
#endif
         case SYS_DVBT:
            if (this->i_frequency > 50000000)
               return SYS_DVBT;
            break;
         case SYS_DVBT2:
            if (i_frequency > 50000000 && (this->dvb_plp_id))
               return SYS_DVBT2;
            break;
         default:
            break;
      }
   }

   cLbug(cL::dbg_dvb, "couldn't guess delivery system, use --delsys\n");
   return p_systems[0];
}

void cLdvbdev::FrontendSet(bool b_init)
{
   struct dvb_frontend_info info;
   struct dtv_properties *p;
   fe_delivery_system_t p_systems[DVB_MAX_DELIVERY_SYSTEMS] = { (fe_delivery_system_t)0 };
   int i_systems = 0;

   if (ioctl(this->i_frontend, FE_GET_INFO, &info) < 0) {
      cLbugf(cL::dbg_dvb, "FE_GET_INFO failed (%s)\n", strerror(errno));
      exit(1);
   }

   uint32_t version = 0x300;
#if DVBAPI_VERSION >= 505
   if (ioctl(this->i_frontend, FE_GET_PROPERTY, &info_cmdseq) < 0) {
#endif
      /* DVBv3 device */
      switch (info.type) {
         case FE_OFDM:
            p_systems[i_systems++] = SYS_DVBT;
#if DVBAPI_VERSION >= 505
            if (info.caps & FE_CAN_2G_MODULATION)
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
            if (info.caps & FE_CAN_2G_MODULATION)
               p_systems[i_systems++] = SYS_DVBS2;
            break;
         case FE_ATSC:
            if (info.caps & (FE_CAN_8VSB | FE_CAN_16VSB))
               p_systems[i_systems++] = SYS_ATSC;
            if (info.caps & (FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO))
               p_systems[i_systems++] = SYS_DVBC_ANNEX_B;
            break;
         default:
            cLbugf(cL::dbg_dvb, "unknown frontend type %d\n", info.type);
            exit(1);
      }
#if DVBAPI_VERSION >= 505
   } else {
      version = info_cmdargs[0].u.data;
      if (ioctl(this->i_frontend, FE_GET_PROPERTY, &enum_cmdseq) < 0) {
         cLbug(cL::dbg_dvb, "unable to query frontend\n");
         exit(1);
      }
      i_systems = enum_cmdargs[0].u.buffer.len;
      if (i_systems < 1) {
         cLbug(cL::dbg_dvb, "no available delivery system\n");
         exit(1);
      }

      cLbugf(cL::dbg_dvb, "adding %d systems\n", i_systems);
      for (int i = 0; i < i_systems; i++) {
         cLbugf(cL::dbg_dvb, "adding: %d\n", i);
         p_systems[i] = (fe_delivery_system_t)(enum_cmdargs[0].u.buffer.data[i]);
      }
   }
#endif

   if (b_init)
      this->FrontendInfo(&info, version, p_systems, i_systems);

   /* Clear frontend commands */
   if (ioctl(this->i_frontend, FE_SET_PROPERTY, &cmdclear) < 0) {
      cLbug(cL::dbg_dvb, "Unable to clear frontend\n");
      exit(1);
   }

   fe_delivery_system_t system = this->FrontendGuessSystem(p_systems, i_systems);
   switch (system) {
      case SYS_DVBT:
         p = &dvbt_cmdseq;
         p->props[DELSYS].u.data = system;
         p->props[FREQUENCY].u.data = this->i_frequency;
         p->props[INVERSION].u.data = this->GetInversion();
         if (this->psz_modulation != (char *) 0)
            p->props[MODULATION].u.data = this->GetModulation();
         p->props[BANDWIDTH].u.data = this->i_bandwidth * 1000000;
         p->props[FEC_INNER].u.data = this->GetFECInner(info.caps);
         p->props[FEC_LP].u.data = this->GetFECLP(info.caps);
         p->props[GUARD].u.data = this->GetGuard();
         p->props[TRANSMISSION].u.data = this->GetTransmission();
         p->props[HIERARCHY].u.data = this->GetHierarchy();

         cLbugf(cL::dbg_dvb, "tuning DVB-T frontend to f=%d bandwidth=%d inversion=%d fec_hp=%d fec_lp=%d hierarchy=%d modulation=%s guard=%d transmission=%d\n",
               this->i_frequency, this->i_bandwidth, this->i_inversion, this->i_fec, this->i_fec_lp,
               this->i_hierarchy,
               (this->psz_modulation == (char *) 0 ? "qam_auto" : this->psz_modulation),
               this->i_guard, this->i_transmission
         );
         break;
      case SYS_DVBT2:
         p = &dvbt2_cmdseq;
         p->props[DELSYS].u.data = system;
         p->props[FREQUENCY].u.data = this->i_frequency;
         p->props[INVERSION].u.data = this->GetInversion();
         if (this->psz_modulation != (char *) 0)
            p->props[MODULATION].u.data = this->GetModulation();
         p->props[BANDWIDTH].u.data = this->i_bandwidth * 1000000;
         p->props[FEC_INNER].u.data = this->GetFECInner(info.caps);
         p->props[FEC_LP].u.data = this->GetFECLP(info.caps);
         p->props[GUARD].u.data = this->GetGuard();
         p->props[TRANSMISSION].u.data = this->GetTransmission();
         p->props[HIERARCHY].u.data = this->GetHierarchy();
         p->props[PLP_ID].u.data = this->dvb_plp_id;
 
         cLbugf(cL::dbg_dvb, "tuning DVB-T2 frontend to f=%d bandwidth=%d inversion=%d fec_hp=%d fec_lp=%d hierarchy=%d modulation=%s guard=%d transmission=%d PLP_ID=%d ",
               this->i_frequency, this->i_bandwidth, this->i_inversion, this->i_fec, this->i_fec_lp,
               this->i_hierarchy,
               (this->psz_modulation == (char *) 0 ? "qam_auto" : this->psz_modulation),
               this->i_guard, this->i_transmission, p->props[PLP_ID].u.data
         );
         break;

#if DVBAPI_VERSION >= 505
      case SYS_DVBC_ANNEX_A:
#else
      case SYS_DVBC_ANNEX_AC:
#endif
         p = &dvbc_cmdseq;
         p->props[FREQUENCY].u.data = this->i_frequency;
         p->props[INVERSION].u.data = this->GetInversion();
         if (this->psz_modulation != (char *) 0)
            p->props[MODULATION].u.data = this->GetModulation();
         p->props[SYMBOL_RATE].u.data = this->i_srate;

         cLbugf(cL::dbg_dvb, "tuning DVB-C frontend to f=%d srate=%d inversion=%d modulation=%s\n", this->i_frequency, this->i_srate, this->i_inversion, this->psz_modulation == (char *) 0 ? "qam_auto" : this->psz_modulation);
         break;

      case SYS_DVBC_ANNEX_B:
         p = &atsc_cmdseq;
         p->props[DELSYS].u.data = system;
         p->props[FREQUENCY].u.data = this->i_frequency;
         p->props[INVERSION].u.data = this->GetInversion();
         if (this->psz_modulation != (char *) 0)
            p->props[MODULATION].u.data = this->GetModulation();

         cLbugf(cL::dbg_dvb, "tuning ATSC cable frontend to f=%d inversion=%d modulation=%s\n", this->i_frequency, this->i_inversion, this->psz_modulation == (char *) 0 ? "qam_auto" : this->psz_modulation);
         break;

      case SYS_DVBS:
      case SYS_DVBS2:
         if (this->psz_modulation != (char *) 0) {
            p = &dvbs2_cmdseq;
            p->props[MODULATION].u.data = this->GetModulation();
            p->props[IDX_DVBS2_PILOT].u.data = this->GetPilot();
            p->props[IDX_DVBS2_ROLLOFF].u.data = this->GetRollOff();
            p->props[IDX_DVBS2_STREAM_ID].u.data = this->i_mis;

         } else {
            p = &dvbs_cmdseq;
         }
         p->props[INVERSION].u.data = this->GetInversion();
         p->props[SYMBOL_RATE].u.data = this->i_srate;
         p->props[FEC_INNER].u.data = this->GetFECInner(info.caps);
         p->props[FREQUENCY].u.data = this->FrontendDoDiseqc();

         cLbugf(cL::dbg_dvb, "tuning DVB-S frontend to f=%d srate=%d inversion=%d fec=%d rolloff=%d modulation=%s pilot=%d mis=%d\n",
               this->i_frequency, this->i_srate, this->i_inversion, this->i_fec, this->i_rolloff,
               (this->psz_modulation == (char *) 0 ? "legacy" : this->psz_modulation),
               this->i_pilot,
               this->i_mis
         );
         break;

      case SYS_ATSC:
         p = &atsc_cmdseq;
         p->props[FREQUENCY].u.data = this->i_frequency;
         p->props[INVERSION].u.data = this->GetInversion();
         if (this->psz_modulation != (char *) 0)
            p->props[MODULATION].u.data = this->GetModulation();

         cLbugf(cL::dbg_dvb, "tuning ATSC frontend to f=%d inversion=%d modulation=%s\n", this->i_frequency, this->i_inversion, this->psz_modulation == (char *) 0 ? "qam_auto" : this->psz_modulation);
         break;

      case SYS_ISDBT:
         p = &isdbt_cmdseq;
         p->props[DELSYS].u.data = system;
         p->props[FREQUENCY].u.data = this->i_frequency;
         p->props[ISDBT_BANDWIDTH].u.data = this->i_bandwidth * 1000000;
         p->props[INVERSION].u.data = this->GetInversion();
         p->props[ISDBT_LAYERA_FEC].u.data = FEC_AUTO;
         p->props[ISDBT_LAYERA_MODULATION].u.data = QAM_AUTO;
         p->props[ISDBT_LAYERA_SEGMENT_COUNT].u.data = 0;
         p->props[ISDBT_LAYERA_TIME_INTERLEAVING].u.data = 0;
         p->props[ISDBT_LAYERB_FEC].u.data = FEC_AUTO;
         p->props[ISDBT_LAYERB_MODULATION].u.data = QAM_AUTO;
         p->props[ISDBT_LAYERB_SEGMENT_COUNT].u.data = 0;
         p->props[ISDBT_LAYERB_TIME_INTERLEAVING].u.data = 0;
         p->props[ISDBT_LAYERC_FEC].u.data = FEC_AUTO;
         p->props[ISDBT_LAYERC_MODULATION].u.data = QAM_AUTO;
         p->props[ISDBT_LAYERC_SEGMENT_COUNT].u.data = 0;
         p->props[ISDBT_LAYERC_TIME_INTERLEAVING].u.data = 0;
 
         cLbugf(cL::dbg_dvb, "tuning ISDB-T frontend to f=%d bandwidth=%d ", this->i_frequency, this->i_bandwidth);
         break;

      default:
         cLbugf(cL::dbg_dvb, "unknown frontend type %d\n", info.type);
         exit(1);
   }

   /* Empty the event queue */
   for (;;) {
      struct dvb_frontend_event event;
      if (ioctl(this->i_frontend, FE_GET_EVENT, &event) < 0 && errno == EWOULDBLOCK)
         break;
   }

   /* Now send it all to the frontend device */
   if (ioctl(this->i_frontend, FE_SET_PROPERTY, p) < 0) {
      cLbugf(cL::dbg_dvb, "setting frontend failed (%s)\n", strerror(errno));
      exit(1);
   }

   this->i_last_status = (fe_status_t) 0;

   if (this->i_frontend_timeout_duration)
      cLev_timer_again(this->event_loop, &this->lock_watcher);
}

#else /* !S2API, DVB_API_VERSION < 5 */

#warning "You are trying to compile DVBlast with an outdated linux-dvb interface."
#warning "DVBlast will be very limited and some options will have no effect."

void cLdvbdev::FrontendSet(bool b_init)
{
   struct dvb_frontend_info info;
   struct dvb_frontend_parameters fep;

   if (ioctl(this->i_frontend, FE_GET_INFO, &info) < 0) {
      cLbugf(cL::dbg_dvb, "FE_GET_INFO failed (%s)\n", strerror(errno));
      exit(1);
   }

   switch (info.type) {
      case FE_OFDM: {
         fep.frequency = this->i_frequency;
         fep.inversion = INVERSION_AUTO;

         switch (this->i_bandwidth) {
            case 6:
               fep.u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
               break;
            case 7:
               fep.u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
               break;
            default:
            case 8:
               fep.u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
               break;
         }

         fep.u.ofdm.code_rate_HP = FEC_AUTO;
         fep.u.ofdm.code_rate_LP = FEC_AUTO;
         fep.u.ofdm.constellation = QAM_AUTO;
         fep.u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
         fep.u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
         fep.u.ofdm.hierarchy_information = HIERARCHY_AUTO;

         cLbugf(cL::dbg_dvb, "tuning OFDM frontend to f=%d, bandwidth=%d\n", this->i_frequency, this->i_bandwidth);
         break;
      }
      case FE_QAM: {
         fep.frequency = this->i_frequency;
         fep.inversion = INVERSION_AUTO;
         fep.u.qam.symbol_rate = this->i_srate;
         fep.u.qam.fec_inner = FEC_AUTO;
         fep.u.qam.modulation = QAM_AUTO;

         cLbugf(cL::dbg_dvb, "tuning QAM frontend to f=%d, srate=%d\n", this->i_frequency, this->i_srate);
         break;
      }
      case FE_QPSK: {
         fep.inversion = INVERSION_AUTO;
         fep.u.qpsk.symbol_rate = this->i_srate;
         fep.u.qpsk.fec_inner = FEC_AUTO;
         fep.frequency = this->FrontendDoDiseqc();

         cLbugf(cL::dbg_dvb, "tuning QPSK frontend to f=%d, srate=%d\n", this->i_frequency, this->i_srate);
         break;
      }

#if DVBAPI_VERSION >= 301
      case FE_ATSC: {
         fep.frequency = this->i_frequency;

         fep.u.vsb.modulation = QAM_AUTO;

         cLbugf(cL::dbg_dvb, "tuning ATSC frontend to f=%d\n", this->i_frequency);
         break;
      }
#endif

      default:
         cLbugf(cL::dbg_dvb, "unknown frontend type %d\n", info.type);
         exit(1);
   }

   /* Empty the event queue */
   for (;;) {
      struct dvb_frontend_event event;
      if (ioctl(this->i_frontend, FE_GET_EVENT, &event) < 0 && errno == EWOULDBLOCK)
         break;
   }

   /* Now send it all to the frontend device */
   if (ioctl(this->i_frontend, FE_SET_FRONTEND, &fep) < 0) {
      cLbugf(cL::dbg_dvb, "setting frontend failed (%s)\n", strerror(errno));
      exit(1);
   }

   this->i_last_status = 0;

   if (this->i_frontend_timeout_duration)
      cLev_timer_again(this->event_loop, &this->lock_watcher);
}

#endif /* S2API, DVB_API_VERSION >= 5 */

uint8_t cLdvbdev::dvb_FrontendStatus(uint8_t *p_answer, ssize_t *pi_size)
{
   struct ret_frontend_status *p_ret = (struct ret_frontend_status *)p_answer;

   if (ioctl(this->i_frontend, FE_GET_INFO, &p_ret->info) < 0) {
      cLbugf(cL::dbg_dvb, "ioctl FE_GET_INFO failed (%s)\n", strerror(errno));
      return cLdvben50221::RET_ERR;
   }

   if (ioctl(this->i_frontend, FE_READ_STATUS, &p_ret->i_status) < 0) {
      cLbugf(cL::dbg_dvb, "ioctl FE_READ_STATUS failed (%s)\n", strerror(errno));
      return cLdvben50221::RET_ERR;
   }

   if (p_ret->i_status & FE_HAS_LOCK) {
      if (ioctl(this->i_frontend, FE_READ_BER, &p_ret->i_ber) < 0) {
         cLbugf(cL::dbg_dvb, "ioctl FE_READ_BER failed (%s)\n", strerror(errno));
      }
      if (ioctl(this->i_frontend, FE_READ_SIGNAL_STRENGTH, &p_ret->i_strength) < 0) {
         cLbugf(cL::dbg_dvb, "ioctl FE_READ_SIGNAL_STRENGTH failed (%s)\n", strerror(errno));
      }
      if (ioctl(this->i_frontend, FE_READ_SNR, &p_ret->i_snr) < 0) {
         cLbugf(cL::dbg_dvb, "ioctl FE_READ_SNR failed (%s)\n", strerror(errno));
      }
   }

   *pi_size = sizeof(struct ret_frontend_status);
   return cLdvben50221::RET_FRONTEND_STATUS;
}

void cLdvbdev::set_dvb_buffer_size(int i)
{
   this->i_dvr_buffer_size = i;
   /* roundup to packet size */
   this->i_dvr_buffer_size += TS_SIZE - 1;
   this->i_dvr_buffer_size /= TS_SIZE;
   this->i_dvr_buffer_size *= TS_SIZE;
}


