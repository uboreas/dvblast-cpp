/*
 * cLdvbcomm.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * comm.h
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

#ifndef CLDVBCOMM_H_
#define CLDVBCOMM_H_

#include <cLdvbcore.h>

namespace libcLdvbcomm {

   typedef enum {
      CMD_INVALID             = 0,
      CMD_RELOAD              = 1,
      CMD_SHUTDOWN            = 2,
      CMD_FRONTEND_STATUS     = 3,
      CMD_MMI_STATUS          = 4,
      CMD_MMI_SLOT_STATUS     = 5, /* arg: slot */
      CMD_MMI_OPEN            = 6, /* arg: slot */
      CMD_MMI_CLOSE           = 7, /* arg: slot */
      CMD_MMI_RECV            = 8, /* arg: slot */
      CMD_GET_PAT             = 10,
      CMD_GET_CAT             = 11,
      CMD_GET_NIT             = 12,
      CMD_GET_SDT             = 13,
      CMD_GET_PMT             = 14, /* arg: service_id (uint16_t) */
      CMD_GET_PIDS            = 15,
      CMD_GET_PID             = 16, /* arg: pid (uint16_t) */
      CMD_MMI_SEND_TEXT       = 17, /* arg: slot, libcLdvben50221::en50221_mmi_object_t */
      CMD_MMI_SEND_CHOICE     = 18, /* arg: slot, libcLdvben50221::en50221_mmi_object_t */
   } ctl_cmd_t;

   extern char *psz_srv_socket;
   extern void comm_Open( void );
   extern void comm_Close( void );

} /* namespace libcLdvbcomm */

#endif /*CLDVBCOMM_H_*/
