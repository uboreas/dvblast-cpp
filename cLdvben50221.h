/*
 * cLdvben50221.h
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
 *****************************************************************************
 * en50221.h, en50221.c
 *****************************************************************************
 * Copyright (C) 2004-2005, 2010, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 * Based on code from libdvbci Copyright (C) 2000 Klaus Schmidinger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef CLDVBEN50221_H_
#define CLDVBEN50221_H_

#include <cLdvboutput.h>
#include <stddef.h>
#include <string.h>

#ifdef HAVE_CLDVBHW

#include <linux_dvb_version.h>
#include <linux_dvb_dmx.h>
#include <linux_dvb_frontend.h>
#include <linux_dvb_ca.h>

#define STRINGIFY(z)          UGLY_KLUDGE(z)
#define UGLY_KLUDGE(z)        #z
#define EN50221_MMI_NONE      0
#define EN50221_MMI_ENQ       1
#define EN50221_MMI_ANSW      2
#define EN50221_MMI_MENU      3
#define EN50221_MMI_MENU_ANSW 4
#define EN50221_MMI_LIST      5
#define MAX_CI_SLOTS          16
#define MAX_SESSIONS          32

#endif //HAVE_CLDVBHW

class cLdvben50221 : public cLdvboutput {

   public:

      typedef enum {
         RET_OK                  = 0,
         RET_ERR                 = 1,
         RET_FRONTEND_STATUS     = 2,
         RET_MMI_STATUS          = 3,
         RET_MMI_SLOT_STATUS     = 4,
         RET_MMI_RECV            = 5,
         RET_MMI_WAIT            = 6,
         RET_NODATA              = 7,
         RET_PAT                 = 8,
         RET_CAT                 = 9,
         RET_NIT                 = 10,
         RET_SDT                 = 11,
         RET_PMT                 = 12,
         RET_PIDS                = 13,
         RET_PID                 = 14,
         RET_HUH                 = 255,
      } ctl_cmd_answer_t;

#ifdef HAVE_CLDVBHW

      typedef struct en50221_mmi_object_t {
            int i_object_type;
            union {
                  struct {
                        int b_blind;
                        char *psz_text;
                  } enq;
                  struct {
                        int b_ok;
                        char *psz_answ;
                  } answ;
                  struct {
                        char *psz_title, *psz_subtitle, *psz_bottom;
                        char **ppsz_choices;
                        int i_choices;
                  } menu; /* menu and list are the same */
                  struct {
                        int i_choice;
                  } menu_answ;
            } u;
      } en50221_mmi_object_t;

      struct ret_frontend_status {
            struct dvb_frontend_info info;
            fe_status_t i_status;
            uint32_t i_ber;
            uint16_t i_strength, i_snr;
      };

      struct ret_mmi_status {
            ca_caps_t caps;
      };

      struct ret_mmi_slot_status {
            ca_slot_info_t sinfo;
      };

      struct ret_mmi_recv {
            en50221_mmi_object_t object;
      };

      struct cmd_mmi_send {
            uint8_t i_slot;
            en50221_mmi_object_t object;
      };

#endif //HAVE_CLDVBHW

   private:

#ifdef HAVE_CLDVBHW

      typedef void (*fssn_handle)(int, uint8_t *, int, cLdvben50221 *);
      typedef void (*fssn_common)(int, cLdvben50221 *);

      typedef struct en50221_msg_t {
            uint8_t *p_data;
            int i_size;
            struct en50221_msg_t *p_next;
      } en50221_msg_t;

      typedef struct en50221_session_t {
            int i_slot;
            int i_resource_id;
            fssn_handle pf_handle;
            fssn_common pf_close;
            fssn_common pf_manage;
            void *p_sys;
      } en50221_session_t;

      typedef struct ci_slot_t {
            bool b_active;
            bool b_expect_answer;
            bool b_has_data;
            bool b_mmi_expected;
            bool b_mmi_undisplayed;

            /* TPDU reception */
            en50221_msg_t *p_recv;

            /* TPDU emission */
            en50221_msg_t *p_send;
            en50221_msg_t **pp_send_last;
            uint8_t *p;

            /* InitSlot timer */
            struct cLev_timer init_watcher;

            /* SPDUSend callback, if p_spdu is not NULL */
            /* SessionOpen callback, if not 0 */
            int i_pending_session_id;
      } ci_slot_t;

      typedef struct {
            int i_nb_system_ids;
            uint16_t *pi_system_ids;
            int i_selected_programs;
            int b_high_level;
      } system_ids_t;

      typedef struct {
            int i_session_id;
            int i_interval;
            struct cLev_timer watcher;
      } date_time_t;

      typedef struct {
            en50221_mmi_object_t last_object;
      } mmi_t;

      struct cLev_io cam_watcher;
      struct cLev_timer slot_watcher;
      int i_nb_slots;
      ci_slot_t p_slots[MAX_CI_SLOTS];
      en50221_session_t p_sessions[MAX_SESSIONS];
#endif

      int i_ca_type;

#ifdef HAVE_CLDVBHW
      static int en50221_SerializeMMIObject(uint8_t *p_answer, ssize_t *pi_size, en50221_mmi_object_t *p_object);
      static int en50221_UnserializeMMIObject( en50221_mmi_object_t *p_object, ssize_t i_size);
      static uint8_t *GetLength(uint8_t *p_data, int *pi_length);
      static uint8_t *SetLength(uint8_t *p_data, int i_length);
      static void Dump(bool b_outgoing, uint8_t *p_data, int i_size);
      int TPDUWrite(uint8_t i_slot);
      int TPDUSend(uint8_t i_slot, uint8_t i_tag, const uint8_t *p_content, int i_length);
      int TPDURecv();
      static int ResourceIdToInt(uint8_t *p_data);
      int SPDUSend(int i_session_id, uint8_t *p_data, int i_size);
      void SessionOpenCb(uint8_t i_slot);
      void SessionOpen(uint8_t i_slot, uint8_t *p_spdu, int i_size);
#if 0
      void SessionCreate(int i_slot, int i_resource_id);
#endif
      void SessionCreateResponse(uint8_t i_slot, uint8_t *p_spdu, int i_size);
      void SessionSendClose(int i_session_id);
      void SessionClose(int i_session_id);
      void SPDUHandle(uint8_t i_slot, uint8_t *p_spdu, int i_size);
      static int APDUGetTag(const uint8_t *p_apdu, int i_size);
      static uint8_t *APDUGetLength(uint8_t *p_apdu, int *pi_size);
      int APDUSend(int i_session_id, int i_tag, uint8_t *p_data, int i_size);
      static void ResourceManagerHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj);
      void ResourceManagerOpen(int i_session_id);
      void ApplicationInformationEnterMenu(int i_session_id);
      static void ApplicationInformationHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj);
      void ApplicationInformationOpen(int i_session_id);
      static bool CheckSystemID(system_ids_t *p_ids, uint16_t i_id);
      static bool HasCADescriptors(system_ids_t *p_ids, uint8_t *p_descs);
      static void CopyCADescriptors(system_ids_t *p_ids, uint8_t i_cmd, uint8_t *p_infos, uint8_t *p_descs);
      uint8_t *CAPMTBuild(int i_session_id, uint8_t *p_pmt, uint8_t i_list_mgt, uint8_t i_cmd, int *pi_capmt_size);
      void CAPMTFirst(int i_session_id, uint8_t *p_pmt);
      void CAPMTAdd(int i_session_id, uint8_t *p_pmt);
      void CAPMTUpdate(int i_session_id, uint8_t *p_pmt);
      void CAPMTDelete(int i_session_id, uint8_t *p_pmt);
      static void ConditionalAccessHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj);
      static void ConditionalAccessClose(int i_session_id, cLdvben50221 *pobj);
      void ConditionalAccessOpen(int i_session_id);
      void DateTimeSend(int i_session_id);
      static void _DateTimeSend(void *loop, void *p, int revents);
      static void DateTimeHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj);
      static void DateTimeClose(int i_session_id, cLdvben50221 *pobj);
      void DateTimeOpen(int i_session_id);
      static void en50221_MMIFree(en50221_mmi_object_t *p_object);
      void MMISendObject(int i_session_id, en50221_mmi_object_t *p_object);
      void MMISendClose(int i_session_id);
      void MMIDisplayReply(int i_session_id);
      char *MMIGetText(uint8_t **pp_apdu, int *pi_size);
      void MMIHandleEnq(int i_session_id, uint8_t *p_apdu, int i_size);
      void MMIHandleMenu(int i_session_id, int i_tag, uint8_t *p_apdu, int i_size);
      static void MMIHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj);
      static void MMIClose(int i_session_id, cLdvben50221 *pobj);
      void MMIOpen(int i_session_id);
      void InitSlot(int i_slot);
      static void ResetSlotCb(void *loop, void *w, int revents);
      void ResetSlot(int i_slot);
      static void en50221_Read(void *loop, void *p, int revents);
      static void en50221_Poll(void *loop, void *p, int revents);
#endif

   protected:
      int i_ca_handle;
      int i_canum;

#ifdef HAVE_CLDVBHW
      void en50221_Init();
      void en50221_Reset();
      void en50221_AddPMT(uint8_t *p_pmt);
      void en50221_UpdatePMT(uint8_t *p_pmt);
      void en50221_DeletePMT(uint8_t *p_pmt);
      uint8_t en50221_StatusMMI(uint8_t *p_answer, ssize_t *pi_size);
      uint8_t en50221_StatusMMISlot(uint8_t *p_buffer, ssize_t i_size, uint8_t *p_answer, ssize_t *pi_size);
      uint8_t en50221_OpenMMI(uint8_t *p_buffer, ssize_t i_size);
      uint8_t en50221_CloseMMI(uint8_t *p_buffer, ssize_t i_size);
      uint8_t en50221_GetMMIObject(uint8_t *p_buffer, ssize_t i_size, uint8_t *p_answer, ssize_t *pi_size);
      uint8_t en50221_SendMMIObject(uint8_t *p_buffer, ssize_t i_size);

      /* dev_PIDIsSelected: 0: false; 1: true; -1: not applicable */
      virtual int dev_PIDIsSelected(uint16_t i_pid) = 0;
      virtual void dev_ResendCAPMTs() = 0;

#else //HAVE_CLDVBHW

      static inline void en50221_Init() {};
      static inline void en50221_AddPMT(uint8_t *p_pmt) {};
      static inline void en50221_UpdatePMT(uint8_t *p_pmt) {};
      static inline void en50221_DeletePMT(uint8_t *p_pmt) {};
      static inline void en50221_Reset() {};

#endif //HAVE_CLDVBHW

   public:
      inline void set_cadevice(int i) {
         this->i_canum = i;
      }

      cLdvben50221();
      virtual ~cLdvben50221();

};

#endif /*CLDVBEN50221_H_*/
