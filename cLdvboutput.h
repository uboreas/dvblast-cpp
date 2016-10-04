/*
 * cLdvboutput.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * dvblast.h, dvblast.c, util.c
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

#ifndef CLDVBOUTPUT_H_
#define CLDVBOUTPUT_H_

#include <cLdvbcore.h>
#ifdef HAVE_CLLINUX
#include <netinet/ip.h>
#endif
#include <netdb.h>

/*
Output configuration flags (for output_t -> i_config) - bit values
Bit  0 : Set for watch mode
Bit  1 : Set output still present
Bit  2 : Set if output is valid (replaces m_addr != 0 tests)
Bit  3 : Set for UDP, otherwise use RTP if a network stream
Bit  4 : Set for file / FIFO output, unset for network (future use)
Bit  5 : Set if DVB conformance tables are inserted
Bit  6 : Set if DVB EIT schedule tables are forwarded
Bit  7 : Set for RAW socket output
*/
#define OUTPUT_WATCH                0x01
#define OUTPUT_STILL_PRESENT        0x02
#define OUTPUT_VALID                0x04
#define OUTPUT_UDP                  0x08
#define OUTPUT_FILE                 0x10
#define OUTPUT_DVB                  0x20
#define OUTPUT_EPG                  0x40
#define OUTPUT_RAW                  0x80

#define CLDVB_OUTPUT_MAX_PACKETS 100

namespace libcLdvboutput {

   typedef struct block_t {
         uint8_t p_ts[TS_SIZE];
         int i_refcount;
         libcLdvb::mtime_t i_dts;
         uint16_t tmp_pid;
         struct block_t *p_next;
   } block_t;

   typedef struct packet_t {
         struct packet_t *p_next;
         libcLdvb::mtime_t i_dts;
         int i_depth;
         block_t *pp_blocks[];
   } packet_t;

   struct udpheader {
         u_int16_t source;
         u_int16_t dest;
         u_int16_t len;
         u_int16_t check;
   };

#if defined(HAVE_CLFBSD) || defined(HAVE_CLMACOS)
   struct iphdr {
         unsigned int ihl:4;
         unsigned int version:4;
         uint8_t tos;
         uint16_t tot_len;
         uint16_t id;
         uint16_t frag_off;
         uint8_t ttl;
         uint8_t protocol;
         uint16_t check;
         uint32_t saddr;
         uint32_t daddr;
   };
#define libcLdvb_iphdr libcLdvboutput::iphdr
#else
#define libcLdvb_iphdr iphdr
#endif

   struct udprawpkt {
         struct  iphdr iph;
         struct  udpheader udph;
         uint8_t payload[];
   } __attribute__((packed));

   typedef struct dvb_string_t {
         uint8_t *p;
         size_t i;
   } dvb_string_t;

   typedef struct output_config_t {
         /* identity */
         int i_family;
         struct sockaddr_storage connect_addr;
         struct sockaddr_storage bind_addr;
         int i_if_index_v6;
         /* common config */
         char *psz_displayname;
         uint64_t i_config;
         /* output config */
         uint16_t i_network_id;
         dvb_string_t network_name;
         dvb_string_t service_name;
         dvb_string_t provider_name;
         uint8_t pi_ssrc[4];
         libcLdvb::mtime_t i_output_latency, i_max_retention;
         int i_ttl;
         uint8_t i_tos;
         int i_mtu;
         char *psz_srcaddr; /* raw packets */
         int i_srcport;
         /* demux config */
         int i_tsid;
         uint16_t i_sid; /* 0 if raw mode */
         uint16_t *pi_pids;
         int i_nb_pids;
         uint16_t i_new_sid;
         bool b_passthrough;
         /* for pidmap from config file */
         bool b_do_remap;
         uint16_t pi_confpids[CLDVB_N_MAP_PIDS];
   } output_config_t;

   typedef struct output_t {
         output_config_t config;
         /* output */
         int i_handle;
         packet_t *p_packets, *p_last_packet;
         packet_t *p_packet_lifo;
         unsigned int i_packet_count;
         uint16_t i_seqnum;
         /* demux */
         int i_nb_errors;
         libcLdvb::mtime_t i_last_error;
         uint8_t *p_pat_section;
         uint8_t i_pat_version, i_pat_cc;
         uint8_t *p_pmt_section;
         uint8_t i_pmt_version, i_pmt_cc;
         uint8_t *p_nit_section;
         uint8_t i_nit_version, i_nit_cc;
         uint8_t *p_sdt_section;
         uint8_t i_sdt_version, i_sdt_cc;
         block_t *p_eit_ts_buffer;
         uint8_t i_eit_ts_buffer_offset, i_eit_cc;
         uint16_t i_tsid;
         // Arrays used for mapping pids.
         // newpids is indexed using the original pid
         uint16_t pi_newpids[MAX_PIDS];
         uint16_t pi_freepids[MAX_PIDS];   // used where multiple streams of the same type are used
         struct udprawpkt raw_pkt_header;
   } output_t;

   extern const char *psz_dvb_charset;
   extern int b_random_tsid;
   extern bool b_do_remap;
   extern output_t output_dup;
   extern output_t **pp_outputs;
   extern int i_nb_outputs;
   extern bool b_udp_global;
   extern bool b_dvb_global;
   extern bool b_epg_global;
   extern libcLdvb::mtime_t i_latency_global;
   extern libcLdvb::mtime_t i_retention_global;
   extern int i_ttl_global;
   extern uint8_t pi_ssrc_global[4];
   extern dvb_string_t network_name;
   extern dvb_string_t provider_name;
   extern uint16_t i_network_id;

   extern int output_Init( libcLdvboutput::output_t *p_output, const libcLdvboutput::output_config_t *p_config);
   extern libcLdvboutput::output_t *output_Find( const libcLdvboutput::output_config_t *p_config );
   extern libcLdvboutput::output_t *output_Create( const libcLdvboutput::output_config_t *p_config );
   extern void output_Change( libcLdvboutput::output_t *p_output, const libcLdvboutput::output_config_t *p_config );
   extern void output_Close( libcLdvboutput::output_t *p_output );
   extern void output_Put( libcLdvboutput::output_t *p_output, libcLdvboutput::block_t *p_block );

   extern void outputs_Init( void );
   extern void outputs_Close( int i_num_outputs );
   extern void init_pid_mapping( libcLdvboutput::output_t *p_output );

   extern block_t *block_New();
   extern void block_Delete( block_t *p_block );
   extern void block_Vacuum( void );
   static inline void block_DeleteChain( block_t *p_block ) {
      while ( p_block != NULL ) {
         block_t *p_next = p_block->p_next;
         block_Delete( p_block );
         p_block = p_next;
      }
   }

   extern void dvb_string_clean(dvb_string_t *p_dvb_string);
   extern void dvb_string_copy(dvb_string_t *p_dst, const dvb_string_t *p_src);
   extern int dvb_string_cmp(const dvb_string_t *p_1, const dvb_string_t *p_2);

   extern struct addrinfo *ParseNodeService(char *_psz_string, char **ppsz_end, uint16_t i_default_port );
   extern void config_Init( output_config_t *p_config );
   extern void config_Free( output_config_t *p_config );
   extern char *config_stropt( const char *psz_string );
   extern void config_strdvb( dvb_string_t *p_dvb_string, const char *psz_string );
   extern void config_Print( output_config_t *p_config );
   extern bool config_ParseHost( output_config_t *p_config, char *psz_string );
   extern void config_Defaults( output_config_t *p_config );

   extern char *iconv_cb(void *_unused, const char *psz_encoding, char *p_string, size_t i_length);

} /* namespace libcLdvboutput */


#endif /*CLDVBOUTPUT_H_*/
