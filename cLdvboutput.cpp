/*
 * cLdvboutput.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * output.c, dvblast.c, util.c
 *****************************************************************************
 * Copyright (C) 2004, 2008-2010, 2015 VideoLAN
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

#include <cLdvbev.h>
#include <cLdvboutput.h>

#ifdef HAVE_CLMACOS
#include <sys/uio.h>
#endif

#include <unistd.h>
#include <arpa/inet.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/ietf/rtp.h>
#include <bitstream/dvb/si/strings.h>
#include <errno.h>

namespace libcLdvboutput {

   static struct cLev_timer output_watcher;
   static libcLdvb::mtime_t i_next_send = INT64_MAX;
   libcLdvb::mtime_t i_wallclock = 0;
   int b_random_tsid = 0;
   bool b_do_remap = false;
   output_t output_dup;
   static block_t *p_block_lifo = (block_t *) 0;
   static unsigned int i_block_count = 0;
   output_t **pp_outputs = (output_t **) 0;
   int i_nb_outputs = 0;
   /*static*/ bool b_udp_global = false;
   /*static*/ bool b_dvb_global = false;
   /*static*/ bool b_epg_global = false;
   /*static*/ libcLdvb::mtime_t i_latency_global = DEFAULT_OUTPUT_LATENCY;
   /*static*/ libcLdvb::mtime_t i_retention_global = DEFAULT_MAX_RETENTION;
   /*static*/ int i_ttl_global = 64;
   uint8_t pi_ssrc_global[4] = { 0, 0, 0, 0 };
   /*static*/ dvb_string_t network_name;
   /*static*/ dvb_string_t provider_name;
   /*static*/ uint16_t i_network_id = 0xffff;

   static uint8_t p_pad_ts[TS_SIZE] = {
         0x47, 0x1f, 0xff, 0x10, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
   };

   block_t *block_New()
   {
      block_t *p_block;

      if (i_block_count) {
         p_block = p_block_lifo;
         p_block_lifo = p_block->p_next;
         i_block_count--;
      } else {
         p_block = cLmalloc(block_t, 1);
      }

      p_block->p_next = (block_t *) 0;
      p_block->i_refcount = 1;
      return p_block;
   }

   void block_Delete(block_t *p_block)
   {
      if (i_block_count >= CLDVB_MAX_BLOCKS) {
         free(p_block);
         return;
      }
      p_block->p_next = p_block_lifo;
      p_block_lifo = p_block;
      i_block_count++;
   }

   void block_Vacuum()
   {
      while (i_block_count) {
         block_t *p_block = p_block_lifo;
         p_block_lifo = p_block->p_next;
         free(p_block);
         i_block_count--;
      }
   }

   void dvb_string_init(dvb_string_t *p_dvb_string)
   {
      p_dvb_string->p = (uint8_t *) 0;
      p_dvb_string->i = 0;
   }

   void dvb_string_clean(dvb_string_t *p_dvb_string)
   {
      free(p_dvb_string->p);
   }

   void dvb_string_copy(dvb_string_t *p_dst, const dvb_string_t *p_src)
   {
      p_dst->p = cLmalloc(uint8_t, p_src->i);
      memcpy(p_dst->p, p_src->p, p_src->i);
      p_dst->i = p_src->i;
   }

   int dvb_string_cmp(const dvb_string_t *p_1, const dvb_string_t *p_2)
   {
      if (p_1->i != p_2->i)
         return p_1->i - p_2->i;
      return memcmp(p_1->p, p_2->p, p_1->i);
   }

   struct addrinfo *ParseNodeService(char *_psz_string, char **ppsz_end, uint16_t i_default_port)
   {
         int i_family = AF_INET;
         char psz_port_buffer[6];
         char *psz_string = strdup(_psz_string);
         char *psz_node, *psz_port = (char *) 0, *psz_end;
         struct addrinfo *p_res;
         struct addrinfo hint;
         int i_ret;

         if (psz_string[0] == '[') {
            i_family = AF_INET6;
            psz_node = psz_string + 1;
            psz_end = strchr(psz_node, ']');
            if (psz_end == (char *) 0) {
               cLbugf(cL::dbg_dvb, "invalid IPv6 address %s\n", _psz_string);
               free(psz_string);
               cLbug(cL::dbg_dvb, "invalied ipv6, return false\n");
               return (struct addrinfo *) 0;
            }
            *psz_end++ = '\0';
         } else {
            psz_node = psz_string;
            psz_end = strpbrk(psz_string, "@:,/");
         }

         if (psz_end != ((char *) 0) && psz_end[0] == ':') {
            *psz_end++ = '\0';
            psz_port = psz_end;
            psz_end = strpbrk(psz_port, "@:,/");
         }

         if (psz_end != (char *) 0) {
            *psz_end = '\0';
            if (ppsz_end != (char **) 0)
               *ppsz_end = _psz_string + (psz_end - psz_string);
         } else
            if (ppsz_end != (char **) 0) {
               *ppsz_end = _psz_string + strlen(_psz_string);
            }

         if (i_default_port != 0 && (psz_port == (char *) 0 || !*psz_port)) {
            sprintf(psz_port_buffer, "%u", i_default_port);
            psz_port = psz_port_buffer;
         }

         if (psz_node[0] == '\0') {
            free(psz_string);
            cLbug(cL::dbg_dvb, "parse node services return false 2\n");
            return (struct addrinfo *) 0;
         }

         memset(&hint, 0, sizeof(hint));
         hint.ai_family = i_family;
         hint.ai_socktype = SOCK_DGRAM;
         hint.ai_protocol = 0;
         hint.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG;
         if ((i_ret = getaddrinfo(psz_node, psz_port, (struct addrinfo *) 0, &p_res)) != 0) {
            cLbugf(cL::dbg_dvb, "getaddrinfo(host=%s, port=%s) error: %s\n", psz_node, psz_port ? psz_port : "", gai_strerror(i_ret));
            free(psz_string);
            cLbug(cL::dbg_dvb, "parse node services return false 3\n");
            return (struct addrinfo *) 0;
         }

         free(psz_string);
         return p_res;
   }

   void config_Init( output_config_t *p_config )
   {
      memset( p_config, 0, sizeof(output_config_t) );

      p_config->psz_displayname = NULL;
      p_config->i_network_id = i_network_id;
      dvb_string_init(&p_config->network_name);
      dvb_string_init(&p_config->service_name);
      dvb_string_init(&p_config->provider_name);
      p_config->psz_srcaddr = NULL;

      p_config->i_family = AF_UNSPEC;
      p_config->connect_addr.ss_family = AF_UNSPEC;
      p_config->bind_addr.ss_family = AF_UNSPEC;
      p_config->i_if_index_v6 = -1;
      p_config->i_srcport = 0;

      p_config->pi_pids = NULL;
      p_config->b_passthrough = false;
      p_config->b_do_remap = false;
      unsigned int i;
      for ( i = 0; i < CLDVB_N_MAP_PIDS; i++ ) {
         p_config->pi_confpids[i]  = UNUSED_PID;
      }
   }

   void config_Free( output_config_t *p_config )
   {
      free( p_config->psz_displayname );
      dvb_string_clean( &p_config->network_name );
      dvb_string_clean( &p_config->service_name );
      dvb_string_clean( &p_config->provider_name );
      free( p_config->pi_pids );
      free( p_config->psz_srcaddr );
   }

   char *config_stropt( const char *psz_string )
   {
      char *ret, *tmp;
      if ( !psz_string || strlen( psz_string ) == 0 )
         return NULL;
      ret = tmp = strdup( psz_string );
      while (*tmp) {
         if (*tmp == '_')
            *tmp = ' ';
         if (*tmp == '/') {
            *tmp = '\0';
            break;
         }
         tmp++;
      }
      return ret;
   }

   static char *config_striconv( const char *psz_string )
   {
      char *psz_input = config_stropt(psz_string);

      if ( !strcasecmp( libcLdvb::psz_native_charset, libcLdvb::psz_dvb_charset ) )
         return psz_input;

#ifdef HAVE_CLICONV
      if ( libcLdvb::conf_iconv == (iconv_t)-1 )
      {
         libcLdvb::conf_iconv = iconv_open( libcLdvb::psz_dvb_charset, libcLdvb::psz_native_charset );
         if ( libcLdvb::conf_iconv == (iconv_t)-1 )
            return psz_input;
      }

      size_t i_input = strlen( psz_input );
      size_t i_output = i_input * 6;
      char *psz_output = cLmalloc(char, i_output);
      char *p = psz_output;
      if ((long )iconv( libcLdvb::conf_iconv, &psz_input, &i_input, &p, &i_output ) == -1 )
      {
         free( psz_output );
         return psz_input;
      }

      free(psz_input);
      return psz_output;
#else
      cLbugf(cL::dbg_dvb, "unable to convert from %s to %s (iconv is not available)\n",
            libcLdvb::psz_native_charset, libcLdvb::psz_dvb_charset );
      return psz_input;
#endif
   }

   //static
   void config_strdvb( dvb_string_t *p_dvb_string, const char *psz_string )
   {
      if (psz_string == NULL)
      {
         dvb_string_init(p_dvb_string);
         return;
      }

      char *psz_iconv = config_striconv(psz_string);
      dvb_string_clean(p_dvb_string);
      p_dvb_string->p = dvb_string_set((uint8_t *)psz_iconv, strlen(psz_iconv), libcLdvb::psz_dvb_charset, &p_dvb_string->i);
   }


   //static
   void config_Print( output_config_t *p_config )
   {
      if ( p_config->b_passthrough ) {
         cLbugf(cL::dbg_dvb, "conf: %s config=0x%"PRIx64" sid=*\n", p_config->psz_displayname, p_config->i_config);
         return;
      }

      const char *psz_base = "conf: %s config=0x%"PRIx64" sid=%hu pids[%d]=";
      size_t i_len = strlen(psz_base) + 6 * p_config->i_nb_pids + 2;
      char psz_format[i_len];
      int i, j = strlen(psz_base);

      strcpy( psz_format, psz_base );
      for ( i = 0; i < p_config->i_nb_pids; i++ )
         j += sprintf( psz_format + j, "%u,", p_config->pi_pids[i] );
      psz_format[j - 1] = '\n';
      psz_format[j] = '\0';

      cLbugf(cL::dbg_dvb, psz_format, p_config->psz_displayname, p_config->i_config, p_config->i_sid, p_config->i_nb_pids );
   }

   // static
   bool config_ParseHost( output_config_t *p_config, char *psz_string )
   {
      struct addrinfo *p_ai;
      int i_mtu;

      p_config->psz_displayname = strdup( psz_string );

      p_ai = ParseNodeService( psz_string, &psz_string, DEFAULT_PORT );
      if ( p_ai == NULL ) return false;
      memcpy( &p_config->connect_addr, p_ai->ai_addr, p_ai->ai_addrlen );
      freeaddrinfo( p_ai );

      p_config->i_family = p_config->connect_addr.ss_family;
      if ( p_config->i_family == AF_UNSPEC ) {
         cLbug(cL::dbg_dvb, "family AF_UNSPEC, return false\n");
         return false;
      }

      if ( psz_string == NULL || !*psz_string ) {
         cLbug(cL::dbg_dvb, "string null, return false\n");
         goto end;
      }

      if ( *psz_string == '@' ) {
         psz_string++;
         p_ai = ParseNodeService( psz_string, &psz_string, 0 );
         if ( p_ai == NULL || p_ai->ai_family != p_config->i_family )
            cLbug(cL::dbg_dvb, "invalid bind address\n" );
         else
            memcpy( &p_config->bind_addr, p_ai->ai_addr, p_ai->ai_addrlen );
         freeaddrinfo( p_ai );
      }

      while ( (psz_string = strchr( psz_string, '/' )) != NULL )
      {
         *psz_string++ = '\0';

#define IS_OPTION( option ) (!strncasecmp( psz_string, option, strlen(option) ))
#define ARG_OPTION( option ) (psz_string + strlen(option))

         if ( IS_OPTION("udp") )
            p_config->i_config |= OUTPUT_UDP;
         else if ( IS_OPTION("dvb") )
            p_config->i_config |= OUTPUT_DVB;
         else if ( IS_OPTION("epg") )
            p_config->i_config |= OUTPUT_EPG;
         else if ( IS_OPTION("tsid=") )
            p_config->i_tsid = strtol( ARG_OPTION("tsid="), NULL, 0 );
         else if ( IS_OPTION("retention=") )
            p_config->i_max_retention = strtoll( ARG_OPTION("retention="),
                  NULL, 0 ) * 1000;
         else if ( IS_OPTION("latency=") )
            p_config->i_output_latency = strtoll( ARG_OPTION("latency="),
                  NULL, 0 ) * 1000;
         else if ( IS_OPTION("ttl=") )
            p_config->i_ttl = strtol( ARG_OPTION("ttl="), NULL, 0 );
         else if ( IS_OPTION("tos=") )
            p_config->i_tos = strtol( ARG_OPTION("tos="), NULL, 0 );
         else if ( IS_OPTION("mtu=") )
            p_config->i_mtu = strtol( ARG_OPTION("mtu="), NULL, 0 );
         else if ( IS_OPTION("ifindex=") )
            p_config->i_if_index_v6 = strtol( ARG_OPTION("ifindex="), NULL, 0 );
         else if ( IS_OPTION("networkid=") )
            p_config->i_network_id = strtol( ARG_OPTION("networkid="), NULL, 0 );
         else if ( IS_OPTION("networkname=")  )
         {
            config_strdvb( &p_config->network_name, ARG_OPTION("networkname=") );
         }
         else if ( IS_OPTION("srvname=")  )
         {
            config_strdvb( &p_config->service_name, ARG_OPTION("srvname=") );
         }
         else if ( IS_OPTION("srvprovider=") )
         {
            config_strdvb( &p_config->provider_name, ARG_OPTION("srvprovider=") );
         }
         else if ( IS_OPTION("srcaddr=") )
         {
            if ( p_config->i_family != AF_INET ) {
               cLbug(cL::dbg_dvb, "RAW sockets currently implemented for ipv4 only\n");
               return false;
            }
            free( p_config->psz_srcaddr );
            p_config->psz_srcaddr = config_stropt( ARG_OPTION("srcaddr=") );
            p_config->i_config |= OUTPUT_RAW;
         }
         else if ( IS_OPTION("srcport=") )
            p_config->i_srcport = strtol( ARG_OPTION("srcport="), NULL, 0 );
         else if ( IS_OPTION("ssrc=") )
         {
            in_addr_t i_addr = inet_addr( ARG_OPTION("ssrc=") );
            memcpy( p_config->pi_ssrc, &i_addr, 4 * sizeof(uint8_t) );
         }
         else if ( IS_OPTION("pidmap=") )
         {
            char *str1;
            char *saveptr = NULL;
            char *tok = NULL;
            int i, i_newpid;
            for (i = 0, str1 = config_stropt( (ARG_OPTION("pidmap="))); i < CLDVB_N_MAP_PIDS; i++, str1 = NULL)
            {
               tok = strtok_r(str1, ",", &saveptr);
               if ( !tok )
                  break;
               i_newpid = strtoul(tok, NULL, 0);
               p_config->pi_confpids[i] = i_newpid;
            }
            p_config->b_do_remap = true;
         }
         else if ( IS_OPTION("newsid=") )
            p_config->i_new_sid = strtol( ARG_OPTION("newsid="), NULL, 0 );
         else
            cLbugf(cL::dbg_dvb, "unrecognized option %s\n", psz_string );

#undef IS_OPTION
#undef ARG_OPTION
      }

      end:
      i_mtu = p_config->i_family == AF_INET6 ? DEFAULT_IPV6_MTU :
            DEFAULT_IPV4_MTU;

      if ( !p_config->i_mtu )
         p_config->i_mtu = i_mtu;
      else if ( p_config->i_mtu < TS_SIZE + RTP_HEADER_SIZE )
      {
         cLbugf(cL::dbg_dvb, "invalid MTU %d, setting %d\n", p_config->i_mtu, i_mtu );
         p_config->i_mtu = i_mtu;
      }

      return true;
   }

   // static
   void config_Defaults( output_config_t *p_config )
   {
      config_Init( p_config );

      p_config->i_config = (b_udp_global ? OUTPUT_UDP : 0) |
            (b_dvb_global ? OUTPUT_DVB : 0) |
            (b_epg_global ? OUTPUT_EPG : 0);
      p_config->i_max_retention = i_retention_global;
      p_config->i_output_latency = i_latency_global;
      p_config->i_tsid = -1;
      p_config->i_ttl = i_ttl_global;
      memcpy( p_config->pi_ssrc, pi_ssrc_global, 4 * sizeof(uint8_t) );
      dvb_string_copy(&p_config->network_name, &network_name);
      dvb_string_copy(&p_config->provider_name, &provider_name);
   }

   /*****************************************************************************
    * RawFillHeaders - fill ipv4/udp headers for RAW socket
    *****************************************************************************/
   static void RawFillHeaders(struct udprawpkt *dgram,
         in_addr_t ipsrc, in_addr_t ipdst,
         uint16_t portsrc, uint16_t portdst,
         uint8_t ttl, uint8_t tos, uint16_t len)
   {
      struct libcLdvb_iphdr *iph = &(dgram->iph);
      struct udpheader *udph = &(dgram->udph);

#ifdef DEBUG_SOCKET
      char ipsrc_str[16], ipdst_str[16];
      struct in_addr insrc, indst;
      insrc.s_addr = ipsrc;
      indst.s_addr = ipdst;
      strncpy(ipsrc_str, inet_ntoa(insrc), 16);
      strncpy(ipdst_str, inet_ntoa(indst), 16);
      printf("Filling raw header (%p) (%s:%u -> %s:%u)\n", dgram, ipsrc_str, portsrc, ipdst_str, portdst);
#endif

      // Fill ip header
      iph->ihl      = 5;              // ip header with no specific option
      iph->version  = 4;
      iph->tos      = tos;
      iph->tot_len  = sizeof(struct udprawpkt) + len; // auto-htoned ?
      iph->id       = htons(0);       // auto-generated if frag_off (flags) = 0 ?
      iph->frag_off = 0;
      iph->ttl      = ttl;
      iph->protocol = IPPROTO_UDP;
      iph->check    = 0;
      iph->saddr    = ipsrc;
      iph->daddr    = ipdst;

      // Fill udp header
      udph->source = htons(portsrc);
      udph->dest   = htons(portdst);
      udph->len    = htons(sizeof(struct udpheader) + len);
      udph->check  = 0;

      // Compute ip header checksum. Computed by kernel when frag_off = 0 ?
      //iph->check = csum((unsigned short *)iph, sizeof(struct libcLdvb_iphdr));
   }

   /*****************************************************************************
    * output_BlockCount
    *****************************************************************************/
   static int output_BlockCount( output_t *p_output )
   {
      int i_mtu = p_output->config.i_mtu;
      if ( !(p_output->config.i_config & OUTPUT_UDP) )
         i_mtu -= RTP_HEADER_SIZE;
      return i_mtu / TS_SIZE;
   }

   /*****************************************************************************
    * output_PacketNew
    *****************************************************************************/
   packet_t *output_PacketNew( output_t *p_output )
   {
      packet_t *p_packet;

      if (p_output->i_packet_count)
      {
         p_packet = p_output->p_packet_lifo;
         p_output->p_packet_lifo = p_packet->p_next;
         p_output->i_packet_count--;
      }
      else
      {
         p_packet = (packet_t *)malloc(sizeof(packet_t) + output_BlockCount(p_output) * sizeof(block_t *));
      }

      p_packet->i_depth = 0;
      p_packet->p_next = NULL;
      return p_packet;
   }

   /*****************************************************************************
    * output_PacketDelete
    *****************************************************************************/
   void output_PacketDelete( output_t *p_output, packet_t *p_packet )
   {
      if (p_output->i_packet_count >= CLDVB_OUTPUT_MAX_PACKETS )
      {
         free( p_packet );
         return;
      }

      p_packet->p_next = p_output->p_packet_lifo;
      p_output->p_packet_lifo = p_packet;
      p_output->i_packet_count++;
   }

   /*****************************************************************************
    * output_PacketVacuum
    *****************************************************************************/
   void output_PacketVacuum( output_t *p_output )
   {
      while (p_output->i_packet_count)
      {
         packet_t *p_packet = p_output->p_packet_lifo;
         p_output->p_packet_lifo = p_packet->p_next;
         free(p_packet);
         p_output->i_packet_count--;
      }
   }

   /*****************************************************************************
    * output_Create : create and insert the output_t structure
    *****************************************************************************/
   output_t *output_Create( const output_config_t *p_config )
   {
      int i;
      output_t *p_output = NULL;

      for ( i = 0; i < i_nb_outputs; i++ )
      {
         if ( !( pp_outputs[i]->config.i_config & OUTPUT_VALID ) )
         {
            p_output = pp_outputs[i];
            break;
         }
      }

      if ( p_output == NULL )
      {
         p_output = cLmalloc(output_t, 1);
         i_nb_outputs++;
         pp_outputs = (output_t **)realloc(pp_outputs, i_nb_outputs * sizeof(output_t *));
         pp_outputs[i] = p_output;
      }

      if ( output_Init( p_output, p_config ) < 0 )
         return NULL;

      return p_output;
   }

   /* Init the mapped pids to unused */
   void init_pid_mapping( output_t *p_output )
   {
      unsigned int i;
      for ( i = 0; i < MAX_PIDS; i++ ) {
         p_output->pi_newpids[i]  = UNUSED_PID;
         p_output->pi_freepids[i] = UNUSED_PID;
      }
   }

   /*****************************************************************************
    * output_Init : set up the output initial config
    *****************************************************************************/
   int output_Init( output_t *p_output, const output_config_t *p_config )
   {
      socklen_t i_sockaddr_len = (p_config->i_family == AF_INET) ?
            sizeof(struct sockaddr_in) :
            sizeof(struct sockaddr_in6);

      memset( p_output, 0, sizeof(output_t) );
      config_Init( &p_output->config );

      /* Init run-time values */
      p_output->p_packets = p_output->p_last_packet = NULL;
      p_output->p_packet_lifo = NULL;
      p_output->i_packet_count = 0;
      p_output->i_seqnum = rand() & 0xffff;
      p_output->i_pat_cc = rand() & 0xf;
      p_output->i_pmt_cc = rand() & 0xf;
      p_output->i_nit_cc = rand() & 0xf;
      p_output->i_sdt_cc = rand() & 0xf;
      p_output->i_eit_cc = rand() & 0xf;
      p_output->i_pat_version = rand() & 0xff;
      p_output->i_pmt_version = rand() & 0xff;
      p_output->i_nit_version = rand() & 0xff;
      p_output->i_sdt_version = rand() & 0xff;
      p_output->p_pat_section = NULL;
      p_output->p_pmt_section = NULL;
      p_output->p_nit_section = NULL;
      p_output->p_sdt_section = NULL;
      p_output->p_eit_ts_buffer = NULL;
      if ( b_random_tsid )
         p_output->i_tsid = rand() & 0xffff;

      /* Init the mapped pids to unused */
      init_pid_mapping( p_output );

      /* Init socket-related fields */
      p_output->config.i_family = p_config->i_family;
      memcpy( &p_output->config.connect_addr, &p_config->connect_addr,
            sizeof(struct sockaddr_storage) );
      memcpy( &p_output->config.bind_addr, &p_config->bind_addr,
            sizeof(struct sockaddr_storage) );
      p_output->config.i_if_index_v6 = p_config->i_if_index_v6;

      if ( (p_config->i_config & OUTPUT_RAW) ) {
         p_output->config.i_config |= OUTPUT_RAW;
         p_output->i_handle = socket( AF_INET, SOCK_RAW, IPPROTO_RAW );
      } else {
         p_output->i_handle = socket( p_config->i_family, SOCK_DGRAM, IPPROTO_UDP );
      }
      if ( p_output->i_handle < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't create socket (%s)\n", strerror(errno) );
         p_output->config.i_config &= ~OUTPUT_VALID;
         return -errno;
      }

      int ret = 0;
      if ( p_config->bind_addr.ss_family != AF_UNSPEC )
      {
         if ( bind( p_output->i_handle, (struct sockaddr *)&p_config->bind_addr,
               i_sockaddr_len ) < 0 )
            cLbugf(cL::dbg_dvb, "couldn't bind socket (%s)\n", strerror(errno) );

         if ( p_config->i_family == AF_INET )
         {
            struct sockaddr_in *p_connect_addr =
                  (struct sockaddr_in *)&p_output->config.connect_addr;
            struct sockaddr_in *p_bind_addr =
                  (struct sockaddr_in *)&p_output->config.bind_addr;

            if ( IN_MULTICAST( ntohl( p_connect_addr->sin_addr.s_addr ) ) )
               ret = setsockopt( p_output->i_handle, IPPROTO_IP,
                     IP_MULTICAST_IF,
                     (void *)&p_bind_addr->sin_addr.s_addr,
                     sizeof(p_bind_addr->sin_addr.s_addr) );
         }
      }

      if ( (p_config->i_config & OUTPUT_RAW) )
      {
         struct sockaddr_in *p_connect_addr =
               (struct sockaddr_in *)&p_output->config.connect_addr;
         RawFillHeaders(&p_output->raw_pkt_header, inet_addr(p_config->psz_srcaddr),
               p_connect_addr->sin_addr.s_addr,
               (uint16_t) p_config->i_srcport, ntohs(p_connect_addr->sin_port),
               p_config->i_ttl, p_config->i_tos, 0);
      }

      if ( p_config->i_family == AF_INET6 && p_config->i_if_index_v6 != -1 )
      {
         struct sockaddr_in6 *p_addr =
               (struct sockaddr_in6 *)&p_output->config.connect_addr;
         if ( IN6_IS_ADDR_MULTICAST( &p_addr->sin6_addr ) )
            ret = setsockopt( p_output->i_handle, IPPROTO_IPV6,
                  IPV6_MULTICAST_IF,
                  (void *)&p_config->i_if_index_v6,
                  sizeof(p_config->i_if_index_v6) );
      }

      if (ret == -1)
         cLbugf(cL::dbg_dvb, "couldn't join multicast address (%s)\n",
               strerror(errno) );

      if ( connect( p_output->i_handle,
            (struct sockaddr *)&p_output->config.connect_addr,
            i_sockaddr_len ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't connect socket (%s)\n", strerror(errno) );
         close( p_output->i_handle );
         p_output->config.i_config &= ~OUTPUT_VALID;
         return -errno;
      }

      p_output->config.i_config |= OUTPUT_VALID;

      return 0;
   }

   /*****************************************************************************
    * output_Close
    *****************************************************************************/
   void output_Close( output_t *p_output )
   {
      packet_t *p_packet = p_output->p_packets;
      while ( p_packet != NULL )
      {
         int i;

         for ( i = 0; i < p_packet->i_depth; i++ )
         {
            p_packet->pp_blocks[i]->i_refcount--;
            if ( !p_packet->pp_blocks[i]->i_refcount )
               block_Delete( p_packet->pp_blocks[i] );
         }
         p_output->p_packets = p_packet->p_next;
         output_PacketDelete( p_output, p_packet );
         p_packet = p_output->p_packets;
      }
      output_PacketVacuum( p_output );

      p_output->p_packets = p_output->p_last_packet = NULL;
      free( p_output->p_pat_section );
      free( p_output->p_pmt_section );
      free( p_output->p_nit_section );
      free( p_output->p_sdt_section );
      free( p_output->p_eit_ts_buffer );
      p_output->config.i_config &= ~OUTPUT_VALID;

      close( p_output->i_handle );

      config_Free( &p_output->config );
   }

   /*****************************************************************************
    * output_Flush
    *****************************************************************************/
   static void output_Flush( output_t *p_output )
   {
      packet_t *p_packet = p_output->p_packets;
      int i_block_cnt = output_BlockCount( p_output );
      struct iovec p_iov[i_block_cnt + 2];
      uint8_t p_rtp_hdr[RTP_HEADER_SIZE];
      int i_iov = 0, i_payload_len, i_block;

      if ( (p_output->config.i_config & OUTPUT_RAW) )
      {
         p_iov[i_iov].iov_base = &p_output->raw_pkt_header;
         p_iov[i_iov].iov_len = sizeof(struct udprawpkt);
         i_iov++;
      }

      if ( !(p_output->config.i_config & OUTPUT_UDP) )
      {
         p_iov[i_iov].iov_base = p_rtp_hdr;
         p_iov[i_iov].iov_len = sizeof(p_rtp_hdr);

         rtp_set_hdr( p_rtp_hdr );
         rtp_set_type( p_rtp_hdr, RTP_TYPE_TS );
         rtp_set_seqnum( p_rtp_hdr, p_output->i_seqnum++ );
         /* New timestamp based only on local time when sent */
         /* 90 kHz clock = 90000 counts per second */
         rtp_set_timestamp( p_rtp_hdr, i_wallclock * 9 / 100);
         rtp_set_ssrc( p_rtp_hdr, p_output->config.pi_ssrc );

         i_iov++;
      }

      for ( i_block = 0; i_block < p_packet->i_depth; i_block++ )
      {
         /* Do pid mapping here if needed.
          * save the original pid in the block.
          * set the pid to the new pid
          * later we re-instate the old pid for the next output
          */
         if ( b_do_remap || p_output->config.b_do_remap ) {
            block_t *p_block = p_packet->pp_blocks[i_block];
            uint16_t i_pid = ts_get_pid( p_block->p_ts );
            p_block->tmp_pid = UNUSED_PID;
            if ( p_output->pi_newpids[i_pid] != UNUSED_PID ) {
               uint16_t i_newpid = p_output->pi_newpids[i_pid];
               /* Need to map this pid to the new pid */
               ts_set_pid( p_block->p_ts, i_newpid );
               p_block->tmp_pid = i_pid;
            }
         }

         p_iov[i_iov].iov_base = p_packet->pp_blocks[i_block]->p_ts;
         p_iov[i_iov].iov_len = TS_SIZE;
         i_iov++;
      }

      for ( ; i_block < i_block_cnt; i_block++ )
      {
         p_iov[i_iov].iov_base = p_pad_ts;
         p_iov[i_iov].iov_len = TS_SIZE;
         i_iov++;
      }


      if ( (p_output->config.i_config & OUTPUT_RAW) )
      {
         i_payload_len = 0;
         for ( i_block = 1; i_block < i_iov; i_block++ ) {
            i_payload_len += p_iov[i_block].iov_len; 
         }
         p_output->raw_pkt_header.udph.len = htons(sizeof(struct udpheader) + i_payload_len);
      }

      if ( writev( p_output->i_handle, p_iov, i_iov ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't writev to %s (%s)\n",
               p_output->config.psz_displayname, strerror(errno) );
      }
      /* Update the wallclock because writev() can take some time. */
      i_wallclock = libcLdvb::mdate();

      for ( i_block = 0; i_block < p_packet->i_depth; i_block++ )
      {
         p_packet->pp_blocks[i_block]->i_refcount--;
         if ( !p_packet->pp_blocks[i_block]->i_refcount )
            block_Delete( p_packet->pp_blocks[i_block] );
         else if ( b_do_remap || p_output->config.b_do_remap ) {
            /* still referenced so re-instate the orignial pid if remapped */
            block_t * p_block = p_packet->pp_blocks[i_block];
            if (p_block->tmp_pid != UNUSED_PID)
               ts_set_pid( p_block->p_ts, p_block->tmp_pid );
         }
      }
      p_output->p_packets = p_packet->p_next;
      output_PacketDelete( p_output, p_packet );
      if ( p_output->p_packets == NULL )
         p_output->p_last_packet = NULL;
   }

   /*****************************************************************************
    * output_Put : called from demux
    *****************************************************************************/
   void output_Put( output_t *p_output, block_t *p_block )
   {

      int i_block_cnt = output_BlockCount( p_output );
      packet_t *p_packet;

      p_block->i_refcount++;

      if ( (p_output->p_last_packet != NULL) && (p_output->p_last_packet->i_depth < i_block_cnt) && ((p_output->p_last_packet->i_dts + p_output->config.i_max_retention) > p_block->i_dts) ) {
         p_packet = p_output->p_last_packet;
         if ( ts_has_adaptation( p_block->p_ts ) && ts_get_adaptation( p_block->p_ts ) && tsaf_has_pcr( p_block->p_ts ) ) {
            p_packet->i_dts = p_block->i_dts;
         }
      } else {
         p_packet = output_PacketNew( p_output );
         p_packet->i_dts = p_block->i_dts;
         if ( p_output->p_last_packet != NULL ) {
            p_output->p_last_packet->p_next = p_packet;
         } else {
            p_output->p_packets = p_packet;
         }
         p_output->p_last_packet = p_packet;
      }

      p_packet->pp_blocks[p_packet->i_depth] = p_block;
      p_packet->i_depth++;

      if (i_next_send > p_packet->i_dts + p_output->config.i_output_latency) {
         i_next_send = p_packet->i_dts + p_output->config.i_output_latency;
         cLev_timer_stop(libcLdvb::event_loop, &output_watcher);
         cLev_timer_set(&output_watcher, (i_next_send - i_wallclock) / 1000000., 0);
         cLev_timer_start(libcLdvb::event_loop, &output_watcher);
      }
   }

   /*****************************************************************************
    * outputs_Send :
    *****************************************************************************/
   static void outputs_Send(void *loop, void *w, int revents)
   {

      i_wallclock = libcLdvb::mdate();

      do
      {
         int i;
         i_next_send = INT64_MAX;

         if ( output_dup.config.i_config & OUTPUT_VALID )
         {
            while ( output_dup.p_packets != NULL
                  && output_dup.p_packets->i_dts
                  + output_dup.config.i_output_latency <= i_wallclock )
               output_Flush( &output_dup );

            if ( output_dup.p_packets != NULL )
               i_next_send = output_dup.p_packets->i_dts
               + output_dup.config.i_output_latency;
         }

         for ( i = 0; i < i_nb_outputs; i++ )
         {
            output_t *p_output = pp_outputs[i];
            if ( !( p_output->config.i_config & OUTPUT_VALID ) )
               continue;

            while ( p_output->p_packets != NULL
                  && p_output->p_packets->i_dts
                  + p_output->config.i_output_latency <= i_wallclock )
               output_Flush( p_output );

            if ( p_output->p_packets != NULL
                  && (p_output->p_packets->i_dts
                        + p_output->config.i_output_latency < i_next_send) )
               i_next_send = p_output->p_packets->i_dts
               + p_output->config.i_output_latency;
         }
      }
      while (i_next_send <= i_wallclock);

      if (i_next_send < INT64_MAX) {
         cLev_timer_set(&output_watcher, (i_next_send - i_wallclock) / 1000000., 0);
         cLev_timer_start(loop, &output_watcher);
      }
   }

   /*****************************************************************************
    * outputs_Init :
    *****************************************************************************/
   void outputs_Init( void )
   {
      i_wallclock = libcLdvb::mdate();
      cLev_timer_init(&output_watcher, outputs_Send, 0, 0);
   }

   /*****************************************************************************
    * output_Find : find an existing output from a given output_config_t
    *****************************************************************************/
   output_t *output_Find( const output_config_t *p_config )
   {
      socklen_t i_sockaddr_len = (p_config->i_family == AF_INET) ?
            sizeof(struct sockaddr_in) :
            sizeof(struct sockaddr_in6);
      int i;

      for ( i = 0; i < i_nb_outputs; i++ )
      {
         output_t *p_output = pp_outputs[i];

         if ( !(p_output->config.i_config & OUTPUT_VALID) ) continue;

         if ( p_config->i_family != p_output->config.i_family ||
               memcmp( &p_config->connect_addr, &p_output->config.connect_addr,
                     i_sockaddr_len ) ||
                     memcmp( &p_config->bind_addr, &p_output->config.bind_addr,
                           i_sockaddr_len ) )
            continue;

         if ( p_config->i_family == AF_INET6 &&
               p_config->i_if_index_v6 != p_output->config.i_if_index_v6 )
            continue;

         if ( (p_config->i_config ^ p_output->config.i_config) & OUTPUT_RAW ) {
            continue;
         }

         return p_output;
      }

      return NULL;
   }

   /*****************************************************************************
    * output_Change : get changes from a new output_config_t
    *****************************************************************************/
   void output_Change( output_t *p_output, const output_config_t *p_config )
   {
      int ret = 0;
      memcpy( p_output->config.pi_ssrc, p_config->pi_ssrc, 4 * sizeof(uint8_t) );
      p_output->config.i_output_latency = p_config->i_output_latency;
      p_output->config.i_max_retention = p_config->i_max_retention;

      if ( p_output->config.i_ttl != p_config->i_ttl )
      {
         if ( p_output->config.i_family == AF_INET6 )
         {
            struct sockaddr_in6 *p_addr =
                  (struct sockaddr_in6 *)&p_output->config.connect_addr;
            if ( IN6_IS_ADDR_MULTICAST( &p_addr->sin6_addr ) )
               ret = setsockopt( p_output->i_handle, IPPROTO_IPV6,
                     IPV6_MULTICAST_HOPS, (void *)&p_config->i_ttl,
                     sizeof(p_config->i_ttl) );
         }
         else
         {
            struct sockaddr_in *p_addr =
                  (struct sockaddr_in *)&p_output->config.connect_addr;
            if ( IN_MULTICAST( ntohl( p_addr->sin_addr.s_addr ) ) )
               ret = setsockopt( p_output->i_handle, IPPROTO_IP,
                     IP_MULTICAST_TTL, (void *)&p_config->i_ttl,
                     sizeof(p_config->i_ttl) );
         }
         p_output->config.i_ttl = p_config->i_ttl;
         p_output->raw_pkt_header.iph.ttl = p_config->i_ttl;
      }

      if ( p_output->config.i_tos != p_config->i_tos )
      {
         if ( p_output->config.i_family == AF_INET )
            ret = setsockopt( p_output->i_handle, IPPROTO_IP, IP_TOS,
                  (void *)&p_config->i_tos,
                  sizeof(p_config->i_tos) );
         p_output->config.i_tos = p_config->i_tos;
         p_output->raw_pkt_header.iph.tos = p_config->i_tos;
      }

      if (ret == -1)
         cLbugf(cL::dbg_dvb, "couldn't change socket (%s)\n", strerror(errno) );

      if ( p_output->config.i_mtu != p_config->i_mtu
            || ((p_output->config.i_config ^ p_config->i_config) & OUTPUT_UDP) )
      {
         int i_block_cnt;
         packet_t *p_packet = p_output->p_last_packet;
         p_output->config.i_config &= ~OUTPUT_UDP;
         p_output->config.i_config |= p_config->i_config & OUTPUT_UDP;
         p_output->config.i_mtu = p_config->i_mtu;

         output_PacketVacuum( p_output );

         i_block_cnt = output_BlockCount( p_output );
         if ( p_packet != NULL && p_packet->i_depth < i_block_cnt )
         {
            p_packet = (packet_t *)realloc(p_packet, sizeof(packet_t *) + i_block_cnt * sizeof(block_t *));
            p_output->p_last_packet = p_packet;
         }
      }

      if ( p_config->i_config & OUTPUT_RAW ) {
         p_output->raw_pkt_header.iph.saddr = inet_addr(p_config->psz_srcaddr);
         p_output->raw_pkt_header.udph.source = htons(p_config->i_srcport);
      }
   }

   /*****************************************************************************
    * outputs_Close : Close all outputs and free allocated memory
    *****************************************************************************/
   void outputs_Close( int i_num_outputs )
   {
      int i;

      for ( i = 0; i < i_num_outputs; i++ )
      {
         output_t *p_output = pp_outputs[i];

         if ( p_output->config.i_config & OUTPUT_VALID )
         {
            cLbugf(cL::dbg_dvb, "removing %s\n", p_output->config.psz_displayname );

            if ( p_output->p_packets )
               output_Flush( p_output );
            output_Close( p_output );
         }

         free( p_output );
      }

      free( pp_outputs );
   }

} /* namespace libcLdvboutput */

