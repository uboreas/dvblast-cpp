/*
 * cLdvbdemux.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * dvblast.h
 *****************************************************************************
 * Copyright (C) 2004, 2008-2011, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Andy Gatward <a.j.gatward@reading.ac.uk>
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

#include <cLdvbcore.h>
#include <cLdvboutput.h>

namespace libcLdvbdemux {

   typedef enum {
      I_PMTPID = 0,
      I_APID,
      I_VPID,
      I_SPUPID
   } pidmap_offset;

   typedef struct ts_pid_info {
         libcLdvb::mtime_t  i_first_packet_ts;     /* Time of the first seen packet */
         libcLdvb::mtime_t  i_last_packet_ts;      /* Time of the last seen packet */
         unsigned long i_packets;                  /* How much packets have been seen */
         unsigned long i_cc_errors;                /* Countinuity counter errors */
         unsigned long i_transport_errors;         /* Transport errors */
         unsigned long i_bytes_per_sec;            /* How much bytes were process last second */
         uint8_t  i_scrambling;                    /* Scrambling bits from the last ts packet: 0 = Not scrambled, 1 = Reserved for future use, 2 = Scrambled with even key, 3 = Scrambled with odd key */
   } ts_pid_info_t;

   extern void demux_Open( void );
   extern void demux_Run( libcLdvboutput::block_t *p_ts );
   extern void demux_Change( libcLdvboutput::output_t *p_output, const libcLdvboutput::output_config_t *p_config );
   extern void demux_ResendCAPMTs( void );
   extern bool demux_PIDIsSelected( uint16_t i_pid );
   extern void demux_Close( void );
   extern uint8_t *demux_get_current_packed_PAT( unsigned int *pi_pack_size );
   extern uint8_t *demux_get_current_packed_CAT( unsigned int *pi_pack_size );
   extern uint8_t *demux_get_current_packed_NIT( unsigned int *pi_pack_size );
   extern uint8_t *demux_get_current_packed_SDT( unsigned int *pi_pack_size );
   extern uint8_t *demux_get_packed_PMT( uint16_t service_id, unsigned int *pi_pack_size );
   extern void demux_get_PID_info( uint16_t i_pid, uint8_t *p_data );
   extern void demux_get_PIDS_info( uint8_t *p_data );

   extern void config_ReadFile();

} /* namespace libcLdvbdemux */


#endif /*CLDVBDEMUX_H_*/
