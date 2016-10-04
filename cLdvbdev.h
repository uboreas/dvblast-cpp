/*
 * cLdvbdev.h
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

#ifndef CLDVBDEV_H_
#define CLDVBDEV_H_

#include <cLdvbcore.h>

#define DVB_MAX_DELIVERY_SYSTEMS 20
#define DVB_DVR_READ_TIMEOUT     30000000 /* 30 s */
#define DVB_MAX_READ_ONCE        50
#define DVB_DVR_BUFFER_SIZE      40*188*1024 /* bytes */

namespace libcLdvbdev {

   extern int i_frequency;
   extern int i_fenum;
   extern int i_voltage;
   extern int b_tone;
   extern int i_bandwidth;
   extern int i_inversion;
   extern int i_srate;
   extern int i_fec;
   extern int i_rolloff;
   extern int i_satnum;
   extern int i_uncommitted;
   extern char *psz_modulation;
   extern char *psz_delsys;
   extern int i_pilot;
   extern int i_mis;
   extern int i_fec_lp;
   extern int i_guard;
   extern int i_transmission;
   extern int i_hierarchy;
   extern libcLdvb::mtime_t i_frontend_timeout_duration;
   extern libcLdvb::mtime_t i_quit_timeout_duration;

   extern void dvb_Open();
   extern void dvb_Reset();
   extern int dvb_SetFilter(uint16_t i_pid);
   extern void dvb_UnsetFilter(int i_fd, uint16_t i_pid);
   extern uint8_t dvb_FrontendStatus(uint8_t *p_answer, ssize_t *pi_size);

} /* namespace libcLdvbdev */

#endif /*CLDVBDEV_H_*/

