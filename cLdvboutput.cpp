/*
 * cLdvboutput.cpp
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
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
#include <ctype.h> //isascii

cLdvboutput::cLdvboutput()
{
   this->i_next_send = INT64_MAX;
   this->i_wallclock = 0;
   this->p_block_lifo = (cLdvboutput::block_t *) 0;
   this->i_block_count = 0;
   #ifdef HAVE_CLICONV
   this->conf_iconv = (iconv_t)-1;
   this->iconv_handle = (iconv_t)-1;
   #endif
   for (int i = 0; i < TS_SIZE; i++)
      this->p_pad_ts[i] = 0xff;
   this->p_pad_ts[0] = 0x47;
   this->p_pad_ts[1] = 0x1f;
   this->p_pad_ts[3] = 0x10;

   this->psz_dvb_charset = "UTF-8//IGNORE";
   this->b_random_tsid = 0;
   this->b_do_remap = false;
   this->pp_outputs = (output_t **) 0;
   this->i_nb_outputs = 0;
   this->b_udp_global = false;
   this->b_dvb_global = false;
   this->b_epg_global = false;
   this->i_latency_global = DEFAULT_OUTPUT_LATENCY;
   this->i_retention_global = DEFAULT_MAX_RETENTION;
   this->i_ttl_global = 64;
   for (int i = 0; i < 4; i++)
      this->pi_ssrc_global[i] = 0;
   this->i_network_id = 0xffff;
   this->psz_dup_config = (char *) 0;
   this->output_dup = new output_t;
   memset(this->output_dup, 0, sizeof(cLdvboutput::output_t));
   cLbug(cL::dbg_high, "cLdvboutput created\n");
}

cLdvboutput::~cLdvboutput()
{
   delete(this->output_dup);
   cLbug(cL::dbg_high, "cLdvboutput deleted\n");
}

cLdvboutput::block_t *cLdvboutput::block_New()
{
   block_t *p_block;

   if (this->i_block_count) {
      p_block = this->p_block_lifo;
      this->p_block_lifo = p_block->p_next;
      this->i_block_count--;
   } else {
      p_block = cLmalloc(block_t, 1);
   }

   p_block->p_next = (block_t *) 0;
   p_block->i_refcount = 1;
   return p_block;
}

void cLdvboutput::block_Delete(block_t *p_block)
{
   if (this->i_block_count >= CLDVB_MAX_BLOCKS) {
      ::free(p_block);
      return;
   }
   p_block->p_next = this->p_block_lifo;
   this->p_block_lifo = p_block;
   this->i_block_count++;
}

void cLdvboutput::block_DeleteChain(block_t *p_block)
{
   while (p_block != (block_t *) 0) {
      block_t *p_next = p_block->p_next;
      this->block_Delete(p_block);
      p_block = p_next;
   }
}

void cLdvboutput::block_Vacuum()
{
   while (this->i_block_count) {
      block_t *p_block = this->p_block_lifo;
      this->p_block_lifo = p_block->p_next;
      ::free(p_block);
      this->i_block_count--;
   }
}

void cLdvboutput::dvb_string_init(dvb_string_t *p_dvb_string)
{
   p_dvb_string->p = (uint8_t *) 0;
   p_dvb_string->i = 0;
}

void cLdvboutput::dvb_string_clean(dvb_string_t *p_dvb_string)
{
   free(p_dvb_string->p);
}

void cLdvboutput::dvb_string_copy(dvb_string_t *p_dst, const dvb_string_t *p_src)
{
   p_dst->p = cLmalloc(uint8_t, p_src->i);
   memcpy(p_dst->p, p_src->p, p_src->i);
   p_dst->i = p_src->i;
}

int cLdvboutput::dvb_string_cmp(const dvb_string_t *p_1, const dvb_string_t *p_2)
{
   if (p_1->i != p_2->i)
      return p_1->i - p_2->i;
   return memcmp(p_1->p, p_2->p, p_1->i);
}

char *cLdvboutput::config_stropt(const char *psz_string)
{
   char *ret, *tmp;
   if (!psz_string || strlen(psz_string) == 0)
      return (char *) 0;
   ret = tmp = strdup(psz_string);
   while (*tmp) {
      if (*tmp == '_')
         *tmp = ' ';
      if (*tmp == '/') {
         *tmp = '\0';
         break;
      }
      ++tmp;
   }
   return ret;
}

uint8_t *cLdvboutput::config_striconv(const char *psz_string, const char *psz_charset, size_t *pi_length)
{
   char *psz_input = this->config_stropt(psz_string);
   *pi_length = strlen(psz_input);

   /* do not convert ASCII strings */
   const char *c = psz_string;
   while (*c)
      if (!isascii(*c++))
         break;
   if (!*c)
      return (uint8_t *)psz_input;

   if (!strcasecmp(this->psz_native_charset, psz_charset))
      return (uint8_t *)psz_input;

#ifdef HAVE_CLICONV
   if (this->conf_iconv == (iconv_t) -1) {
      this->conf_iconv = iconv_open(psz_charset, this->psz_native_charset);
      if (this->conf_iconv == (iconv_t) -1)
         return (uint8_t *)psz_input;
   }
   char *psz_tmp = psz_input;
   size_t i_input = *pi_length;
   size_t i_output = i_input * 6;
   size_t i_available = i_output;
   char *p_output = cLmalloc(char, i_output);
   char *p = p_output;
   if (iconv(conf_iconv, &psz_tmp, &i_input, &p, &i_available) == (size_t)-1) {
      free(p_output);
      return (uint8_t *)psz_input;
   }
   ::free(psz_input);
   *pi_length = i_output - i_available;
   return (uint8_t *)p_output;
#else
   cLbugf(cL::dbg_dvb, "unable to convert from %s to %s (iconv is not available)\n", this->psz_native_charset, psz_charset);
   return (uint8_t *)psz_input;
#endif
}

void cLdvboutput::config_strdvb(dvb_string_t *p_dvb_string, const char *psz_string, const char *psz_charset)
{
   if (psz_string == (const char *) 0) {
      this->dvb_string_init(p_dvb_string);
      return;
   }
   this->dvb_string_clean(p_dvb_string);
   size_t i_iconv;
   uint8_t *p_iconv = config_striconv(psz_string, psz_charset, &i_iconv);
   p_dvb_string->p = dvb_string_set(p_iconv, i_iconv, psz_charset, &p_dvb_string->i);
   ::free(p_iconv);
}

void cLdvboutput::config_Init(output_config_t *p_config)
{
   memset(p_config, 0, sizeof(output_config_t));

   p_config->psz_displayname = (char *) 0;
   p_config->i_network_id = this->i_network_id;
   this->dvb_string_init(&p_config->network_name);
   this->dvb_string_init(&p_config->service_name);
   this->dvb_string_init(&p_config->provider_name);
   p_config->psz_srcaddr = (char *) 0;

   p_config->i_family = AF_UNSPEC;
   p_config->connect_addr.ss_family = AF_UNSPEC;
   p_config->bind_addr.ss_family = AF_UNSPEC;
   p_config->i_if_index_v6 = -1;
   p_config->i_srcport = 0;

   p_config->pi_pids = (uint16_t *) 0;
   p_config->b_passthrough = false;
   p_config->b_do_remap = false;

   for (unsigned int i = 0; i < CLDVB_N_MAP_PIDS; i++)
      p_config->pi_confpids[i] = UNUSED_PID;
}

void cLdvboutput::config_Free(output_config_t *p_config)
{
   ::free(p_config->psz_displayname);
   cLdvboutput::dvb_string_clean(&p_config->network_name);
   cLdvboutput::dvb_string_clean(&p_config->service_name);
   cLdvboutput::dvb_string_clean(&p_config->provider_name);
   ::free(p_config->pi_pids);
   ::free(p_config->psz_srcaddr);
}

void cLdvboutput::config_Print(output_config_t *p_config)
{
   if (p_config->b_passthrough) {
      cLbugf(cL::dbg_dvb, "conf: %s config=0x%"PRIx64" sid=*\n", p_config->psz_displayname, p_config->i_config);
      return;
   }

   const char *psz_base = "conf: %s config=0x%"PRIx64" sid=%hu pids[%d]=";
   size_t i_len = strlen(psz_base) + 6 * p_config->i_nb_pids + 2;
   char psz_format[i_len];
   int j = strlen(psz_base);

   strcpy(psz_format, psz_base);
   for (int i = 0; i < p_config->i_nb_pids; i++)
      j += sprintf(psz_format + j, "%u,", p_config->pi_pids[i]);
   psz_format[j - 1] = '\n';
   psz_format[j] = '\0';

   cLbugf(cL::dbg_dvb, psz_format, p_config->psz_displayname, p_config->i_config, p_config->i_sid, p_config->i_nb_pids);
}

void cLdvboutput::config_Defaults(output_config_t *p_config)
{
   this->config_Init(p_config);

   p_config->i_config = (this->b_udp_global ? OUTPUT_UDP : 0) | (this->b_dvb_global ? OUTPUT_DVB : 0) | (this->b_epg_global ? OUTPUT_EPG : 0);
   p_config->i_max_retention = this->i_retention_global;
   p_config->i_output_latency = this->i_latency_global;
   p_config->i_tsid = -1;
   p_config->i_ttl = this->i_ttl_global;
   memcpy(p_config->pi_ssrc, this->pi_ssrc_global, 4 * sizeof(uint8_t));
   this->dvb_string_copy(&p_config->network_name, &this->network_name);
   this->dvb_string_copy(&p_config->provider_name, &this->provider_name);
}

struct addrinfo *cLdvboutput::ParseNodeService(char *_psz_string, char **ppsz_end, uint16_t i_default_port)
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
         ::free(psz_string);
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
      ::free(psz_string);
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
      ::free(psz_string);
      return (struct addrinfo *) 0;
   }

   ::free(psz_string);
   return p_res;
}

bool cLdvboutput::config_ParseHost(output_config_t *p_config, char *psz_string)
{
   struct addrinfo *p_ai;
   int i_mtu;

   p_config->psz_displayname = strdup(psz_string);

   p_ai = this->ParseNodeService(psz_string, &psz_string, DEFAULT_PORT);
   if (p_ai == (struct addrinfo *) 0)
      return false;

   memcpy(&p_config->connect_addr, p_ai->ai_addr, p_ai->ai_addrlen);
   freeaddrinfo(p_ai);

   p_config->i_family = p_config->connect_addr.ss_family;
   if (p_config->i_family == AF_UNSPEC) {
      cLbug(cL::dbg_dvb, "family AF_UNSPEC, return false\n");
      return false;
   }

   const char *psz_charset = this->psz_dvb_charset;
   const char *psz_network_name = (const char *) 0;
   const char *psz_service_name = (const char *) 0;
   const char *psz_provider_name = (const char *) 0;

   if (psz_string == (char *) 0 || !*psz_string) {
      cLbug(cL::dbg_dvb, "string null, return false\n");
      goto end;
   }

   if (*psz_string == '@') {
      psz_string++;
      p_ai = this->ParseNodeService(psz_string, &psz_string, 0);
      if (p_ai == (struct addrinfo *) 0 || p_ai->ai_family != p_config->i_family) {
         cLbug(cL::dbg_dvb, "invalid bind address\n");
      } else {
         memcpy(&p_config->bind_addr, p_ai->ai_addr, p_ai->ai_addrlen);
      }
      freeaddrinfo(p_ai);
   }

#define IS_OPTION(option) (!strncasecmp(psz_string, option, strlen(option)))
#define ARG_OPTION(option) (char *)(psz_string + strlen(option))

   while ((psz_string = strchr(psz_string, '/')) != (char *) 0) {
      *psz_string++ = '\0';
      if (IS_OPTION("udp")) {
         p_config->i_config |= OUTPUT_UDP;
      } else
      if (IS_OPTION("dvb")) {
         p_config->i_config |= OUTPUT_DVB;
      } else
      if (IS_OPTION("epg")) {
         p_config->i_config |= OUTPUT_EPG;
      } else
      if (IS_OPTION("tsid=")) {
         p_config->i_tsid = strtol((const char *)ARG_OPTION("tsid="), (char **) 0, 0);
      } else
      if (IS_OPTION("retention=")) {
         p_config->i_max_retention = strtoll((const char *)ARG_OPTION("retention="), (char **) 0, 0) * 1000;
      } else
      if (IS_OPTION("latency=")) {
         p_config->i_output_latency = strtoll((const char *)ARG_OPTION("latency="), (char **) 0, 0) * 1000;
      } else
      if (IS_OPTION("ttl=")) {
         p_config->i_ttl = strtol((const char *)ARG_OPTION("ttl="), (char **) 0, 0);
      } else
      if (IS_OPTION("tos=")) {
         p_config->i_tos = strtol((const char *)ARG_OPTION("tos="), (char **) 0, 0);
      } else
      if (IS_OPTION("mtu=")) {
         p_config->i_mtu = strtol((const char *)ARG_OPTION("mtu="), (char **) 0, 0);
      } else
      if (IS_OPTION("ifindex=")) {
         p_config->i_if_index_v6 = strtol((const char *)ARG_OPTION("ifindex="), (char **) 0, 0);
      } else
      if (IS_OPTION("networkid=")) {
         p_config->i_network_id = strtol((const char *)ARG_OPTION("networkid="), (char **) 0, 0);
      } else
      if (IS_OPTION("onid=")) {
         p_config->i_onid = strtol((const char *)ARG_OPTION("onid="), (char **) 0, 0 );
      } else
      if (IS_OPTION("charset=")) {
         psz_charset = ARG_OPTION("charset=");
      } else
      if (IS_OPTION("networkname=")) {
         psz_network_name = ARG_OPTION("networkname=");
      } else
      if (IS_OPTION("srvname=")) {
         psz_service_name = ARG_OPTION("srvname=");
      } else
      if (IS_OPTION("srvprovider=")) {
         psz_provider_name = ARG_OPTION("srvprovider=");
      } else
      if (IS_OPTION("srcaddr=")) {
         if (p_config->i_family != AF_INET) {
            cLbug(cL::dbg_dvb, "RAW sockets currently implemented for ipv4 only\n");
            return false;
         }
         ::free(p_config->psz_srcaddr);
         p_config->psz_srcaddr = this->config_stropt((const char *) ARG_OPTION("srcaddr="));
         p_config->i_config |= OUTPUT_RAW;
      } else
      if (IS_OPTION("srcport=")) {
         p_config->i_srcport = strtol((const char *)ARG_OPTION("srcport="), (char **) 0, 0);
      } else
      if (IS_OPTION("ssrc=")) {
         in_addr_t i_addr = inet_addr((const char *)ARG_OPTION("ssrc="));
         memcpy(p_config->pi_ssrc, &i_addr, 4 * sizeof(uint8_t));
      } else
      if (IS_OPTION("pidmap=")) {
         for (int i = 0; i < CLDVB_N_MAP_PIDS; i++) {
            char *str1 = this->config_stropt((ARG_OPTION("pidmap=")));
            char *saveptr = (char *) 0;
            char *tok = strtok_r(str1, ",", &saveptr);
            if (!tok)
               break;
            int i_newpid = strtoul(tok, (char **) 0, 0);
            p_config->pi_confpids[i] = i_newpid;
         }
         p_config->b_do_remap = true;
      } else
      if (IS_OPTION("newsid=")) {
         p_config->i_new_sid = strtol((const char *)ARG_OPTION("newsid="), (char **) 0, 0);
      } else {
         cLbugf(cL::dbg_dvb, "unrecognized option %s\n", psz_string);
      }
   }

   if (psz_network_name != (const char *) 0)
       this->config_strdvb(&p_config->network_name, psz_network_name, psz_charset);
   if (psz_service_name != (const char *) 0)
      this->config_strdvb(&p_config->service_name, psz_service_name, psz_charset);
   if (psz_provider_name != (const char *) 0)
      this->config_strdvb(&p_config->provider_name, psz_provider_name, psz_charset);

   end:
   i_mtu = p_config->i_family == AF_INET6 ? DEFAULT_IPV6_MTU : DEFAULT_IPV4_MTU;
   if (!p_config->i_mtu) {
      p_config->i_mtu = i_mtu;
   } else
   if (p_config->i_mtu < TS_SIZE + RTP_HEADER_SIZE) {
      cLbugf(cL::dbg_dvb, "invalid MTU %d, setting %d\n", p_config->i_mtu, i_mtu);
      p_config->i_mtu = i_mtu;
   }

   return true;
}

// fill ipv4/udp headers for RAW socket
void cLdvboutput::RawFillHeaders(struct udprawpkt *dgram, in_addr_t ipsrc, in_addr_t ipdst, uint16_t portsrc, uint16_t portdst, uint8_t ttl, uint8_t tos, uint16_t len)
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
   cLbugf(cL::dbg_dvb, "Filling raw header (%p) (%s:%u -> %s:%u)\n", dgram, ipsrc_str, portsrc, ipdst_str, portdst);
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

int cLdvboutput::output_BlockCount(output_t *p_output)
{
   int i_mtu = p_output->config.i_mtu;
   if (!(p_output->config.i_config & OUTPUT_UDP))
      i_mtu -= RTP_HEADER_SIZE;
   return i_mtu / TS_SIZE;
}

cLdvboutput::packet_t *cLdvboutput::output_PacketNew(output_t *p_output)
{
   packet_t *p_packet;

   if (p_output->i_packet_count) {
      p_packet = p_output->p_packet_lifo;
      p_output->p_packet_lifo = p_packet->p_next;
      p_output->i_packet_count--;
   } else {
      p_packet = (packet_t *)malloc(sizeof(packet_t) + cLdvboutput::output_BlockCount(p_output) * sizeof(block_t *));
   }

   p_packet->i_depth = 0;
   p_packet->p_next = (struct packet_t *) 0;
   return p_packet;
}

void cLdvboutput::output_PacketDelete(output_t *p_output, packet_t *p_packet)
{
   if (p_output->i_packet_count >= CLDVB_OUTPUT_MAX_PACKETS) {
      ::free(p_packet);
      return;
   }

   p_packet->p_next = p_output->p_packet_lifo;
   p_output->p_packet_lifo = p_packet;
   p_output->i_packet_count++;
}

void cLdvboutput::output_PacketVacuum(output_t *p_output)
{
   while (p_output->i_packet_count) {
      packet_t *p_packet = p_output->p_packet_lifo;
      p_output->p_packet_lifo = p_packet->p_next;
      ::free(p_packet);
      p_output->i_packet_count--;
   }
}

/* Init the mapped pids to unused */
void cLdvboutput::init_pid_mapping(output_t *p_output)
{
   for (unsigned int i = 0; i < MAX_PIDS; i++) {
      p_output->pi_newpids[i]  = UNUSED_PID;
      p_output->pi_freepids[i] = UNUSED_PID;
   }
}

/* set up the output initial config */
int cLdvboutput::output_Init(output_t *p_output, const output_config_t *p_config)
{
   socklen_t i_sockaddr_len = (p_config->i_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

   memset(p_output, 0, sizeof(output_t));
   this->config_Init(&p_output->config);

   /* Init run-time values */
   p_output->p_packets = p_output->p_last_packet = (packet_t *) 0;
   p_output->p_packet_lifo = (packet_t *) 0;
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
   p_output->p_pat_section = (uint8_t *) 0;
   p_output->p_pmt_section = (uint8_t *) 0;
   p_output->p_nit_section = (uint8_t *) 0;
   p_output->p_sdt_section = (uint8_t *) 0;
   p_output->p_eit_ts_buffer = (block_t *) 0;
   if (this->b_random_tsid)
      p_output->i_tsid = rand() & 0xffff;
   p_output->i_pcr_pid = 0;

   /* Init the mapped pids to unused */
   this->init_pid_mapping(p_output);

   /* Init socket-related fields */
   p_output->config.i_family = p_config->i_family;
   memcpy(&p_output->config.connect_addr, &p_config->connect_addr, sizeof(struct sockaddr_storage));
   memcpy(&p_output->config.bind_addr, &p_config->bind_addr, sizeof(struct sockaddr_storage));
   p_output->config.i_if_index_v6 = p_config->i_if_index_v6;

   if ((p_config->i_config & OUTPUT_RAW)) {
      p_output->config.i_config |= OUTPUT_RAW;
      p_output->i_handle = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
   } else {
      p_output->i_handle = socket(p_config->i_family, SOCK_DGRAM, IPPROTO_UDP);
   }

   if (p_output->i_handle < 0) {
      cLbugf(cL::dbg_dvb, "couldn't create socket (%s)\n", strerror(errno));
      p_output->config.i_config &= ~OUTPUT_VALID;
      return -errno;
   }

   int ret = 0;
   if (p_config->bind_addr.ss_family != AF_UNSPEC) {
      if (bind(p_output->i_handle, (struct sockaddr *)&p_config->bind_addr, i_sockaddr_len) < 0)
         cLbugf(cL::dbg_dvb, "couldn't bind socket (%s)\n", strerror(errno));
      if (p_config->i_family == AF_INET) {
         struct sockaddr_in *p_connect_addr = (struct sockaddr_in *)&p_output->config.connect_addr;
         struct sockaddr_in *p_bind_addr = (struct sockaddr_in *)&p_output->config.bind_addr;
         if (IN_MULTICAST(ntohl(p_connect_addr->sin_addr.s_addr)))
            ret = setsockopt(p_output->i_handle, IPPROTO_IP, IP_MULTICAST_IF, (void *)&p_bind_addr->sin_addr.s_addr, sizeof(p_bind_addr->sin_addr.s_addr));
      }
   }

   if ((p_config->i_config & OUTPUT_RAW)) {
      struct sockaddr_in *p_connect_addr = (struct sockaddr_in *)&p_output->config.connect_addr;
      this->RawFillHeaders(&p_output->raw_pkt_header, inet_addr(p_config->psz_srcaddr), p_connect_addr->sin_addr.s_addr, (uint16_t) p_config->i_srcport, ntohs(p_connect_addr->sin_port), p_config->i_ttl, p_config->i_tos, 0);
   }

   if (p_config->i_family == AF_INET6 && p_config->i_if_index_v6 != -1) {
      struct sockaddr_in6 *p_addr = (struct sockaddr_in6 *)&p_output->config.connect_addr;
      if (IN6_IS_ADDR_MULTICAST(&p_addr->sin6_addr))
         ret = setsockopt(p_output->i_handle, IPPROTO_IPV6, IPV6_MULTICAST_IF, (void *)&p_config->i_if_index_v6, sizeof(p_config->i_if_index_v6));
   }

   if (ret == -1)
      cLbugf(cL::dbg_dvb, "couldn't join multicast address (%s)\n", strerror(errno));

   if (connect(p_output->i_handle, (struct sockaddr *)&p_output->config.connect_addr, i_sockaddr_len) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't connect socket (%s)\n", strerror(errno));
      close(p_output->i_handle);
      p_output->config.i_config &= ~OUTPUT_VALID;
      return -errno;
   }

   p_output->config.i_config |= OUTPUT_VALID;
   return 0;
}

/* create and insert the output_t structure */
cLdvboutput::output_t *cLdvboutput::output_Create(const output_config_t *p_config)
{
   int i;
   output_t *p_output = (output_t *) 0;

   for (i = 0; i < this->i_nb_outputs; i++) {
      if (!(this->pp_outputs[i]->config.i_config & OUTPUT_VALID)) {
         p_output = this->pp_outputs[i];
         break;
      }
   }

   if (p_output == (output_t *) 0) {
      p_output = cLmalloc(output_t, 1);
      this->i_nb_outputs++;
      this->pp_outputs = (output_t **)realloc(this->pp_outputs, this->i_nb_outputs * sizeof(output_t *));
      this->pp_outputs[i] = p_output;
   }

   if (this->output_Init(p_output, p_config) < 0)
      return (output_t *) 0;

   return p_output;
}

void cLdvboutput::output_Close(output_t *p_output)
{
   packet_t *p_packet = p_output->p_packets;
   while (p_packet != (packet_t *) 0) {
      for (int i = 0; i < p_packet->i_depth; i++) {
         p_packet->pp_blocks[i]->i_refcount--;
         if (!p_packet->pp_blocks[i]->i_refcount)
            this->block_Delete(p_packet->pp_blocks[i]);
      }
      p_output->p_packets = p_packet->p_next;
      this->output_PacketDelete(p_output, p_packet);
      p_packet = p_output->p_packets;
   }
   this->output_PacketVacuum(p_output);

   p_output->p_packets = p_output->p_last_packet = (packet_t *) 0;
   ::free(p_output->p_pat_section);
   ::free(p_output->p_pmt_section);
   ::free(p_output->p_nit_section);
   ::free(p_output->p_sdt_section);
   ::free(p_output->p_eit_ts_buffer);
   p_output->config.i_config &= ~OUTPUT_VALID;

   close(p_output->i_handle);
   this->config_Free(&p_output->config);
}

void cLdvboutput::output_Flush(output_t *p_output)
{
   packet_t *p_packet = p_output->p_packets;
   int i_block_cnt = this->output_BlockCount(p_output);
   struct iovec p_iov[i_block_cnt + 2];
   uint8_t p_rtp_hdr[RTP_HEADER_SIZE];
   int i_iov = 0;

   if ((p_output->config.i_config & OUTPUT_RAW)) {
      p_iov[i_iov].iov_base = &p_output->raw_pkt_header;
      p_iov[i_iov].iov_len = sizeof(struct udprawpkt);
      i_iov++;
   }

   if (!(p_output->config.i_config & OUTPUT_UDP)) {
      p_iov[i_iov].iov_base = p_rtp_hdr;
      p_iov[i_iov].iov_len = sizeof(p_rtp_hdr);

      rtp_set_hdr(p_rtp_hdr);
      rtp_set_type(p_rtp_hdr, RTP_TYPE_TS);
      rtp_set_seqnum(p_rtp_hdr, p_output->i_seqnum++);
      /* New timestamp based only on local time when sent */
      /* 90 kHz clock = 90000 counts per second */
      rtp_set_timestamp(p_rtp_hdr, this->i_wallclock * 9 / 100);
      rtp_set_ssrc(p_rtp_hdr, p_output->config.pi_ssrc);

      i_iov++;
   }

   int i_block;
   for (i_block = 0; i_block < p_packet->i_depth; i_block++) {
      /* Do pid mapping here if needed.
       * save the original pid in the block.
       * set the pid to the new pid
       * later we re-instate the old pid for the next output
       */
      if (this->b_do_remap || p_output->config.b_do_remap) {
         block_t *p_block = p_packet->pp_blocks[i_block];
         uint16_t i_pid = ts_get_pid(p_block->p_ts);
         p_block->tmp_pid = UNUSED_PID;
         if (p_output->pi_newpids[i_pid] != UNUSED_PID) {
            uint16_t i_newpid = p_output->pi_newpids[i_pid];
            /* Need to map this pid to the new pid */
            ts_set_pid(p_block->p_ts, i_newpid);
            p_block->tmp_pid = i_pid;
         }
      }

      p_iov[i_iov].iov_base = p_packet->pp_blocks[i_block]->p_ts;
      p_iov[i_iov].iov_len = TS_SIZE;
      i_iov++;
   }

   for (; i_block < i_block_cnt; i_block++) {
      p_iov[i_iov].iov_base = this->p_pad_ts;
      p_iov[i_iov].iov_len = TS_SIZE;
      i_iov++;
   }

   if ((p_output->config.i_config & OUTPUT_RAW)) {
      int i_payload_len = 0;
      for (i_block = 1; i_block < i_iov; i_block++) {
         i_payload_len += p_iov[i_block].iov_len;
      }
      p_output->raw_pkt_header.udph.len = htons(sizeof(struct udpheader) + i_payload_len);
   }

   if (writev(p_output->i_handle, p_iov, i_iov) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't writev to %s (%s)\n", p_output->config.psz_displayname, strerror(errno));
   }
   /* Update the wallclock because writev() can take some time. */
   this->i_wallclock = this->mdate();

   for (i_block = 0; i_block < p_packet->i_depth; i_block++) {
      p_packet->pp_blocks[i_block]->i_refcount--;
      if (!p_packet->pp_blocks[i_block]->i_refcount) {
         this->block_Delete(p_packet->pp_blocks[i_block]);
      } else
      if (this->b_do_remap || p_output->config.b_do_remap) {
         /* still referenced so re-instate the orignial pid if remapped */
         block_t *p_block = p_packet->pp_blocks[i_block];
         if (p_block->tmp_pid != UNUSED_PID)
            ts_set_pid(p_block->p_ts, p_block->tmp_pid);
      }
   }
   p_output->p_packets = p_packet->p_next;
   this->output_PacketDelete(p_output, p_packet);
   if (p_output->p_packets == (packet_t *) 0)
      p_output->p_last_packet = (packet_t *) 0;
}

void cLdvboutput::output_Put(output_t *p_output, block_t *p_block)
{
   int i_block_cnt = this->output_BlockCount(p_output);
   packet_t *p_packet;

   p_block->i_refcount++;

   if ((p_output->p_last_packet != (packet_t *) 0) && (p_output->p_last_packet->i_depth < i_block_cnt) && ((p_output->p_last_packet->i_dts + p_output->config.i_max_retention) > p_block->i_dts)) {
      p_packet = p_output->p_last_packet;
      if (ts_has_adaptation(p_block->p_ts) && ts_get_adaptation(p_block->p_ts) && tsaf_has_pcr(p_block->p_ts)) {
         p_packet->i_dts = p_block->i_dts;
      }
   } else {
      p_packet = this->output_PacketNew(p_output);
      p_packet->i_dts = p_block->i_dts;
      if (p_output->p_last_packet != (packet_t *) 0) {
         p_output->p_last_packet->p_next = p_packet;
      } else {
         p_output->p_packets = p_packet;
      }
      p_output->p_last_packet = p_packet;
   }

   p_packet->pp_blocks[p_packet->i_depth] = p_block;
   p_packet->i_depth++;

   if (this->i_next_send > p_packet->i_dts + p_output->config.i_output_latency) {
      this->i_next_send = p_packet->i_dts + p_output->config.i_output_latency;
      cLev_timer_stop(this->event_loop, &this->output_watcher);
      cLev_timer_set(&this->output_watcher, (this->i_next_send - this->i_wallclock) / 1000000., 0);
      cLev_timer_start(this->event_loop, &this->output_watcher);
   }
}

void cLdvboutput::outputs_Send(void *loop, void *p, int revents)
{
   cLev_timer *w = (cLev_timer *)p;
   cLdvboutput *pobj = (cLdvboutput *)w->data;
   pobj->i_wallclock = cLdvbobj::mdate();
   do {
      pobj->i_next_send = INT64_MAX;
      if (pobj->output_dup->config.i_config & OUTPUT_VALID) {
         while (pobj->output_dup->p_packets != (packet_t *) 0 && pobj->output_dup->p_packets->i_dts + pobj->output_dup->config.i_output_latency <= pobj->i_wallclock)
            pobj->output_Flush(pobj->output_dup);
         if (pobj->output_dup->p_packets != (packet_t *) 0)
            pobj->i_next_send = pobj->output_dup->p_packets->i_dts + pobj->output_dup->config.i_output_latency;
      }

      for (int i = 0; i < pobj->i_nb_outputs; i++) {
         output_t *p_output = pobj->pp_outputs[i];
         if (!(p_output->config.i_config & OUTPUT_VALID))
            continue;
         while (p_output->p_packets != (packet_t *) 0 && p_output->p_packets->i_dts + p_output->config.i_output_latency <= pobj->i_wallclock)
            pobj->output_Flush(p_output);
         if (p_output->p_packets != (packet_t *) 0 && (p_output->p_packets->i_dts + p_output->config.i_output_latency < pobj->i_next_send))
            pobj->i_next_send = p_output->p_packets->i_dts + p_output->config.i_output_latency;
      }
   }
   while (pobj->i_next_send <= pobj->i_wallclock);
   if (pobj->i_next_send < INT64_MAX) {
      cLev_timer_set(&pobj->output_watcher, (pobj->i_next_send - pobj->i_wallclock) / 1000000., 0);
      cLev_timer_start(loop, &pobj->output_watcher);
   }
}

void cLdvboutput::outputs_Init(void)
{
   this->i_wallclock = this->mdate();
   this->output_watcher.data = this;
   cLev_timer_init(&this->output_watcher, cLdvboutput::outputs_Send, 0, 0);
}

/* output_Find : find an existing output from a given output_config_t */
cLdvboutput::output_t *cLdvboutput::output_Find(const output_config_t *p_config)
{
   socklen_t i_sockaddr_len = (p_config->i_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
   for (int i = 0; i < this->i_nb_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];
      if (!(p_output->config.i_config & OUTPUT_VALID))
         continue;
      if (p_config->i_family != p_output->config.i_family || memcmp(&p_config->connect_addr, &p_output->config.connect_addr, i_sockaddr_len) || memcmp(&p_config->bind_addr, &p_output->config.bind_addr, i_sockaddr_len))
         continue;
      if (p_config->i_family == AF_INET6 && p_config->i_if_index_v6 != p_output->config.i_if_index_v6)
         continue;
      if ((p_config->i_config ^ p_output->config.i_config) & OUTPUT_RAW)
         continue;
      return p_output;
   }
   return (output_t *) 0;
}

/* output_Change : get changes from a new output_config_t */
void cLdvboutput::output_Change(output_t *p_output, const output_config_t *p_config)
{
   int ret = 0;
   memcpy(p_output->config.pi_ssrc, p_config->pi_ssrc, 4 * sizeof(uint8_t));
   p_output->config.i_output_latency = p_config->i_output_latency;
   p_output->config.i_max_retention = p_config->i_max_retention;

   if (p_output->config.i_ttl != p_config->i_ttl) {
      if (p_output->config.i_family == AF_INET6) {
         struct sockaddr_in6 *p_addr = (struct sockaddr_in6 *)&p_output->config.connect_addr;
         if (IN6_IS_ADDR_MULTICAST(&p_addr->sin6_addr))
            ret = setsockopt(p_output->i_handle, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const void *)&p_config->i_ttl, (int)sizeof(p_config->i_ttl));
      } else {
         struct sockaddr_in *p_addr = (struct sockaddr_in *)&p_output->config.connect_addr;
         if (IN_MULTICAST(ntohl(p_addr->sin_addr.s_addr)))
            ret = setsockopt(p_output->i_handle, IPPROTO_IP, IP_MULTICAST_TTL, (const void *)&p_config->i_ttl, (int)sizeof(p_config->i_ttl));
      }
      p_output->config.i_ttl = p_config->i_ttl;
      p_output->raw_pkt_header.iph.ttl = p_config->i_ttl;
   }

   if (p_output->config.i_tos != p_config->i_tos) {
      if (p_output->config.i_family == AF_INET)
         ret = setsockopt(p_output->i_handle, IPPROTO_IP, IP_TOS, (const void *)&p_config->i_tos, (int)sizeof(p_config->i_tos));
      p_output->config.i_tos = p_config->i_tos;
      p_output->raw_pkt_header.iph.tos = p_config->i_tos;
   }

   if (ret == -1)
      cLbugf(cL::dbg_dvb, "couldn't change socket (%s)\n", strerror(errno));

   if (p_output->config.i_mtu != p_config->i_mtu || ((p_output->config.i_config ^ p_config->i_config) & OUTPUT_UDP)) {
      int i_block_cnt;
      packet_t *p_packet = p_output->p_last_packet;
      p_output->config.i_config &= ~OUTPUT_UDP;
      p_output->config.i_config |= p_config->i_config & OUTPUT_UDP;
      p_output->config.i_mtu = p_config->i_mtu;

      cLdvboutput::output_PacketVacuum(p_output);

      i_block_cnt = cLdvboutput::output_BlockCount(p_output);
      if (p_packet != (packet_t *) 0 && p_packet->i_depth < i_block_cnt) {
         p_packet = (packet_t *)realloc(p_packet, sizeof(packet_t *) + i_block_cnt * sizeof(block_t *));
         p_output->p_last_packet = p_packet;
      }
   }

   if (p_config->i_config & OUTPUT_RAW) {
      p_output->raw_pkt_header.iph.saddr = inet_addr(p_config->psz_srcaddr);
      p_output->raw_pkt_header.udph.source = htons(p_config->i_srcport);
   }
}

void cLdvboutput::outputs_Close(int i_num_outputs)
{
   for (int i = 0; i < i_num_outputs; i++) {
      output_t *p_output = this->pp_outputs[i];
      if (p_output->config.i_config & OUTPUT_VALID) {
         cLbugf(cL::dbg_dvb, "removing %s\n", p_output->config.psz_displayname);
         if (p_output->p_packets)
            this->output_Flush(p_output);
         this->output_Close(p_output);
      }
      ::free(p_output);
   }
   ::free(this->pp_outputs);
   this->pp_outputs = (output_t **) 0;

#ifdef HAVE_CLICONV
   if (this->iconv_handle != (iconv_t) -1) {
      iconv_close(this->iconv_handle);
      this->iconv_handle = (iconv_t) -1;
   }
   if (this->conf_iconv != (iconv_t) -1) {
      iconv_close(this->conf_iconv);
      this->conf_iconv = (iconv_t) -1;
   }
#endif

   this->dvb_string_clean(&this->network_name);
   this->dvb_string_clean(&this->provider_name);
}

char *cLdvboutput::iconv_append_null(const char *p_string, size_t i_length)
{
   char *psz_string = cLmalloc(char, i_length + 1);
   memcpy(psz_string, p_string, i_length);
   psz_string[i_length] = '\0';
   return psz_string;
}

char *cLdvboutput::iconv_cb(void *iconv_opaque, const char *psz_encoding, char *p_string, size_t i_length)
{
#ifdef HAVE_CLICONV
   static const char *psz_current_encoding = "";
   cLdvboutput *pobj = (cLdvboutput *)iconv_opaque;

   char *psz_string, *p;
   size_t i_out_length;

   if (!strcmp(psz_encoding, pobj->psz_native_charset))
      return iconv_append_null(p_string, i_length);

   if (pobj->iconv_handle != (iconv_t) -1 && strcmp(psz_encoding, psz_current_encoding)) {
      iconv_close(pobj->iconv_handle);
      pobj->iconv_handle = (iconv_t) -1;
   }

   if (pobj->iconv_handle == (iconv_t) -1)
      pobj->iconv_handle = iconv_open(pobj->psz_native_charset, psz_encoding);
   if (pobj->iconv_handle == (iconv_t) -1) {
      cLbugf(cL::dbg_dvb, "couldn't open converter from %s to %s\n", psz_encoding, pobj->psz_native_charset);
      return iconv_append_null(p_string, i_length);
   }
   psz_current_encoding = psz_encoding;

   /* converted strings can be up to six times larger */
   i_out_length = i_length * 6;
   p = psz_string = cLmalloc(char, i_out_length);
   if ((long)iconv(pobj->iconv_handle, &p_string, &i_length, &p, &i_out_length) == -1) {
      cLbugf(cL::dbg_dvb, "couldn't convert from %s to %s\n", psz_encoding, pobj->psz_native_charset);
      ::free(psz_string);
      return iconv_append_null(p_string, i_length);
   }
   if (i_length) {
      cLbugf(cL::dbg_dvb, "partial conversion from %s to %s\n", psz_encoding, pobj->psz_native_charset);
   }

   *p = '\0';
   return psz_string;
#else
   return cLdvboutput::iconv_append_null((const char *)p_string, i_length);
#endif
}

bool cLdvboutput::set_rtpsrc(const char *s)
{
   struct in_addr maddr;
   if (!inet_aton(s, &maddr))
      return false;
   memcpy(this->pi_ssrc_global, &maddr.s_addr, 4 * sizeof(uint8_t));
   return true;
}

bool cLdvboutput::output_Setup(const char *netname, const char *proname)
{
   if (this->b_udp_global) {
      cLbug(cL::dbg_dvb, "raw UDP output is deprecated.  Please consider using RTP.\n");
      cLbug(cL::dbg_dvb, "for DVB-IP compliance you should use RTP.\n");
   }
   if (this->b_epg_global && !this->b_dvb_global) {
      cLbug(cL::dbg_dvb, "turning on DVB compliance, required by EPG information\n");
      this->b_dvb_global = true;
   }

   this->config_strdvb(&this->network_name, netname, this->psz_native_charset);
   this->config_strdvb(&this->provider_name, proname, this->psz_native_charset);

   bool rc = true;

   if (this->psz_dup_config != (char *) 0) {
      cLdvboutput::output_config_t config;
      this->config_Defaults(&config);
      bool rc = this->config_ParseHost(&config, this->psz_dup_config);
      if (rc) {
         this->output_Init(this->output_dup, &config);
         this->output_Change(this->output_dup, &config);
      } else {
         cLbug(cL::dbg_dvb, "Invalid target address for output duplication\n");
      }
      this->config_Free(&config);
   }

   return rc;
}
