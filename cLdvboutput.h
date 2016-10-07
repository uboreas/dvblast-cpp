/*
 * cLdvboutput.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * dvblast.h, dvblast.c, util.c, output.c
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

#include <cLdvbev.h>
#include <cLdvbcore.h>
#ifdef HAVE_CLLINUX
#include <netinet/ip.h>
#endif
#include <netdb.h>
#ifdef HAVE_CLICONV
#include <iconv.h>
#endif

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

class cLdvboutput : public cLdvbobj {

   public:
      typedef struct block_t {
         uint8_t p_ts[TS_SIZE];
         int i_refcount;
         mtime_t i_dts;
         uint16_t tmp_pid;
         struct block_t *p_next;
      } block_t;

      typedef struct packet_t {
            struct packet_t *p_next;
            mtime_t i_dts;
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
#define libcLdvb_iphdr cLdvboutput::iphdr
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
            mtime_t i_output_latency, i_max_retention;
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
            mtime_t i_last_error;
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

   private:
      struct cLev_timer output_watcher;
      mtime_t i_next_send;
      mtime_t i_wallclock;
      cLdvboutput::block_t *p_block_lifo;
      unsigned int i_block_count;
      #ifdef HAVE_CLICONV
      iconv_t conf_iconv;
      iconv_t iconv_handle;
      #endif
      uint8_t p_pad_ts[TS_SIZE];

      static void dvb_string_init(dvb_string_t *p_dvb_string);
      char *config_striconv(const char *psz_string);

      static void RawFillHeaders(struct udprawpkt *dgram, in_addr_t ipsrc, in_addr_t ipdst, uint16_t portsrc, uint16_t portdst, uint8_t ttl, uint8_t tos, uint16_t len);
      static int output_BlockCount(output_t *p_output);
      static packet_t *output_PacketNew(output_t *p_output);
      static void output_PacketDelete(output_t *p_output, packet_t *p_packet);
      static void output_PacketVacuum(output_t *p_output);
      void output_Flush(output_t *p_output);
      static void outputs_Send(void *loop, void *w, int revents);

      static char *iconv_append_null(const char *p_string, size_t i_length);

   protected:
      const char *psz_dvb_charset;
      int b_random_tsid;
      bool b_do_remap;
      output_t **pp_outputs;
      int i_nb_outputs;
      bool b_udp_global;
      bool b_dvb_global;
      bool b_epg_global;
      mtime_t i_latency_global;
      mtime_t i_retention_global;
      int i_ttl_global;
      uint8_t pi_ssrc_global[4];
      dvb_string_t network_name;
      dvb_string_t provider_name;
      uint16_t i_network_id;
      char *psz_dup_config;
      output_t *output_dup;

      block_t *block_New();
      void block_Delete(block_t *p_block);
      void block_DeleteChain(block_t *p_block);
      void block_Vacuum(void);

      static void dvb_string_clean(dvb_string_t *p_dvb_string);
      static void dvb_string_copy(dvb_string_t *p_dst, const dvb_string_t *p_src);
      static int dvb_string_cmp(const dvb_string_t *p_1, const dvb_string_t *p_2);
      static char *config_stropt(const char *psz_string);
      void config_strdvb(dvb_string_t *p_dvb_string, const char *psz_string);
      void config_Init(output_config_t *p_config);
      static void config_Free(output_config_t *p_config);
      static void config_Print(output_config_t *p_config);
      void config_Defaults(output_config_t *p_config);
      static struct addrinfo *ParseNodeService(char *_psz_string, char **ppsz_end, uint16_t i_default_port);
      bool config_ParseHost(output_config_t *p_config, char *psz_string);

      static void init_pid_mapping(cLdvboutput::output_t *p_output);
      int output_Init(cLdvboutput::output_t *p_output, const cLdvboutput::output_config_t *p_config);
      output_t *output_Create(const output_config_t *p_config);
      void output_Close(cLdvboutput::output_t *p_output);
      void output_Put(cLdvboutput::output_t *p_output, cLdvboutput::block_t *p_block);
      void outputs_Init(void);
      cLdvboutput::output_t *output_Find(const cLdvboutput::output_config_t *p_config);
      static void output_Change(cLdvboutput::output_t *p_output, const cLdvboutput::output_config_t *p_config);
      void outputs_Close(int i_num_outputs);

      static char *iconv_cb(void *iconv_opaque, const char *psz_encoding, char *p_string, size_t i_length);

   public:
      inline void set_rawudp(bool b = true) {
         this->b_udp_global = b;
      }
      inline void set_dvb_compliance(bool b = true) {
         this->b_dvb_global = b;
      }
      inline void set_pass_epg(bool b = true) {
         this->b_epg_global = b;
      }
      inline void set_ttl(int t) {
         this->i_ttl_global = t;
      }
      inline void set_max_latency(mtime_t i) {
         this->i_latency_global = i;
      }
      inline void set_max_retention(mtime_t i) {
         this->i_retention_global = i;
      }
      inline void set_network_id(uint16_t i) {
         this->i_network_id = i;
      }
      inline void set_random_tsid(int i = 1) {
         this->b_random_tsid = i;
      }
      inline void set_dvb_charset(const char *s) {
         this->psz_dvb_charset = s;
      }
      inline void set_dupconfig(char *s) {
         this->psz_dup_config = s;
      }

      bool set_rtpsrc(const char *s);
      bool output_Setup(const char *netname, const char *proname);

      cLdvboutput();
      virtual ~cLdvboutput();
};

#endif /*CLDVBOUTPUT_H_*/
