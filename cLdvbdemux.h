/*
 * cLdvbdemux.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * dvblast.h, demux.c
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

#ifndef CLDVBDEMUX_H_
#define CLDVBDEMUX_H_

#include <cLdvbmrtgcnt.h>
#include <cLdvben50221.h>
#include <bitstream/mpeg/psi.h>

class cLdvbdemux : public cLdvben50221 {

   public:
      typedef enum {
         I_PMTPID = 0,
         I_APID,
         I_VPID,
         I_SPUPID
      } pidmap_offset;

      typedef struct ts_pid_info {
            mtime_t  i_first_packet_ts;     /* Time of the first seen packet */
            mtime_t  i_last_packet_ts;      /* Time of the last seen packet */
            unsigned long i_packets;                  /* How much packets have been seen */
            unsigned long i_cc_errors;                /* Countinuity counter errors */
            unsigned long i_transport_errors;         /* Transport errors */
            unsigned long i_bytes_per_sec;            /* How much bytes were process last second */
            uint8_t  i_scrambling;                    /* Scrambling bits from the last ts packet: 0 = Not scrambled, 1 = Reserved for future use, 2 = Scrambled with even key, 3 = Scrambled with odd key */
      } ts_pid_info_t;

   private:
      typedef struct ts_pid_t
      {
         int i_refcount;
         int i_psi_refcount;
         bool b_pes;
         int8_t i_last_cc;
         int i_demux_fd;
         /* b_emm is set to true when PID carries EMM packet
          and should be outputed in all services */
         bool b_emm;

         /* PID info and stats */
         mtime_t i_bytes_ts;
         unsigned long i_packets_passed;
         ts_pid_info_t info;

         /* biTStream PSI section gathering */
         uint8_t *p_psi_buffer;
         uint16_t i_psi_buffer_used;

         output_t **pp_outputs;
         int i_nb_outputs;

         int i_pes_status; /* pes + unscrambled */
         struct cLev_timer timeout_watcher;
      } ts_pid_t;

      typedef struct sid_t
      {
            uint16_t i_sid, i_pmt_pid;
            uint8_t *p_current_pmt;
      } sid_t;

      mtime_t i_wallclock;
      PSI_TABLE_DECLARE(pp_current_pat_sections);
      PSI_TABLE_DECLARE(pp_next_pat_sections);
      PSI_TABLE_DECLARE(pp_current_cat_sections);
      PSI_TABLE_DECLARE(pp_next_cat_sections);
      PSI_TABLE_DECLARE(pp_current_nit_sections);
      PSI_TABLE_DECLARE(pp_next_nit_sections);
      PSI_TABLE_DECLARE(pp_current_sdt_sections);
      PSI_TABLE_DECLARE(pp_next_sdt_sections);
      mtime_t i_last_dts;
      int i_demux_fd;
      uint64_t i_nb_packets;
      uint64_t i_nb_invalids;
      uint64_t i_nb_discontinuities;
      uint64_t i_nb_errors;
      int i_tuner_errors;
      mtime_t i_last_error;
      mtime_t i_last_reset;
      struct cLev_timer print_watcher;
      struct cLev_timer quit_watcher;
      struct cLev_signal sigint_watcher, sigterm_watcher, sighup_watcher;

      static void break_cb(void *loop, void *w, int revents);
      static void debug_cb(void *p, const char *fmt, ...);
      uint16_t map_es_pid(output_t * p_output, uint8_t *p_es, uint16_t i_pid);
      sid_t *FindSID(uint16_t i_sid);
      static void PrintCb(void *loop, void *w, int revents);
      static void PrintESCb(void *loop, void *p, int revents);
      void PrintES(uint16_t i_pid);
      void demux_Handle(block_t *p_ts);
      static int IsIn(uint16_t *pi_pids, int i_nb_pids, uint16_t i_pid);
      void SetDTS(block_t *p_list);
      void SetPID(uint16_t i_pid);
      void SetPID_EMM(uint16_t i_pid);
      void UnsetPID(uint16_t i_pid);
      void StartPID(output_t *p_output, uint16_t i_pid);
      void StopPID(output_t *p_output, uint16_t i_pid);
      void SelectPID(uint16_t i_sid, uint16_t i_pid);
      void UnselectPID(uint16_t i_sid, uint16_t i_pid);
      void SelectPMT(uint16_t i_sid, uint16_t i_pid);
      void UnselectPMT(uint16_t i_sid, uint16_t i_pid);
      void GetPIDS(uint16_t **ppi_wanted_pids, int *pi_nb_wanted_pids, uint16_t i_sid, const uint16_t *pi_pids, int i_nb_pids);
      void OutputPSISection(output_t *p_output, uint8_t *p_section, uint16_t i_pid, uint8_t *pi_cc, mtime_t i_dts, block_t **pp_ts_buffer, uint8_t *pi_ts_buffer_offset);
      void SendPAT(mtime_t i_dts);
      void SendPMT(sid_t *p_sid, mtime_t i_dts);
      void SendNIT(mtime_t i_dts);
      void SendSDT(mtime_t i_dts);
      void SendEIT(sid_t *p_sid, mtime_t i_dts, uint8_t *p_eit);
      void FlushEIT(output_t *p_output, mtime_t i_dts);
      void SendTDT(block_t *p_ts);
      void SendEMM(block_t *p_ts);
      void NewPAT(output_t *p_output);
      void CopyDescriptors(uint8_t *p_descs, uint8_t *p_current_descs);
      void NewPMT(output_t *p_output);
      static void NewNIT(output_t *p_output);
      void NewSDT(output_t *p_output);
      void UpdatePAT(uint16_t i_sid);
      void UpdatePMT(uint16_t i_sid);
      void UpdateSDT(uint16_t i_sid);
      void UpdateTSID(void);
      bool PIDWouldBeSelected(uint8_t *p_es);
      static bool PIDCarriesPES(const uint8_t *p_es);
      static uint8_t *ca_desc_find(uint8_t *p_descl, uint16_t i_length, uint16_t i_ca_pid);
      void DeleteProgram(uint16_t i_sid, uint16_t i_pid);
      void HandlePAT(mtime_t i_dts);
      void HandlePATSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts);
      void HandleCAT(mtime_t i_dts);
      void HandleCATSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts);
      void mark_pmt_pids(uint8_t *p_pmt, uint8_t pid_map[], uint8_t marker);
      void HandlePMT(uint16_t i_pid, uint8_t *p_pmt, mtime_t i_dts);
      void HandleNIT(mtime_t i_dts);
      void HandleNITSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts);
      void HandleSDT(mtime_t i_dts);
      void HandleSDTSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts);
      void HandleEIT(uint16_t i_pid, uint8_t *p_eit, mtime_t i_dts);
      void HandleSection(uint16_t i_pid, uint8_t *p_section, mtime_t i_dts);
      void HandlePSIPacket(uint8_t *p_ts, mtime_t i_dts);
      static const char *h222_stream_type_desc(uint8_t i_stream_type);
      const char *get_pid_desc(uint16_t i_pid, uint16_t *i_sid);
      static uint8_t *psi_pack_section(uint8_t *p_section, unsigned int *pi_size);
      static uint8_t *psi_pack_sections(uint8_t **pp_sections, unsigned int *pi_size);
      static uint8_t **psi_unpack_sections(uint8_t *p_flat_sections, unsigned int i_size);

   protected:
      cLdvbmrtgcnt *pmrtg;
      const char *psz_conf_file;
      bool b_enable_emm;
      bool b_enable_ecm;
      mtime_t i_es_timeout;
      int b_budget_mode;
      int b_select_pmts;
      int b_any_type;
      uint16_t pi_newpids[CLDVB_N_MAP_PIDS];
      ts_pid_t p_pids[MAX_PIDS];
      sid_t **pp_sids;
      int i_nb_sids;
      mtime_t i_quit_timeout_duration;

      bool SIDIsSelected(uint16_t i_sid);
      static bool PMTNeedsDescrambling(uint8_t *p_pmt);
      void demux_Run(block_t *p_ts);
      void demux_Change(output_t *p_output, const output_config_t *p_config);
      uint8_t *demux_get_current_packed_PAT(unsigned int *pi_pack_size);
      uint8_t *demux_get_current_packed_CAT(unsigned int *pi_pack_size);
      uint8_t *demux_get_current_packed_NIT(unsigned int *pi_pack_size);
      uint8_t *demux_get_current_packed_SDT(unsigned int *pi_pack_size);
      uint8_t *demux_get_packed_PMT(uint16_t service_id, unsigned int *pi_pack_size);
      void demux_get_PID_info(uint16_t i_pid, uint8_t *p_data);
      void demux_get_PIDS_info(uint8_t *p_data);

#ifdef HAVE_CLDVBHW
      virtual int dev_PIDIsSelected(uint16_t i_pid) = 0;
      virtual void dev_ResendCAPMTs() = 0;
#endif
      virtual void dev_Open() = 0;
      virtual void dev_Reset() = 0;
      virtual int dev_SetFilter(uint16_t i_pid) = 0;
      virtual void dev_UnsetFilter(int i_fd, uint16_t i_pid) = 0;

   public:
      bool set_pid_map(char *s);
      inline void set_configfile(const char *cf) {
         this->psz_conf_file = cf;
      }
      inline bool get_pid_filter() {
         return (this->b_select_pmts == 1);
      }
      inline void set_pid_filter(bool b = true) {
         this->b_select_pmts = (b ? 1 : 0);
      }
      inline void hw_filtering(bool b = true) {
         this->b_budget_mode = b ? 0 : 1;
      }
      inline void pass_all_es(int i = 1) {
         this->b_any_type = i;
      }
      inline void set_pass_emm(bool b = true) {
         this->b_enable_emm = b;
      }
      inline void set_pass_ecm(bool b = true) {
         this->b_enable_ecm = b;
      }
      inline void set_quit_timeout(mtime_t i) {
         this->i_quit_timeout_duration = i;
      }
      inline void set_es_timeout(mtime_t i) {
         this->i_es_timeout = i;
      }

      bool demux_Setup(cLevCB sighandler = (cLevCB) 0, void *opaque = (void *) 0);

      void demux_Open();
      void demux_Close();
      void config_ReadFile();

      cLdvbdemux();
      virtual ~cLdvbdemux();

};

#endif /*CLDVBDEMUX_H_*/
