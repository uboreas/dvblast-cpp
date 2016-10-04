/*
 * cLdvben50221.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * en50221.h
 *****************************************************************************
 * Copyright (C) 2008 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 *****************************************************************************/

#ifndef CLDVBEN50221_H_
#define CLDVBEN50221_H_

#include <cLdvbcore.h>
#include <stddef.h>
#include <string.h>

#ifdef HAVE_CLDVBHW
#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/ca.h>
#endif

namespace libcLdvben50221 {

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

   typedef void (*fdev_ResendCAPMTsCB)(void);
   typedef bool (*fdev_PIDIsSelectedCB)(uint16_t);

   extern int i_ca_handle;
   extern int i_ca_type;
   extern int i_canum;
   extern fdev_ResendCAPMTsCB pf_ResendCAPMTs;
   extern fdev_PIDIsSelectedCB pf_PIDIsSelected;

#ifdef HAVE_CLDVBHW

#define STRINGIFY(z) UGLY_KLUDGE(z)
#define UGLY_KLUDGE(z) #z

#define EN50221_MMI_NONE      0
#define EN50221_MMI_ENQ       1
#define EN50221_MMI_ANSW      2
#define EN50221_MMI_MENU      3
#define EN50221_MMI_MENU_ANSW 4
#define EN50221_MMI_LIST      5

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

#define MAX_CI_SLOTS 16
#define MAX_SESSIONS 32

   /*****************************************************************************
    * Prototypes
    *****************************************************************************/
   extern void en50221_Init( void );
   extern void en50221_Reset( void );
   extern void en50221_AddPMT( uint8_t *p_pmt );
   extern void en50221_UpdatePMT( uint8_t *p_pmt );
   extern void en50221_DeletePMT( uint8_t *p_pmt );
   extern uint8_t en50221_StatusMMI( uint8_t *p_answer, ssize_t *pi_size );
   extern uint8_t en50221_StatusMMISlot( uint8_t *p_buffer, ssize_t i_size, uint8_t *p_answer, ssize_t *pi_size );
   extern uint8_t en50221_OpenMMI( uint8_t *p_buffer, ssize_t i_size );
   extern uint8_t en50221_CloseMMI( uint8_t *p_buffer, ssize_t i_size );
   extern uint8_t en50221_GetMMIObject( uint8_t *p_buffer, ssize_t i_size, uint8_t *p_answer, ssize_t *pi_size );
   extern uint8_t en50221_SendMMIObject( uint8_t *p_buffer, ssize_t i_size );

   /*
    * This is where it gets scary: do not show to < 18 yrs old
    */

   /*****************************************************************************
    * en50221_SerializeMMIObject :
    *****************************************************************************/
   static inline int en50221_SerializeMMIObject(uint8_t *p_answer, ssize_t *pi_size, en50221_mmi_object_t *p_object)
   {
      ssize_t i_max_size = *pi_size;
      en50221_mmi_object_t *p_serialized = (en50221_mmi_object_t *)p_answer;
      char **pp_tmp;
      int i;

#define STORE_MEMBER(pp_pointer, i_typ, i_size) \
      if ( i_size + *pi_size > i_max_size ) \
      return -1; \
      memcpy( p_answer, *pp_pointer, i_size); \
      *pp_pointer = (i_typ)*pi_size; \
      *pi_size += i_size; \
      p_answer += i_size;

      if ( (ssize_t)sizeof(en50221_mmi_object_t) > i_max_size )
         return -1;
      memcpy( p_answer, p_object, sizeof(en50221_mmi_object_t) );
      *pi_size = sizeof(en50221_mmi_object_t);
      p_answer += sizeof(en50221_mmi_object_t);

      switch ( p_object->i_object_type ) {
         case EN50221_MMI_ENQ:
            STORE_MEMBER( &p_serialized->u.enq.psz_text, char *, (ssize_t)(strlen(p_object->u.enq.psz_text) + 1));
            break;
         case EN50221_MMI_ANSW:
            STORE_MEMBER( &p_serialized->u.answ.psz_answ, char *, (ssize_t)(strlen(p_object->u.answ.psz_answ) + 1));
            break;
         case EN50221_MMI_MENU:
         case EN50221_MMI_LIST:
            STORE_MEMBER( &p_serialized->u.menu.psz_title, char *, (ssize_t)(strlen(p_object->u.menu.psz_title) + 1));
            STORE_MEMBER( &p_serialized->u.menu.psz_subtitle, char *, (ssize_t)(strlen(p_object->u.menu.psz_subtitle) + 1));
            STORE_MEMBER( &p_serialized->u.menu.psz_bottom, char *, (ssize_t)(strlen(p_object->u.menu.psz_bottom) + 1));
            /* pointer alignment */
            i = ((*pi_size + 7) / 8) * 8 - *pi_size;
            *pi_size += i;
            p_answer += i;
            pp_tmp = (char **)p_answer;
            STORE_MEMBER( &p_serialized->u.menu.ppsz_choices, char **, (ssize_t)(p_object->u.menu.i_choices * sizeof(char *)));
            for ( i = 0; i < p_object->u.menu.i_choices; i++ ) {
               STORE_MEMBER( &pp_tmp[i], char *, (ssize_t)(strlen(p_object->u.menu.ppsz_choices[i]) + 1));
            }
            break;
         default:
            break;
      }
      return 0;
   }

   /*****************************************************************************
    * en50221_UnserializeMMIObject :
    *****************************************************************************/
   static inline int en50221_UnserializeMMIObject( en50221_mmi_object_t *p_object, ssize_t i_size )
   {
      int i, j;

#define CHECK_MEMBER(pp_member) \
      if ( (ptrdiff_t)*pp_member >= i_size) \
      return -1; \
      for (i = 0; ((char *)p_object + (ptrdiff_t)*pp_member)[i] != '\0'; i++) \
      if ( (ptrdiff_t)*pp_member + i >= i_size) \
      return -1; \
      *pp_member += (ptrdiff_t)p_object;

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
            for ( j = 0; j < p_object->u.menu.i_choices; j++ ) {
               CHECK_MEMBER(&p_object->u.menu.ppsz_choices[j]);
            }
            break;
         default:
            break;
      }
      return 0;
   }

#else //HAVE_CLDVBHW

   extern void en50221_Init( void );
   extern void en50221_AddPMT( uint8_t *p_pmt );
   extern void en50221_UpdatePMT( uint8_t *p_pmt );
   extern void en50221_DeletePMT( uint8_t *p_pmt );
   extern void en50221_Reset( void );

#endif //HAVE_CLDVBHW

} /* namespace libcLdvben50221 */

#endif /*CLDVBEN50221_H_*/
