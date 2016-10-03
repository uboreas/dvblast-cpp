/*
 * cLdvbudp.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
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

#include <cLdvbev.h>
#include <cLdvbdemux.h>
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

namespace libcLdvbudp {

   /*****************************************************************************
    * Local declarations
    *****************************************************************************/
#define UDP_LOCK_TIMEOUT 5000000 /* 5 s */

   static int i_handle;
   static struct cLev_io udp_watcher;
   static struct cLev_timer mute_watcher;
   static bool b_udp = false;
   static int i_block_cnt;
   static uint8_t pi_ssrc[4] = { 0, 0, 0, 0 };
   static uint16_t i_seqnum = 0;
   static bool b_sync = false;

   char *psz_udp_src = (char *) 0;

   /*****************************************************************************
    * Local prototypes
    *****************************************************************************/
   static void udp_Read(void *loop, void *w, int revents);
   static void udp_MuteCb(void *loop, void *w, int revents);

   /*****************************************************************************
    * udp_Open
    *****************************************************************************/
   void udp_Open( void )
   {
      int i_family;
      struct addrinfo *p_connect_ai = NULL, *p_bind_ai;
      int i_if_index = 0;
      in_addr_t i_if_addr = INADDR_ANY;
      int i_mtu = 0;
      char *psz_ifname = NULL;

      char *psz_bind, *psz_string = strdup( psz_udp_src );
      char *psz_save = psz_string;
      int i = 1;

      /* Parse configuration. */

      if ( (psz_bind = strchr( psz_string, '@' )) != NULL )
      {
         *psz_bind++ = '\0';
         p_connect_ai = libcLdvboutput::ParseNodeService( psz_string, NULL, 0 );
      }
      else
         psz_bind = psz_string;

      p_bind_ai = libcLdvboutput::ParseNodeService( psz_bind, &psz_string, DEFAULT_PORT );
      if ( p_bind_ai == NULL )
      {
         cLbugf(cL::dbg_dvb, "couldn't parse %s\n", psz_bind );
         exit(EXIT_FAILURE);
      }
      i_family = p_bind_ai->ai_family;

      if ( p_connect_ai != NULL && p_connect_ai->ai_family != i_family )
      {
         cLbug(cL::dbg_dvb, "invalid connect address\n" );
         freeaddrinfo( p_connect_ai );
         p_connect_ai = NULL;
      }

      while ( (psz_string = strchr( psz_string, '/' )) != NULL )
      {
         *psz_string++ = '\0';

#define IS_OPTION( option ) (!strncasecmp( psz_string, option, strlen(option) ))
#define ARG_OPTION( option ) (psz_string + strlen(option))

         if ( IS_OPTION("udp") )
            b_udp = true;
         else if ( IS_OPTION("mtu=") )
            i_mtu = strtol( ARG_OPTION("mtu="), NULL, 0 );
         else if ( IS_OPTION("ifindex=") )
            i_if_index = strtol( ARG_OPTION("ifindex="), NULL, 0 );
         else if ( IS_OPTION("ifaddr=") ) {
            char *option = libcLdvboutput::config_stropt( ARG_OPTION("ifaddr=") );
            i_if_addr = inet_addr( option );
            free( option );
         }
         else if ( IS_OPTION("ifname=") )
         {
            psz_ifname = libcLdvboutput::config_stropt( ARG_OPTION("ifname=") );
            if (strlen(psz_ifname) >= IFNAMSIZ) {
               psz_ifname[IFNAMSIZ-1] = '\0';
            }
         } else
            cLbugf(cL::dbg_dvb, "unrecognized option %s\n", psz_string );

#undef IS_OPTION
#undef ARG_OPTION
      }

      if ( !i_mtu )
         i_mtu = i_family == AF_INET6 ? DEFAULT_IPV6_MTU : DEFAULT_IPV4_MTU;
      i_block_cnt = (i_mtu - (b_udp ? 0 : RTP_HEADER_SIZE)) / TS_SIZE;


      /* Do stuff. */

      if ( (i_handle = socket( i_family, SOCK_DGRAM, IPPROTO_UDP )) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't create socket (%s)\n", strerror(errno) );
         exit(EXIT_FAILURE);
      }

      setsockopt( i_handle, SOL_SOCKET, SO_REUSEADDR, (void *) &i, sizeof( i ) );

      /* Increase the receive buffer size to 1/2MB (8Mb/s during 1/2s) to avoid
       * packet loss caused by scheduling problems */
      i = 0x80000;

      setsockopt( i_handle, SOL_SOCKET, SO_RCVBUF, (void *) &i, sizeof( i ) );

      if ( bind( i_handle, p_bind_ai->ai_addr, p_bind_ai->ai_addrlen ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't bind (%s)\n", strerror(errno) );
         close( i_handle );
         exit(EXIT_FAILURE);
      }

      if ( p_connect_ai != NULL )
      {
         uint16_t i_port;
         if ( i_family == AF_INET6 )
            i_port = ((struct sockaddr_in6 *)p_connect_ai->ai_addr)->sin6_port;
         else
            i_port = ((struct sockaddr_in *)p_connect_ai->ai_addr)->sin_port;

         if ( i_port != 0 && connect( i_handle, p_connect_ai->ai_addr,
               p_connect_ai->ai_addrlen ) < 0 )
            cLbugf(cL::dbg_dvb, "couldn't connect socket (%s)\n", strerror(errno) );
      }

      /* Join the multicast group if the socket is a multicast address */
      if ( i_family == AF_INET6 )
      {
         struct sockaddr_in6 *p_addr =
               (struct sockaddr_in6 *)p_bind_ai->ai_addr;
         if ( IN6_IS_ADDR_MULTICAST( &p_addr->sin6_addr ) )
         {
            struct ipv6_mreq imr;
            imr.ipv6mr_multiaddr = p_addr->sin6_addr;
            imr.ipv6mr_interface = i_if_index;
            if ( i_if_addr != INADDR_ANY )
               cLbug(cL::dbg_dvb, "ignoring ifaddr option in IPv6\n" );

            if ( setsockopt( i_handle, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                  (char *)&imr, sizeof(struct ipv6_mreq) ) < 0 )
               cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n",
                     strerror(errno) );
         }
      }
      else
      {
         struct sockaddr_in *p_addr =
               (struct sockaddr_in *)p_bind_ai->ai_addr;
         if ( IN_MULTICAST( ntohl(p_addr->sin_addr.s_addr)) )
         {
            if ( p_connect_ai != NULL )
            {
#ifndef IP_ADD_SOURCE_MEMBERSHIP
               cLbug(cL::dbg_dvb, "IP_ADD_SOURCE_MEMBERSHIP is unsupported.\n" );
#else
               /* Source-specific multicast */
               struct sockaddr *p_src = p_connect_ai->ai_addr;
               struct ip_mreq_source imr;
               imr.imr_multiaddr = p_addr->sin_addr;
               imr.imr_interface.s_addr = i_if_addr;
               imr.imr_sourceaddr = ((struct sockaddr_in *)p_src)->sin_addr;
               if ( i_if_index )
                  cLbug(cL::dbg_dvb, "ignoring ifindex option in SSM\n" );

               if ( setsockopt( i_handle, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                     (char *)&imr, sizeof(struct ip_mreq_source) ) < 0 )
                  cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n",
                        strerror(errno) );
#endif
            }
            else if ( i_if_index )
            {
               /* Linux-specific interface-bound multicast */
               struct ip_mreqn imr;
               imr.imr_multiaddr = p_addr->sin_addr;
#ifdef HAVE_CLLINUX
               imr.imr_address.s_addr = i_if_addr;
               imr.imr_ifindex = i_if_index;
#endif

               if ( setsockopt( i_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     (char *)&imr, sizeof(struct ip_mreqn) ) < 0 )
                  cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n",
                        strerror(errno) );
            }
            else
            {
               /* Regular multicast */
               struct ip_mreq imr;
               imr.imr_multiaddr = p_addr->sin_addr;
               imr.imr_interface.s_addr = i_if_addr;

               if ( setsockopt( i_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     (char *)&imr, sizeof(struct ip_mreq) ) == -1 )
                  cLbugf(cL::dbg_dvb, "couldn't join multicast group (%s)\n",
                        strerror(errno) );
            }
#ifdef SO_BINDTODEVICE
            if (psz_ifname) {
               if ( setsockopt( i_handle, SOL_SOCKET, SO_BINDTODEVICE,
                     psz_ifname, strlen(psz_ifname)+1 ) < 0 ) {
                  cLbugf(cL::dbg_dvb, "couldn't bind to device %s (%s)\n",
                        psz_ifname, strerror(errno) );
               }
               free(psz_ifname);
               psz_ifname = NULL;
            }
#endif
         }
      }

      freeaddrinfo( p_bind_ai );
      if ( p_connect_ai != NULL )
         freeaddrinfo( p_connect_ai );
      free( psz_save );

      cLbugf(cL::dbg_dvb, "binding socket to %s\n", psz_udp_src );

      cLev_io_init(&udp_watcher, udp_Read, i_handle, 1); //EV_READ
      cLev_io_start(libcLdvb::event_loop, &udp_watcher);

      cLev_timer_init(&mute_watcher, udp_MuteCb,
            UDP_LOCK_TIMEOUT / 1000000., UDP_LOCK_TIMEOUT / 1000000.);
   }

   /*****************************************************************************
    * UDP events, struct ev_io *
    *****************************************************************************/
   static void udp_Read(void *loop, void *w, int revents)
   {
      struct iovec p_iov[i_block_cnt + 1];
      libcLdvboutput::block_t *p_ts, **pp_current = &p_ts;
      int i_iov, i_block;
      ssize_t i_len;
      uint8_t p_rtp_hdr[RTP_HEADER_SIZE];

      if ( !b_udp )
      {
         /* FIXME : this is wrong if RTP header > 12 bytes */
         p_iov[0].iov_base = p_rtp_hdr;
         p_iov[0].iov_len = RTP_HEADER_SIZE;
         i_iov = 1;
      }
      else
         i_iov = 0;

      for ( i_block = 0; i_block < i_block_cnt; i_block++ )
      {
         *pp_current = libcLdvboutput::block_New();
         p_iov[i_iov].iov_base = (*pp_current)->p_ts;
         p_iov[i_iov].iov_len = TS_SIZE;
         pp_current = &(*pp_current)->p_next;
         i_iov++;
      }
      pp_current = &p_ts;

      if ( (i_len = readv( i_handle, p_iov, i_iov )) < 0 )
      {
         cLbugf(cL::dbg_dvb, "couldn't read from network (%s)\n", strerror(errno) );
         goto err;
      }

      if ( !b_udp )
      {
         uint8_t pi_new_ssrc[4];

         if ( !rtp_check_hdr(p_rtp_hdr) )
            cLbug(cL::dbg_dvb, "invalid RTP packet received\n" );
         if ( rtp_get_type(p_rtp_hdr) != RTP_TYPE_TS )
            cLbug(cL::dbg_dvb, "non-TS RTP packet received\n" );
         rtp_get_ssrc(p_rtp_hdr, pi_new_ssrc);
         if ( !memcmp( pi_ssrc, pi_new_ssrc, 4 * sizeof(uint8_t) ) ) {
            if ( rtp_get_seqnum(p_rtp_hdr) != i_seqnum )
               cLbug(cL::dbg_dvb, "RTP discontinuity\n" );
         } else {
            struct in_addr addr;
            memcpy( &addr.s_addr, pi_new_ssrc, 4 * sizeof(uint8_t) );
            cLbugf(cL::dbg_dvb, "new RTP source: %s\n", inet_ntoa( addr ) );
            memcpy( pi_ssrc, pi_new_ssrc, 4 * sizeof(uint8_t) );
            cLbugf(cL::dbg_dvb, "source: %s\n", inet_ntoa( addr ));
         }
         i_seqnum = rtp_get_seqnum(p_rtp_hdr) + 1;
         i_len -= RTP_HEADER_SIZE;
      }

      i_len /= TS_SIZE;

      if ( i_len ) {
         if ( !b_sync ) {
            cLbug(cL::dbg_dvb, "frontend has acquired lock\n" );
            b_sync = true;
         }
         cLev_timer_again(loop, &mute_watcher);
      }

      while ( i_len && *pp_current ) {
         pp_current = &(*pp_current)->p_next;
         i_len--;
      }

      err:
      libcLdvboutput::block_DeleteChain( *pp_current );
      *pp_current = NULL;

      libcLdvbdemux::demux_Run( p_ts );
   }

   //struct ev_timer *
   static void udp_MuteCb(void *loop, void *w, int revents)
   {
      cLbug(cL::dbg_dvb, "frontend has lost lock\n" );
      cLev_timer_stop(loop, w);
   }

   /* From now on these are just stubs */

   /*****************************************************************************
    * udp_SetFilter
    *****************************************************************************/
   int udp_SetFilter( uint16_t i_pid )
   {
      return -1;
   }

   /*****************************************************************************
    * udp_UnsetFilter: normally never called
    *****************************************************************************/
   void udp_UnsetFilter( int i_fd, uint16_t i_pid )
   {
   }

   /*****************************************************************************
    * udp_Reset:
    *****************************************************************************/
   void udp_Reset( void )
   {
   }

} /* namespace libcLdvbudp */
