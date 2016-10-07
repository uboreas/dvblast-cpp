/*
 * cLdvben50221.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * en50221.c : implementation of the transport, session and applications
 * layers of EN 50 221
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

#include <cLdvben50221.h>

#ifdef HAVE_CLDVBHW

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

/* DVB Card Drivers */
#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/ca.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/ci.h>
#include <bitstream/dvb/si.h>

#undef DEBUG_TPDU
#define CAM_INIT_TIMEOUT      15000000 /* 15 s */
#undef HLCI_WAIT_CAM_READY
#define CAPMT_WAIT            100 /* ms */
#define CA_POLL_PERIOD        100000 /* 100 ms */
#define SIZE_INDICATOR        0x80

/*
 * Transport layer
 */
#define MAX_TPDU_SIZE         4096
#define MAX_TPDU_DATA         (MAX_TPDU_SIZE - 7)
#define DATA_INDICATOR        0x80
#define T_SB                  0x80
#define T_RCV                 0x81
#define T_CREATE_TC           0x82
#define T_CTC_REPLY           0x83
#define T_DELETE_TC           0x84
#define T_DTC_REPLY           0x85
#define T_REQUEST_TC          0x86
#define T_NEW_TC              0x87
#define T_TC_ERROR            0x88
#define T_DATA_LAST           0xA0
#define T_DATA_MORE           0xA1

/*
 * Session layer
 */
#define ST_SESSION_NUMBER           0x90
#define ST_OPEN_SESSION_REQUEST     0x91
#define ST_OPEN_SESSION_RESPONSE    0x92
#define ST_CREATE_SESSION           0x93
#define ST_CREATE_SESSION_RESPONSE  0x94
#define ST_CLOSE_SESSION_REQUEST    0x95
#define ST_CLOSE_SESSION_RESPONSE   0x96
#define SS_OK                       0x00
#define SS_NOT_ALLOCATED            0xF0
#define RI_RESOURCE_MANAGER            0x00010041
#define RI_APPLICATION_INFORMATION     0x00020041
#define RI_CONDITIONAL_ACCESS_SUPPORT  0x00030041
#define RI_HOST_CONTROL                0x00200041
#define RI_DATE_TIME                   0x00240041
#define RI_MMI                         0x00400041

/*
 * Application layer
 */
#define AOT_NONE                    0x000000
#define AOT_PROFILE_ENQ             0x9F8010
#define AOT_PROFILE                 0x9F8011
#define AOT_PROFILE_CHANGE          0x9F8012
#define AOT_APPLICATION_INFO_ENQ    0x9F8020
#define AOT_APPLICATION_INFO        0x9F8021
#define AOT_ENTER_MENU              0x9F8022
#define AOT_CA_INFO_ENQ             0x9F8030
#define AOT_CA_INFO                 0x9F8031
#define AOT_CA_PMT                  0x9F8032
#define AOT_CA_PMT_REPLY            0x9F8033
#define AOT_CA_UPDATE               0x9F8034
#define AOT_TUNE                    0x9F8400
#define AOT_REPLACE                 0x9F8401
#define AOT_CLEAR_REPLACE           0x9F8402
#define AOT_ASK_RELEASE             0x9F8403
#define AOT_DATE_TIME_ENQ           0x9F8440
#define AOT_DATE_TIME               0x9F8441
#define AOT_CLOSE_MMI               0x9F8800
#define AOT_DISPLAY_CONTROL         0x9F8801
#define AOT_DISPLAY_REPLY           0x9F8802
#define AOT_TEXT_LAST               0x9F8803
#define AOT_TEXT_MORE               0x9F8804
#define AOT_KEYPAD_CONTROL          0x9F8805
#define AOT_KEYPRESS                0x9F8806
#define AOT_ENQ                     0x9F8807
#define AOT_ANSW                    0x9F8808
#define AOT_MENU_LAST               0x9F8809
#define AOT_MENU_MORE               0x9F880A
#define AOT_MENU_ANSW               0x9F880B
#define AOT_LIST_LAST               0x9F880C
#define AOT_LIST_MORE               0x9F880D
#define AOT_SUBTITLE_SEGMENT_LAST   0x9F880E
#define AOT_SUBTITLE_SEGMENT_MORE   0x9F880F
#define AOT_DISPLAY_MESSAGE         0x9F8810
#define AOT_SCENE_END_MARK          0x9F8811
#define AOT_SCENE_DONE              0x9F8812
#define AOT_SCENE_CONTROL           0x9F8813
#define AOT_SUBTITLE_DOWNLOAD_LAST  0x9F8814
#define AOT_SUBTITLE_DOWNLOAD_MORE  0x9F8815
#define AOT_FLUSH_DOWNLOAD          0x9F8816
#define AOT_DOWNLOAD_REPLY          0x9F8817
#define AOT_COMMS_CMD               0x9F8C00
#define AOT_CONNECTION_DESCRIPTOR   0x9F8C01
#define AOT_COMMS_REPLY             0x9F8C02
#define AOT_COMMS_SEND_LAST         0x9F8C03
#define AOT_COMMS_SEND_MORE         0x9F8C04
#define AOT_COMMS_RCV_LAST          0x9F8C05
#define AOT_COMMS_RCV_MORE          0x9F8C06

/*
 * MMI
 */
/* Display Control Commands */
#define DCC_SET_MMI_MODE                           0x01
#define DCC_DISPLAY_CHARACTER_TABLE_LIST           0x02
#define DCC_INPUT_CHARACTER_TABLE_LIST             0x03
#define DCC_OVERLAY_GRAPHICS_CHARACTERISTICS       0x04
#define DCC_FULL_SCREEN_GRAPHICS_CHARACTERISTICS   0x05
/* MMI Modes */
#define MM_HIGH_LEVEL                              0x01
#define MM_LOW_LEVEL_OVERLAY_GRAPHICS              0x02
#define MM_LOW_LEVEL_FULL_SCREEN_GRAPHICS          0x03
/* Display Reply IDs */
#define DRI_MMI_MODE_ACK                              0x01
#define DRI_LIST_DISPLAY_CHARACTER_TABLES             0x02
#define DRI_LIST_INPUT_CHARACTER_TABLES               0x03
#define DRI_LIST_GRAPHIC_OVERLAY_CHARACTERISTICS      0x04
#define DRI_LIST_FULL_SCREEN_GRAPHIC_CHARACTERISTICS  0x05
#define DRI_UNKNOWN_DISPLAY_CONTROL_CMD               0xF0
#define DRI_UNKNOWN_MMI_MODE                          0xF1
#define DRI_UNKNOWN_CHARACTER_TABLE                   0xF2
/* Enquiry Flags */
#define EF_BLIND     0x01
/* Answer IDs */
#define AI_CANCEL    0x00
#define AI_ANSWER    0x01


#define TAB_APPEND(count, tab, p) \
      if ((count) > 0) { \
         (tab) = (char **)realloc(tab, sizeof(void **) * ((count) + 1)); \
      } else {                                           \
         (tab) = (char **)malloc(sizeof(void **)); \
      } \
      (tab)[count] = (p); \
      (count)++

#endif //HAVE_CLDVBHW


cLdvben50221::cLdvben50221()
{
#ifdef HAVE_CLDVBHW
   this->i_nb_slots = 0;
#endif
   this->i_ca_handle = 0;
   this->i_ca_type = -1;
   this->i_canum = 0;

   cLbug(cL::dbg_high, "cLdvben50221 created\n");
}

cLdvben50221::~cLdvben50221()
{
   cLbug(cL::dbg_high, "cLdvben50221 deleted\n");
}

#ifdef HAVE_CLDVBHW


#define STORE_MEMBER(pp_pointer, i_typ, i_size) \
      if (i_size + *pi_size > i_max_size) \
         return -1; \
      memcpy( p_answer, *pp_pointer, i_size); \
      *pp_pointer = (i_typ)*pi_size; \
      *pi_size += i_size; \
      p_answer += i_size;

int cLdvben50221::en50221_SerializeMMIObject(uint8_t *p_answer, ssize_t *pi_size, en50221_mmi_object_t *p_object)
{
   ssize_t i_max_size = *pi_size;
   en50221_mmi_object_t *p_serialized = (en50221_mmi_object_t *)p_answer;

   if ((ssize_t)sizeof(en50221_mmi_object_t) > i_max_size)
      return -1;

   memcpy(p_answer, p_object, sizeof(en50221_mmi_object_t));
   *pi_size = sizeof(en50221_mmi_object_t);
   p_answer += sizeof(en50221_mmi_object_t);

   switch (p_object->i_object_type) {
      case EN50221_MMI_ENQ:
         STORE_MEMBER(&p_serialized->u.enq.psz_text, char *, (ssize_t)(strlen(p_object->u.enq.psz_text) + 1));
         break;
      case EN50221_MMI_ANSW:
         STORE_MEMBER(&p_serialized->u.answ.psz_answ, char *, (ssize_t)(strlen(p_object->u.answ.psz_answ) + 1));
         break;
      case EN50221_MMI_MENU:
      case EN50221_MMI_LIST:
      {
         STORE_MEMBER(&p_serialized->u.menu.psz_title, char *, (ssize_t)(strlen(p_object->u.menu.psz_title) + 1));
         STORE_MEMBER(&p_serialized->u.menu.psz_subtitle, char *, (ssize_t)(strlen(p_object->u.menu.psz_subtitle) + 1));
         STORE_MEMBER(&p_serialized->u.menu.psz_bottom, char *, (ssize_t)(strlen(p_object->u.menu.psz_bottom) + 1));
         /* pointer alignment */
         int i = ((*pi_size + 7) / 8) * 8 - *pi_size;
         *pi_size += i;
         p_answer += i;
         char **pp_tmp = (char **)p_answer;
         STORE_MEMBER(&p_serialized->u.menu.ppsz_choices, char **, (ssize_t)(p_object->u.menu.i_choices * sizeof(char *)));
         for (i = 0; i < p_object->u.menu.i_choices; i++) {
            STORE_MEMBER(&pp_tmp[i], char *, (ssize_t)(strlen(p_object->u.menu.ppsz_choices[i]) + 1));
         }
         break;
      }
      default:
         break;
   }
   return 0;
}

#define CHECK_MEMBER(pp_member) \
      if ((ptrdiff_t)*pp_member >= i_size) \
         return -1; \
      for (int i = 0; ((char *)p_object + (ptrdiff_t)*pp_member)[i] != '\0'; i++) {\
         if ((ptrdiff_t)*pp_member + i >= i_size) \
            return -1; \
      } \
      *pp_member += (ptrdiff_t)p_object;

int cLdvben50221::en50221_UnserializeMMIObject( en50221_mmi_object_t *p_object, ssize_t i_size)
{
   switch (p_object->i_object_type) {
      case EN50221_MMI_ENQ:
         CHECK_MEMBER(&p_object->u.enq.psz_text);
         break;
      case EN50221_MMI_ANSW:
         CHECK_MEMBER(&p_object->u.answ.psz_answ);
         break;
      case EN50221_MMI_MENU:
      case EN50221_MMI_LIST:
         CHECK_MEMBER(&p_object->u.menu.psz_title);
         CHECK_MEMBER(&p_object->u.menu.psz_subtitle);
         CHECK_MEMBER(&p_object->u.menu.psz_bottom);
         if ((ssize_t)((ptrdiff_t)p_object->u.menu.ppsz_choices + p_object->u.menu.i_choices * sizeof(char *)) >= i_size)
            return -1;
         p_object->u.menu.ppsz_choices = (char **)((char *)p_object + (ptrdiff_t)p_object->u.menu.ppsz_choices);
         for (int j = 0; j < p_object->u.menu.i_choices; j++) {
            CHECK_MEMBER(&p_object->u.menu.ppsz_choices[j]);
         }
         break;
      default:
         break;
   }
   return 0;
}

uint8_t *cLdvben50221::GetLength(uint8_t *p_data, int *pi_length)
{
   *pi_length = *p_data++;
   if ((*pi_length & SIZE_INDICATOR) != 0) {
      int l = *pi_length & ~SIZE_INDICATOR;
      *pi_length = 0;
      for (int i = 0; i < l; i++)
         *pi_length = (*pi_length << 8) | *p_data++;
   }
   return p_data;
}

uint8_t *cLdvben50221::SetLength(uint8_t *p_data, int i_length)
{
   uint8_t *p = p_data;

   if (i_length < 128) {
      *p++ = i_length;
   } else
   if (i_length < 256) {
      *p++ = SIZE_INDICATOR | 0x1;
      *p++ = i_length;
   } else
   if (i_length < 65536) {
      *p++ = SIZE_INDICATOR | 0x2;
      *p++ = i_length >> 8;
      *p++ = i_length & 0xff;
   } else
   if (i_length < 16777216) {
      *p++ = SIZE_INDICATOR | 0x3;
      *p++ = i_length >> 16;
      *p++ = (i_length >> 8) & 0xff;
      *p++ = i_length & 0xff;
   } else {
      *p++ = SIZE_INDICATOR | 0x4;
      *p++ = i_length >> 24;
      *p++ = (i_length >> 16) & 0xff;
      *p++ = (i_length >> 8) & 0xff;
      *p++ = i_length & 0xff;
   }
   return p;
}

void cLdvben50221::Dump(bool b_outgoing, uint8_t *p_data, int i_size)
{
#ifdef DEBUG_TPDU
#define MAX_DUMP 256
   cLbugf(cL::dbg_dvb, "%s ", b_outgoing ? "-->" : "<--");
   for (int i = 0; i < i_size && i < MAX_DUMP; i++)
      cLbugf(cL::dbg_dvb, "%02X ", p_data[i]);
   cLbugf(cL::dbg_dvb, "%s\n", i_size >= MAX_DUMP ? "..." : "");
#endif
}

int cLdvben50221::TPDUWrite(uint8_t i_slot)
{
   ci_slot_t *p_slot = &this->p_slots[i_slot];
   en50221_msg_t *p_send = p_slot->p_send;

   if (p_slot->b_expect_answer) {
      cLbugf(cL::dbg_dvb, "en50221: writing while expecting an answer on slot %u\n", i_slot);
   }
   if (p_send == (en50221_msg_t *) 0) {
      cLbugf(cL::dbg_dvb, "en50221: no data to write on slot %u !\n", i_slot);
      return -1;
   }
   p_slot->p_send = p_send->p_next;
   if (p_slot->p_send == (en50221_msg_t *) 0)
      p_slot->pp_send_last = &p_slot->p_send;

   this->Dump(true, p_send->p_data, p_send->i_size);

   if (write(this->i_ca_handle, p_send->p_data, p_send->i_size) != p_send->i_size) {
      cLbug(cL::dbg_dvb, "en50221: cannot write to CAM device\n");
      ::free(p_send->p_data);
      ::free(p_send);
      return -1;
   }

   ::free( p_send->p_data);
   ::free( p_send);
   p_slot->b_expect_answer = true;

   return 0;
}

int cLdvben50221::TPDUSend(uint8_t i_slot, uint8_t i_tag, const uint8_t *p_content, int i_length)
{
   ci_slot_t *p_slot = &this->p_slots[i_slot];
   uint8_t i_tcid = i_slot + 1;
   en50221_msg_t *p_send = cLmalloc(en50221_msg_t, 1);
   uint8_t *p_data = cLmalloc(uint8_t, MAX_TPDU_SIZE);
   int i_size;

   i_size = 0;
   p_data[0] = i_slot;
   p_data[1] = i_tcid;
   p_data[2] = i_tag;

   switch (i_tag) {
      case T_RCV:
      case T_CREATE_TC:
      case T_CTC_REPLY:
      case T_DELETE_TC:
      case T_DTC_REPLY:
      case T_REQUEST_TC:
         p_data[3] = 1; /* length */
         p_data[4] = i_tcid;
         i_size = 5;
         break;
      case T_NEW_TC:
      case T_TC_ERROR:
         p_data[3] = 2; /* length */
         p_data[4] = i_tcid;
         p_data[5] = p_content[0];
         i_size = 6;
         break;
      case T_DATA_LAST:
      case T_DATA_MORE: {
         /* i_length <= MAX_TPDU_DATA */
         uint8_t *p = p_data + 3;
         p = this->SetLength(p, i_length + 1);
         *p++ = i_tcid;
         if (i_length)
            memcpy(p, p_content, i_length);
         i_size = i_length + (p - p_data);
         break;
      }
      default:
         break;
   }

   p_send->p_data = p_data;
   p_send->i_size = i_size;
   p_send->p_next = (en50221_msg_t *) 0;

   *p_slot->pp_send_last = p_send;
   p_slot->pp_send_last = &p_send->p_next;

   if (!p_slot->b_expect_answer)
      return this->TPDUWrite(i_slot);

   return 0;
}

int cLdvben50221::TPDURecv()
{
   ci_slot_t *p_slot;
   uint8_t i_tag, i_slot;
   uint8_t p_data[MAX_TPDU_SIZE];
   int i_size;
   bool b_last = false;

   do {
      i_size = read(this->i_ca_handle, p_data, MAX_TPDU_SIZE);
   } while ( i_size < 0 && errno == EINTR);

   if (i_size < 5) {
      cLbugf(cL::dbg_dvb, "en50221: cannot read from CAM device (%d)\n", i_size);
      return -1;
   }

   this->Dump(false, p_data, i_size);

   i_slot = p_data[1] - 1;
   i_tag = p_data[2];

   if (i_slot >= this->i_nb_slots) {
      cLbugf(cL::dbg_dvb, "en50221: TPDU is from an unknown slot %u\n", i_slot);
      return -1;
   }
   p_slot = &this->p_slots[i_slot];

   p_slot->b_has_data = !!(p_data[i_size - 4] == T_SB && p_data[i_size - 3] == 2 && (p_data[i_size - 1] & DATA_INDICATOR));
   p_slot->b_expect_answer = false;

   switch (i_tag) {
      case T_CTC_REPLY:
         p_slot->b_active = true;
         cLev_timer_stop(this->event_loop, &p_slot->init_watcher);
         cLbugf(cL::dbg_dvb, "CI slot %d is active\n", i_slot);
         break;
      case T_SB:
         break;
      case T_DATA_LAST:
         b_last = true;
         /* intended pass-through */
         //no break
      case T_DATA_MORE: {
         en50221_msg_t *p_recv;
         int i_session_size;
         uint8_t *p_session = GetLength(&p_data[3], &i_session_size);

         if (i_session_size <= 1)
            break;

         p_session++;
         i_session_size--;

         if (p_slot->p_recv == (en50221_msg_t *) 0) {
            p_slot->p_recv = cLmalloc(en50221_msg_t, 1);
            p_slot->p_recv->p_data = (uint8_t *) 0;
            p_slot->p_recv->i_size = 0;
         }

         p_recv = p_slot->p_recv;
         p_recv->p_data = cLrealloc(uint8_t, p_recv->p_data, p_recv->i_size + i_session_size);
         memcpy(&p_recv->p_data[ p_recv->i_size ], p_session, i_session_size);
         p_recv->i_size += i_session_size;

         if (b_last) {
            this->SPDUHandle(i_slot, p_recv->p_data, p_recv->i_size);
            ::free(p_recv->p_data);
            ::free(p_recv);
            p_slot->p_recv = (en50221_msg_t *) 0;
         }
         break;
      }

      default:
         cLbugf(cL::dbg_dvb, "en50221: unhandled R_TPDU tag %u slot %u\n", i_tag, i_slot);
         break;
   }

   if (!p_slot->b_expect_answer && p_slot->p_send != (en50221_msg_t *) 0)
      this->TPDUWrite(i_slot);

   if (!p_slot->b_expect_answer && p_slot->i_pending_session_id != 0)
      this->SessionOpenCb(i_slot);

   if (!p_slot->b_expect_answer && p_slot->b_has_data)
      this->TPDUSend(i_slot, T_RCV, (const uint8_t *) 0, 0);

   return 0;
}

int cLdvben50221::ResourceIdToInt(uint8_t *p_data)
{
   return ((int)p_data[0] << 24) | ((int)p_data[1] << 16) | ((int)p_data[2] << 8) | p_data[3];
}

int cLdvben50221::SPDUSend(int i_session_id, uint8_t *p_data, int i_size)
{
   uint8_t *p_spdu = cLmalloc(uint8_t, i_size + 4);
   uint8_t *p = p_spdu;
   uint8_t i_slot = this->p_sessions[i_session_id - 1].i_slot;

   *p++ = ST_SESSION_NUMBER;
   *p++ = 0x02;
   *p++ = (i_session_id >> 8);
   *p++ = i_session_id & 0xff;

   memcpy(p, p_data, i_size);

   i_size += 4;
   p = p_spdu;

   while (i_size > 0) {
      if (i_size > MAX_TPDU_DATA) {
         if (this->TPDUSend(i_slot, T_DATA_MORE, p, MAX_TPDU_DATA ) != 0) {
            cLbugf(cL::dbg_dvb, "couldn't send TPDU on session %d\n", i_session_id);
            ::free(p_spdu);
            return -1;
         }
         p += MAX_TPDU_DATA;
         i_size -= MAX_TPDU_DATA;
      } else {
         if (this->TPDUSend(i_slot, T_DATA_LAST, p, i_size) != 0 ) {
            cLbugf(cL::dbg_dvb, "couldn't send TPDU on session %d\n", i_session_id);
            ::free(p_spdu);
            return -1;
         }
         i_size = 0;
      }
   }

   ::free(p_spdu);
   return 0;
}

void cLdvben50221::SessionOpenCb(uint8_t i_slot)
{
   ci_slot_t *p_slot = &this->p_slots[i_slot];
   int i_session_id = p_slot->i_pending_session_id;
   int i_resource_id = this->p_sessions[i_session_id - 1].i_resource_id;

   p_slot->i_pending_session_id = 0;

   switch (i_resource_id) {
      case RI_RESOURCE_MANAGER:
         this->ResourceManagerOpen(i_session_id);
         break;
      case RI_APPLICATION_INFORMATION:
         this->ApplicationInformationOpen(i_session_id);
         break;
      case RI_CONDITIONAL_ACCESS_SUPPORT:
         this->ConditionalAccessOpen(i_session_id);
         break;
      case RI_DATE_TIME:
         this->DateTimeOpen(i_session_id);
         break;
      case RI_MMI:
         this->MMIOpen(i_session_id);
         break;
      case RI_HOST_CONTROL:
      default:
         cLbugf(cL::dbg_dvb, "unknown resource id (0x%x)\n", i_resource_id);
         this->p_sessions[i_session_id - 1].i_resource_id = 0;
   }
}

void cLdvben50221::SessionOpen(uint8_t i_slot, uint8_t *p_spdu, int i_size)
{
   ci_slot_t *p_slot = &this->p_slots[i_slot];
   int i_session_id;
   int i_resource_id = this->ResourceIdToInt(&p_spdu[2]);
   uint8_t p_response[16];
   int i_status = SS_NOT_ALLOCATED;

   for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (!this->p_sessions[i_session_id - 1].i_resource_id)
         break;
   }
   if (i_session_id > MAX_SESSIONS) {
      cLbug(cL::dbg_dvb, "too many sessions !\n");
      return;
   }
   this->p_sessions[i_session_id - 1].i_slot = i_slot;
   this->p_sessions[i_session_id - 1].i_resource_id = i_resource_id;
   this->p_sessions[i_session_id - 1].pf_close = (fssn_common) 0;
   this->p_sessions[i_session_id - 1].pf_manage = (fssn_common) 0;

   if (i_resource_id == RI_RESOURCE_MANAGER
         || i_resource_id == RI_APPLICATION_INFORMATION
         || i_resource_id == RI_CONDITIONAL_ACCESS_SUPPORT
         || i_resource_id == RI_DATE_TIME
         || i_resource_id == RI_MMI) {
      i_status = SS_OK;
   }

   p_response[0] = ST_OPEN_SESSION_RESPONSE;
   p_response[1] = 0x7;
   p_response[2] = i_status;
   p_response[3] = p_spdu[2];
   p_response[4] = p_spdu[3];
   p_response[5] = p_spdu[4];
   p_response[6] = p_spdu[5];
   p_response[7] = i_session_id >> 8;
   p_response[8] = i_session_id & 0xff;

   if (this->TPDUSend(i_slot, T_DATA_LAST, p_response, 9) != 0) {
      cLbugf(cL::dbg_dvb, "SessionOpen: couldn't send TPDU on slot %d\n", i_slot);
      return;
   }

   if (p_slot->i_pending_session_id != 0)
      cLbugf(cL::dbg_dvb, "overwriting pending session %d\n", p_slot->i_pending_session_id);
   p_slot->i_pending_session_id = i_session_id;
}

#if 0
/* unused code for the moment - commented out to keep gcc happy */
 void cLdvben50221::SessionCreate(int i_slot, int i_resource_id)
{
   uint8_t p_response[16];
   //uint8_t i_tag;
   int i_session_id;

   for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (!this->p_sessions[i_session_id - 1].i_resource_id)
         break;
   }
   if (i_session_id > MAX_SESSIONS) {
      cLbug(cL::dbg_dvb, "too many sessions !\n");
      return;
   }
   this->p_sessions[i_session_id - 1].i_slot = i_slot;
   this->p_sessions[i_session_id - 1].i_resource_id = i_resource_id;
   this->p_sessions[i_session_id - 1].pf_close = (fssn_common) 0;
   this->p_sessions[i_session_id - 1].pf_manage = (fssn_common) 0;
   this->p_sessions[i_session_id - 1].p_sys = (void *) 0;

   p_response[0] = ST_CREATE_SESSION;
   p_response[1] = 0x6;
   p_response[2] = i_resource_id >> 24;
   p_response[3] = (i_resource_id >> 16) & 0xff;
   p_response[4] = (i_resource_id >> 8) & 0xff;
   p_response[5] = i_resource_id & 0xff;
   p_response[6] = i_session_id >> 8;
   p_response[7] = i_session_id & 0xff;

   if (this->TPDUSend(i_slot, T_DATA_LAST, p_response, 4) != 0) {
      cLbugf(cL::dbg_dvb, "SessionCreate: couldn't send TPDU on slot %d\n", i_slot);
      return;
   }
}
#endif

void cLdvben50221::SessionCreateResponse(uint8_t i_slot, uint8_t *p_spdu, int i_size)
{
   int i_status = p_spdu[2];
   int i_resource_id = this->ResourceIdToInt( &p_spdu[3] );
   int i_session_id = ((int)p_spdu[7] << 8) | p_spdu[8];

   if (i_status != SS_OK) {
      cLbugf(cL::dbg_dvb, "SessionCreateResponse: failed to open session %d resource=0x%x status=0x%x\n", i_session_id, i_resource_id, i_status );
      this->p_sessions[i_session_id - 1].i_resource_id = 0;
      return;
   }

   switch (i_resource_id) {
      case RI_RESOURCE_MANAGER:
         this->ResourceManagerOpen(i_session_id);
         break;
      case RI_APPLICATION_INFORMATION:
         this->ApplicationInformationOpen(i_session_id);
         break;
      case RI_CONDITIONAL_ACCESS_SUPPORT:
         this->ConditionalAccessOpen(i_session_id);
         break;
      case RI_DATE_TIME:
         this->DateTimeOpen(i_session_id);
         break;
      case RI_MMI:
         this->MMIOpen(i_session_id);
         break;
      case RI_HOST_CONTROL:
      default:
         cLbugf(cL::dbg_dvb, "unknown resource id (0x%x)\n", i_resource_id);
         this->p_sessions[i_session_id - 1].i_resource_id = 0;
   }
}

void cLdvben50221::SessionSendClose(int i_session_id)
{
   uint8_t p_response[16];
   uint8_t i_slot = this->p_sessions[i_session_id - 1].i_slot;

   p_response[0] = ST_CLOSE_SESSION_REQUEST;
   p_response[1] = 0x2;
   p_response[2] = i_session_id >> 8;
   p_response[3] = i_session_id & 0xff;

   if (this->TPDUSend(i_slot, T_DATA_LAST, p_response, 4) != 0) {
      cLbugf(cL::dbg_dvb, "SessionSendClose: couldn't send TPDU on slot %d\n", i_slot);
      return;
   }
}

void cLdvben50221::SessionClose(int i_session_id)
{
   uint8_t p_response[16];
   uint8_t i_slot = this->p_sessions[i_session_id - 1].i_slot;

   if (this->p_sessions[i_session_id - 1].pf_close != (fssn_common) 0)
      this->p_sessions[i_session_id - 1].pf_close(i_session_id, this);
   this->p_sessions[i_session_id - 1].i_resource_id = 0;

   p_response[0] = ST_CLOSE_SESSION_RESPONSE;
   p_response[1] = 0x3;
   p_response[2] = SS_OK;
   p_response[3] = i_session_id >> 8;
   p_response[4] = i_session_id & 0xff;

   if (this->TPDUSend(i_slot, T_DATA_LAST, p_response, 5) != 0) {
      cLbugf(cL::dbg_dvb, "SessionClose: couldn't send TPDU on slot %d\n", i_slot);
      return;
   }
}

void cLdvben50221::SPDUHandle(uint8_t i_slot, uint8_t *p_spdu, int i_size)
{
   int i_session_id;

   switch (p_spdu[0]) {
      case ST_SESSION_NUMBER:
         if (i_size <= 4)
            return;
         i_session_id = (p_spdu[2] << 8) | p_spdu[3];
         if (i_session_id <= MAX_SESSIONS && this->p_sessions[i_session_id - 1].pf_handle != (fssn_handle) 0)
            this->p_sessions[i_session_id - 1].pf_handle(i_session_id, p_spdu + 4, i_size - 4, this);
         break;
      case ST_OPEN_SESSION_REQUEST:
         if (i_size != 6 || p_spdu[1] != 0x4)
            return;
         this->SessionOpen(i_slot, p_spdu, i_size);
         break;
      case ST_CREATE_SESSION_RESPONSE:
         if (i_size != 9 || p_spdu[1] != 0x7)
            return;
         this->SessionCreateResponse(i_slot, p_spdu, i_size);
         break;
      case ST_CLOSE_SESSION_REQUEST:
         if (i_size != 4 || p_spdu[1] != 0x2)
            return;
         i_session_id = ((int)p_spdu[2] << 8) | p_spdu[3];
         this->SessionClose(i_session_id);
         break;
      case ST_CLOSE_SESSION_RESPONSE:
         if (i_size != 5 || p_spdu[1] != 0x3)
            return;
         i_session_id = ((int)p_spdu[3] << 8) | p_spdu[4];
         if (p_spdu[2]) {
            cLbugf(cL::dbg_dvb, "closing a session which is not allocated (%d)\n", i_session_id );
         } else {
            if (this->p_sessions[i_session_id - 1].pf_close != (fssn_common) 0)
               this->p_sessions[i_session_id - 1].pf_close(i_session_id, this);
            this->p_sessions[i_session_id - 1].i_resource_id = 0;
         }
         break;
      default:
         cLbugf(cL::dbg_dvb, "unexpected tag in SPDUHandle (%x)\n", p_spdu[0]);
         break;
   }
}

int cLdvben50221::APDUGetTag(const uint8_t *p_apdu, int i_size)
{
   if (i_size >= 3) {
      int t = 0;
      for (int i = 0; i < 3; i++)
         t = (t << 8) | *p_apdu++;
      return t;
   }
   return AOT_NONE;
}

uint8_t *cLdvben50221::APDUGetLength(uint8_t *p_apdu, int *pi_size)
{
   return cLdvben50221::GetLength(&p_apdu[3], pi_size);
}

int cLdvben50221::APDUSend(int i_session_id, int i_tag, uint8_t *p_data, int i_size)
{
   uint8_t *p_apdu = cLmalloc(uint8_t, i_size + 12);
   uint8_t *p = p_apdu;
   ca_msg_t ca_msg;
   int i_ret;

   *p++ = (i_tag >> 16);
   *p++ = (i_tag >> 8) & 0xff;
   *p++ = i_tag & 0xff;
   p = this->SetLength(p, i_size);
   if (i_size)
      memcpy( p, p_data, i_size );
   if (this->i_ca_type == CA_CI_LINK) {
      i_ret = this->SPDUSend(i_session_id, p_apdu, i_size + p - p_apdu);
   } else {
      if (i_size + p - p_apdu > 256) {
         cLbug(cL::dbg_dvb, "CAM: apdu overflow\n");
         i_ret = -1;
      } else {
         ca_msg.length = i_size + p - p_apdu;
         if (i_size == 0)
            ca_msg.length=3;
         memcpy(ca_msg.msg, p_apdu, i_size + p - p_apdu);
         i_ret = ioctl(this->i_ca_handle, CA_SEND_MSG, &ca_msg);
         if (i_ret < 0) {
            cLbug(cL::dbg_dvb, "Error sending to CAM \n");
            i_ret = -1;
         }
      }
   }
   ::free(p_apdu);
   return i_ret;
}

/*
 * Resource Manager
 */
void cLdvben50221::ResourceManagerHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj)
{
   int i_tag = pobj->APDUGetTag(p_apdu, i_size);

   switch (i_tag) {
      case AOT_PROFILE_ENQ: {
         int resources[] = { htonl(RI_RESOURCE_MANAGER),
               htonl(RI_APPLICATION_INFORMATION),
               htonl(RI_CONDITIONAL_ACCESS_SUPPORT),
               htonl(RI_DATE_TIME),
               htonl(RI_MMI)
         };
         pobj->APDUSend(i_session_id, AOT_PROFILE, (uint8_t*)resources, (int)sizeof(resources));
         break;
      }
      case AOT_PROFILE:
         pobj->APDUSend(i_session_id, AOT_PROFILE_CHANGE, (uint8_t *) 0, 0);
         break;
      default:
         cLbugf(cL::dbg_dvb, "unexpected tag in ResourceManagerHandle (0x%x)\n", i_tag);
   }
}

void cLdvben50221::ResourceManagerOpen(int i_session_id)
{
   cLbugf(cL::dbg_dvb, "opening ResourceManager session (%d)\n", i_session_id);
   this->p_sessions[i_session_id - 1].pf_handle = cLdvben50221::ResourceManagerHandle;
   APDUSend(i_session_id, AOT_PROFILE_ENQ, (uint8_t *) 0, 0 );
}

/*
 * Application Information
 */
void cLdvben50221::ApplicationInformationEnterMenu(int i_session_id)
{
   int i_slot = this->p_sessions[i_session_id - 1].i_slot;

   cLbugf(cL::dbg_dvb, "entering MMI menus on session %d\n", i_session_id );
   this->APDUSend(i_session_id, AOT_ENTER_MENU, (uint8_t *) 0, 0 );
   this->p_slots[i_slot].b_mmi_expected = true;
}

void cLdvben50221::ApplicationInformationHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj)
{
   int i_tag = pobj->APDUGetTag(p_apdu, i_size);

   switch (i_tag) {
      case AOT_APPLICATION_INFO: {
         int i_type, i_manufacturer, i_code;
         int l = 0;
         uint8_t *d = pobj->APDUGetLength( p_apdu, &l );

         if (l < 4)
            break;

         i_type = *d++;
         i_manufacturer = ((int)d[0] << 8) | d[1];
         d += 2;
         i_code = ((int)d[0] << 8) | d[1];
         d += 2;
         d = pobj->GetLength(d, &l);
         {
            char *psz_name = cLmalloc(char, l + 1);
            memcpy( psz_name, d, l );
            psz_name[l] = '\0';
            cLbugf(cL::dbg_dvb, "CAM: %s, %02X, %04X, %04X\n", psz_name, i_type, i_manufacturer, i_code);
            ::free(psz_name);
         }
         break;
      }
      default:
         cLbugf(cL::dbg_dvb, "unexpected tag in ApplicationInformationHandle (0x%x)\n", i_tag);
   }
}

/*****************************************************************************
 * ApplicationInformationOpen
 *****************************************************************************/
void cLdvben50221::ApplicationInformationOpen(int i_session_id)
{
   cLbugf(cL::dbg_dvb, "opening ApplicationInformation session (%d)\n", i_session_id);
   this->p_sessions[i_session_id - 1].pf_handle = cLdvben50221::ApplicationInformationHandle;
   this->APDUSend(i_session_id, AOT_APPLICATION_INFO_ENQ, (uint8_t *) 0, 0);
}

/*
 * Conditional Access
 */
bool cLdvben50221::CheckSystemID(system_ids_t *p_ids, uint16_t i_id)
{
   if (p_ids == (system_ids_t *) 0)
      return false;
   if (p_ids->b_high_level)
      return true;

   for (int i = 0; i < p_ids->i_nb_system_ids; i++) {
      if (p_ids->pi_system_ids[i] == i_id)
         return true;
   }

   return false;
}

/*
 * CAPMTBuild
 */
bool cLdvben50221::HasCADescriptors(system_ids_t *p_ids, uint8_t *p_descs)
{
   const uint8_t *p_desc;
   uint16_t j = 0;

   while ((p_desc = descs_get_desc(p_descs, j)) != (uint8_t *) 0) {
      uint8_t i_tag = desc_get_tag(p_desc);
      j++;

      if (i_tag == 0x9 && desc09_validate( p_desc) && cLdvben50221::CheckSystemID( p_ids, desc09_get_sysid(p_desc)))
         return true;
   }

   return false;
}

void cLdvben50221::CopyCADescriptors(system_ids_t *p_ids, uint8_t i_cmd, uint8_t *p_infos, uint8_t *p_descs)
{
   const uint8_t *p_desc;
   uint16_t j = 0, k = 0;

   capmti_init(p_infos);
   capmti_set_length(p_infos, 0xfff);
   capmti_set_cmd(p_infos, i_cmd);

   while ((p_desc = descs_get_desc(p_descs, j)) != (uint8_t *) 0) {
      uint8_t i_tag = desc_get_tag(p_desc);
      j++;

      if (i_tag == 0x9 && desc09_validate(p_desc) && cLdvben50221::CheckSystemID(p_ids, desc09_get_sysid(p_desc))) {
         uint8_t *p_info = capmti_get_info(p_infos, k);
         k++;
         memcpy(p_info, p_desc, DESC_HEADER_SIZE + desc_get_length(p_desc));
      }
   }

   if (k) {
      uint8_t *p_info = capmti_get_info(p_infos, k);
      capmti_set_length(p_infos, p_info - p_infos - DESCS_HEADER_SIZE);
   } else {
      capmti_set_length(p_infos, 0);
   }
}

uint8_t *cLdvben50221::CAPMTBuild(int i_session_id, uint8_t *p_pmt, uint8_t i_list_mgt, uint8_t i_cmd, int *pi_capmt_size)
{
   system_ids_t *p_ids = (system_ids_t *)this->p_sessions[i_session_id - 1].p_sys;
   uint8_t *p_es;
   uint8_t *p_capmt, *p_capmt_n;
   uint16_t j, k;
   bool b_has_ca = this->HasCADescriptors(p_ids, pmt_get_descs(p_pmt));
   bool b_has_es = false;

   j = 0;
   while ((p_es = pmt_get_es( p_pmt, j )) != (uint8_t *) 0) {
      uint16_t i_pid = pmtn_get_pid(p_es);
      j++;
      if (this->dev_PIDIsSelected(i_pid) == 1) {
         b_has_es = true;
         b_has_ca = b_has_ca || this->HasCADescriptors(p_ids, pmtn_get_descs(p_es));
      }
   }

   if (!b_has_es) {
      *pi_capmt_size = 0;
      return (uint8_t *) 0;
   }

   if (!b_has_ca) {
      cLbugf(cL::dbg_dvb, "no compatible scrambling system for SID %d on session %d\n", pmt_get_program(p_pmt), i_session_id);
      *pi_capmt_size = 0;
      return (uint8_t *) 0;
   }

   p_capmt = capmt_allocate();
   capmt_init(p_capmt);
   capmt_set_listmanagement(p_capmt, i_list_mgt);
   capmt_set_program(p_capmt, pmt_get_program(p_pmt));
   capmt_set_version(p_capmt, psi_get_version(p_pmt));

   this->CopyCADescriptors(p_ids, i_cmd, capmt_get_infos(p_capmt), pmt_get_descs(p_pmt));

   j = 0; k = 0;
   while ((p_es = pmt_get_es(p_pmt, j)) != (uint8_t *) 0) {
      uint16_t i_pid = pmtn_get_pid(p_es);
      j++;
      if (this->dev_PIDIsSelected(i_pid) == 0)
         continue;
      p_capmt_n = capmt_get_es(p_capmt, k);
      k++;
      capmtn_init(p_capmt_n);
      capmtn_set_streamtype(p_capmt_n, pmtn_get_streamtype(p_es));
      capmtn_set_pid( p_capmt_n, pmtn_get_pid(p_es));
      this->CopyCADescriptors(p_ids, i_cmd, capmtn_get_infos( p_capmt_n), pmtn_get_descs(p_es));
   }

   p_capmt_n = capmt_get_es(p_capmt, k);
   *pi_capmt_size = p_capmt_n - p_capmt;

   return p_capmt;
}

void cLdvben50221::CAPMTFirst(int i_session_id, uint8_t *p_pmt)
{
   uint8_t *p_capmt;
   int i_capmt_size;

   cLbugf(cL::dbg_dvb, "adding first CAPMT for SID %d on session %d\n", pmt_get_program(p_pmt), i_session_id);

   p_capmt = this->CAPMTBuild(i_session_id, p_pmt, 0x3 /* only */, 0x1 /* ok_descrambling */, &i_capmt_size);

   if (i_capmt_size) {
      this->APDUSend(i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size);
      ::free(p_capmt);
   }
}

void cLdvben50221::CAPMTAdd(int i_session_id, uint8_t *p_pmt)
{
   system_ids_t *p_ids = (system_ids_t *)this->p_sessions[i_session_id - 1].p_sys;
   uint8_t *p_capmt;
   int i_capmt_size;

   p_ids->i_selected_programs++;
   if (p_ids->i_selected_programs == 1) {
      this->CAPMTFirst(i_session_id, p_pmt);
      return;
   }

   cLbugf(cL::dbg_dvb, "adding CAPMT for SID %d on session %d\n", pmt_get_program(p_pmt), i_session_id);

   p_capmt = this->CAPMTBuild(i_session_id, p_pmt, 0x4 /* add */, 0x1 /* ok_descrambling */, &i_capmt_size);

   if (i_capmt_size) {
      this->APDUSend(i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size);
      ::free(p_capmt);
   }
}

void cLdvben50221::CAPMTUpdate(int i_session_id, uint8_t *p_pmt)
{
   uint8_t *p_capmt;
   int i_capmt_size;

   cLbugf(cL::dbg_dvb, "updating CAPMT for SID %d on session %d\n", pmt_get_program(p_pmt), i_session_id);

   p_capmt = CAPMTBuild(i_session_id, p_pmt, 0x5 /* update */, 0x1 /* ok_descrambling */, &i_capmt_size);

   if (i_capmt_size) {
      this->APDUSend(i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size);
      ::free(p_capmt);
   }
}

void cLdvben50221::CAPMTDelete(int i_session_id, uint8_t *p_pmt)
{
   system_ids_t *p_ids = (system_ids_t *)this->p_sessions[i_session_id - 1].p_sys;
   uint8_t *p_capmt;
   int i_capmt_size;

   p_ids->i_selected_programs--;
   cLbugf(cL::dbg_dvb, "deleting CAPMT for SID %d on session %d\n", pmt_get_program(p_pmt), i_session_id);

   p_capmt = this->CAPMTBuild(i_session_id, p_pmt, 0x5 /* update */, 0x4 /* not selected */, &i_capmt_size);

   if (i_capmt_size) {
      this->APDUSend(i_session_id, AOT_CA_PMT, p_capmt, i_capmt_size);
      ::free(p_capmt);
   }
}

void cLdvben50221::ConditionalAccessHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj)
{
   system_ids_t *p_ids = (system_ids_t *)pobj->p_sessions[i_session_id - 1].p_sys;
   int i_tag = pobj->APDUGetTag(p_apdu, i_size);

   switch (i_tag) {
      case AOT_CA_INFO: {
         int l = 0;
         uint8_t *d = pobj->APDUGetLength(p_apdu, &l);
         cLbug(cL::dbg_dvb, "CA system IDs supported by the application :\n");

         if (p_ids->i_nb_system_ids)
            ::free( p_ids->pi_system_ids );
         p_ids->i_nb_system_ids = l / 2;
         p_ids->pi_system_ids = cLmalloc(uint16_t, p_ids->i_nb_system_ids);

         for (int i = 0; i < p_ids->i_nb_system_ids; i++) {
            p_ids->pi_system_ids[i] = ((uint16_t)d[0] << 8) | d[1];
            d += 2;
            cLbugf(cL::dbg_dvb, "- 0x%x\n", p_ids->pi_system_ids[i]);
         }
         pobj->dev_ResendCAPMTs();
         break;
      }
      case AOT_CA_UPDATE:
         /* http://www.cablelabs.com/specifications/OC-SP-HOSTPOD-IF-I08-011221.pdf */
      case AOT_CA_PMT_REPLY:
         /* We do not care */
         break;
      default:
         cLbugf(cL::dbg_dvb, "unexpected tag in ConditionalAccessHandle (0x%x)\n", i_tag );
   }
}

void cLdvben50221::ConditionalAccessClose(int i_session_id, cLdvben50221 *pobj)
{
   cLbugf(cL::dbg_dvb, "closing ConditionalAccess session (%d)\n", i_session_id);
   ::free(pobj->p_sessions[i_session_id - 1].p_sys);
}

void cLdvben50221::ConditionalAccessOpen(int i_session_id)
{
   cLbugf(cL::dbg_dvb, "opening ConditionalAccess session (%d)\n", i_session_id);
   this->p_sessions[i_session_id - 1].pf_handle = cLdvben50221::ConditionalAccessHandle;
   this->p_sessions[i_session_id - 1].pf_close = cLdvben50221::ConditionalAccessClose;
   this->p_sessions[i_session_id - 1].p_sys = (void *)cLmalloc(system_ids_t, 1);
   memset( this->p_sessions[i_session_id - 1].p_sys, 0, sizeof(system_ids_t));
   this->APDUSend(i_session_id, AOT_CA_INFO_ENQ, (uint8_t *) 0, 0);
}

/*
 * Date Time
 */

#define DEC2BCD(d) (((d / 10) << 4) + (d % 10))

void cLdvben50221::DateTimeSend(int i_session_id)
{
   date_time_t *p_date = (date_time_t *)this->p_sessions[i_session_id - 1].p_sys;

   time_t t = time((time_t *) 0);
   struct tm tm_gmt;
   struct tm tm_loc;

   if (gmtime_r(&t, &tm_gmt) && localtime_r(&t, &tm_loc)) {
      int Y = tm_gmt.tm_year;
      int M = tm_gmt.tm_mon + 1;
      int D = tm_gmt.tm_mday;
      int L = (M == 1 || M == 2) ? 1 : 0;
      int MJD = 14956 + D + (int)((Y - L) * 365.25) + (int)((M + 1 + L * 12) * 30.6001);
      uint8_t p_response[7];

      p_response[0] = htons(MJD) >> 8;
      p_response[1] = htons(MJD) & 0xff;
      p_response[2] = DEC2BCD(tm_gmt.tm_hour);
      p_response[3] = DEC2BCD(tm_gmt.tm_min);
      p_response[4] = DEC2BCD(tm_gmt.tm_sec);
      p_response[5] = htons(tm_loc.tm_gmtoff / 60) >> 8;
      p_response[6] = htons(tm_loc.tm_gmtoff / 60) & 0xff;

      this->APDUSend(i_session_id, AOT_DATE_TIME, p_response, 7);

      cLev_timer_again(this->event_loop, &p_date->watcher);
   }
}

void cLdvben50221::_DateTimeSend(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *)p;
   cLdvben50221 *pobj = (cLdvben50221 *)w->data;
   date_time_t *p_date = container_of(w, date_time_t, watcher);
   pobj->DateTimeSend(p_date->i_session_id);
}

void cLdvben50221::DateTimeHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj)
{
   date_time_t *p_date = (date_time_t *)pobj->p_sessions[i_session_id - 1].p_sys;

   int i_tag = pobj->APDUGetTag(p_apdu, i_size);

   switch (i_tag) {
      case AOT_DATE_TIME_ENQ: {
         int l;
         const uint8_t *d = pobj->APDUGetLength(p_apdu, &l);
         if (l > 0) {
            p_date->i_interval = *d;
            cLbugf(cL::dbg_dvb, "DateTimeHandle : interval set to %d\n", p_date->i_interval );
         } else {
            p_date->i_interval = 0;
         }

         cLev_timer_stop(pobj->event_loop, &p_date->watcher);
         cLev_timer_set(&p_date->watcher, p_date->i_interval, p_date->i_interval);
         pobj->DateTimeSend(i_session_id);
         break;
      }
      default:
         cLbugf(cL::dbg_dvb, "unexpected tag in DateTimeHandle (0x%x)\n", i_tag);
   }
}

void cLdvben50221::DateTimeClose(int i_session_id, cLdvben50221 *pobj)
{
   date_time_t *p_date = (date_time_t *)pobj->p_sessions[i_session_id - 1].p_sys;
   cLev_timer_stop(pobj->event_loop, &p_date->watcher);
   cLbugf(cL::dbg_dvb, "closing DateTime session (%d)\n", i_session_id );
   ::free(p_date);
}

void cLdvben50221::DateTimeOpen(int i_session_id)
{
   cLbugf(cL::dbg_dvb, "opening DateTime session (%d)\n", i_session_id);

   this->p_sessions[i_session_id - 1].pf_handle = cLdvben50221::DateTimeHandle;
   this->p_sessions[i_session_id - 1].pf_manage = (fssn_common) 0;
   this->p_sessions[i_session_id - 1].pf_close = cLdvben50221::DateTimeClose;
   this->p_sessions[i_session_id - 1].p_sys = (void *)cLmalloc(date_time_t, 1);
   memset(this->p_sessions[i_session_id - 1].p_sys, 0, sizeof(date_time_t));

   date_time_t *p_date = (date_time_t *)this->p_sessions[i_session_id - 1].p_sys;
   p_date->i_session_id = i_session_id;
   p_date->watcher.data = this;
   cLev_timer_init(&p_date->watcher, cLdvben50221::_DateTimeSend, 0, 0);
   this->DateTimeSend(i_session_id);
}

/*
 * MMI
 */
void cLdvben50221::en50221_MMIFree( en50221_mmi_object_t *p_object)
{
   switch (p_object->i_object_type) {
      case EN50221_MMI_ENQ:
         ::free(p_object->u.enq.psz_text);
         break;
      case EN50221_MMI_ANSW:
         if (p_object->u.answ.b_ok) {
            ::free(p_object->u.answ.psz_answ);
         }
         break;
      case EN50221_MMI_MENU:
      case EN50221_MMI_LIST:
         ::free(p_object->u.menu.psz_title);
         ::free(p_object->u.menu.psz_subtitle);
         ::free(p_object->u.menu.psz_bottom);
         for (int i = 0; i < p_object->u.menu.i_choices; i++) {
            ::free(p_object->u.menu.ppsz_choices[i]);
         }
         ::free(p_object->u.menu.ppsz_choices);
         break;
      default:
         break;
   }
}

void cLdvben50221::MMISendObject(int i_session_id, en50221_mmi_object_t *p_object)
{
   int i_slot = this->p_sessions[i_session_id - 1].i_slot;
   uint8_t *p_data;
   int i_size, i_tag;

   switch (p_object->i_object_type) {
      case EN50221_MMI_ANSW:
         i_tag = AOT_ANSW;
         i_size = 1 + strlen(p_object->u.answ.psz_answ);
         p_data = cLmalloc(uint8_t, i_size);
         p_data[0] = (p_object->u.answ.b_ok == true) ? 0x1 : 0x0;
         strncpy((char *)&p_data[1], p_object->u.answ.psz_answ, i_size - 1);
         break;
      case EN50221_MMI_MENU_ANSW:
         i_tag = AOT_MENU_ANSW;
         i_size = 1;
         p_data = cLmalloc(uint8_t, i_size);
         p_data[0] = p_object->u.menu_answ.i_choice;
         break;
      default:
         cLbugf(cL::dbg_dvb, "unknown MMI object %d\n", p_object->i_object_type);
         return;
   }

   this->APDUSend(i_session_id, i_tag, p_data, i_size);
   ::free(p_data);

   this->p_slots[i_slot].b_mmi_expected = true;
}

void cLdvben50221::MMISendClose(int i_session_id)
{
   int i_slot = this->p_sessions[i_session_id - 1].i_slot;
   this->APDUSend(i_session_id, AOT_CLOSE_MMI, (uint8_t *) 0, 0);
   this->p_slots[i_slot].b_mmi_expected = true;
}

void cLdvben50221::MMIDisplayReply(int i_session_id)
{
   uint8_t p_response[2];
   p_response[0] = DRI_MMI_MODE_ACK;
   p_response[1] = MM_HIGH_LEVEL;
   this->APDUSend(i_session_id, AOT_DISPLAY_REPLY, p_response, 2);
   cLbugf(cL::dbg_dvb, "sending DisplayReply on session (%d)\n", i_session_id);
}

char *cLdvben50221::MMIGetText(uint8_t **pp_apdu, int *pi_size)
{
   int i_tag = this->APDUGetTag(*pp_apdu, *pi_size);
   int l;
   uint8_t *d;

   if (i_tag != AOT_TEXT_LAST) {
      cLbugf(cL::dbg_dvb, "unexpected text tag: %06x\n", i_tag);
      *pi_size = 0;
      return strdup("");
   }

   d = this->APDUGetLength(*pp_apdu, &l);
   *pp_apdu += l + 4;
   *pi_size -= l + 4;

   return dvb_string_get(d, l, cLdvboutput::iconv_cb, this);
}

void cLdvben50221::MMIHandleEnq(int i_session_id, uint8_t *p_apdu, int i_size)
{
   mmi_t *p_mmi = (mmi_t *)this->p_sessions[i_session_id - 1].p_sys;
   int i_slot = this->p_sessions[i_session_id - 1].i_slot;
   int l;
   uint8_t *d = this->APDUGetLength(p_apdu, &l);

   this->en50221_MMIFree(&p_mmi->last_object);
   p_mmi->last_object.i_object_type = EN50221_MMI_ENQ;
   p_mmi->last_object.u.enq.b_blind = (*d & 0x1) ? true : false;
   d += 2; /* skip answer_text_length because it is not mandatory */
   l -= 2;
   p_mmi->last_object.u.enq.psz_text = cLmalloc(char, l + 1);
   strncpy(p_mmi->last_object.u.enq.psz_text, (char *)d, l);
   p_mmi->last_object.u.enq.psz_text[l] = '\0';

   cLbugf(cL::dbg_dvb, "MMI enq: %s%s\n", p_mmi->last_object.u.enq.psz_text, p_mmi->last_object.u.enq.b_blind == true ? " (blind)" : "");

   this->p_slots[i_slot].b_mmi_expected = false;
   this->p_slots[i_slot].b_mmi_undisplayed = true;
}

#define GET_FIELD(x) \
      if (l > 0) { \
         p_mmi->last_object.u.menu.psz_##x = this->MMIGetText(&d, &l); \
         cLbugf(cL::dbg_dvb, "MMI " STRINGIFY(x) ": %s\n", p_mmi->last_object.u.menu.psz_##x); \
      }

void cLdvben50221::MMIHandleMenu(int i_session_id, int i_tag, uint8_t *p_apdu, int i_size)
{
   mmi_t *p_mmi = (mmi_t *)this->p_sessions[i_session_id - 1].p_sys;
   int i_slot = this->p_sessions[i_session_id - 1].i_slot;
   int l;
   uint8_t *d = this->APDUGetLength( p_apdu, &l );

   this->en50221_MMIFree(&p_mmi->last_object);
   p_mmi->last_object.i_object_type = (i_tag == AOT_MENU_LAST) ? EN50221_MMI_MENU : EN50221_MMI_LIST;
   p_mmi->last_object.u.menu.i_choices = 0;
   p_mmi->last_object.u.menu.ppsz_choices = (char **) 0;

   if (l > 0) {
      l--; d++; /* choice_nb */
      GET_FIELD(title);
      GET_FIELD(subtitle);
      GET_FIELD(bottom);
      while (l > 0) {
         char *psz_text = this->MMIGetText(&d, &l);
         TAB_APPEND(p_mmi->last_object.u.menu.i_choices, p_mmi->last_object.u.menu.ppsz_choices, psz_text);
         cLbugf(cL::dbg_dvb, "MMI choice: %s\n", psz_text);
      }
   }

   this->p_slots[i_slot].b_mmi_expected = false;
   this->p_slots[i_slot].b_mmi_undisplayed = true;
}

#undef GET_FIELD

void cLdvben50221::MMIHandle(int i_session_id, uint8_t *p_apdu, int i_size, cLdvben50221 *pobj)
{
   int i_tag = pobj->APDUGetTag(p_apdu, i_size);

   switch (i_tag) {
      case AOT_DISPLAY_CONTROL: {
         int l;
         uint8_t *d = pobj->APDUGetLength(p_apdu, &l);
         if (l > 0) {
            switch (*d) {
               case DCC_SET_MMI_MODE:
                  if (l == 2 && d[1] == MM_HIGH_LEVEL) {
                     pobj->MMIDisplayReply(i_session_id);
                  } else {
                     cLbugf(cL::dbg_dvb, "unsupported MMI mode %02x\n", d[1]);
                  }
                  break;
               default:
                  cLbugf(cL::dbg_dvb, "unsupported display control command %02x\n", *d);
                  break;
            }
         }
         break;
      }
      case AOT_ENQ:
         pobj->MMIHandleEnq(i_session_id, p_apdu, i_size);
         break;
      case AOT_LIST_LAST:
      case AOT_MENU_LAST:
         pobj->MMIHandleMenu(i_session_id, i_tag, p_apdu, i_size);
         break;
      case AOT_CLOSE_MMI:
         pobj->SessionSendClose(i_session_id);
         break;
      default:
         cLbugf(cL::dbg_dvb, "unexpected tag in MMIHandle (0x%x)\n", i_tag);
   }
}

void cLdvben50221::MMIClose(int i_session_id, cLdvben50221 *pobj)
{
   int i_slot = pobj->p_sessions[i_session_id - 1].i_slot;
   mmi_t *p_mmi = (mmi_t *)pobj->p_sessions[i_session_id - 1].p_sys;

   pobj->en50221_MMIFree( &p_mmi->last_object );
   ::free(pobj->p_sessions[i_session_id - 1].p_sys);

   cLbugf(cL::dbg_dvb, "closing MMI session (%d)\n", i_session_id);

   pobj->p_slots[i_slot].b_mmi_expected = false;
   pobj->p_slots[i_slot].b_mmi_undisplayed = true;
}

void cLdvben50221::MMIOpen(int i_session_id)
{
   mmi_t *p_mmi;
   cLbugf(cL::dbg_dvb, "opening MMI session (%d)\n", i_session_id);

   this->p_sessions[i_session_id - 1].pf_handle = cLdvben50221::MMIHandle;
   this->p_sessions[i_session_id - 1].pf_close = cLdvben50221::MMIClose;
   this->p_sessions[i_session_id - 1].p_sys = (void *)cLmalloc(mmi_t, 1);
   p_mmi = (mmi_t *)this->p_sessions[i_session_id - 1].p_sys;
   p_mmi->last_object.i_object_type = EN50221_MMI_NONE;
}

/*
 * Hardware handling
 */

 /* Open the transport layer */
void cLdvben50221::InitSlot(int i_slot)
{
   if (this->TPDUSend(i_slot, T_CREATE_TC, (uint8_t *) 0, 0) != 0) {
      cLbugf(cL::dbg_dvb, "en50221_Init: couldn't send TPDU on slot %d\n", i_slot);
   }
}

void cLdvben50221::ResetSlotCb(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *)p;
   cLdvben50221 *pobj = (cLdvben50221 *)w->data;

   ci_slot_t *p_slot = container_of(w, ci_slot_t, init_watcher);
   int i_slot = p_slot - &pobj->p_slots[0];

   if (p_slot->b_active || !p_slot->b_expect_answer)
      return;

   cLbugf(cL::dbg_dvb, "no answer from CAM, resetting slot %d\n", i_slot);
   pobj->ResetSlot(i_slot);
}

void cLdvben50221::ResetSlot(int i_slot)
{
   ci_slot_t *p_slot = &this->p_slots[i_slot];
   int i_session_id;

   if (ioctl(this->i_ca_handle, CA_RESET, 1 << i_slot) != 0)
      cLbugf(cL::dbg_dvb, "en50221_Poll: couldn't reset slot %d\n", i_slot);

   p_slot->b_active = false;
   p_slot->init_watcher.data = this;
   cLev_timer_init(&p_slot->init_watcher, cLdvben50221::ResetSlotCb, CAM_INIT_TIMEOUT / 1000000., 0);
   cLev_timer_start(this->event_loop, &p_slot->init_watcher);

   p_slot->b_expect_answer = false;
   p_slot->b_mmi_expected = false;
   p_slot->b_mmi_undisplayed = false;
   if (p_slot->p_recv != (en50221_msg_t *) 0) {
      ::free(p_slot->p_recv->p_data);
      ::free(p_slot->p_recv);
   }
   p_slot->p_recv = (en50221_msg_t *) 0;
   while ( p_slot->p_send != (en50221_msg_t *) 0) {
      en50221_msg_t *p_next = p_slot->p_send->p_next;
      ::free(p_slot->p_send->p_data);
      ::free(p_slot->p_send);
      p_slot->p_send = p_next;
   }
   p_slot->pp_send_last = &p_slot->p_send;

   /* Close all sessions for this slot. */
   for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (this->p_sessions[i_session_id - 1].i_resource_id && this->p_sessions[i_session_id - 1].i_slot == i_slot) {
         if (this->p_sessions[i_session_id - 1].pf_close != (fssn_common) 0) {
            this->p_sessions[i_session_id - 1].pf_close(i_session_id, this);
         }
         this->p_sessions[i_session_id - 1].i_resource_id = 0;
      }
   }
}

void cLdvben50221::en50221_Read(void *loop, void *p, int revents)
{
   struct cLev_io *w = (struct cLev_io *)p;
   cLdvben50221 *pobj = (cLdvben50221 *)w->data;
   pobj->TPDURecv();
   cLev_timer_again(pobj->event_loop, &pobj->slot_watcher);
}

/* Send a poll TPDU to the CAM */
void cLdvben50221::en50221_Poll(void *loop, void *p, int revents)
{
   struct cLev_timer *w = (struct cLev_timer *)p;
   cLdvben50221 *pobj = (cLdvben50221 *)w->data;

   int i_slot;
   int i_session_id;

   /* Check module status */
   for (i_slot = 0; i_slot < pobj->i_nb_slots; i_slot++) {
      ci_slot_t *p_slot = &pobj->p_slots[i_slot];
      ca_slot_info_t sinfo;

      sinfo.num = i_slot;
      if (ioctl( pobj->i_ca_handle, CA_GET_SLOT_INFO, &sinfo ) != 0) {
         cLbugf(cL::dbg_dvb, "en50221_Poll: couldn't get info on slot %d\n", i_slot);
         continue;
      }

      if (!(sinfo.flags & CA_CI_MODULE_READY)) {
         if (p_slot->b_active) {
            cLbugf(cL::dbg_dvb, "en50221_Poll: slot %d has been removed\n", i_slot);
            pobj->ResetSlot(i_slot);
         }
      } else
      if (!p_slot->b_active) {
         if (!p_slot->b_expect_answer)
            pobj->InitSlot(i_slot);
      }
   }

   /* Check if applications have data to send */
   for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      en50221_session_t *p_session = &pobj->p_sessions[i_session_id - 1];
      if (p_session->i_resource_id && p_session->pf_manage != (fssn_common) 0 && !pobj->p_slots[ p_session->i_slot ].b_expect_answer)
         p_session->pf_manage(i_session_id, pobj);
   }

   /* Now send the poll command to inactive slots */
   for (i_slot = 0; i_slot < pobj->i_nb_slots; i_slot++) {
      ci_slot_t *p_slot = &pobj->p_slots[i_slot];

      if (p_slot->b_active && !p_slot->b_expect_answer) {
         if (pobj->TPDUSend(i_slot, T_DATA_LAST, (const uint8_t *) 0, 0) != 0) {
            cLbugf(cL::dbg_dvb, "couldn't send TPDU, resetting slot %d\n", i_slot);
            pobj->ResetSlot(i_slot);
         }
      }
   }
}

/*
 * External entry points
 */
void cLdvben50221::en50221_Init()
{
   char psz_tmp[128];
   ca_caps_t caps;

   memset(&caps, 0, sizeof(ca_caps_t));

   sprintf( psz_tmp, "/dev/dvb/adapter%d/ca%d", this->i_adapter, this->i_canum);
   if ((this->i_ca_handle = open(psz_tmp, O_RDWR | O_NONBLOCK)) < 0) {
      cLbugf(cL::dbg_dvb, "failed opening CAM device %s (%s)\n", psz_tmp, strerror(errno));
      this->i_ca_handle = 0;
      return;
   }

   if (ioctl(this->i_ca_handle, CA_GET_CAP, &caps) != 0) {
      cLbugf(cL::dbg_dvb, "failed getting CAM capabilities (%s)\n", strerror(errno));
      close(this->i_ca_handle);
      this->i_ca_handle = 0;
      return;
   }

   /* Output CA capabilities */
   cLbugf(cL::dbg_dvb, "CA interface with %d %s\n", caps.slot_num, caps.slot_num == 1 ? "slot" : "slots");
   if (caps.slot_type & CA_CI) {
      cLbug(cL::dbg_dvb, "  CI high level interface type\n");
   }
   if (caps.slot_type & CA_CI_LINK) {
      cLbug(cL::dbg_dvb, "  CI link layer level interface type\n");
   }
   if (caps.slot_type & CA_CI_PHYS) {
      cLbug(cL::dbg_dvb, "  CI physical layer level interface type (not supported) \n");
   }
   if (caps.slot_type & CA_DESCR) {
      cLbug(cL::dbg_dvb, "  built-in descrambler detected\n");
   }
   if (caps.slot_type & CA_SC) {
      cLbug(cL::dbg_dvb, "  simple smart card interface\n");
   }
   cLbugf(cL::dbg_dvb, "  %d available %s\n", caps.descr_num, caps.descr_num == 1 ? "descrambler (key)" : "descramblers (keys)");
   if (caps.descr_type & CA_ECD) {
      cLbug(cL::dbg_dvb, "  ECD scrambling system supported\n");
   }
   if (caps.descr_type & CA_NDS) {
      cLbug(cL::dbg_dvb, "  NDS scrambling system supported\n");
   }
   if (caps.descr_type & CA_DSS) {
      cLbug(cL::dbg_dvb, "  DSS scrambling system supported\n");
   }
   if (caps.slot_num == 0) {
      cLbug(cL::dbg_dvb, "CAM module with no slots\n");
      close(this->i_ca_handle);
      this->i_ca_handle = 0;
      return;
   }

   if (caps.slot_type & CA_CI_LINK) {
      this->i_ca_type = CA_CI_LINK;
   } else
   if (caps.slot_type & CA_CI) {
      this->i_ca_type = CA_CI;
   } else {
      cLbug(cL::dbg_dvb, "Incompatible CAM interface\n");
      close(this->i_ca_handle);
      this->i_ca_handle = 0;
      return;
   }

   this->i_nb_slots = caps.slot_num;
   memset(this->p_sessions, 0, sizeof(en50221_session_t) * MAX_SESSIONS);

   if (this->i_ca_type & CA_CI_LINK) {
      this->cam_watcher.data = this;
      cLev_io_init(&this->cam_watcher, cLdvben50221::en50221_Read, this->i_ca_handle, 1); //EV_READ
      cLev_io_start(this->event_loop, &this->cam_watcher);
      this->slot_watcher.data = this;
      cLev_timer_init(&this->slot_watcher, cLdvben50221::en50221_Poll, CA_POLL_PERIOD / 1000000., CA_POLL_PERIOD / 1000000.);
      cLev_timer_start(this->event_loop, &this->slot_watcher);
   }
   this->en50221_Reset();
}

void cLdvben50221::en50221_Reset()
{
   memset(this->p_slots, 0, sizeof(ci_slot_t) * MAX_CI_SLOTS);

   if (this->i_ca_type & CA_CI_LINK) {
      for (int i_slot = 0; i_slot < this->i_nb_slots; i_slot++)
         this->ResetSlot(i_slot);
   } else {
      struct ca_slot_info info;
      system_ids_t *p_ids;
      ca_msg_t ca_msg;
      info.num = 0;

      /* We don't reset the CAM in that case because it's done by the
       * ASIC. */
      if (ioctl(this->i_ca_handle, CA_GET_SLOT_INFO, &info) < 0) {
         cLbug(cL::dbg_dvb, "en50221_Init: couldn't get slot info\n");
         close(this->i_ca_handle);
         this->i_ca_handle = 0;
         return;
      }
      if (info.flags == 0) {
         cLbug(cL::dbg_dvb, "en50221_Init: no CAM inserted\n");
         close(this->i_ca_handle);
         this->i_ca_handle = 0;
         return;
      }

      /* Allocate a dummy sessions */
      this->p_sessions[0].i_resource_id = RI_CONDITIONAL_ACCESS_SUPPORT;
      this->p_sessions[0].pf_close = cLdvben50221::ConditionalAccessClose;
      if ( this->p_sessions[0].p_sys == (void *) 0)
         this->p_sessions[0].p_sys = (void *)cLmalloc(system_ids_t, 1);
      memset(this->p_sessions[0].p_sys, 0, sizeof(system_ids_t));
      p_ids = (system_ids_t *)this->p_sessions[0].p_sys;
      p_ids->b_high_level = 1;

      /* Get application info to find out which cam we are using and make
           sure everything is ready to play */
      ca_msg.length=3;
      ca_msg.msg[0] = (AOT_APPLICATION_INFO & 0xFF0000) >> 16;
      ca_msg.msg[1] = (AOT_APPLICATION_INFO & 0x00FF00) >> 8;
      ca_msg.msg[2] = (AOT_APPLICATION_INFO & 0x0000FF) >> 0;
      memset(&ca_msg.msg[3], 0, 253);
      this->APDUSend(1, AOT_APPLICATION_INFO_ENQ, (uint8_t *) 0, 0);
      if (ioctl(this->i_ca_handle, CA_GET_MSG, &ca_msg) < 0) {
         cLbug(cL::dbg_dvb, "en50221_Init: failed getting message\n");
         close(this->i_ca_handle);
         this->i_ca_handle = 0;
         return;
      }

#ifdef HLCI_WAIT_CAM_READY
      while(ca_msg.msg[8] == 0xff && ca_msg.msg[9] == 0xff) {
         this->msleep(1);
         cLbug(cL::dbg_dvb, "CAM: please wait\n");
         this->APDUSend(1, AOT_APPLICATION_INFO_ENQ, (uint8_t *) 0, 0);
         ca_msg.length=3;
         ca_msg.msg[0] = (AOT_APPLICATION_INFO & 0xFF0000) >> 16;
         ca_msg.msg[1] = (AOT_APPLICATION_INFO & 0x00FF00) >> 8;
         ca_msg.msg[2] = (AOT_APPLICATION_INFO & 0x0000FF) >> 0;
         memset(&ca_msg.msg[3], 0, 253);
         if (ioctl(this->i_ca_handle, CA_GET_MSG, &ca_msg ) < 0) {
            cLbugf(cL::dbg_dvb, "en50221_Init: failed getting message\n");
            close(this->i_ca_handle);
            this->i_ca_handle = 0;
            return;
         }
         cLbugf(cL::dbg_dvb, "en50221_Init: Got length: %d, tag: 0x%x\n", ca_msg.length, APDUGetTag(ca_msg.msg, ca_msg.length));
      }
#else
      if (ca_msg.msg[8] == 0xff && ca_msg.msg[9] == 0xff) {
         cLbug(cL::dbg_dvb, "CAM returns garbage as application info!\n");
         close(this->i_ca_handle);
         this->i_ca_handle = 0;
         return;
      }
#endif
      cLbugf(cL::dbg_dvb, "found CAM %s using id 0x%x\n", &ca_msg.msg[12], (ca_msg.msg[8]<<8)|ca_msg.msg[9]);
   }
}


void cLdvben50221::en50221_AddPMT(uint8_t *p_pmt)
{
   for (int i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (this->p_sessions[i_session_id - 1].i_resource_id == RI_CONDITIONAL_ACCESS_SUPPORT)
         this->CAPMTAdd(i_session_id, p_pmt);
   }
}

void cLdvben50221::en50221_UpdatePMT(uint8_t *p_pmt)
{
   for (int i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++ ) {
      if (this->p_sessions[i_session_id - 1].i_resource_id == RI_CONDITIONAL_ACCESS_SUPPORT)
         this->CAPMTUpdate(i_session_id, p_pmt);
   }
}

void cLdvben50221::en50221_DeletePMT(uint8_t *p_pmt)
{
   for (int i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (this->p_sessions[i_session_id - 1].i_resource_id == RI_CONDITIONAL_ACCESS_SUPPORT)
         this->CAPMTDelete(i_session_id, p_pmt);
   }
}

uint8_t cLdvben50221::en50221_StatusMMI(uint8_t *p_answer, ssize_t *pi_size)
{
   struct ret_mmi_status *p_ret = (struct ret_mmi_status *)p_answer;

   if (ioctl( this->i_ca_handle, CA_GET_CAP, &p_ret->caps) != 0) {
      cLbugf(cL::dbg_dvb, "ioctl CA_GET_CAP failed (%s)\n", strerror(errno));
      return cLdvben50221::RET_ERR;
   }

   *pi_size = sizeof(struct ret_mmi_status);
   return cLdvben50221::RET_MMI_STATUS;
}

uint8_t cLdvben50221::en50221_StatusMMISlot(uint8_t *p_buffer, ssize_t i_size, uint8_t *p_answer, ssize_t *pi_size)
{
   int i_slot;
   struct ret_mmi_slot_status *p_ret = (struct ret_mmi_slot_status *)p_answer;

   if (i_size != 1)
      return cLdvben50221::RET_HUH;

   i_slot = *p_buffer;

   p_ret->sinfo.num = i_slot;
   if (ioctl(this->i_ca_handle, CA_GET_SLOT_INFO, &p_ret->sinfo) != 0) {
      cLbugf(cL::dbg_dvb, "ioctl CA_GET_SLOT_INFO failed (%s)\n", strerror(errno));
      return cLdvben50221::RET_ERR;
   }

   *pi_size = sizeof(struct ret_mmi_slot_status);
   return cLdvben50221::RET_MMI_SLOT_STATUS;
}

uint8_t cLdvben50221::en50221_OpenMMI( uint8_t *p_buffer, ssize_t i_size)
{
   int i_slot;

   if (i_size != 1)
      return cLdvben50221::RET_HUH;

   i_slot = *p_buffer;

   if (this->i_ca_type & CA_CI_LINK) {
      int i_session_id;
      for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
         if (this->p_sessions[i_session_id - 1].i_resource_id == RI_MMI && this->p_sessions[i_session_id - 1].i_slot == i_slot) {
            cLbugf(cL::dbg_dvb, "MMI menu is already opened on slot %d (session=%d)\n", i_slot, i_session_id);
            return cLdvben50221::RET_OK;
         }
      }

      for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
         if (this->p_sessions[i_session_id - 1].i_resource_id == RI_APPLICATION_INFORMATION && this->p_sessions[i_session_id - 1].i_slot == i_slot) {
            this->ApplicationInformationEnterMenu(i_session_id);
            return cLdvben50221::RET_OK;
         }
      }

      cLbugf(cL::dbg_dvb, "no application information on slot %d\n", i_slot);
      return cLdvben50221::RET_ERR;
   } else {
      cLbug(cL::dbg_dvb, "MMI menu not supported\n");
      return cLdvben50221::RET_ERR;
   }
}

uint8_t cLdvben50221::en50221_CloseMMI(uint8_t *p_buffer, ssize_t i_size)
{
   int i_slot;

   if (i_size != 1)
      return cLdvben50221::RET_HUH;

   i_slot = *p_buffer;

   if (this->i_ca_type & CA_CI_LINK) {
      int i_session_id;
      for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
         if (this->p_sessions[i_session_id - 1].i_resource_id == RI_MMI && this->p_sessions[i_session_id - 1].i_slot == i_slot) {
            this->MMISendClose(i_session_id);
            return cLdvben50221::RET_OK;
         }
      }

      cLbugf(cL::dbg_dvb, "closing a non-existing MMI session on slot %d\n", i_slot);
      return cLdvben50221::RET_ERR;
   } else {
      cLbug(cL::dbg_dvb, "MMI menu not supported\n");
      return cLdvben50221::RET_ERR;
   }
}

uint8_t cLdvben50221::en50221_GetMMIObject(uint8_t *p_buffer, ssize_t i_size, uint8_t *p_answer, ssize_t *pi_size)
{
   int i_session_id, i_slot;
   struct ret_mmi_recv *p_ret = (struct ret_mmi_recv *)p_answer;

   if (i_size != 1)
      return cLdvben50221::RET_HUH;

   i_slot = *p_buffer;

   if (this->p_slots[i_slot].b_mmi_expected)
      return cLdvben50221::RET_MMI_WAIT; /* data not yet available */

   p_ret->object.i_object_type = EN50221_MMI_NONE;
   *pi_size = sizeof(struct ret_mmi_recv);

   for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (this->p_sessions[i_session_id - 1].i_resource_id == RI_MMI && this->p_sessions[i_session_id - 1].i_slot == i_slot) {
         mmi_t *p_mmi = (mmi_t *)this->p_sessions[i_session_id - 1].p_sys;
         if (p_mmi == (mmi_t *) 0) {
            *pi_size = 0;
            return cLdvben50221::RET_ERR; /* should not happen */
         }

         *pi_size = CLDVB_COMM_BUFFER_SIZE - CLDVB_COMM_HEADER_SIZE - (&p_ret->object - (cLdvben50221::en50221_mmi_object_t *)p_ret);
         if (this->en50221_SerializeMMIObject((uint8_t *)&p_ret->object, pi_size, &p_mmi->last_object ) == -1) {
            *pi_size = 0;
            cLbug(cL::dbg_dvb, "MMI structure too big\n");
            return cLdvben50221::RET_ERR;
         }
         *pi_size += (&p_ret->object - (cLdvben50221::en50221_mmi_object_t *)p_ret);
         break;
      }
   }

   return cLdvben50221::RET_MMI_RECV;
}

uint8_t cLdvben50221::en50221_SendMMIObject(uint8_t *p_buffer, ssize_t i_size)
{
   int i_session_id, i_slot;
   struct cmd_mmi_send *p_cmd = (struct cmd_mmi_send *)p_buffer;

   if ((unsigned long)i_size < sizeof(struct cmd_mmi_send)) {
      cLbugf(cL::dbg_dvb, "command packet too short (%zd)\n", i_size);
      return cLdvben50221::RET_HUH;
   }

   if (en50221_UnserializeMMIObject( &p_cmd->object, i_size - (&p_cmd->object - (cLdvben50221::en50221_mmi_object_t *)p_cmd)) == -1)
      return cLdvben50221::RET_ERR;

   i_slot = p_cmd->i_slot;

   for (i_session_id = 1; i_session_id <= MAX_SESSIONS; i_session_id++) {
      if (this->p_sessions[i_session_id - 1].i_resource_id == RI_MMI && this->p_sessions[i_session_id - 1].i_slot == i_slot) {
         this->MMISendObject(i_session_id, &p_cmd->object);
         return cLdvben50221::RET_OK;
      }
   }

   cLbug(cL::dbg_dvb, "SendMMIObject when no MMI session is opened !\n");
   return cLdvben50221::RET_ERR;
}

#endif // HAVE_CLDVBHW
