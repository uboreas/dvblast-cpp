/*
 * cLdvbdemux.cpp
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
 *****************************************************************************
 * demux.c, util.c, dvblast.c
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

#include <cLdvbdemux.h>

#include <ctype.h>
#include <bitstream/mpeg/pes.h>
#include <bitstream/mpeg/psi_print.h>
#include <bitstream/dvb/si.h>
#include <bitstream/dvb/si_print.h>
#include <signal.h>
#include <pthread.h>

#ifdef HAVE_CLMACOS
#include <stdarg.h>
#endif

#include <cLdvbatsc.h>
#define MIN_SECTION_FRAGMENT  PSI_HEADER_SIZE_SYNTAX1
#define PID_ATSC              0x1ffb
#define TID_ATSC_CVT1         0xC8
#define TID_ATSC_CVT2         0xC9

#define MIN_SECTION_FRAGMENT    PSI_HEADER_SIZE_SYNTAX1

cLdvbdemux::cLdvbdemux()
{
   this->pmrtg = new cLdvbmrtgcnt();
   this->psz_conf_file = (const char *) 0;
   this->b_enable_emm = false;
   this->b_enable_ecm = false;
   this->i_es_timeout = 0;
   this->b_budget_mode = 0;
   this->b_select_pmts = 0;
   this->b_any_type = 0;

   this->i_wallclock = 0;
   this->pp_sids = (sid_t **) 0;
   this->i_nb_sids = 0;
   this->i_quit_timeout_duration = 0;

   this->i_last_dts = -1;
   this->i_demux_fd = -1;
   this->i_nb_packets = 0;
   this->i_nb_invalids = 0;
   this->i_nb_discontinuities = 0;
   this->i_nb_errors = 0;
   this->i_tuner_errors = 0;
   this->i_last_error = 0;
   this->i_last_reset = 0;

   cLbug(cL::dbg_high, "cLdvbdemux created\n");

}

cLdvbdemux::~cLdvbdemux()
{
   delete(this->pmrtg);
   cLbug(cL::dbg_high, "cLdvbdemux deleted\n");
}

bool cLdvbdemux::demux_Setup(cLevCB sighandler, void *opaque)
{
   if ((this->event_loop = cLev_default_loop(0)) == (void *) 0) {
      cLbug(cL::dbg_dvb, "unable to initialize libev\n");
      return false;
   } else
   if (sighandler != (cLevCB) 0) {
      /* Set signal handlers */
      this->sigint_watcher.data = opaque;
      cLev_signal_init(&this->sigint_watcher, sighandler, SIGINT);
      cLev_signal_start(this->event_loop, &this->sigint_watcher);
      cLev_unref(this->event_loop);

      this->sigterm_watcher.data = opaque;
      cLev_signal_init(&this->sigterm_watcher, sighandler, SIGTERM);
      cLev_signal_start(this->event_loop, &this->sigterm_watcher);
      cLev_unref(this->event_loop);

      this->sighup_watcher.data = opaque;
      cLev_signal_init(&this->sighup_watcher, sighandler, SIGHUP);
      cLev_signal_start(this->event_loop, &this->sighup_watcher);
      cLev_unref(this->event_loop);
   }
   return true;
}

void cLdvbdemux::break_cb(void *loop, void *w, int revents)
{
   cLev_break(loop, 2); //EVBREAK_ALL
}

void cLdvbdemux::debug_cb(void *p, const char *fmt, ...)
{
   cLpf(fmt, tbuf, nump);
   fprintf(stdout, "debug: %s\n", tbuf);
   //cLbugf(cL::dbg_dvb, "%s\n", tbuf);
   ::free(tbuf);
}

/*
 * Remap an ES pid to a fixed value.
 * Multiple streams of the same type use sequential pids
 * Returns the new pid and updates the map tables
 */
uint16_t cLdvbdemux::map_es_pid(output_t *p_output, uint8_t *p_es, uint16_t i_pid)
{
   uint16_t i_newpid = i_pid;
   uint16_t i_stream_type = pmtn_get_streamtype(p_es);

   if (!this->b_do_remap && !p_output->config.b_do_remap)
      return i_pid;

   cLbugf(cL::dbg_dvb, "REMAP: Found elementary stream type 0x%02x with original PID 0x%x (%u):\n", i_stream_type, i_pid, i_pid);

   switch (i_stream_type) {
      case 0x03: /* audio MPEG-1 */
      case 0x04: /* audio MPEG-2 */
      case 0x0f: /* audio AAC ADTS */
      case 0x11: /* audio AAC LATM */
      case 0x81: /* ATSC AC-3 */
      case 0x87: /* ATSC Enhanced AC-3 */
         if (this->b_do_remap) {
            i_newpid = this->pi_newpids[cLdvbdemux::I_APID];
         } else {
            i_newpid = p_output->config.pi_confpids[cLdvbdemux::I_APID];
         }
         break;
      case 0x01: /* video MPEG-1 */
      case 0x02: /* video MPEG-2 */
      case 0x10: /* video MPEG-4 */
      case 0x1b: /* video H264 */
      case 0x24: /* video H265 */
      case 0x42: /* video AVS */
         if (this->b_do_remap) {
            i_newpid = this->pi_newpids[cLdvbdemux::I_VPID];
         } else {
            i_newpid = p_output->config.pi_confpids[cLdvbdemux::I_VPID];
         }
         break;
      case 0x06: { /* PES Private Data - We must check the descriptors */
         /* By default, nothing identified */
         uint8_t SubStreamType = CLDVB_N_MAP_PIDS;
         uint16_t j = 0;
         const uint8_t *p_desc;
         /* Loop over the descriptors */
         while ((p_desc = descs_get_desc(pmtn_get_descs(p_es), j)) != (uint8_t *) 0) {
            /* Get the descriptor tag */
            uint8_t i_tag = desc_get_tag(p_desc);
            j++;
            /* Check if the tag is: A/52, Enhanced A/52, DTS, AAC */
            if (i_tag == 0x6a || i_tag == 0x7a || i_tag == 0x7b || i_tag == 0x7c)
               SubStreamType=cLdvbdemux::I_APID;
            /* Check if the tag is: VBI + teletext, teletext, dvbsub */
            if (i_tag == 0x46 || i_tag == 0x56 || i_tag == 0x59)
               SubStreamType=cLdvbdemux::I_SPUPID;
         }
         /* Audio found */
         if (SubStreamType==cLdvbdemux::I_APID) {
            cLbug(cL::dbg_dvb, "REMAP: PES Private Data stream identified as [Audio]\n");
            if (this->b_do_remap) {
               i_newpid = this->pi_newpids[cLdvbdemux::I_APID];
            } else {
               i_newpid = p_output->config.pi_confpids[cLdvbdemux::I_APID];
            }
         }
         /* Subtitle found */
         if (SubStreamType==cLdvbdemux::I_SPUPID) {
            cLbug(cL::dbg_dvb, "REMAP: PES Private Data stream identified as [Subtitle]\n");
            if (this->b_do_remap) {
               i_newpid = this->pi_newpids[cLdvbdemux::I_SPUPID];
            } else {
               i_newpid = p_output->config.pi_confpids[cLdvbdemux::I_SPUPID];
            }
         }
         break;
      }
   }

   if (!i_newpid)
      return i_pid;

   /* Got the new base for the mapped pid. Find the next free one
       we do this to ensure that multiple audios get unique pids */
   while (p_output->pi_freepids[i_newpid] != UNUSED_PID)
      i_newpid++;
   p_output->pi_freepids[i_newpid] = i_pid;  /* Mark as in use */
   p_output->pi_newpids[i_pid] = i_newpid;   /* Save the new pid */

   cLbugf(cL::dbg_dvb, "REMAP: => Elementary stream is remapped to PID 0x%x (%u)\n", i_newpid, i_newpid);

   return i_newpid;
}

cLdvbdemux::sid_t *cLdvbdemux::FindSID(uint16_t i_sid)
{
   for (int i = 0; i < this->i_nb_sids; i++) {
      sid_t *p_sid = this->pp_sids[i];
      if (p_sid->i_sid == i_sid)
         return p_sid;
   }
   return (sid_t *) 0;
}

void cLdvbdemux::cLdvbdemux::PrintCb(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *)p;
   cLdvbdemux *pobj = (cLdvbdemux *)w->data;

   uint64_t i_bitrate = pobj->i_nb_packets * TS_SIZE * 8 * 1000000 / pobj->i_print_period;
   cLbugf(cL::dbg_dvb, "bitrate: %"PRIu64"\n", i_bitrate);
   pobj->i_nb_packets = 0;
   if (pobj->i_nb_invalids) {
      cLbugf(cL::dbg_dvb, "invalids: %"PRIu64"\n", pobj->i_nb_invalids);
      pobj->i_nb_invalids = 0;
   }
   if (pobj->i_nb_discontinuities) {
      cLbugf(cL::dbg_dvb, "discontinuities: %"PRIu64"\n", pobj->i_nb_discontinuities);
      pobj->i_nb_discontinuities = 0;
   }
   if (pobj->i_nb_errors) {
      cLbugf(cL::dbg_dvb, "errors: %"PRIu64"\n", pobj->i_nb_errors);
      pobj->i_nb_errors = 0;
   }
}

void cLdvbdemux::cLdvbdemux::PrintESCb(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *)p;
   cLdvbdemux *pobj = (cLdvbdemux *)w->data;

   ts_pid_t *p_pid = container_of(w, ts_pid_t, timeout_watcher);
   uint16_t i_pid = p_pid - pobj->p_pids;
   cLbugf(cL::dbg_dvb, "pid: %"PRIu16" down\n", i_pid);
   cLev_timer_stop(loop, w);
   p_pid->i_pes_status = -1;
}

void cLdvbdemux::PrintES(uint16_t i_pid)
{
   const ts_pid_t *p_pid = &this->p_pids[i_pid];
   cLbugf(cL::dbg_dvb, "pid: %"PRIu16" up(pes:%d)\n", i_pid, (p_pid->i_pes_status == 1 ? 1 : 0));
}

void cLdvbdemux::demux_Open()
{
   memset(this->p_pids, 0, sizeof(this->p_pids));

   this->dev_Open();

   for (int i = 0; i < MAX_PIDS; i++) {
      this->p_pids[i].i_last_cc = -1;
      this->p_pids[i].i_demux_fd = -1;
      psi_assemble_init(&this->p_pids[i].p_psi_buffer, &this->p_pids[i].i_psi_buffer_used);
      this->p_pids[i].i_pes_status = -1;
   }

   if (this->b_budget_mode)
      this->i_demux_fd = this->dev_SetFilter(8192);

   psi_table_init(this->pp_current_pat_sections);
   psi_table_init(this->pp_next_pat_sections);
   this->SetPID(PAT_PID);
   this->p_pids[PAT_PID].i_psi_refcount++;

   if (this->b_enable_emm) {
      psi_table_init(this->pp_current_cat_sections);
      psi_table_init(this->pp_next_cat_sections);
      this->SetPID_EMM(CAT_PID);
      this->p_pids[CAT_PID].i_psi_refcount++;
   }

   this->SetPID(NIT_PID);
   this->p_pids[NIT_PID].i_psi_refcount++;

   psi_table_init(this->pp_current_sdt_sections);
   psi_table_init(this->pp_next_sdt_sections);
   this->SetPID(SDT_PID);
   this->p_pids[SDT_PID].i_psi_refcount++;

   this->SetPID(EIT_PID);
   this->p_pids[EIT_PID].i_psi_refcount++;

   this->SetPID(RST_PID);

   this->SetPID(TDT_PID);

   if (this->i_print_period) {
      this->print_watcher.data = this;
      cLev_timer_init(&this->print_watcher, cLdvbdemux::PrintCb, this->i_print_period / 1000000., this->i_print_period / 1000000.);
      cLev_timer_start(this->event_loop, &this->print_watcher);
   }

   if (this->psz_mrtg_file != (char *) 0)
      this->pmrtg->mrtgInit(this->psz_mrtg_file);

   if (this->i_priority > 0) {
      struct sched_param param;
      memset(&param, 0, sizeof(struct sched_param));
      param.sched_priority = this->i_priority;
      int e = pthread_setschedparam(pthread_self(), SCHED_RR, &param);
      if (e) {
         cLbugf(cL::dbg_dvb, "couldn't set thread priority: %s\n", strerror(e));
      }
   }

   this->config_ReadFile();

   if (this->i_quit_timeout_duration) {
      quit_watcher.data = this;
      cLev_timer_init(&quit_watcher, cLdvbdemux::break_cb, this->i_quit_timeout_duration / 1000000., 0);
      cLev_timer_start(this->event_loop, &quit_watcher);
   }

   this->outputs_Init();
}

void cLdvbdemux::demux_Close(void)
{
   this->pmrtg->mrtgClose();
   this->outputs_Close(this->i_nb_outputs);

   psi_table_free(this->pp_current_pat_sections);
   psi_table_free(this->pp_next_pat_sections);
   psi_table_free(this->pp_current_cat_sections);
   psi_table_free(this->pp_next_cat_sections);
   psi_table_free(this->pp_current_nit_sections);
   psi_table_free(this->pp_next_nit_sections);
   psi_table_free(this->pp_current_sdt_sections);
   psi_table_free(this->pp_next_sdt_sections);

   int i;
   for (i = 0; i < MAX_PIDS; i++) {
      cLev_timer_stop(this->event_loop, &this->p_pids[i].timeout_watcher);
      ::free(this->p_pids[i].p_psi_buffer);
      ::free(this->p_pids[i].pp_outputs);
   }

   for (i = 0; i < this->i_nb_sids; i++) {
      sid_t *p_sid = this->pp_sids[i];
      for (int r = 0; r < MAX_EIT_TABLES; r++) {
         psi_table_free(p_sid->eit_table[r].data);
      }
      ::free(p_sid->p_current_pmt);
      ::free(p_sid);
   }
   ::free(this->pp_sids);

   if (this->i_print_period)
      cLev_timer_stop(this->event_loop, &this->print_watcher);

   this->block_Vacuum();
}

void cLdvbdemux::demux_Run(block_t *p_ts)
{
   this->i_wallclock = this->mdate();
   this->pmrtg->mrtgAnalyse(p_ts);
   this->SetDTS(p_ts);

   while (p_ts != (block_t *) 0) {
      block_t *p_next = p_ts->p_next;
      p_ts->p_next = NULL;
      this->demux_Handle(p_ts);
      p_ts = p_next;
   }
}

void cLdvbdemux::demux_Handle(block_t *p_ts)
{
   uint16_t i_pid = ts_get_pid(p_ts->p_ts);
   ts_pid_t *p_pid = &this->p_pids[i_pid];
   uint8_t i_cc = ts_get_cc(p_ts->p_ts);
   int i;

   this->i_nb_packets++;

   if (!ts_validate(p_ts->p_ts)) {
      cLbug(cL::dbg_dvb, "lost TS sync\n");
      this->block_Delete(p_ts);
      this->i_nb_invalids++;
      return;
   }

   if (i_pid != PADDING_PID)
      p_pid->info.i_scrambling = ts_get_scrambling(p_ts->p_ts);

   p_pid->info.i_last_packet_ts = this->i_wallclock;
   p_pid->info.i_packets++;

   p_pid->i_packets_passed++;

   /* Calculate bytes_per_sec */
   if (this->i_wallclock > p_pid->i_bytes_ts + 1000000) {
      p_pid->info.i_bytes_per_sec = p_pid->i_packets_passed * TS_SIZE;
      p_pid->i_packets_passed = 0;
      p_pid->i_bytes_ts = this->i_wallclock;
   }

   if (p_pid->info.i_first_packet_ts == 0)
      p_pid->info.i_first_packet_ts = this->i_wallclock;

   if (i_pid != PADDING_PID && p_pid->i_last_cc != -1
         && !ts_check_duplicate(i_cc, p_pid->i_last_cc)
         && ts_check_discontinuity(i_cc, p_pid->i_last_cc))
   {
      unsigned int expected_cc = (p_pid->i_last_cc + 1) & 0x0f;
      uint16_t i_sid = 0;
      const char *pid_desc = this->get_pid_desc(i_pid, &i_sid);

      p_pid->info.i_cc_errors++;
      this->i_nb_discontinuities++;

      cLbugf(cL::dbg_dvb, "TS discontinuity on pid %4hu expected_cc %2u got %2u (%s, sid %d)\n", i_pid, expected_cc, i_cc, pid_desc, i_sid);
   }

   if (ts_get_transporterror(p_ts->p_ts)) {
      uint16_t i_sid = 0;
      const char *pid_desc = this->get_pid_desc(i_pid, &i_sid);

      p_pid->info.i_transport_errors++;

      cLbugf(cL::dbg_dvb, "transport_error_indicator on pid %hu (%s, sid %u)\n", i_pid, pid_desc, i_sid);

      this->i_nb_errors++;
      this->i_tuner_errors++;
      this->i_last_error = this->i_wallclock;
   } else
   if (this->i_wallclock > this->i_last_error + WATCHDOG_WAIT) {
      this->i_tuner_errors = 0;
   }

   if (this->i_tuner_errors > MAX_ERRORS) {
      this->i_tuner_errors = 0;
      cLbug(cL::dbg_dvb, "too many transport errors, tuning again\n");
      this->dev_Reset();
   }

   if (this->i_es_timeout) {
      int i_pes_status = -1;
      if (ts_get_scrambling(p_ts->p_ts)) {
         i_pes_status = 0;
      } else
      if (ts_get_unitstart(p_ts->p_ts)) {
         uint8_t *p_payload = ts_payload(p_ts->p_ts);
         if (p_payload + 3 < p_ts->p_ts + TS_SIZE)
            i_pes_status = pes_validate(p_payload) ? 1 : 0;
      }

      if (i_pes_status != -1) {
         if (p_pid->i_pes_status == -1) {
            p_pid->i_pes_status = i_pes_status;
            this->PrintES(i_pid);

            if (i_pid != TDT_PID) {
               p_pid->timeout_watcher.data = this;
               cLev_timer_init(&p_pid->timeout_watcher, cLdvbdemux::PrintESCb, this->i_es_timeout / 1000000., this->i_es_timeout / 1000000.);
               cLev_timer_start(this->event_loop, &p_pid->timeout_watcher);
            } else {
               p_pid->timeout_watcher.data = this;
               cLev_timer_init(&p_pid->timeout_watcher, cLdvbdemux::PrintESCb, 30, 30);
               cLev_timer_start(this->event_loop, &p_pid->timeout_watcher);
            }
         } else {
            if (p_pid->i_pes_status != i_pes_status) {
               p_pid->i_pes_status = i_pes_status;
               this->PrintES(i_pid);
            }
            cLev_timer_again(this->event_loop, &p_pid->timeout_watcher);
         }
      }
   }

   if (!ts_get_transporterror(p_ts->p_ts)) {
      /* PSI parsing */
      if (i_pid == TDT_PID || i_pid == RST_PID) {
         this->SendTDT(p_ts);
      } else
      if (p_pid->i_psi_refcount) {
         this->HandlePSIPacket(p_ts->p_ts, p_ts->i_dts);
      } else
      if (this->i_delsysatsc && (i_pid != PADDING_PID)) {
         if (ts_has_payload(p_ts->p_ts)) {
            this->HandlePSIPacket(p_ts->p_ts, p_ts->i_dts);
         }
      }
      if (this->b_enable_emm && p_pid->b_emm)
         this->SendEMM(p_ts);
   }

   p_pid->i_last_cc = i_cc;

   /* Output */
   for (i = 0; i < p_pid->i_nb_outputs; i++) {
      output_t *p_output = p_pid->pp_outputs[i];
      if (p_output != (output_t *) 0) {
         if (this->i_ca_handle && (p_output->config.i_config & OUTPUT_WATCH) && ts_get_unitstart(p_ts->p_ts)) {
            uint8_t *p_payload;
            if (ts_get_scrambling(p_ts->p_ts) || (p_pid->b_pes && (p_payload = ts_payload(p_ts->p_ts)) + 3 < p_ts->p_ts + TS_SIZE && !pes_validate(p_payload))) {
               if (this->i_wallclock > this->i_last_reset + WATCHDOG_REFRACTORY_PERIOD) {
                  p_output->i_nb_errors++;
                  p_output->i_last_error = this->i_wallclock;
               }
            } else
            if (this->i_wallclock > p_output->i_last_error + WATCHDOG_WAIT) {
               p_output->i_nb_errors = 0;
            }

            if (p_output->i_nb_errors > MAX_ERRORS) {
               for (int j = 0; j < this->i_nb_outputs; j++)
                  this->pp_outputs[j]->i_nb_errors = 0;

               cLbugf(cL::dbg_dvb, "too many errors for stream %s, resetting\n", p_output->config.psz_displayname);
               this->i_last_reset = this->i_wallclock;
               this->en50221_Reset();
            }
         }

         if (p_output->i_pcr_pid != i_pid || (ts_has_adaptation(p_ts->p_ts) && ts_get_adaptation(p_ts->p_ts) && tsaf_has_pcr(p_ts->p_ts)))
            output_Put(p_output, p_ts);

         if (p_output->p_eit_ts_buffer != (block_t *) 0 && p_ts->i_dts > p_output->p_eit_ts_buffer->i_dts + MAX_EIT_RETENTION)
            this->FlushEIT(p_output, p_ts->i_dts);
      }
   }

   for (i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if (!(p_output->config.i_config & OUTPUT_VALID) || !p_output->config.b_passthrough)
         continue;

      this->output_Put(p_output, p_ts);
   }

   if (this->output_dup->config.i_config & OUTPUT_VALID)
      this->output_Put(this->output_dup, p_ts);

   p_ts->i_refcount--;
   if (!p_ts->i_refcount)
      this->block_Delete(p_ts);
}

bool cLdvbdemux::IsIn(const uint16_t *pi_pids, int i_nb_pids, uint16_t i_pid)
{
   int i;
   for (i = 0; i < i_nb_pids; i++)
      if (i_pid == pi_pids[i]) break;
   return (i != i_nb_pids);
}

void cLdvbdemux::demux_Change(output_t *p_output, const output_config_t *p_config)
{
   uint16_t *pi_wanted_pids, *pi_current_pids;
   int i_nb_wanted_pids, i_nb_current_pids;
   uint16_t i_wanted_pcr_pid, i_current_pcr_pid;

   uint16_t i_old_sid = p_output->config.i_sid;
   uint16_t i_sid = p_config->i_sid;
   uint16_t *pi_old_pids = p_output->config.pi_pids;
   uint16_t *pi_pids = p_config->pi_pids;
   int i_old_nb_pids = p_output->config.i_nb_pids;
   int i_nb_pids = p_config->i_nb_pids;

   bool b_sid_change = i_sid != i_old_sid;
   bool b_pid_change = false, b_tsid_change = false;
   bool b_dvb_change = !!((p_output->config.i_config ^ p_config->i_config) & OUTPUT_DVB);
   bool b_epg_change = !!((p_output->config.i_config ^ p_config->i_config) & OUTPUT_EPG);
   bool b_network_change = (this->dvb_string_cmp(&p_output->config.network_name, &p_config->network_name) || p_output->config.i_network_id != p_config->i_network_id);
   bool b_service_name_change = (this->dvb_string_cmp(&p_output->config.service_name, &p_config->service_name) || this->dvb_string_cmp(&p_output->config.provider_name, &p_config->provider_name));
   bool b_remap_change = p_output->config.i_new_sid != p_config->i_new_sid ||
         p_output->config.i_onid != p_config->i_onid ||
         p_output->config.b_do_remap != p_config->b_do_remap ||
         p_output->config.pi_confpids[cLdvbdemux::I_PMTPID] != p_config->pi_confpids[cLdvbdemux::I_PMTPID] ||
         p_output->config.pi_confpids[cLdvbdemux::I_APID] != p_config->pi_confpids[cLdvbdemux::I_APID] ||
         p_output->config.pi_confpids[cLdvbdemux::I_VPID] != p_config->pi_confpids[cLdvbdemux::I_VPID] ||
         p_output->config.pi_confpids[cLdvbdemux::I_SPUPID] != p_config->pi_confpids[cLdvbdemux::I_SPUPID];
   int i;

   p_output->config.i_config = p_config->i_config;
   p_output->config.i_network_id = p_config->i_network_id;
   p_output->config.i_new_sid = p_config->i_new_sid;
   p_output->config.i_onid = p_config->i_onid;
   p_output->config.b_do_remap = p_config->b_do_remap;
   memcpy(p_output->config.pi_confpids, p_config->pi_confpids, sizeof(uint16_t) * CLDVB_N_MAP_PIDS);

   /* Change output settings related to names. */
   this->dvb_string_clean(&p_output->config.network_name);
   this->dvb_string_clean(&p_output->config.service_name);
   this->dvb_string_clean(&p_output->config.provider_name);
   this->dvb_string_copy(&p_output->config.network_name, &p_config->network_name);
   this->dvb_string_copy(&p_output->config.service_name, &p_config->service_name);
   this->dvb_string_copy(&p_output->config.provider_name, &p_config->provider_name);

   if (p_config->i_tsid != -1 && p_output->config.i_tsid != p_config->i_tsid) {
      p_output->i_tsid = p_output->config.i_tsid = p_config->i_tsid;
      b_tsid_change = true;
   }
   if (p_config->i_tsid == -1 && p_output->config.i_tsid != -1) {
      p_output->config.i_tsid = p_config->i_tsid;
      if (psi_table_validate(this->pp_current_pat_sections) && !this->b_random_tsid) {
         p_output->i_tsid = psi_table_get_tableidext(this->pp_current_pat_sections);
      } else {
         p_output->i_tsid = rand() & 0xffff;
      }
      b_tsid_change = true;
   }

   if (p_config->b_passthrough == p_output->config.b_passthrough && !b_sid_change && p_config->i_nb_pids == p_output->config.i_nb_pids && (!p_config->i_nb_pids || !memcmp(p_output->config.pi_pids, p_config->pi_pids, p_config->i_nb_pids * sizeof(uint16_t))))
      goto out_change;

   this->GetPIDS(&pi_wanted_pids, &i_nb_wanted_pids, &i_wanted_pcr_pid, i_sid, pi_pids, i_nb_pids);
   this->GetPIDS(&pi_current_pids, &i_nb_current_pids, &i_current_pcr_pid, i_old_sid, pi_old_pids, i_old_nb_pids);

   if (b_sid_change && i_old_sid) {
      sid_t *p_old_sid = this->FindSID(i_old_sid);
      p_output->config.i_sid = p_config->i_sid;

      if (p_old_sid != (sid_t *) 0) {
         if (i_sid != i_old_sid)
            this->UnselectPMT(i_old_sid, p_old_sid->i_pmt_pid);

         if (this->i_ca_handle && !this->SIDIsSelected(i_old_sid) && p_old_sid->p_current_pmt != (uint8_t *) 0 && this->PMTNeedsDescrambling(p_old_sid->p_current_pmt))
            this->en50221_DeletePMT(p_old_sid->p_current_pmt);
      }
   }

   for (i = 0; i < i_nb_current_pids; i++) {
      if (!this->IsIn(pi_wanted_pids, i_nb_wanted_pids, pi_current_pids[i])) {
         this->StopPID(p_output, pi_current_pids[i]);
         b_pid_change = true;
      }
   }

   if (b_sid_change && this->i_ca_handle && i_old_sid && this->SIDIsSelected(i_old_sid)) {
      sid_t *p_old_sid = this->FindSID(i_old_sid);
      if (p_old_sid != (sid_t *) 0 && p_old_sid->p_current_pmt != (uint8_t *) 0 && this->PMTNeedsDescrambling(p_old_sid->p_current_pmt))
         this->en50221_UpdatePMT(p_old_sid->p_current_pmt);
   }

   for (i = 0; i < i_nb_wanted_pids; i++) {
      if (!this->IsIn(pi_current_pids, i_nb_current_pids, pi_wanted_pids[i])) {
         this->StartPID(p_output, pi_wanted_pids[i]);
         b_pid_change = true;
      }
   }

   ::free(pi_wanted_pids);
   ::free(pi_current_pids);
   p_output->i_pcr_pid = i_wanted_pcr_pid;

   if (b_sid_change && i_sid) {
      sid_t *p_sid = this->FindSID(i_sid);
      p_output->config.i_sid = i_old_sid;

      if (p_sid != (sid_t *) 0) {
         if (i_sid != i_old_sid)
            this->SelectPMT(i_sid, p_sid->i_pmt_pid);

         if (this->i_ca_handle && !this->SIDIsSelected(i_sid)  && p_sid->p_current_pmt != (uint8_t *) 0 && this->PMTNeedsDescrambling(p_sid->p_current_pmt))
            this->en50221_AddPMT(p_sid->p_current_pmt);
      }
   }

   if (this->i_ca_handle && i_sid && this->SIDIsSelected(i_sid)) {
      sid_t *p_sid = this->FindSID(i_sid);
      if (p_sid != (sid_t *) 0 && p_sid->p_current_pmt != (uint8_t *) 0 && this->PMTNeedsDescrambling(p_sid->p_current_pmt))
         this->en50221_UpdatePMT(p_sid->p_current_pmt);
   }

   p_output->config.b_passthrough = p_config->b_passthrough;
   p_output->config.i_sid = i_sid;
   ::free(p_output->config.pi_pids);
   p_output->config.pi_pids = cLmalloc(uint16_t, i_nb_pids);
   memcpy(p_output->config.pi_pids, pi_pids, sizeof(uint16_t) * i_nb_pids);
   p_output->config.i_nb_pids = i_nb_pids;

   out_change:
   if (b_sid_change || b_pid_change || b_tsid_change || b_dvb_change || b_network_change || b_service_name_change || b_remap_change) {
      cLbugf(cL::dbg_dvb, "change %s%s%s%s%s%s%s",
                 b_sid_change ? "sid " : "",
                 b_pid_change ? "pid " : "",
                 b_tsid_change ? "tsid " : "",
                 b_dvb_change ? "dvb " : "",
                 b_network_change ? "network " : "",
                 b_service_name_change ? "service_name " : "",
                 b_remap_change ? "remap " : ""
      );
   }
   if (b_sid_change || b_remap_change) {
      this->NewSDT(p_output);
      this->NewNIT(p_output);
      this->NewPAT(p_output);
      this->NewPMT(p_output);
   } else {
      if (b_tsid_change) {
         this->NewSDT(p_output);
         this->NewNIT(p_output);
         this->NewPAT(p_output);
      } else
      if (b_dvb_change) {
         this->NewNIT(p_output);
         this->NewPAT(p_output);
      } else
      if (b_network_change) {
         this->NewNIT(p_output);
      }
      if (!b_tsid_change && (b_service_name_change || b_epg_change))
         this->NewSDT(p_output);
      if (b_pid_change)
         this->NewPMT(p_output);
   }
}

void cLdvbdemux::SetDTS(block_t *p_list)
{
   int i_nb_ts = 0, i;
   mtime_t i_duration;
   block_t *p_ts = p_list;

   while (p_ts != (block_t *) 0) {
      i_nb_ts++;
      p_ts = p_ts->p_next;
   }

   /* We suppose the stream is CBR, at least between two consecutive read().
    * This is especially true in budget mode */
   if (this->i_last_dts == -1) {
      i_duration = 0;
   } else {
      i_duration = this->i_wallclock - this->i_last_dts;
   }

   p_ts = p_list;
   i = i_nb_ts - 1;
   while (p_ts != (block_t *) 0) {
      p_ts->i_dts = this->i_wallclock - i_duration * i / i_nb_ts;
      i--;
      p_ts = p_ts->p_next;
   }

   this->i_last_dts = this->i_wallclock;
}

void cLdvbdemux::SetPID(uint16_t i_pid)
{
   this->p_pids[i_pid].i_refcount++;

   if (!this->b_budget_mode && this->p_pids[i_pid].i_refcount && this->p_pids[i_pid].i_demux_fd == -1)
      this->p_pids[i_pid].i_demux_fd = this->dev_SetFilter(i_pid);
}

void cLdvbdemux::SetPID_EMM(uint16_t i_pid)
{
   this->SetPID(i_pid);
   this->p_pids[i_pid].b_emm = true;
}

void cLdvbdemux::UnsetPID(uint16_t i_pid)
{
   this->p_pids[i_pid].i_refcount--;

   if (!this->b_budget_mode && !this->p_pids[i_pid].i_refcount && this->p_pids[i_pid].i_demux_fd != -1) {
      this->dev_UnsetFilter(this->p_pids[i_pid].i_demux_fd, i_pid);
      this->p_pids[i_pid].i_demux_fd = -1;
      this->p_pids[i_pid].b_emm = false;
   }
}

void cLdvbdemux::StartPID(output_t *p_output, uint16_t i_pid)
{
   int j;

   for (j = 0; j < this->p_pids[i_pid].i_nb_outputs; j++)
      if (this->p_pids[i_pid].pp_outputs[j] == p_output)
         break;

   if (j == this->p_pids[i_pid].i_nb_outputs) {
      for (j = 0; j < this->p_pids[i_pid].i_nb_outputs; j++) {
         if (this->p_pids[i_pid].pp_outputs[j] == (output_t *) 0)
            break;
      }

      if (j == this->p_pids[i_pid].i_nb_outputs) {
         this->p_pids[i_pid].i_nb_outputs++;
         this->p_pids[i_pid].pp_outputs = (output_t **)realloc(this->p_pids[i_pid].pp_outputs, sizeof(output_t *) * this->p_pids[i_pid].i_nb_outputs);
      }

      this->p_pids[i_pid].pp_outputs[j] = p_output;
      this->SetPID(i_pid);
   }
}

void cLdvbdemux::StopPID(output_t *p_output, uint16_t i_pid)
{
   int j;

   for (j = 0; j < this->p_pids[i_pid].i_nb_outputs; j++) {
      if (this->p_pids[i_pid].pp_outputs[j] != (output_t *) 0) {
         if (this->p_pids[i_pid].pp_outputs[j] == p_output)
            break;
      }
   }

   if (j != this->p_pids[i_pid].i_nb_outputs) {
      this->p_pids[i_pid].pp_outputs[j] = (output_t *) 0;
      this->UnsetPID(i_pid);
   }
}

void cLdvbdemux::SelectPID(uint16_t i_sid, uint16_t i_pid, bool b_pcr)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      if ((this->pp_outputs[i]->config.i_config & OUTPUT_VALID) && pp_outputs[i]->config.i_sid == i_sid) {

         if (pp_outputs[i]->config.i_nb_pids && !this->IsIn(pp_outputs[i]->config.pi_pids, pp_outputs[i]->config.i_nb_pids, i_pid)) {
            if (b_pcr) {
               pp_outputs[i]->i_pcr_pid = i_pid;
            } else {
               continue;
            }
         }
         this->StartPID(this->pp_outputs[i], i_pid);
      }
   }
}

void cLdvbdemux::UnselectPID(uint16_t i_sid, uint16_t i_pid)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      if ((this->pp_outputs[i]->config.i_config & OUTPUT_VALID) && this->pp_outputs[i]->config.i_sid == i_sid && !this->pp_outputs[i]->config.i_nb_pids)
         this->StopPID(this->pp_outputs[i], i_pid);
   }
}

void cLdvbdemux::SelectPMT(uint16_t i_sid, uint16_t i_pid)
{
   this->p_pids[i_pid].i_psi_refcount++;
   this->p_pids[i_pid].b_pes = false;

   if (this->b_select_pmts) {
      this->SetPID(i_pid);
   } else {
      for (int i = 0; i < this->i_nb_outputs; i++) {
         if ((this->pp_outputs[i]->config.i_config & OUTPUT_VALID) && this->pp_outputs[i]->config.i_sid == i_sid)
            this->SetPID(i_pid);
      }
   }
}

void cLdvbdemux::UnselectPMT(uint16_t i_sid, uint16_t i_pid)
{
   this->p_pids[i_pid].i_psi_refcount--;
   if (!this->p_pids[i_pid].i_psi_refcount)
      psi_assemble_reset(&this->p_pids[i_pid].p_psi_buffer, &this->p_pids[i_pid].i_psi_buffer_used);

   if (this->b_select_pmts) {
      this->UnsetPID(i_pid);
   } else {
      for (int i = 0; i < this->i_nb_outputs; i++) {
         if ((this->pp_outputs[i]->config.i_config & OUTPUT_VALID) && this->pp_outputs[i]->config.i_sid == i_sid)
            this->UnsetPID(i_pid);
      }
   }
}

void cLdvbdemux::GetPIDS(uint16_t **ppi_wanted_pids, int *pi_nb_wanted_pids, uint16_t *pi_wanted_pcr_pid, uint16_t i_sid, const uint16_t *pi_pids, int i_nb_pids)
{
   sid_t *p_sid;
   uint8_t *p_pmt;
   uint16_t i_pmt_pid, i_pcr_pid;
   uint8_t *p_es;
   uint8_t j;
   const uint8_t *p_desc;

   *pi_wanted_pcr_pid = 0;

   if (i_nb_pids || i_sid == 0) {
      *pi_nb_wanted_pids = i_nb_pids;
      *ppi_wanted_pids = cLmalloc(uint16_t, i_nb_pids);
      memcpy(*ppi_wanted_pids, pi_pids, sizeof(uint16_t) * i_nb_pids);
      if (i_sid == 0)
         return;
   } else {
      *pi_nb_wanted_pids = 0;
      *ppi_wanted_pids = NULL;
   }

   p_sid = this->FindSID(i_sid);
   if (p_sid == (sid_t *) 0)
      return;

   p_pmt = p_sid->p_current_pmt;
   i_pmt_pid = p_sid->i_pmt_pid;
   if (p_pmt == (uint8_t *) 0) {
      cLbugf(cL::dbg_dvb, "no current PMT on sid %d\n", i_sid);
      return;
   }

   i_pcr_pid = pmt_get_pcrpid(p_pmt);
   j = 0;
   while ((p_es = pmt_get_es(p_pmt, j)) != (uint8_t *) 0) {
      j++;

      uint16_t i_pid = pmtn_get_pid(p_es);
      bool b_select;
      if (i_nb_pids) {
         b_select = this->IsIn(pi_pids, i_nb_pids, i_pid);
      } else {
         b_select = this->PIDWouldBeSelected(p_es);
         if (b_select) {
            *ppi_wanted_pids = (uint16_t *)::realloc(*ppi_wanted_pids, (*pi_nb_wanted_pids + 1) * sizeof(uint16_t));
            (*ppi_wanted_pids)[(*pi_nb_wanted_pids)++] = i_pid;
         }
      }

      if (b_select && b_enable_ecm) {
         uint8_t k = 0;
         while ((p_desc = descs_get_desc(pmtn_get_descs(p_es), k++)) != NULL) {
            if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
               continue;
            *ppi_wanted_pids = (uint16_t *)::realloc(*ppi_wanted_pids, (*pi_nb_wanted_pids + 1) * sizeof(uint16_t));
            (*ppi_wanted_pids)[(*pi_nb_wanted_pids)++] = desc09_get_pid(p_desc);
         }
      }
   }
   if (b_enable_ecm) {
      j = 0;
      while ((p_desc = descs_get_desc(pmt_get_descs(p_pmt), j++)) != NULL) {
         if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
            continue;
         *ppi_wanted_pids = (uint16_t *)realloc(*ppi_wanted_pids, (*pi_nb_wanted_pids + 1) * sizeof(uint16_t));
         (*ppi_wanted_pids)[(*pi_nb_wanted_pids)++] = desc09_get_pid(p_desc);
      }
   }

   if (i_pcr_pid != PADDING_PID && i_pcr_pid != i_pmt_pid && !this->IsIn(*ppi_wanted_pids, *pi_nb_wanted_pids, i_pcr_pid)) {
      *ppi_wanted_pids = (uint16_t *)realloc(*ppi_wanted_pids, (*pi_nb_wanted_pids + 1) * sizeof(uint16_t));
      (*ppi_wanted_pids)[(*pi_nb_wanted_pids)++] = i_pcr_pid;
      /* We only need the PCR packets of this stream (incomplete) */
      *pi_wanted_pcr_pid = i_pcr_pid;
      cLbugf(cL::dbg_dvb, "Requesting partial PCR PID %"PRIu16, i_pcr_pid);
   }
}

void cLdvbdemux::OutputPSISection(output_t *p_output, uint8_t *p_section, uint16_t i_pid, uint8_t *pi_cc, mtime_t i_dts, block_t **pp_ts_buffer, uint8_t *pi_ts_buffer_offset)
{
   uint16_t i_section_length = psi_get_length(p_section) + PSI_HEADER_SIZE;
   uint16_t i_section_offset = 0;

   do {
      block_t *p_block;
      uint8_t *p;
      uint8_t i_ts_offset;
      bool b_append = (pp_ts_buffer != NULL && *pp_ts_buffer != NULL);

      if (b_append) {
         p_block = *pp_ts_buffer;
         i_ts_offset = *pi_ts_buffer_offset;
      } else {
         p_block = this->block_New();
         p_block->i_dts = i_dts;
         i_ts_offset = 0;
      }
      p = p_block->p_ts;

      psi_split_section(p, &i_ts_offset, p_section, &i_section_offset);

      if (!b_append) {
         ts_set_pid(p, i_pid);
         ts_set_cc(p, *pi_cc);
         (*pi_cc)++;
         *pi_cc &= 0xf;
      }

      if (i_section_offset == i_section_length) {
         if (i_ts_offset < TS_SIZE - MIN_SECTION_FRAGMENT && pp_ts_buffer != (block_t **) 0) {
            *pp_ts_buffer = p_block;
            *pi_ts_buffer_offset = i_ts_offset;
            break;
         } else {
            psi_split_end(p, &i_ts_offset);
         }
      }

      p_block->i_dts = i_dts;
      p_block->i_refcount--;
      this->output_Put(p_output, p_block);
      if (pp_ts_buffer != (block_t **) 0) {
         *pp_ts_buffer = (block_t *) 0;
         *pi_ts_buffer_offset = 0;
      }
   } while (i_section_offset < i_section_length);
}

void cLdvbdemux::SendPAT(mtime_t i_dts)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];
      if (!(p_output->config.i_config & OUTPUT_VALID) || p_output->config.b_passthrough)
         continue;

      if (p_output->p_pat_section == (uint8_t *) 0 && psi_table_validate(this->pp_current_pat_sections)) {
         /* SID doesn't exist - build an empty PAT. */
         uint8_t *p;
         p_output->i_pat_version++;

         p = p_output->p_pat_section = psi_allocate();
         pat_init(p);
         pat_set_length(p, 0);
         pat_set_tsid(p, p_output->i_tsid);
         psi_set_version(p, p_output->i_pat_version);
         psi_set_current(p);
         psi_set_section(p, 0);
         psi_set_lastsection(p, 0);
         psi_set_crc(p_output->p_pat_section);
      }

      if (p_output->p_pat_section != (uint8_t *) 0)
         this->OutputPSISection(p_output, p_output->p_pat_section, PAT_PID, &p_output->i_pat_cc, i_dts, (block_t **) 0, (uint8_t *) 0);
   }
}

void cLdvbdemux::SendPMT(sid_t *p_sid, mtime_t i_dts)
{
   int i_pmt_pid = p_sid->i_pmt_pid;

   if (this->b_do_remap)
      i_pmt_pid = this->pi_newpids[ cLdvbdemux::I_PMTPID ];

   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID) && p_output->config.i_sid == p_sid->i_sid && p_output->p_pmt_section != (uint8_t *) 0) {
         if (p_output->config.b_do_remap && p_output->config.pi_confpids[cLdvbdemux::I_PMTPID])
            i_pmt_pid = p_output->config.pi_confpids[cLdvbdemux::I_PMTPID];
         this->OutputPSISection(p_output, p_output->p_pmt_section, i_pmt_pid, &p_output->i_pmt_cc, i_dts, (block_t **) 0, (uint8_t *) 0);
      }
   }
}

void cLdvbdemux::SendNIT(mtime_t i_dts)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID) && !p_output->config.b_passthrough && (p_output->config.i_config & OUTPUT_DVB) && p_output->p_nit_section != (uint8_t *) 0)
         this->OutputPSISection(p_output, p_output->p_nit_section, NIT_PID, &p_output->i_nit_cc, i_dts, (block_t **) 0, (uint8_t *) 0);
   }
}

void cLdvbdemux::SendSDT(mtime_t i_dts)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID) && !p_output->config.b_passthrough && (p_output->config.i_config & OUTPUT_DVB) && p_output->p_sdt_section != NULL)
         this->OutputPSISection(p_output, p_output->p_sdt_section, SDT_PID, &p_output->i_sdt_cc, i_dts, (block_t **) 0, (uint8_t *) 0);
   }
}

bool cLdvbdemux::handle_epg(int i_table_id)
{
    return (i_table_id == EIT_TABLE_ID_PF_ACTUAL || (i_table_id >= EIT_TABLE_ID_SCHED_ACTUAL_FIRST && i_table_id <= EIT_TABLE_ID_SCHED_ACTUAL_LAST));
}

void cLdvbdemux::SendEIT(sid_t *p_sid, mtime_t i_dts, uint8_t *p_eit)
{
   uint8_t i_table_id = psi_get_tableid(p_eit);
   bool b_epg = handle_epg(i_table_id);
   uint16_t i_onid = eit_get_onid(p_eit);

   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID) && !p_output->config.b_passthrough && (p_output->config.i_config & OUTPUT_DVB) && (!b_epg || (p_output->config.i_config & OUTPUT_EPG)) && p_output->config.i_sid == p_sid->i_sid) {
         eit_set_tsid(p_eit, p_output->i_tsid);

         if (p_output->config.i_new_sid) {
            eit_set_sid(p_eit, p_output->config.i_new_sid);
         } else {
            eit_set_sid(p_eit, p_output->config.i_sid);
         }

         if (p_output->config.i_onid)
             eit_set_onid(p_eit, p_output->config.i_onid);

         psi_set_crc(p_eit);

        this->OutputPSISection(p_output, p_eit, EIT_PID, &p_output->i_eit_cc, i_dts, &p_output->p_eit_ts_buffer, &p_output->i_eit_ts_buffer_offset);

         if (p_output->config.i_onid)
            eit_set_onid(p_eit, i_onid);
      }
   }
}

void cLdvbdemux::FlushEIT(output_t *p_output, mtime_t i_dts)
{
   block_t *p_block = p_output->p_eit_ts_buffer;

   psi_split_end(p_block->p_ts, &p_output->i_eit_ts_buffer_offset);
   p_block->i_dts = i_dts;
   p_block->i_refcount--;
   this->output_Put(p_output, p_block);
   p_output->p_eit_ts_buffer = (block_t *) 0;
   p_output->i_eit_ts_buffer_offset = 0;
}

void cLdvbdemux::SendTDT(block_t *p_ts)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID)
            && !p_output->config.b_passthrough
            && (p_output->config.i_config & OUTPUT_DVB)
            && p_output->p_sdt_section != (uint8_t *) 0)
         this->output_Put(p_output, p_ts);
   }
}

void cLdvbdemux::SendEMM(block_t *p_ts)
{
   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID) && !p_output->config.b_passthrough)
         this->output_Put(p_output, p_ts);
   }
}

void cLdvbdemux::NewPAT(output_t *p_output)
{
   const uint8_t *p_program;
   uint8_t *p;
   uint8_t k = 0;

   ::free(p_output->p_pat_section);
   p_output->p_pat_section = NULL;
   p_output->i_pat_version++;

   if (!p_output->config.i_sid)
      return;
   if (!psi_table_validate(this->pp_current_pat_sections))
      return;

   p_program = pat_table_find_program(this->pp_current_pat_sections, p_output->config.i_sid);
   if (p_program == (const uint8_t *) 0)
      return;

   p = p_output->p_pat_section = psi_allocate();
   pat_init(p);
   psi_set_length(p, PSI_MAX_SIZE);
   pat_set_tsid(p, p_output->i_tsid);
   psi_set_version(p, p_output->i_pat_version);
   psi_set_current(p);
   psi_set_section(p, 0);
   psi_set_lastsection(p, 0);

   if (p_output->config.i_config & OUTPUT_DVB) {
      /* NIT */
      p = pat_get_program(p_output->p_pat_section, k++);
      patn_init(p);
      patn_set_program(p, 0);
      patn_set_pid(p, NIT_PID);
   }

   p = pat_get_program(p_output->p_pat_section, k++);
   patn_init(p);
   if (p_output->config.i_new_sid) {
      cLbugf(cL::dbg_dvb, "Mapping PAT SID %d to %d\n", p_output->config.i_sid, p_output->config.i_new_sid);
      patn_set_program(p, p_output->config.i_new_sid);
   } else {
      patn_set_program(p, p_output->config.i_sid);
   }

   if (this->b_do_remap) {
      cLbugf(cL::dbg_dvb, "Mapping PMT PID %d to %d\n", patn_get_pid(p_program), this->pi_newpids[cLdvbdemux::I_PMTPID]);
      patn_set_pid(p, this->pi_newpids[cLdvbdemux::I_PMTPID]);
   } else if (p_output->config.b_do_remap && p_output->config.pi_confpids[cLdvbdemux::I_PMTPID]) {
      cLbugf(cL::dbg_dvb, "Mapping PMT PID %d to %d\n", patn_get_pid(p_program), p_output->config.pi_confpids[cLdvbdemux::I_PMTPID]);
      patn_set_pid(p, p_output->config.pi_confpids[cLdvbdemux::I_PMTPID]);
   } else {
      patn_set_pid(p, patn_get_pid(p_program));
   }

   p = pat_get_program(p_output->p_pat_section, k);
   pat_set_length(p_output->p_pat_section, p - p_output->p_pat_section - PAT_HEADER_SIZE);
   psi_set_crc(p_output->p_pat_section);
}

void cLdvbdemux::CopyDescriptors(uint8_t *p_descs, uint8_t *p_current_descs)
{
   uint8_t *p_desc;
   const uint8_t *p_current_desc;
   uint16_t j = 0, k = 0;

   descs_set_length(p_descs, DESCS_MAX_SIZE);

   while ((p_current_desc = descs_get_desc(p_current_descs, j)) != (uint8_t *) 0) {
      uint8_t i_tag = desc_get_tag(p_current_desc);

      j++;
      if (!this->b_enable_ecm && i_tag == 0x9)
         continue;

      p_desc = descs_get_desc(p_descs, k);
      if (p_desc == (uint8_t *) 0)
         continue; /* This shouldn't happen */
      k++;
      memcpy(p_desc, p_current_desc, DESC_HEADER_SIZE + desc_get_length(p_current_desc));
   }

   p_desc = descs_get_desc(p_descs, k);
   if (p_desc == (uint8_t *) 0) {
      /* This shouldn't happen if the incoming PMT is valid */
      descs_set_length(p_descs, 0);
   } else {
      descs_set_length(p_descs, p_desc - p_descs - DESCS_HEADER_SIZE);
   }
}

void cLdvbdemux::NewPMT(output_t *p_output)
{
   sid_t *p_sid;
   uint8_t *p_current_pmt;
   uint8_t *p_es, *p_current_es;
   uint8_t *p;
   uint16_t j, k;
   uint16_t i_pcrpid;

   ::free(p_output->p_pmt_section);
   p_output->p_pmt_section = NULL;
   p_output->i_pmt_version++;

   if (!p_output->config.i_sid)
      return;

   p_sid = this->FindSID(p_output->config.i_sid);
   if (p_sid == (sid_t *) 0)
      return;

   if (p_sid->p_current_pmt == (uint8_t *) 0)
      return;
   p_current_pmt = p_sid->p_current_pmt;

   p = p_output->p_pmt_section = psi_allocate();
   pmt_init(p);
   psi_set_length(p, PSI_MAX_SIZE);
   if (p_output->config.i_new_sid) {
      cLbugf(cL::dbg_dvb, "Mapping PMT SID %d to %d\n", p_output->config.i_sid, p_output->config.i_new_sid);
      pmt_set_program(p, p_output->config.i_new_sid);
   } else {
      pmt_set_program(p, p_output->config.i_sid);
   }
   psi_set_version(p, p_output->i_pmt_version);
   psi_set_current(p);
   pmt_set_desclength(p, 0);
   this->init_pid_mapping(p_output);

   this->CopyDescriptors(pmt_get_descs(p), pmt_get_descs(p_current_pmt));

   j = 0; k = 0;
   while ((p_current_es = pmt_get_es(p_current_pmt, j)) != (uint8_t *) 0) {
      uint16_t i_pid = pmtn_get_pid(p_current_es);

      j++;
      if ((p_output->config.i_nb_pids || !this->PIDWouldBeSelected(p_current_es)) && !this->IsIn(p_output->config.pi_pids, p_output->config.i_nb_pids, i_pid))
         continue;

      p_es = pmt_get_es(p, k);
      if (p_es == (uint8_t *) 0)
         continue; /* This shouldn't happen */
      k++;
      pmtn_init(p_es);
      pmtn_set_streamtype(p_es, pmtn_get_streamtype(p_current_es));
      pmtn_set_pid(p_es, this->map_es_pid(p_output, p_current_es, i_pid));
      pmtn_set_desclength(p_es, 0);

      this->CopyDescriptors(pmtn_get_descs(p_es), pmtn_get_descs(p_current_es));
   }

   /* Do the pcr pid after everything else as it may have been remapped */
   i_pcrpid = pmt_get_pcrpid(p_current_pmt);
   if (p_output->pi_newpids[i_pcrpid] != UNUSED_PID) {
      cLbugf(cL::dbg_dvb, "REMAP: The PCR PID was changed from 0x%x (%u) to 0x%x (%u)\n", i_pcrpid, i_pcrpid, p_output->pi_newpids[i_pcrpid], p_output->pi_newpids[i_pcrpid]);
      i_pcrpid = p_output->pi_newpids[i_pcrpid];
   } else {
      cLbugf(cL::dbg_dvb, "The PCR PID has kept its original value of 0x%x (%u)\n", i_pcrpid, i_pcrpid);
   }
   pmt_set_pcrpid(p, i_pcrpid);
   p_es = pmt_get_es(p, k);
   if (p_es == (uint8_t *) 0) {
      /* This shouldn't happen if the incoming PMT is valid */
      pmt_set_length(p, 0);
   } else {
      pmt_set_length(p, p_es - p - PMT_HEADER_SIZE);
   }
   psi_set_crc(p);
}

void cLdvbdemux::NewNIT(output_t *p_output)
{
   uint8_t *p_ts;
   uint8_t *p_header2;
   uint8_t *p;

   ::free(p_output->p_nit_section);
   p_output->p_nit_section = NULL;
   p_output->i_nit_version++;

   p = p_output->p_nit_section = psi_allocate();
   nit_init(p, true);
   nit_set_length(p, PSI_MAX_SIZE);
   nit_set_nid(p, p_output->config.i_network_id);
   psi_set_version(p, p_output->i_nit_version);
   psi_set_current(p);
   psi_set_section(p, 0);
   psi_set_lastsection(p, 0);

   if (p_output->config.network_name.i) {
      uint8_t *p_descs;
      uint8_t *p_desc;
      nit_set_desclength(p, DESCS_MAX_SIZE);
      p_descs = nit_get_descs(p);
      p_desc = descs_get_desc(p_descs, 0);
      desc40_init(p_desc);
      desc40_set_networkname(p_desc, p_output->config.network_name.p, p_output->config.network_name.i);
      p_desc = descs_get_desc(p_descs, 1);
      descs_set_length(p_descs, p_desc - p_descs - DESCS_HEADER_SIZE);
   } else {
      nit_set_desclength(p, 0);
   }

   p_header2 = nit_get_header2(p);
   nith_init(p_header2);
   nith_set_tslength(p_header2, NIT_TS_SIZE);

   p_ts = nit_get_ts(p, 0);
   nitn_init(p_ts);
   nitn_set_tsid(p_ts, p_output->i_tsid);
   if (p_output->config.i_onid) {
      nitn_set_onid(p_ts, p_output->config.i_onid);
   } else {
      nitn_set_onid(p_ts, p_output->config.i_network_id);
   }
   nitn_set_desclength(p_ts, 0);

   p_ts = nit_get_ts(p, 1);
   if (p_ts == (uint8_t *) 0) {
      /* This shouldn't happen */
      nit_set_length(p, 0);
   } else {
      nit_set_length(p, p_ts - p - NIT_HEADER_SIZE);
   }
   psi_set_crc(p_output->p_nit_section);
}

void cLdvbdemux::NewSDT(output_t *p_output)
{
   uint8_t *p_service, *p_current_service;
   uint8_t *p;

   ::free(p_output->p_sdt_section);
   p_output->p_sdt_section = NULL;
   p_output->i_sdt_version++;

   if (!p_output->config.i_sid) return;
   if (!psi_table_validate(this->pp_current_sdt_sections)) return;

   p_current_service = sdt_table_find_service(this->pp_current_sdt_sections, p_output->config.i_sid);

   if (p_current_service == (uint8_t *) 0) {
      if (p_output->p_pat_section != (uint8_t *) 0 && pat_get_program(p_output->p_pat_section, 0) == (uint8_t *) 0) {
         /* Empty PAT and no SDT anymore */
         ::free(p_output->p_pat_section);
         p_output->p_pat_section = NULL;
         p_output->i_pat_version++;
      }
      return;
   }

   p = p_output->p_sdt_section = psi_allocate();
   sdt_init(p, true);
   sdt_set_length(p, PSI_MAX_SIZE);
   sdt_set_tsid(p, p_output->i_tsid);
   psi_set_version(p, p_output->i_sdt_version);
   psi_set_current(p);
   psi_set_section(p, 0);
   psi_set_lastsection(p, 0);
   if ( p_output->config.i_onid ) {
      sdt_set_onid(p, p_output->config.i_onid);
   } else {
      sdt_set_onid(p, sdt_get_onid(psi_table_get_section(this->pp_current_sdt_sections, 0)));
   }

   p_service = sdt_get_service(p, 0);
   sdtn_init(p_service);
   if (p_output->config.i_new_sid) {
      cLbugf(cL::dbg_dvb, "Mapping SDT SID %d to %d\n", p_output->config.i_sid, p_output->config.i_new_sid);
      sdtn_set_sid(p_service, p_output->config.i_new_sid);
   } else {
      sdtn_set_sid(p_service, p_output->config.i_sid);
   }

    /* We always forward EITp/f */
   if (sdtn_get_eitpresent(p_current_service))
      sdtn_set_eitpresent(p_service);

   if ((p_output->config.i_config & OUTPUT_EPG) == OUTPUT_EPG && sdtn_get_eitschedule(p_current_service))
      sdtn_set_eitschedule(p_service);

   sdtn_set_running(p_service, sdtn_get_running(p_current_service));
   /* Do not set free_ca */
   sdtn_set_desclength(p_service, sdtn_get_desclength(p_current_service));

   if (!p_output->config.provider_name.i && !p_output->config.service_name.i) {
      /* Copy all descriptors unchanged */
      memcpy(descs_get_desc(sdtn_get_descs(p_service), 0), descs_get_desc(sdtn_get_descs(p_current_service), 0), sdtn_get_desclength(p_current_service));
   } else {
      int j = 0, i_total_desc_len = 0;
      uint8_t *p_desc;
      uint8_t *p_new_desc = descs_get_desc(sdtn_get_descs(p_service), 0);
      while ((p_desc = descs_get_desc(sdtn_get_descs(p_current_service), j++)) != (uint8_t *) 0) {
         /* Regenerate descriptor 48 (service name) */
         if (desc_get_tag(p_desc) == 0x48 && desc48_validate(p_desc)) {
            uint8_t i_old_provider_len, i_old_service_len;
            uint8_t i_new_desc_len = 3; /* 1 byte - type, 1 byte provider_len, 1 byte service_len */
            const uint8_t *p_old_provider = desc48_get_provider(p_desc, &i_old_provider_len);
            const uint8_t *p_old_service = desc48_get_service(p_desc, &i_old_service_len);

            desc48_init(p_new_desc);
            desc48_set_type(p_new_desc, desc48_get_type(p_desc));

            if (p_output->config.provider_name.i) {
               desc48_set_provider(p_new_desc,  p_output->config.provider_name.p, p_output->config.provider_name.i);
               i_new_desc_len += p_output->config.provider_name.i;
            } else {
               desc48_set_provider(p_new_desc, p_old_provider, i_old_provider_len);
               i_new_desc_len += i_old_provider_len;
            }

            if (p_output->config.service_name.i) {
               desc48_set_service(p_new_desc, p_output->config.service_name.p, p_output->config.service_name.i);
               i_new_desc_len += p_output->config.service_name.i;
            } else {
               desc48_set_service(p_new_desc, p_old_service, i_old_service_len);
               i_new_desc_len += i_old_service_len;
            }

            desc_set_length(p_new_desc, i_new_desc_len);
            i_total_desc_len += DESC_HEADER_SIZE + i_new_desc_len;
            p_new_desc += DESC_HEADER_SIZE + i_new_desc_len;
         } else {
            /* Copy single descriptor */
            int i_desc_len = DESC_HEADER_SIZE + desc_get_length(p_desc);
            memcpy(p_new_desc, p_desc, i_desc_len);
            p_new_desc += i_desc_len;
            i_total_desc_len += i_desc_len;
         }
      }
      sdtn_set_desclength(p_service, i_total_desc_len);
   }

   p_service = sdt_get_service(p, 1);
   if (p_service == (uint8_t *) 0) {
      /* This shouldn't happen if the incoming SDT is valid */
      sdt_set_length(p, 0);
   } else {
      sdt_set_length(p, p_service - p - SDT_HEADER_SIZE);
   }
   psi_set_crc(p_output->p_sdt_section);
}

#define DECLARE_UPDATE_FUNC(table) \
      void cLdvbdemux::Update##table(uint16_t i_sid) \
      { \
         for (int i = 0; i < this->i_nb_outputs; i++) { \
            if ((this->pp_outputs[i]->config.i_config & OUTPUT_VALID) && this->pp_outputs[i]->config.i_sid == i_sid) \
               New##table(this->pp_outputs[i]); \
         } \
      }

DECLARE_UPDATE_FUNC(PAT)
DECLARE_UPDATE_FUNC(PMT)
DECLARE_UPDATE_FUNC(SDT)

void cLdvbdemux::UpdateTSID(void)
{
   uint16_t i_tsid = psi_table_get_tableidext(this->pp_current_pat_sections);

   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];

      if ((p_output->config.i_config & OUTPUT_VALID) && p_output->config.i_tsid == -1 && !this->b_random_tsid) {
         p_output->i_tsid = i_tsid;
         this->NewNIT(p_output);
      }
   }
}

bool cLdvbdemux::SIDIsSelected(uint16_t i_sid)
{
   for (int i = 0; i < this->i_nb_outputs; i++)
      if ((this->pp_outputs[i]->config.i_config & OUTPUT_VALID) && this->pp_outputs[i]->config.i_sid == i_sid)
         return true;

   return false;
}

bool cLdvbdemux::PIDWouldBeSelected(uint8_t *p_es)
{
   if (this->b_any_type)
      return true;

   uint8_t i_type = pmtn_get_streamtype(p_es);

   switch (i_type) {
      case 0x1: /* video MPEG-1 */
      case 0x2: /* video */
      case 0x3: /* audio MPEG-1 */
      case 0x4: /* audio */
      case 0xf: /* audio AAC ADTS */
      case 0x10: /* video MPEG-4 */
      case 0x11: /* audio AAC LATM */
      case 0x1b: /* video H264 */
      case 0x24: /* video H265 */
      case 0x81: /* ATSC A/52 */
      case 0x87: /* ATSC Enhanced A/52 */
         return true;
         break;
      case 0x6: {
         uint16_t j = 0;
         const uint8_t *p_desc;
         while ((p_desc = descs_get_desc(pmtn_get_descs(p_es), j)) != (uint8_t *) 0) {
            uint8_t i_tag = desc_get_tag(p_desc);
            j++;
            if(i_tag == 0x46 /* VBI + teletext */
                  || i_tag == 0x56 /* teletext */
                  || i_tag == 0x59 /* dvbsub */
                  || i_tag == 0x6a /* A/52 */
                  || i_tag == 0x7a /* Enhanced A/52 */
                  || i_tag == 0x7b /* DCA */
                  || i_tag == 0x7c /* AAC */)
               return true;
         }
         break;
      }
      default:
         break;
   }

   /* FIXME: also parse IOD */
   return false;
}

bool cLdvbdemux::PIDCarriesPES(const uint8_t *p_es)
{
   uint8_t i_type = pmtn_get_streamtype(p_es);

   switch (i_type) {
      case 0x1: /* video MPEG-1 */
      case 0x2: /* video */
      case 0x3: /* audio MPEG-1 */
      case 0x4: /* audio */
      case 0x6: /* private PES data */
      case 0xf: /* audio AAC */
      case 0x10: /* video MPEG-4 */
      case 0x11: /* audio AAC LATM */
      case 0x1b: /* video H264 */
      case 0x24: /* video H265 */
      case 0x81: /* ATSC A/52 */
      case 0x87: /* ATSC Enhanced A/52 */
         return true;
         // no break;
      default:
         break;
   }
   return false;
}

bool cLdvbdemux::PMTNeedsDescrambling(uint8_t *p_pmt)
{
   uint8_t i;
   uint16_t j;
   uint8_t *p_es;
   const uint8_t *p_desc;

   j = 0;
   while ((p_desc = descs_get_desc(pmt_get_descs(p_pmt), j)) != (uint8_t *) 0) {
      uint8_t i_tag = desc_get_tag(p_desc);
      j++;

      if (i_tag == 0x9)
         return true;
   }

   i = 0;
   while ((p_es = pmt_get_es(p_pmt, i)) != (uint8_t *) 0) {
      i++;
      j = 0;
      while ((p_desc = descs_get_desc(pmtn_get_descs(p_es), j)) != (uint8_t *) 0) {
         uint8_t i_tag = desc_get_tag(p_desc);
         j++;

         if (i_tag == 0x9)
            return true;
      }
   }

   return false;
}

/* Find CA descriptor that have PID i_ca_pid */
uint8_t *cLdvbdemux::ca_desc_find(uint8_t *p_descl, uint16_t i_length, uint16_t i_ca_pid)
{
   int j = 0;
   uint8_t *p_desc;

   while ((p_desc = descl_get_desc(p_descl, i_length, j++)) != (uint8_t *) 0) {
      if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
         continue;
      if (desc09_get_pid(p_desc) == i_ca_pid)
         return p_desc;
   }

   return (uint8_t *) 0;
}

void cLdvbdemux::DeleteProgram(uint16_t i_sid, uint16_t i_pid)
{
   sid_t *p_sid;
   uint8_t *p_pmt;
   uint8_t *p_desc;

   this->UnselectPMT(i_sid, i_pid);

   p_sid = this->FindSID(i_sid);
   if (p_sid == (sid_t *) 0)
      return;

   p_pmt = p_sid->p_current_pmt;

   if (p_pmt != (uint8_t *) 0) {
      uint16_t i_pcr_pid = pmt_get_pcrpid(p_pmt);
      uint8_t *p_es;
      uint8_t j;

      if (this->i_ca_handle && this->SIDIsSelected(i_sid) && this->PMTNeedsDescrambling(p_pmt))
         this->en50221_DeletePMT(p_pmt);

      if (i_pcr_pid != PADDING_PID && i_pcr_pid != p_sid->i_pmt_pid)
         this->UnselectPID(i_sid, i_pcr_pid);

      if (this->b_enable_ecm) {
         j = 0;
         while ((p_desc = descs_get_desc(pmt_get_descs(p_pmt), j++)) != (uint8_t *) 0) {
            if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
               continue;
            this->UnselectPID(i_sid, desc09_get_pid(p_desc));
         }
      }

      j = 0;
      while ((p_es = pmt_get_es(p_pmt, j)) != (uint8_t *) 0) {
         uint16_t i_pid = pmtn_get_pid(p_es);
         j++;

         if (this->PIDWouldBeSelected(p_es))
            this->UnselectPID(i_sid, i_pid);

         if (this->b_enable_ecm) {
            uint8_t k = 0;

            while ((p_desc = descs_get_desc(pmtn_get_descs(p_es), k++)) != (uint8_t *) 0) {
               if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
                  continue;
               this->UnselectPID(i_sid, desc09_get_pid(p_desc));
            }
         }
      }

      ::free(p_pmt);
      p_sid->p_current_pmt = (uint8_t *) 0;
   }
   p_sid->i_sid = 0;
   p_sid->i_pmt_pid = 0;
   for (uint8_t r = 0; r < MAX_EIT_TABLES; r++) {
      psi_table_free(p_sid->eit_table[r].data);
      psi_table_init(p_sid->eit_table[r].data);
   }
}

void cLdvbdemux::HandlePAT(mtime_t i_dts)
{
   bool b_change = false;
   PSI_TABLE_DECLARE(pp_old_pat_sections);
   uint8_t i_last_section = psi_table_get_lastsection(this->pp_next_pat_sections);
   uint8_t i;

   if (psi_table_validate(this->pp_current_pat_sections) && psi_table_compare(this->pp_current_pat_sections, this->pp_next_pat_sections)) {
      /* Identical PAT. Shortcut. */
      psi_table_free(this->pp_next_pat_sections);
      psi_table_init(this->pp_next_pat_sections);
      goto out_pat;
   }

   if (!pat_table_validate(this->pp_next_pat_sections)) {
      cLbug(cL::dbg_dvb, "invalid PAT received\n");
      psi_table_free(this->pp_next_pat_sections);
      psi_table_init(this->pp_next_pat_sections);
      goto out_pat;
   }

   /* Switch tables. */
   psi_table_copy(pp_old_pat_sections, this->pp_current_pat_sections);
   psi_table_copy(this->pp_current_pat_sections, this->pp_next_pat_sections);
   psi_table_init(this->pp_next_pat_sections);

   if (!psi_table_validate(pp_old_pat_sections) || psi_table_get_tableidext(this->pp_current_pat_sections) != psi_table_get_tableidext(pp_old_pat_sections)) {
      b_change = true;
      this->UpdateTSID();
      /* This will trigger a universal reset of everything. */
   }

   for (i = 0; i <= i_last_section; i++) {
      uint8_t *p_section = psi_table_get_section(this->pp_current_pat_sections, i);
      const uint8_t *p_program;
      int j = 0;

      while ((p_program = pat_get_program(p_section, j)) != (uint8_t *) 0) {
         const uint8_t *p_old_program = (const uint8_t *) 0;
         uint16_t i_sid = patn_get_program(p_program);
         uint16_t i_pid = patn_get_pid(p_program);
         j++;

         if (i_sid == 0) {
            if (i_pid != NIT_PID)
               cLbugf(cL::dbg_dvb, "NIT is carried on PID %hu which isn't DVB compliant\n", i_pid);
            continue; /* NIT */
         }

         if (!psi_table_validate(pp_old_pat_sections)
               || (p_old_program = pat_table_find_program(
                     pp_old_pat_sections, i_sid)) == NULL
                     || patn_get_pid(p_old_program) != i_pid
                     || b_change)
         {
            sid_t *p_sid;

            if (p_old_program != (uint8_t *) 0)
               this->DeleteProgram(i_sid, patn_get_pid(p_old_program));

            this->SelectPMT(i_sid, i_pid);

            p_sid = this->FindSID(0);
            if (p_sid == (sid_t *) 0) {
               p_sid = cLmalloc(sid_t, 1);
               p_sid->p_current_pmt = (uint8_t *) 0;
               for (uint8_t r = 0; r < MAX_EIT_TABLES; r++) {
                  psi_table_init(p_sid->eit_table[r].data);
               }
               this->i_nb_sids++;
               this->pp_sids = (sid_t **)realloc(this->pp_sids, sizeof(sid_t *) * this->i_nb_sids);
               this->pp_sids[this->i_nb_sids - 1] = p_sid;
            }

            p_sid->i_sid = i_sid;
            p_sid->i_pmt_pid = i_pid;

            this->UpdatePAT(i_sid);
         }
      }
   }

   if (psi_table_validate(pp_old_pat_sections)) {
      i_last_section = psi_table_get_lastsection(pp_old_pat_sections);
      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_section = psi_table_get_section(pp_old_pat_sections, i);
         const uint8_t *p_program;
         int j = 0;

         while ((p_program = pat_get_program(p_section, j)) != (uint8_t *) 0) {
            uint16_t i_sid = patn_get_program(p_program);
            uint16_t i_pid = patn_get_pid(p_program);
            j++;

            if (i_sid == 0)
               continue; /* NIT */

            if (pat_table_find_program(this->pp_current_pat_sections, i_sid) == (uint8_t *) 0) {
               this->DeleteProgram(i_sid, i_pid);
               this->UpdatePAT(i_sid);
            }
         }
      }

      psi_table_free(pp_old_pat_sections);
   }

   pat_table_print(this->pp_current_pat_sections, cLdvbdemux::debug_cb, this, PRINT_TEXT);

   out_pat:
   this->SendPAT(i_dts);
}

void cLdvbdemux::HandlePATSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts)
{
   if (i_pid != PAT_PID || !pat_validate(p_section)) {
      cLbugf(cL::dbg_dvb, "invalid PAT section received on PID %hu\n", i_pid);
      ::free(p_section);
      return;
   }

   if (!psi_table_section(this->pp_next_pat_sections, p_section))
      return;

   this->HandlePAT(i_dts);
}

void cLdvbdemux::HandleCAT(mtime_t i_dts)
{
   PSI_TABLE_DECLARE(pp_old_cat_sections);
   uint8_t i_last_section = psi_table_get_lastsection(this->pp_next_cat_sections);
   uint8_t i_last_section2;
   uint8_t i, r;
   uint8_t *p_desc;
   int j, k;

   if (psi_table_validate(this->pp_current_cat_sections) && psi_table_compare(this->pp_current_cat_sections, this->pp_next_cat_sections)) {
      /* Identical CAT. Shortcut. */
      psi_table_free(this->pp_next_cat_sections);
      psi_table_init(this->pp_next_cat_sections);
      goto out_cat;
   }

   if (!cat_table_validate(this->pp_next_cat_sections)) {
      cLbug(cL::dbg_dvb, "invalid CAT received\n");
      psi_table_free(this->pp_next_cat_sections);
      psi_table_init(this->pp_next_cat_sections);
      goto out_cat;
   }

   /* Switch tables. */
   psi_table_copy(pp_old_cat_sections, this->pp_current_cat_sections);
   psi_table_copy(this->pp_current_cat_sections, this->pp_next_cat_sections);
   psi_table_init(this->pp_next_cat_sections);

   for (i = 0; i <= i_last_section; i++) {
      uint8_t *p_section = psi_table_get_section(this->pp_current_cat_sections, i);

      j = 0;
      while ((p_desc = descl_get_desc(cat_get_descl(p_section), cat_get_desclength(p_section), j++)) != (uint8_t *) 0) {
         if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
            continue;

         this->SetPID_EMM(desc09_get_pid(p_desc));
      }
   }

   if (psi_table_validate(pp_old_cat_sections)) {
      i_last_section = psi_table_get_lastsection(pp_old_cat_sections);
      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_old_section = psi_table_get_section(pp_old_cat_sections, i);
         j = 0;
         while ((p_desc = descl_get_desc(cat_get_descl(p_old_section), cat_get_desclength(p_old_section), j++)) != (uint8_t *) 0) {
            uint16_t emm_pid;
            int pid_found = 0;

            if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
               continue;

            emm_pid = desc09_get_pid(p_desc);

            // Search in current sections if the pid exists
            i_last_section2 = psi_table_get_lastsection(this->pp_current_cat_sections);
            for (r = 0; r <= i_last_section2; r++) {
               uint8_t *p_section = psi_table_get_section(this->pp_current_cat_sections, r);

               k = 0;
               while ((p_desc = descl_get_desc(cat_get_descl(p_section), cat_get_desclength(p_section), k++)) != (uint8_t *) 0) {
                  if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
                     continue;
                  if (this->ca_desc_find(cat_get_descl(p_section), cat_get_desclength(p_section), emm_pid) != (uint8_t *) 0) {
                     pid_found = 1;
                     break;
                  }
               }
            }

            if (!pid_found)
               this->UnsetPID(emm_pid);
         }
      }

      psi_table_free(pp_old_cat_sections);
   }

   cat_table_print(this->pp_current_cat_sections, cLdvbdemux::debug_cb, this, PRINT_TEXT);

   out_cat:
   return;
}

void cLdvbdemux::HandleCATSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts)
{
   if (i_pid != CAT_PID || !cat_validate(p_section)) {
      cLbugf(cL::dbg_dvb, "invalid CAT section received on PID %hu\n", i_pid);
      ::free(p_section);
      return;
   }

   if (!psi_table_section(this->pp_next_cat_sections, p_section))
      return;

   this->HandleCAT(i_dts);
}

void cLdvbdemux::mark_pmt_pids(uint8_t *p_pmt, uint8_t pid_map[], uint8_t marker)
{
   uint16_t j, k;
   uint8_t *p_es;
   uint8_t *p_desc;

   uint16_t i_pcr_pid = pmt_get_pcrpid(p_pmt);

   if (this->b_enable_ecm) {
      j = 0;
      while ((p_desc = descs_get_desc(pmt_get_descs(p_pmt), j++)) != (uint8_t *) 0) {
         if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
            continue;
         pid_map[desc09_get_pid(p_desc)] |= marker;
      }
   }

   if (i_pcr_pid != PADDING_PID)
      pid_map[ i_pcr_pid ] |= marker;

   j = 0;
   while ((p_es = pmt_get_es(p_pmt, j)) != (uint8_t *) 0) {
      uint16_t i_pid = pmtn_get_pid(p_es);
      j++;

      if (this->PIDWouldBeSelected(p_es))
         pid_map[i_pid] |= marker;

      this->p_pids[i_pid].b_pes = this->PIDCarriesPES(p_es);

      if (this->b_enable_ecm) {
         k = 0;
         while ((p_desc = descs_get_desc(pmtn_get_descs(p_es), k++)) != (uint8_t *) 0) {
            if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
               continue;
            pid_map[desc09_get_pid(p_desc)] |= marker;
         }
      }
   }
}

void cLdvbdemux::HandlePMT(uint16_t i_pid, uint8_t *p_pmt, mtime_t i_dts)
{
   uint16_t i_sid = pmt_get_program(p_pmt);
   sid_t *p_sid;
   bool b_needs_descrambling, b_needed_descrambling, b_is_selected;
   uint8_t pid_map[MAX_PIDS];
   uint16_t i_pcr_pid;

   p_sid = this->FindSID(i_sid);
   if (p_sid == (sid_t *) 0) {
      /* Unwanted SID (happens when the same PMT PID is used for several
       * programs). */
      ::free(p_pmt);
      return;
   }

   if (i_pid != p_sid->i_pmt_pid) {
      cLbugf(cL::dbg_dvb, "invalid PMT section received on program: %hu, PID: %hu\n", i_sid, i_pid);
      ::free(p_pmt);
      return;
   }

   if (p_sid->p_current_pmt != NULL && psi_compare(p_sid->p_current_pmt, p_pmt)) {
      /* Identical PMT. Shortcut. */
      ::free(p_pmt);
      goto out_pmt;
   }

   if (!pmt_validate(p_pmt)) {
      cLbugf(cL::dbg_dvb, "invalid PMT section received on PID %hu\n", i_pid);
      ::free(p_pmt);
      goto out_pmt;
   }

   memset(pid_map, 0, sizeof(pid_map));

   b_needs_descrambling = this->PMTNeedsDescrambling(p_pmt);
   b_needed_descrambling = p_sid->p_current_pmt != (uint8_t *) 0 ? this->PMTNeedsDescrambling(p_sid->p_current_pmt) : false;
   b_is_selected = this->SIDIsSelected(i_sid);

   if (this->i_ca_handle && b_is_selected && !b_needs_descrambling && b_needed_descrambling)
      this->en50221_DeletePMT(p_sid->p_current_pmt);

   if (p_sid->p_current_pmt != (uint8_t *) 0) {
      this->mark_pmt_pids(p_sid->p_current_pmt, pid_map, 0x02);
      ::free(p_sid->p_current_pmt);
   }

   this->mark_pmt_pids(p_pmt, pid_map, 0x01);

   i_pcr_pid = pmt_get_pcrpid(p_pmt);
   for (int i = 0; i < i_nb_outputs; i++) {
      if ((pp_outputs[i]->config.i_config & OUTPUT_VALID) && pp_outputs[i]->config.i_sid == i_sid)
          pp_outputs[i]->i_pcr_pid = 0;
   }

   /* Start to stream PIDs */
   for (int pid = 0; pid < MAX_PIDS; pid++) {
      /* The pid does not exist in the old PMT and in the new PMT. Ignore this pid. */
      if (!pid_map[pid])
         continue;

      switch (pid_map[pid] & 0x03) {
         case 0x03: /* The pid exists in the old PMT and in the new PMT. The pid was already selected in case 0x01. */
            continue;
         case 0x02: /* The pid does not exist in the new PMT but exists in the old PMT. Unselect it. */
            this->UnselectPID(i_sid, pid);
            break;
         case 0x01: /* The pid exists in new PMT. Select it. */
            this->SelectPID(i_sid, pid, pid == i_pcr_pid);
            break;
      }
   }

   p_sid->p_current_pmt = p_pmt;

   if (this->i_ca_handle && b_is_selected) {
      if (b_needs_descrambling && !b_needed_descrambling) {
         this->en50221_AddPMT(p_pmt);
      } else
      if (b_needs_descrambling && b_needed_descrambling) {
         this->en50221_UpdatePMT(p_pmt);
      }
   }

   this->UpdatePMT(i_sid);

   pmt_print(p_pmt, cLdvbdemux::debug_cb, this, cLdvboutput::iconv_cb, this, PRINT_TEXT);

out_pmt:
   this->SendPMT(p_sid, i_dts);
}

void cLdvbdemux::HandleNIT(mtime_t i_dts)
{
   if (psi_table_validate(this->pp_current_nit_sections) && psi_table_compare(this->pp_current_nit_sections, this->pp_next_nit_sections)) {
      /* Identical NIT. Shortcut. */
      psi_table_free(this->pp_next_nit_sections);
      psi_table_init(this->pp_next_nit_sections);
      goto out_nit;
   }

   if (!nit_table_validate(this->pp_next_nit_sections)) {
      cLbug(cL::dbg_dvb, "invalid NIT received\n");
      psi_table_free(this->pp_next_nit_sections);
      psi_table_init(this->pp_next_nit_sections);
      goto out_nit;
   }

   /* Switch tables. */
   psi_table_free(this->pp_current_nit_sections);
   psi_table_copy(this->pp_current_nit_sections, this->pp_next_nit_sections);
   psi_table_init(this->pp_next_nit_sections);

   nit_table_print(this->pp_current_nit_sections, cLdvbdemux::debug_cb, this, cLdvboutput::iconv_cb, this, PRINT_TEXT);

   out_nit:
   ;
}

void cLdvbdemux::HandleNITSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts)
{
   if (i_pid != NIT_PID || !nit_validate(p_section)) {
      cLbugf(cL::dbg_dvb, "invalid NIT section received on PID %hu\n", i_pid);
      ::free(p_section);
      return;
   }

   if (psi_table_section(this->pp_next_nit_sections, p_section))
      this->HandleNIT(i_dts);

   /* This case is different because DVB specifies a minimum bitrate for
    * PID 0x10, even if we don't have any thing to send (for cheap
    * transport over network boundaries). */
   this->SendNIT(i_dts);
}

void cLdvbdemux::HandleSDT(mtime_t i_dts)
{
   PSI_TABLE_DECLARE(pp_old_sdt_sections);
   uint8_t i_last_section = psi_table_get_lastsection(this->pp_next_sdt_sections);
   uint8_t i;
   int j;

   if (psi_table_validate(this->pp_current_sdt_sections) &&  psi_table_compare(this->pp_current_sdt_sections, this->pp_next_sdt_sections)) {
      /* Identical SDT. Shortcut. */
      psi_table_free(this->pp_next_sdt_sections);
      psi_table_init(this->pp_next_sdt_sections);
      goto out_sdt;
   }

   if (!sdt_table_validate(this->pp_next_sdt_sections)) {
      cLbug(cL::dbg_dvb, "invalid SDT received\n");
      psi_table_free(this->pp_next_sdt_sections);
      psi_table_init(this->pp_next_sdt_sections);
      goto out_sdt;
   }

   /* Switch tables. */
   psi_table_copy(pp_old_sdt_sections, this->pp_current_sdt_sections);
   psi_table_copy(this->pp_current_sdt_sections, this->pp_next_sdt_sections);
   psi_table_init(this->pp_next_sdt_sections);

   for (i = 0; i <= i_last_section; i++) {
      uint8_t *p_section = psi_table_get_section(this->pp_current_sdt_sections, i);
      uint8_t *p_service;
      j = 0;

      while ((p_service = sdt_get_service(p_section, j)) != (uint8_t *) 0) {
         uint16_t i_sid = sdtn_get_sid(p_service);
         j++;

         this->UpdateSDT(i_sid);
      }
   }

   if (psi_table_validate(pp_old_sdt_sections)) {
      i_last_section = psi_table_get_lastsection(pp_old_sdt_sections);
      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_section = psi_table_get_section(pp_old_sdt_sections, i);
         const uint8_t *p_service;
         int j = 0;

         while ((p_service = sdt_get_service(p_section, j)) != (const uint8_t *) 0) {
            uint16_t i_sid = sdtn_get_sid(p_service);
            j++;

            if (sdt_table_find_service(this->pp_current_sdt_sections, i_sid)  == (uint8_t *) 0)
               this->UpdateSDT(i_sid);
         }
      }

      psi_table_free(pp_old_sdt_sections);
   }

   sdt_table_print(this->pp_current_sdt_sections, cLdvbdemux::debug_cb, this, cLdvboutput::iconv_cb, this, PRINT_TEXT);

   out_sdt:
   this->SendSDT(i_dts);
}

void cLdvbdemux::HandleSDTSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts)
{
   if (i_pid != SDT_PID || !sdt_validate(p_section)) {
      cLbugf(cL::dbg_dvb, "invalid SDT section received on PID %hu\n", i_pid);
      ::free(p_section);
      return;
   }

   if (!psi_table_section(this->pp_next_sdt_sections, p_section))
      return;

   this->HandleSDT(i_dts);
}

void cLdvbdemux::HandleATSCSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts)
{
   uint8_t *buf = p_section;
   int section_length = getBits(buf, 12, 12);
   int table_id_ext = getBits(buf, 24, 16);
   int section_version_number = getBits(buf, 42, 5);
   buf += 8;
   section_length -= CRC_LEN;
   section_length -= 5;
   if (section_length < 0) {
      cLbugf(cL::dbg_low, "truncated ATSC section on PID %hu, len(%d)", i_pid, section_length + CRC_LEN);
      return;
   }

   uint8_t *pv_sdt = (uint8_t *) 0;
   pv_sdt = cLmalloc(uint8_t, (PSI_MAX_SIZE + PSI_HEADER_SIZE));
   memset(pv_sdt, 0, (PSI_MAX_SIZE + PSI_HEADER_SIZE) * sizeof(uint8_t));
   sdt_init(pv_sdt, true);
   sdt_set_length(pv_sdt, PSI_MAX_SIZE);
   sdt_set_tsid(pv_sdt, table_id_ext);
   psi_set_version(pv_sdt, section_version_number);
   psi_set_current(pv_sdt);
   psi_set_section(pv_sdt, 0);
   psi_set_lastsection(pv_sdt, 0);

   int num_channels_in_section = buf[1];
   int pseudo_id = 0xffff;
   int numscv = 0;
   uint8_t *b = buf + 2;
   for (int i = 0; i < num_channels_in_section; i++) {
      struct tvct_channel ch = read_tvct_channel(b);
      switch (ch.service_type) {
         case 0x01:
            cLbug(cL::dbg_low, "analog channels won't be put info channels.conf\n");
            break;
         case 0x02: /* ATSC TV */
         case 0x03: /* ATSC Radio */
            break;
         case 0x04: /* ATSC Data */
         default:
            continue;
      }
      if (ch.program_number == 0)
         ch.program_number = --pseudo_id;
      char sname[8];
      sname[0] = (char)ch.short_name0;
      sname[1] = (char)ch.short_name1;
      sname[2] = (char)ch.short_name2;
      sname[3] = (char)ch.short_name3;
      sname[4] = (char)ch.short_name4;
      sname[5] = (char)ch.short_name5;
      sname[6] = (char)ch.short_name6;
      sname[7] = 0;

      if (ch.hidden) {
         cLbug(cL::dbg_dvb, "service is not running, pseudo program_number.");
      } else {
         uint8_t *p_service = sdt_get_service(pv_sdt, numscv);
         sdtn_init(p_service);
         sdtn_set_sid(p_service, ch.program_number);

         sdtn_set_running(p_service, 4);
         sdtn_set_desclength(p_service, 2);

         uint8_t *p_desc = descs_get_desc(sdtn_get_descs(p_service), 0);
         uint8_t i_desc_len = 3;
         desc48_init(p_desc);

         desc48_set_type(p_desc, (ch.service_type == 3) ? 2 : (ch.service_type == 2 ? 1 : ch.service_type));
         desc48_set_provider(p_desc, (uint8_t *)sname, 7);
         i_desc_len += 7;
         desc48_set_service(p_desc, (uint8_t *)sname, 7);
         i_desc_len += 7;
         desc_set_length(p_desc, i_desc_len);
         int i_total_desc_len = DESC_HEADER_SIZE + i_desc_len;
         p_desc += DESC_HEADER_SIZE + i_desc_len;
         sdtn_set_desclength(p_service, i_total_desc_len);
         ++numscv;
      }
      b += 32 + ch.descriptors_length;
   }

   uint8_t *p_service = sdt_get_service(pv_sdt, numscv);
   if (p_service == (uint8_t *) 0) {
      sdt_set_length(pv_sdt, 0);
   } else {
      sdt_set_length(pv_sdt, p_service - pv_sdt - SDT_HEADER_SIZE);
   }
   psi_set_crc(pv_sdt);
   if (!psi_table_section(this->pp_next_sdt_sections, pv_sdt)) {
      ::free(pv_sdt);
      return;
   }
   this->HandleSDT(i_dts);

   return;
}

void cLdvbdemux::HandleEIT(uint16_t i_pid, uint8_t *p_eit, mtime_t i_dts)
{
   uint8_t i_table_id = psi_get_tableid(p_eit);
   uint16_t i_sid = eit_get_sid(p_eit);
   sid_t *p_sid;
   uint8_t i_section;
   uint8_t eit_table_id = 0;

   p_sid = this->FindSID(i_sid);
   if (p_sid == (sid_t *) 0) {
      /* Not a selected program. */
      ::free(p_eit);
      return;
   }

   if (i_pid != EIT_PID || !eit_validate(p_eit)) {
      cLbugf(cL::dbg_dvb, "invalid EIT section received on PID %hu\n", i_pid);
      ::free(p_eit);
      return;
   }

    bool b_epg = this->handle_epg(i_table_id);
    if (!b_epg)
        goto out_eit;

   /* We do not use psi_table_* primitives as the spec allows for holes in
    * section numbering, and there is no sure way to know whether you have
    * gathered all sections. */
   i_section = psi_get_section(p_eit);
   eit_table_id = i_table_id - EIT_TABLE_ID_PF_ACTUAL;
   if (eit_table_id >= MAX_EIT_TABLES)
      goto out_eit;
   if (p_sid->eit_table[eit_table_id].data[i_section] != NULL && psi_compare(p_sid->eit_table[eit_table_id].data[i_section], p_eit)) {
       /* Identical section. Shortcut. */
       ::free(p_sid->eit_table[eit_table_id].data[i_section]);
       p_sid->eit_table[eit_table_id].data[i_section] = p_eit;
       goto out_eit;
   }

   ::free(p_sid->eit_table[eit_table_id].data[i_section]);
   p_sid->eit_table[eit_table_id].data[i_section] = p_eit;

   /*
   eit_print(p_eit, msg_Dbg, NULL, demux_Iconv, NULL, PRINT_TEXT);
   if (b_print_enabled) {
      eit_print(p_eit, demux_Print, NULL, demux_Iconv, NULL, i_print_type);
      if (i_print_type == PRINT_XML)
         fprintf(print_fh, "\n");
   }
   */

out_eit:
   this->SendEIT(p_sid, i_dts, p_eit);
   if (!b_epg)
      ::free(p_eit);
}

void cLdvbdemux::HandleSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts)
{
   uint8_t i_table_id = psi_get_tableid(p_section);
   if (!psi_validate(p_section)) {
      cLbugf(cL::dbg_dvb, "invalid section on PID %hu\n", i_pid);
      ::free(p_section);
      return;
   }

   if (!psi_get_current(p_section)) {
      /* Ignore sections which are not in use yet. */
      ::free(p_section);
      return;
   }

   switch (i_table_id) {
      case PAT_TABLE_ID:
         this->HandlePATSection(i_pid, p_section, i_dts);
         break;

      case CAT_TABLE_ID:
         if (this->b_enable_emm)
            this->HandleCATSection(i_pid, p_section, i_dts);
         break;

      case PMT_TABLE_ID:
         this->HandlePMT(i_pid, p_section, i_dts);
         break;

      case NIT_TABLE_ID_ACTUAL:
         this->HandleNITSection(i_pid, p_section, i_dts);
         break;

      case SDT_TABLE_ID_ACTUAL:
         this->HandleSDTSection(i_pid, p_section, i_dts);
         break;

      case TID_ATSC_CVT1:
      case TID_ATSC_CVT2:
         this->HandleATSCSection(i_pid, p_section, i_dts);
         ::free(p_section);
         break;

      default:
         if (handle_epg(i_table_id)) {
            this->HandleEIT(i_pid, p_section, i_dts);
            break;
         }
         ::free(p_section);
         break;
   }
}

void cLdvbdemux::HandlePSIPacket(uint8_t *p_ts, mtime_t i_dts)
{
   uint16_t i_pid = ts_get_pid(p_ts);
   ts_pid_t *p_pid = &this->p_pids[i_pid];
   uint8_t i_cc = ts_get_cc(p_ts);
   if (ts_check_duplicate(i_cc, p_pid->i_last_cc) || !ts_has_payload(p_ts)) {
      cLbugf(cL::dbg_dvb, "PSI not processed on PID %hu\n", i_pid);
      return;
   }

   const uint8_t *p_payload;
   uint8_t i_length;

   if (p_pid->i_last_cc != -1 && ts_check_discontinuity(i_cc, p_pid->i_last_cc))
      psi_assemble_reset(&p_pid->p_psi_buffer, &p_pid->i_psi_buffer_used);

   p_payload = ts_section(p_ts);
   i_length = p_ts + TS_SIZE - p_payload;

   if (!psi_assemble_empty(&p_pid->p_psi_buffer, &p_pid->i_psi_buffer_used)) {
      uint8_t *p_section = psi_assemble_payload(&p_pid->p_psi_buffer, &p_pid->i_psi_buffer_used, &p_payload, &i_length);
      if (p_section != (uint8_t *) 0)
         this->HandleSection(i_pid, p_section, i_dts);
   }

   p_payload = ts_next_section(p_ts);
   i_length = p_ts + TS_SIZE - p_payload;

   while (i_length) {
      uint8_t *p_section = psi_assemble_payload(&p_pid->p_psi_buffer, &p_pid->i_psi_buffer_used, &p_payload, &i_length);
      if (p_section != (uint8_t *) 0)
         this->HandleSection(i_pid, p_section, i_dts);
   }
}

/* PID info functions */
const char *cLdvbdemux::h222_stream_type_desc(uint8_t i_stream_type)
{
   /* See ISO/IEC 13818-1 : 2000 (E) | Table 2-29 - Stream type assignments, Page 66 (48) */
   if (i_stream_type == 0)
      return "Reserved stream";
   switch (i_stream_type) {
      case 0x01: return "11172-2 video (MPEG-1)";
      case 0x02: return "H.262/13818-2 video (MPEG-2) or 11172-2 constrained video";
      case 0x03: return "11172-3 audio (MPEG-1)";
      case 0x04: return "13818-3 audio (MPEG-2)";
      case 0x05: return "H.222.0/13818-1  private sections";
      case 0x06: return "H.222.0/13818-1 PES private data";
      case 0x07: return "13522 MHEG";
      case 0x08: return "H.222.0/13818-1 Annex A - DSM CC";
      case 0x09: return "H.222.1";
      case 0x0A: return "13818-6 type A";
      case 0x0B: return "13818-6 type B";
      case 0x0C: return "13818-6 type C";
      case 0x0D: return "13818-6 type D";
      case 0x0E: return "H.222.0/13818-1 auxiliary";
      case 0x0F: return "13818-7 Audio with ADTS transport syntax";
      case 0x10: return "14496-2 Visual (MPEG-4 part 2 video)";
      case 0x11: return "14496-3 Audio with LATM transport syntax (14496-3/AMD 1)";
      case 0x12: return "14496-1 SL-packetized or FlexMux stream in PES packets";
      case 0x13: return "14496-1 SL-packetized or FlexMux stream in 14496 sections";
      case 0x14: return "ISO/IEC 13818-6 Synchronized Download Protocol";
      case 0x15: return "Metadata in PES packets";
      case 0x16: return "Metadata in metadata_sections";
      case 0x17: return "Metadata in 13818-6 Data Carousel";
      case 0x18: return "Metadata in 13818-6 Object Carousel";
      case 0x19: return "Metadata in 13818-6 Synchronized Download Protocol";
      case 0x1A: return "13818-11 MPEG-2 IPMP stream";
      case 0x1B: return "H.264/14496-10 video (MPEG-4/AVC)";
      case 0x24: return "H.265/23008-2 video (HEVC)";
      case 0x42: return "AVS Video";
      case 0x7F: return "IPMP stream";
      default  : return "Unknown stream";
   }
}

const char *cLdvbdemux::get_pid_desc(uint16_t i_pid, uint16_t *i_sid)
{
   int i, j, k;
   uint8_t i_last_section;
   uint8_t *p_desc;
   uint16_t i_nit_pid = NIT_PID, i_pcr_pid = 0;

   /* Simple cases */
   switch (i_pid) {
      case 0x00: return "PAT";
      case 0x01: return "CAT";
      case 0x11: return "SDT";
      case 0x12: return "EPG";
      case 0x14: return "TDT/TOT";
   }

   /* Detect NIT pid */
   if (psi_table_validate(this->pp_current_pat_sections)) {
      i_last_section = psi_table_get_lastsection(this->pp_current_pat_sections);
      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_section = psi_table_get_section(this->pp_current_pat_sections, i);
         uint8_t *p_program;

         j = 0;
         while ((p_program = pat_get_program(p_section, j++)) != (uint8_t *) 0) {
            /* Programs with PID == 0 are actually NIT */
            if (patn_get_program(p_program) == 0) {
               i_nit_pid = patn_get_pid(p_program);
               break;
            }
         }
      }
   }

   /* Detect EMM pids */
   if (this->b_enable_emm && psi_table_validate(this->pp_current_cat_sections)) {
      i_last_section = psi_table_get_lastsection(this->pp_current_cat_sections);
      for (i = 0; i <= i_last_section; i++) {
         uint8_t *p_section = psi_table_get_section(this->pp_current_cat_sections, i);

         j = 0;
         while ((p_desc = descl_get_desc(cat_get_descl(p_section), cat_get_desclength(p_section), j++)) != (uint8_t *) 0) {
            if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
               continue;

            if (desc09_get_pid(p_desc) == i_pid) {
               return "EMM";
            }
         }
      }
   }

   /* Detect streams in PMT */
   for (k = 0; k < this->i_nb_sids; k++) {
      sid_t *p_sid = this->pp_sids[k];
      if (p_sid->i_pmt_pid == i_pid)  {
         if (i_sid)
            *i_sid = p_sid->i_sid;
         return "PMT";
      }

      if (p_sid->i_sid && p_sid->p_current_pmt != (uint8_t *) 0) {
         uint8_t *p_current_pmt = p_sid->p_current_pmt;
         uint8_t *p_current_es;

         /* The PCR PID can be alone or PCR can be carried in some other PIDs (mostly video)
               so just remember the pid and if it is alone it will be reported as PCR, otherwise
               stream type of the PID will be reported */
         if (i_pid == pmt_get_pcrpid(p_current_pmt)) {
            if (i_sid)
               *i_sid = p_sid->i_sid;
            i_pcr_pid = pmt_get_pcrpid(p_current_pmt);
         }

         /* Look for ECMs */
         j = 0;
         while ((p_desc = descs_get_desc(pmt_get_descs(p_current_pmt), j++)) != (uint8_t *) 0) {
            if (desc_get_tag(p_desc) != 0x09 || !desc09_validate(p_desc))
               continue;

            if (desc09_get_pid(p_desc) == i_pid) {
               if (i_sid)
                  *i_sid = p_sid->i_sid;
               return "ECM";
            }
         }

         /* Detect stream types */
         j = 0;
         while ((p_current_es = pmt_get_es(p_current_pmt, j++)) != (uint8_t *) 0) {
            if (pmtn_get_pid(p_current_es) == i_pid) {
               if (i_sid)
                  *i_sid = p_sid->i_sid;
               return this->h222_stream_type_desc(pmtn_get_streamtype(p_current_es));
            }
         }
      }
   }

   /* Are there any other PIDs? */
   if (i_pid == i_nit_pid)
      return "NIT";

   if (i_pid == i_pcr_pid)
      return "PCR";

   return "...";
}

/*****************************************************************************
 * psi_pack_section: return psi section
 *  Note: Allocates the return value. The caller must free it.
 *****************************************************************************/
uint8_t *cLdvbdemux::psi_pack_section(uint8_t *p_section, unsigned int *pi_size)
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
 * this->psi_pack_sections: return psi sections as array
 *  Note: Allocates the return value. The caller must free it.
 *****************************************************************************/
uint8_t *cLdvbdemux::psi_pack_sections(uint8_t **pp_sections, unsigned int *pi_size)
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
uint8_t **cLdvbdemux::psi_unpack_sections(uint8_t *p_flat_sections, unsigned int i_size)
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

/*****************************************************************************
 * Functions that return packed sections
 *****************************************************************************/
uint8_t *cLdvbdemux::demux_get_current_packed_PAT(unsigned int *pi_pack_size)
{
   return this->psi_pack_sections(this->pp_current_pat_sections, pi_pack_size);
}

uint8_t *cLdvbdemux::demux_get_current_packed_CAT(unsigned int *pi_pack_size)
{
   return this->psi_pack_sections(this->pp_current_cat_sections, pi_pack_size);
}

uint8_t *cLdvbdemux::demux_get_current_packed_NIT(unsigned int *pi_pack_size)
{
   return this->psi_pack_sections(this->pp_current_nit_sections, pi_pack_size);
}

uint8_t *cLdvbdemux::demux_get_current_packed_SDT(unsigned int *pi_pack_size)
{
   return this->psi_pack_sections(this->pp_current_sdt_sections, pi_pack_size);
}

uint8_t *cLdvbdemux::demux_get_packed_EIT(uint16_t i_sid, uint8_t start_table, uint8_t end_table, unsigned int *eit_size)
{
   *eit_size = 0;
   sid_t *p_sid = FindSID(i_sid);
   if (p_sid == NULL)
      return NULL;

   /* Calculate eit table size (sum of all sections in all tables between start_start and end_table) */
   for (int i = start_table; i <= end_table; i++) {
      uint8_t eit_table_idx = i - EIT_TABLE_ID_PF_ACTUAL;
      if (eit_table_idx >= MAX_EIT_TABLES)
         continue;
      uint8_t **eit_sections = p_sid->eit_table[eit_table_idx].data;
      for (int r = 0; r < PSI_TABLE_MAX_SECTIONS; r++) {
         uint8_t *p_eit = eit_sections[r];
         if (!p_eit)
            continue;
         uint16_t psi_length = psi_get_length(p_eit) + PSI_HEADER_SIZE;
         *eit_size += psi_length;
      }
   }

   uint8_t *p_flat_section = cLmalloc(uint8_t, *eit_size);
   if (!p_flat_section)
      return (uint8_t *) 0;

   /* Copy sections */
   unsigned int i_pos = 0;
   for (int i = start_table; i <= end_table; i++) {
      uint8_t eit_table_idx = i - EIT_TABLE_ID_PF_ACTUAL;
      if (eit_table_idx >= MAX_EIT_TABLES)
         continue;
      uint8_t **eit_sections = p_sid->eit_table[eit_table_idx].data;
      for (int r = 0; r < PSI_TABLE_MAX_SECTIONS; r++) {
         uint8_t *p_eit = eit_sections[r];
         if (!p_eit)
            continue;
         uint16_t psi_length = psi_get_length(p_eit) + PSI_HEADER_SIZE;
         memcpy(p_flat_section + i_pos, p_eit, psi_length);
         i_pos += psi_length;
         /* eit_print( p_eit, msg_Dbg, NULL, demux_Iconv, NULL, PRINT_TEXT ); */
      }
   }
   return p_flat_section;
}

uint8_t *cLdvbdemux::demux_get_packed_EIT_pf(uint16_t service_id, unsigned int *pi_pack_size)
{
   return demux_get_packed_EIT(service_id, EIT_TABLE_ID_PF_ACTUAL, EIT_TABLE_ID_PF_ACTUAL, pi_pack_size);
}

uint8_t *cLdvbdemux::demux_get_packed_EIT_schedule(uint16_t service_id, unsigned int *pi_pack_size)
{
   return demux_get_packed_EIT(service_id, EIT_TABLE_ID_SCHED_ACTUAL_FIRST, EIT_TABLE_ID_SCHED_ACTUAL_LAST, pi_pack_size);
}

uint8_t *cLdvbdemux::demux_get_packed_PMT(uint16_t i_sid, unsigned int *pi_pack_size)
{
   sid_t *p_sid = this->FindSID(i_sid);
   if (p_sid != (sid_t *) 0 && p_sid->p_current_pmt && pmt_validate(p_sid->p_current_pmt))
      return this->psi_pack_section(p_sid->p_current_pmt, pi_pack_size);
   return (uint8_t *) 0;
}

void cLdvbdemux::demux_get_PID_info(uint16_t i_pid, uint8_t *p_data)
{
   ts_pid_info_t *p_info = (ts_pid_info_t *)p_data;
   *p_info = this->p_pids[i_pid].info;
}

void cLdvbdemux::demux_get_PIDS_info(uint8_t *p_data)
{
   int i_pid;
   for (i_pid = 0; i_pid < MAX_PIDS; i_pid++)
      this->demux_get_PID_info(i_pid, p_data + (i_pid * sizeof(ts_pid_info_t)));
}

void cLdvbdemux::config_ReadFile(void)
{
   FILE *p_file;
   char psz_line[2048];
   int i;

   if (this->psz_conf_file == (const char *) 0) {
      cLbug(cL::dbg_dvb, "no config file\n");
      return;
   }

   if ((fopen(p_file, this->psz_conf_file, "r")) == (FILE *) 0) {
      cLbugf(cL::dbg_dvb, "can't fopen config file %s\n", this->psz_conf_file);
      return;
   }

   while (fgets(psz_line, sizeof(psz_line), p_file) != (char *) 0) {
      output_config_t config;
      output_t *p_output;
      char *psz_token, *psz_parser;

      psz_parser = strchr(psz_line, '#');

      if (psz_parser != (char *) 0)
         *psz_parser-- = '\0';

      while (psz_parser >= psz_line && isblank(*psz_parser))
         *psz_parser-- = '\0';

      if (psz_line[0] == '\0')
         continue;

      this->config_Defaults(&config);

      psz_token = strtok_r(psz_line, "\t\n ", &psz_parser);
      if (psz_token == NULL || !this->config_ParseHost(&config, psz_token)) {
         this->config_Free(&config);
         continue;
      }

      psz_token = strtok_r((char *) 0, "\t\n ", &psz_parser);
      if (psz_token == (char *) 0) {
         this->config_Free(&config);
         continue;
      }

      if(atoi(psz_token) == 1) {
         config.i_config |= OUTPUT_WATCH;
      } else {
         config.i_config &= ~OUTPUT_WATCH;
      }

      psz_token = strtok_r((char *) 0, "\t\n ", &psz_parser);
      if (psz_token == (char *) 0) {
         this->config_Free(&config);
         continue;
      }

      if (psz_token[0] == '*') {
         config.b_passthrough = true;
      } else {
         config.i_sid = strtol(psz_token, (char **) 0, 0);
         psz_token = strtok_r((char *) 0, "\t\n ", &psz_parser);
         if (psz_token != (char *) 0) {
            psz_parser = (char *) 0;
            for (;;) {
               psz_token = strtok_r(psz_token, ",", &psz_parser);
               if (psz_token == (char *) 0)
                  break;
               config.pi_pids = cLrealloc(uint16_t, config.pi_pids, (config.i_nb_pids + 1));
               config.pi_pids[config.i_nb_pids++] = (uint16_t)strtol(psz_token, (char **) 0, 0);
               psz_token = (char *) 0;
            }
         }
      }

      this->config_Print(&config);

      p_output = this->output_Find(&config);

      if (p_output == (output_t *) 0) {
         p_output = this->output_Create(&config);
      }

      if (p_output != (output_t *) 0) {
         ::free(p_output->config.psz_displayname);
         p_output->config.psz_displayname = strdup(config.psz_displayname);

         config.i_config |= OUTPUT_VALID | OUTPUT_STILL_PRESENT;
         this->output_Change(p_output, &config);
         demux_Change(p_output, &config);
      }

      this->config_Free(&config);
   }

   fclose(p_file);

   for (i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];
      output_config_t config;

      this->config_Init(&config);

      if ((p_output->config.i_config & OUTPUT_VALID) && !(p_output->config.i_config & OUTPUT_STILL_PRESENT)) {
         cLbugf(cL::dbg_dvb, "closing %s\n", p_output->config.psz_displayname);
         demux_Change(p_output, &config);
         this->output_Close(p_output);
      }

      p_output->config.i_config &= ~OUTPUT_STILL_PRESENT;
      this->config_Free(&config);
   }
}

bool cLdvbdemux::set_pid_map(char *s)
{
   char *str1 = s;
   char *saveptr = (char *) 0;
   /* We expect a comma separated list of numbers.
       Put them into the pi_newpids array as they appear */
   for (int i = 0; i < CLDVB_N_MAP_PIDS; i++) {
      char *tok = strtok_r(str1, ",", &saveptr);
      if (!tok)
         break;
      int i_newpid = strtoul(tok, (char **) 0, 0);
      if (!i_newpid) {
         cLbug(cL::dbg_low, "Invalid pidmap string\n");
         return false;
      }
      pi_newpids[i] = i_newpid;
      str1 = (char *) 0;
   }
   this->b_do_remap = true;
   return true;
}


