/*
 * cLdvbudp.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * udp.c: UDP input for DVBlast
 *****************************************************************************
 * Copyright (C) 2009, 2015 VideoLAN
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

#include <cLdvbudp.h>

#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <bitstream/ietf/rtp.h>
#include <errno.h>

#ifdef HAVE_CLMACOS
#include <sys/uio.h>
#endif


#define UDP_LOCK_TIMEOUT 5000000 /* 5 s */

cLdvbudp::cLdvbudp()
{
   this->i_handle = -1;
   this->b_udp = false;
   this->i_block_cnt = 0;
   this->i_seqnum = 0;
   this->b_sync = false;
   this->psz_udp_src = (char *) 0;
   for (int i = 0; i < 4; i++)
      this->pi_ssrc[i] = 0;
   cLbug(cL::dbg_high, "cLdvbudp created\n");
}

cLdvbudp::~cLdvbudp()
{
   cLbug(cL::dbg_high, "cLdvbudp deleted\n");
}

#define IS_OPTION(option) (!strncasecmp(psz_string, option, strlen(option)))
#define ARG_OPTION(option) (psz_string + strlen(option))

void cLdvbudp::dev_Open()
{
   int i_family;
   struct addrinfo *p_connect_ai = (addrinfo *) 0, *p_bind_ai;
   int i_if_index = 0;
   in_addr_t i_if_addr = INADDR_ANY;
   int i_mtu = 0;
   char *psz_ifname = (char *) 0;

   char *psz_bind, *psz_string = strdup(this->psz_udp_src);
   char *psz_save = psz_string;
   int i = 1;

   /* Parse configuration. */

   if ((psz_bind = strchr(psz_string, '@')) != (char *) 0) {
      *psz_bind++ = '\0';
      p_connect_ai = this->ParseNodeService(psz_string, (char **) 0, 0);
   } else {
      psz_bind = psz_string;
   }

   p_bind_ai = this->ParseNodeService(psz_bind, &psz_string, DEFAULT_PORT);
   if (p_bind_ai == (addrinfo *) 0) {
      cLbugf(cL::dbg_dvb, "couldn't parse %s\n", psz_bind);
      exit(EXIT_FAILURE);
   }
   i_family = p_bind_ai->ai_family;

   if (p_connect_ai != NULL && p_connect_ai->ai_family != i_family) {
      cLbug(cL::dbg_dvb, "invalid connect address\n");
      freeaddrinfo(p_connect_ai);
      p_connect_ai = (addrinfo *) 0;
   }

   while ((psz_string = strchr(psz_string, '/')) != (char *) 0) {
      *psz_string++ = '\0';

      if (IS_OPTION("udp")) {
         this->b_udp = true;
      } else
      if (IS_OPTION("mtu=")) {
         i_mtu = strtol((const char *)ARG_OPTION("mtu="), (char **) 0, 0);
      } else
      if (IS_OPTION("ifindex=")) {
         i_if_index = strtol((const char *)ARG_OPTION("ifindex="), (char **) 0, 0);
      } else
      if (IS_OPTION("ifaddr=")) {
         char *option = this->config_stropt((const char *)ARG_OPTION("ifaddr="));
         i_if_addr = inet_addr(option);
         free(option);
      } else
      if (IS_OPTION("ifname=")) {
         psz_ifname = this->config_stropt((const char *)ARG_OPTION("ifname="));
         if (strlen(psz_ifname) >= IFNAMSIZ) {
            psz_ifname[IFNAMSIZ-1] = '\0';
         }
      } else {
         cLbugf(cL::dbg_dvb, "unrecognized option %s\n", psz_string);
      }

   }

   if (!i_mtu)
      i_mtu = i_family == AF_INET6 ? DEFAULT_IPV6_MTU : DEFAULT_IPV4_MTU;

   this->i_block_cnt = (i_mtu - (this->b_udp ? 0 : RTP_HEADER_SIZE)) / TS_SIZE;


   /* Do stuff. */

   if ((this->i_handle = socket(i_family, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't create socket (%s)\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   setsockopt(this->i_handle, SOL_SOCKET, SO_REUSEADDR, (void *) &i, sizeof(i));

   /* Increase the receive buffer size to 1/2MB (8Mb/s during 1/2s) to avoid
    * packet loss caused by scheduling problems */
   i = 0x80000;

   setsockopt(this->i_handle, SOL_SOCKET, SO_RCVBUF, (void *) &i, sizeof(i));

   if (bind(this->i_handle, p_bind_ai->ai_addr, p_bind_ai->ai_addrlen) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't bind (%s)\n", strerror(errno));
      close(this->i_handle);
      exit(EXIT_FAILURE);
   }

   if (p_connect_ai != (addrinfo *) 0) {
      uint16_t i_port;
      if (i_family == AF_INET6) {
         i_port = ((struct sockaddr_in6 *)p_connect_ai->ai_addr)->sin6_port;
      } else {
         i_port = ((struct sockaddr_in *)p_connect_ai->ai_addr)->sin_port;
      }

      if (i_port != 0 && connect(this->i_handle, p_connect_ai->ai_addr, p_connect_ai->ai_addrlen) < 0) {
         cLbugf(cL::dbg_dvb, "couldn't connect socket (%s)\n", strerror(errno));
      }
   }

   /* Join the multicast group if the socket is a multicast address */
   if (i_family == AF_INET6) {
      struct sockaddr_in6 *p_addr = (struct sockaddr_in6 *)p_bind_ai->ai_addr;
      if (IN6_IS_ADDR_MULTICAST(&p_addr->sin6_addr)) {
         struct ipv6_mreq imr;
         imr.ipv6mr_multiaddr = p_addr->sin6_addr;
         imr.ipv6mr_interface = i_if_index;
         if (i_if_addr != INADDR_ANY) {
            cLbug(cL::dbg_dvb, "ignoring ifaddr option in IPv6\n");
         }
         if (setsockopt(this->i_handle, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char *)&imr, sizeof(struct ipv6_mreq)) < 0) {
            cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n", strerror(errno));
         }
      }
   } else {
      struct sockaddr_in *p_addr = (struct sockaddr_in *)p_bind_ai->ai_addr;
      if (IN_MULTICAST(ntohl(p_addr->sin_addr.s_addr))) {
         if (p_connect_ai != (addrinfo *) 0) {
#ifndef IP_ADD_SOURCE_MEMBERSHIP
            cLbug(cL::dbg_dvb, "IP_ADD_SOURCE_MEMBERSHIP is unsupported.\n");
#else
            /* Source-specific multicast */
            struct sockaddr *p_src = p_connect_ai->ai_addr;
            struct ip_mreq_source imr;
            imr.imr_multiaddr = p_addr->sin_addr;
            imr.imr_interface.s_addr = i_if_addr;
            imr.imr_sourceaddr = ((struct sockaddr_in *)p_src)->sin_addr;
            if (i_if_index) {
               cLbug(cL::dbg_dvb, "ignoring ifindex option in SSM\n");
            }
            if (setsockopt(this->i_handle, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, (char *)&imr, sizeof(struct ip_mreq_source)) < 0) {
               cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n", strerror(errno));
            }
#endif
         } else
         if (i_if_index) {
            /* Linux-specific interface-bound multicast */
            struct ip_mreqn imr;
            imr.imr_multiaddr = p_addr->sin_addr;
#ifdef HAVE_CLLINUX
            imr.imr_address.s_addr = i_if_addr;
            imr.imr_ifindex = i_if_index;
#endif
            if (setsockopt(this->i_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&imr, sizeof(struct ip_mreqn)) < 0) {
               cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n", strerror(errno));
            }
         } else {
            /* Regular multicast */
            struct ip_mreq imr;
            imr.imr_multiaddr = p_addr->sin_addr;
            imr.imr_interface.s_addr = i_if_addr;

            if (setsockopt(this->i_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&imr, sizeof(struct ip_mreq)) == -1) {
               cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n", strerror(errno));
            }
         }
#ifdef SO_BINDTODEVICE
         if (psz_ifname) {
            if (setsockopt(this->i_handle, SOL_SOCKET, SO_BINDTODEVICE, psz_ifname, strlen(psz_ifname)+1) < 0) {
               cLbugf(cL::dbg_dvb, "couldn't bind to device %s (%s)\n", psz_ifname, strerror(errno));
            }
            free(psz_ifname);
            psz_ifname = (char *) 0;
         }
#endif
      }
   }

   freeaddrinfo(p_bind_ai);
   if (p_connect_ai != (addrinfo *) 0)
      freeaddrinfo(p_connect_ai);
   free(psz_save);

   cLbugf(cL::dbg_dvb, "binding socket to %s\n", this->psz_udp_src);

   this->udp_watcher.data = this;
   cLev_io_init(&this->udp_watcher, cLdvbudp::udp_Read, this->i_handle, 1); //EV_READ
   cLev_io_start(this->event_loop, &this->udp_watcher);

   this->mute_watcher.data = this;
   cLev_timer_init(&this->mute_watcher, cLdvbudp::udp_MuteCb, UDP_LOCK_TIMEOUT / 1000000., UDP_LOCK_TIMEOUT / 1000000.);
}

void cLdvbudp::udp_Read(void *loop, void *p, int revents)
{
   struct cLev_io *w = (struct cLev_io *) p;
   cLdvbudp *pobj = (cLdvbudp *) w->data;

   struct iovec p_iov[pobj->i_block_cnt + 1];
   block_t *p_ts, **pp_current = &p_ts;
   int i_iov, i_block;
   ssize_t i_len;
   uint8_t p_rtp_hdr[RTP_HEADER_SIZE];

   if (!pobj->b_udp) {
      /* FIXME : this is wrong if RTP header > 12 bytes */
      p_iov[0].iov_base = p_rtp_hdr;
      p_iov[0].iov_len = RTP_HEADER_SIZE;
      i_iov = 1;
   } else {
      i_iov = 0;
   }

   for (i_block = 0; i_block < pobj->i_block_cnt; i_block++) {
      *pp_current = pobj->block_New();
      p_iov[i_iov].iov_base = (*pp_current)->p_ts;
      p_iov[i_iov].iov_len = TS_SIZE;
      pp_current = &(*pp_current)->p_next;
      i_iov++;
   }
   pp_current = &p_ts;

   if ((i_len = readv(pobj->i_handle, p_iov, i_iov)) < 0) {
      cLbugf(cL::dbg_dvb, "couldn't read from network (%s)\n", strerror(errno));
      goto err;
   }

   if (!pobj->b_udp) {
      uint8_t pi_new_ssrc[4];

      if (!rtp_check_hdr(p_rtp_hdr))
         cLbug(cL::dbg_dvb, "invalid RTP packet received\n");
      if (rtp_get_type(p_rtp_hdr) != RTP_TYPE_TS)
         cLbug(cL::dbg_dvb, "non-TS RTP packet received\n");
      rtp_get_ssrc(p_rtp_hdr, pi_new_ssrc);
      if (!memcmp(pobj->pi_ssrc, pi_new_ssrc, 4 * sizeof(uint8_t))) {
         if (rtp_get_seqnum(p_rtp_hdr) != pobj->i_seqnum)
            cLbug(cL::dbg_dvb, "RTP discontinuity\n");
      } else {
         struct in_addr addr;
         memcpy(&addr.s_addr, pi_new_ssrc, 4 * sizeof(uint8_t));
         cLbugf(cL::dbg_dvb, "new RTP source: %s\n", inet_ntoa(addr));
         memcpy(pobj->pi_ssrc, pi_new_ssrc, 4 * sizeof(uint8_t));
         cLbugf(cL::dbg_dvb, "source: %s\n", inet_ntoa(addr));
      }
      pobj->i_seqnum = rtp_get_seqnum(p_rtp_hdr) + 1;
      i_len -= RTP_HEADER_SIZE;
   }

   i_len /= TS_SIZE;

   if (i_len) {
      if (!pobj->b_sync) {
         cLbug(cL::dbg_dvb, "frontend has acquired lock\n");
         pobj->b_sync = true;
      }
      cLev_timer_again(loop, &pobj->mute_watcher);
   }

   while (i_len && *pp_current) {
      pp_current = &(*pp_current)->p_next;
      i_len--;
   }

   err:
   pobj->block_DeleteChain(*pp_current);
   *pp_current = NULL;

   pobj->demux_Run(p_ts);
}

void cLdvbudp::udp_MuteCb(void *loop, void *p, int revents)
{
   //struct cLev_timer *w = (struct cLev_timer *) p;
   //cLdvbudp *pobj = (cLdvbudp *) w->data;

   cLbug(cL::dbg_dvb, "frontend has lost lock\n");
   cLev_timer_stop(loop, p);
}

/* From now on these are just stubs */
int cLdvbudp::dev_SetFilter(uint16_t i_pid)
{
   return -1;
}

void cLdvbudp::dev_UnsetFilter(int i_fd, uint16_t i_pid)
{
}

void cLdvbudp::dev_Reset(void)
{
}
