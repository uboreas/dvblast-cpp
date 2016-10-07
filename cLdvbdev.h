/*
 * cLdvbdev.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
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

#ifndef CLDVBDEV_H_
#define CLDVBDEV_H_

#include <cLdvbdemux.h>

#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/ca.h>

#define DVBAPI_VERSION ((DVB_API_VERSION)*100+(DVB_API_VERSION_MINOR))

#if DVBAPI_VERSION < 508
#define DTV_STREAM_ID            42
#define FE_CAN_MULTISTREAM       0x4000000
#endif

#define DVB_MAX_DELIVERY_SYSTEMS 20
#define DVB_DVR_READ_TIMEOUT     30000000 /* 30 s */
#define DVB_MAX_READ_ONCE        50
#define DVB_DVR_BUFFER_SIZE      40*188*1024 /* bytes */

class cLdvbdev : public cLdvbdemux {

   private:
      int i_frontend, i_dvr;
      struct cLev_io frontend_watcher, dvr_watcher;
      struct cLev_timer lock_watcher, mute_watcher, print_watcher;
      fe_status_t i_last_status;
      block_t *p_freelist;

      int i_frequency;
      int i_fenum;
      int i_voltage;
      int b_tone;
      int i_bandwidth;
      int i_inversion;
      int i_srate;
      int i_fec;
      int i_rolloff;
      int i_satnum;
      int i_uncommitted;
      char *psz_modulation;
      char *psz_delsys;
      int i_pilot;
      int i_mis;
      int i_fec_lp;
      int i_guard;
      int i_transmission;
      int i_hierarchy;
      mtime_t i_frontend_timeout_duration;
      int i_dvr_buffer_size;

      static void DVRRead(void *loop, void *w, int revents);
      static void DVRMuteCb(void *loop, void *w, int revents);
      static void FrontendPrintCb(void *loop, void *w, int revents);
      static void FrontendRead(void *loop, void *w, int revents);
      static void FrontendLockCb(void *loop, void *w, int revents);
      int FrontendDoDiseqc(void);
#if DVB_API_VERSION >= 5
      fe_spectral_inversion_t GetInversion();
      fe_code_rate_t GetFEC(fe_caps_t fe_caps, int li_fec_value);
      fe_modulation_t GetModulation();
      fe_pilot_t GetPilot();
      fe_rolloff_t GetRollOff();
      fe_guard_interval_t GetGuard();
      fe_transmit_mode_t GetTransmission();
      fe_hierarchy_t GetHierarchy();
      static void FrontendInfo(struct dvb_frontend_info *info, uint32_t version, fe_delivery_system_t *p_systems, int i_systems);
      fe_delivery_system_t FrontendGuessSystem(fe_delivery_system_t *p_systems, int i_systems);
#endif
      void FrontendSet(bool b_init);

   protected:
      virtual int dev_PIDIsSelected(uint16_t i_pid);
      virtual void dev_ResendCAPMTs();
      virtual void dev_Open();
      virtual void dev_Reset();
      virtual int dev_SetFilter(uint16_t i_pid);
      virtual void dev_UnsetFilter(int i_fd, uint16_t i_pid);

   public:
      void set_dvb_buffer_size(int i);
      inline void set_frequency(int i) {
         this->i_frequency = i;
      }
      inline void set_srate(int i) {
         this->i_srate = i;
      }
      inline void set_voltage(int i) {
         this->i_voltage = i;
      }
      inline void set_frontend(int i) {
         this->i_fenum = i;
      }
      inline void set_delivery_system(char *s) {
         this->psz_delsys = s;
      }
      inline void set_fec(int i) {
         this->i_fec = i;
      }
      inline void set_rolloff(int i) {
         this->i_rolloff = i;
      }
      inline void set_diseqc(int i) {
         this->i_satnum = i;
      }
      inline void set_diseqc_port(int i) {
         this->i_uncommitted = i;
      }
      inline void set_fource_pulse(int i = 1) {
         this->b_tone = i;
      }
      inline void set_bandwidth(int i) {
         this->i_bandwidth = i;
      }
      inline void set_inversion(int i) {
         this->i_inversion = i;
      }
      inline void set_modulation(char *s) {
         this->psz_modulation = s;
      }
      inline void set_pilot(int i) {
         this->i_pilot = i;
      }
      inline void set_multistream_id(int i) {
         this->i_mis = i;
      }
      inline void set_lpfec(int i) {
         this->i_fec_lp = i;
      }
      inline void set_guard_interval(int i) {
         this->i_guard = i;
      }
      inline void set_transmission(int i) {
         this->i_transmission = i;
      }
      inline void set_frontend_timeout(mtime_t t) {
         this->i_frontend_timeout_duration = t;
      }
      inline void set_hierarchy(int i) {
         this->i_hierarchy = i;
      }

      uint8_t dvb_FrontendStatus(uint8_t *p_answer, ssize_t *pi_size);

      cLdvbdev();
      virtual ~cLdvbdev();

};

#endif /*CLDVBDEV_H_*/

