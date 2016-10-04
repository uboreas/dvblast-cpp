/*
 * cLdvbcomm.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * comm.c: Handles the communication socket
 *****************************************************************************
 * Copyright (C) 2008, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 *****************************************************************************/

#include <sys/un.h>

#include <cLdvbev.h>
#include <cLdvboutput.h>
#include <cLdvben50221.h>
#include <cLdvbdemux.h>
#ifdef HAVE_CLDVBHW
#include <cLdvbdev.h>
#endif
#include <cLdvbcomm.h>

#include <unistd.h>
#include <errno.h>
#include <bitstream/mpeg/psi.h>

namespace libcLdvbcomm {

   /*****************************************************************************
    * Local declarations
    *****************************************************************************/
   static int i_comm_fd = -1;
   static struct cLev_io comm_watcher;

   char *psz_srv_socket = (char *) 0;

   struct cmd_pid_info {
         libcLdvbdemux::ts_pid_info_t pids[MAX_PIDS];
   };

   /*****************************************************************************
    * Local prototypes
    *****************************************************************************/
   static void comm_Read(void *loop, void *w, int revents);

   /*****************************************************************************
    * comm_Open
    *****************************************************************************/
   void comm_Open( void )
   {
      return;
      int i_size = CLDVB_COMM_MAX_MSG_CHUNK;
      struct sockaddr_un sun_server;

      unlink(psz_srv_socket );

      if ( (i_comm_fd = socket( AF_UNIX, SOCK_DGRAM, 0 )) == -1 )
      {
         cLbugf(cL::dbg_dvb, "cannot create comm socket (%s)\n", strerror(errno) );
         return;
      }

      setsockopt( i_comm_fd, SOL_SOCKET, SO_RCVBUF, &i_size, sizeof(i_size) );

      memset( &sun_server, 0, sizeof(sun_server) );
      sun_server.sun_family = AF_UNIX;
      strncpy( sun_server.sun_path, psz_srv_socket, sizeof(sun_server.sun_path) );
      sun_server.sun_path[sizeof(sun_server.sun_path) - 1] = '\0';

      if ( bind( i_comm_fd, (struct sockaddr *)&sun_server,
            SUN_LEN(&sun_server) ) < 0 )
      {
         cLbugf(cL::dbg_dvb, "cannot bind comm socket (%s)\n", strerror(errno) );
         close( i_comm_fd );
         i_comm_fd = -1;
         return;
      }

      cLev_io_init(&comm_watcher, comm_Read, i_comm_fd, 1); //EV_READ
      cLev_io_start(libcLdvb::event_loop, &comm_watcher);
   }

   /*****************************************************************************
    * comm_Read
    *****************************************************************************/
   static void comm_Read(void *loop, void *w, int revents)
   {
      struct sockaddr_un sun_client;
      socklen_t sun_length = sizeof(sun_client);
      ssize_t i_size, i_answer_size = 0;
      uint8_t p_buffer[CLDVB_COMM_BUFFER_SIZE], p_answer[CLDVB_COMM_BUFFER_SIZE];
      uint8_t i_command, i_answer;
      uint8_t *p_packed_section;
      unsigned int i_packed_section_size;
      uint8_t *p_input = p_buffer + CLDVB_COMM_HEADER_SIZE;
      uint8_t *p_output = p_answer + CLDVB_COMM_HEADER_SIZE;

      i_size = recvfrom( i_comm_fd, p_buffer, CLDVB_COMM_BUFFER_SIZE, 0,
            (struct sockaddr *)&sun_client, &sun_length );
      if ( i_size < CLDVB_COMM_HEADER_SIZE )
      {
         cLbugf(cL::dbg_dvb, "cannot read comm socket (%zd:%s)\n", i_size,
               strerror(errno) );
         return;
      }
      if ( sun_length == 0 || sun_length > sizeof(sun_client) )
      {
         cLbug(cL::dbg_dvb, "anonymous packet from comm socket\n" );
         return;
      }

      if ( p_buffer[0] != CLDVB_COMM_HEADER_MAGIC )
      {
         cLbugf(cL::dbg_dvb, "wrong protocol version 0x%x\n", p_buffer[0] );
         return;
      }

      i_command = p_buffer[1];

#ifdef HAVE_CLDVBHW
      if ( libcLdvbdev::i_frequency == 0 ) /* ASI or UDP, disable DVB only commands */
      {
         switch ( i_command )
         {
            case libcLdvbcomm::CMD_FRONTEND_STATUS:
            case libcLdvbcomm::CMD_MMI_STATUS:
            case libcLdvbcomm::CMD_MMI_SLOT_STATUS:
            case libcLdvbcomm::CMD_MMI_OPEN:
            case libcLdvbcomm::CMD_MMI_CLOSE:
            case libcLdvbcomm::CMD_MMI_RECV:
            case libcLdvbcomm::CMD_MMI_SEND_TEXT:
            case libcLdvbcomm::CMD_MMI_SEND_CHOICE:
               i_answer = libcLdvben50221::RET_NODATA;
               i_answer_size = 0;
               goto return_answer;
         }
      }
#endif

      switch ( i_command )
      {
         case libcLdvbcomm::CMD_RELOAD:
            libcLdvbdemux::config_ReadFile();
            i_answer = libcLdvben50221::RET_OK;
            i_answer_size = 0;
            break;

#ifdef HAVE_CLDVBHW
         case libcLdvbcomm::CMD_FRONTEND_STATUS:
            i_answer = libcLdvbdev::dvb_FrontendStatus( p_answer + CLDVB_COMM_HEADER_SIZE,
                  &i_answer_size );
            break;

         case libcLdvbcomm::CMD_MMI_STATUS:
            i_answer = libcLdvben50221::en50221_StatusMMI( p_answer + CLDVB_COMM_HEADER_SIZE,
                  &i_answer_size );
            break;

         case libcLdvbcomm::CMD_MMI_SLOT_STATUS:
            i_answer = libcLdvben50221::en50221_StatusMMISlot( p_input, i_size - CLDVB_COMM_HEADER_SIZE,
                  p_answer + CLDVB_COMM_HEADER_SIZE,
                  &i_answer_size );
            break;

         case libcLdvbcomm::CMD_MMI_OPEN:
            i_answer = libcLdvben50221::en50221_OpenMMI( p_input, i_size - CLDVB_COMM_HEADER_SIZE );
            break;

         case libcLdvbcomm::CMD_MMI_CLOSE:
            i_answer = libcLdvben50221::en50221_CloseMMI( p_input, i_size - CLDVB_COMM_HEADER_SIZE );
            break;

         case libcLdvbcomm::CMD_MMI_RECV:
            i_answer = libcLdvben50221::en50221_GetMMIObject( p_input, i_size - CLDVB_COMM_HEADER_SIZE,
                  p_answer + CLDVB_COMM_HEADER_SIZE,
                  &i_answer_size );
            break;

         case libcLdvbcomm::CMD_MMI_SEND_TEXT:
         case libcLdvbcomm::CMD_MMI_SEND_CHOICE:
            i_answer = libcLdvben50221::en50221_SendMMIObject( p_input, i_size - CLDVB_COMM_HEADER_SIZE );
            break;
#endif // HAVE_CLDVBHW

         case libcLdvbcomm::CMD_SHUTDOWN:
            cLev_break(loop, 2);
            i_answer = libcLdvben50221::RET_OK;
            i_answer_size = 0;
            break;

         case libcLdvbcomm::CMD_GET_PAT:
         case libcLdvbcomm::CMD_GET_CAT:
         case libcLdvbcomm::CMD_GET_NIT:
         case libcLdvbcomm::CMD_GET_SDT:
         {
#define CASE_TABLE(x) \
      case libcLdvbcomm::CMD_GET_##x: { \
         i_answer = libcLdvben50221::RET_##x; \
         p_packed_section = libcLdvbdemux::demux_get_current_packed_##x(&i_packed_section_size); \
         break; \
      }
            switch ( i_command )
            {
               CASE_TABLE(PAT)
               CASE_TABLE(CAT)
               CASE_TABLE(NIT)
               CASE_TABLE(SDT)
            }
#undef CASE_TABLE

            if ( p_packed_section && i_packed_section_size )
            {
               if ( i_packed_section_size <= CLDVB_COMM_BUFFER_SIZE - CLDVB_COMM_HEADER_SIZE )
               {
                  i_answer_size = i_packed_section_size;
                  memcpy( p_answer + CLDVB_COMM_HEADER_SIZE, p_packed_section, i_packed_section_size );
               } else {
                  cLbugf(cL::dbg_dvb, "section size is too big (%u)\n", i_packed_section_size );
                  i_answer = libcLdvben50221::RET_NODATA;
               }
               free( p_packed_section );
            } else {
               i_answer = libcLdvben50221::RET_NODATA;
            }

            break;
         }

         case libcLdvbcomm::CMD_GET_PMT:
         {
            if ( i_size < CLDVB_COMM_HEADER_SIZE + 2 )
            {
               cLbugf(cL::dbg_dvb, "command packet is too short (%zd)\n", i_size );
               return;
            }

            uint16_t i_sid = (uint16_t)((p_input[0] << 8) | p_input[1]);
            p_packed_section = libcLdvbdemux::demux_get_packed_PMT(i_sid, &i_packed_section_size);

            if ( p_packed_section && i_packed_section_size )
            {
               i_answer = libcLdvben50221::RET_PMT;
               i_answer_size = i_packed_section_size;
               memcpy( p_answer + CLDVB_COMM_HEADER_SIZE, p_packed_section, i_packed_section_size );
               free( p_packed_section );
            } else {
               i_answer = libcLdvben50221::RET_NODATA;
            }

            break;
         }

         case libcLdvbcomm::CMD_GET_PIDS:
         {
            i_answer = libcLdvben50221::RET_PIDS;
            i_answer_size = sizeof(struct cmd_pid_info);
            libcLdvbdemux::demux_get_PIDS_info( p_output );
            break;
         }

         case libcLdvbcomm::CMD_GET_PID:
         {
            if ( i_size < CLDVB_COMM_HEADER_SIZE + 2 )
            {
               cLbugf(cL::dbg_dvb, "command packet is too short (%zd)\n", i_size );
               return;
            }

            uint16_t i_pid = (uint16_t)((p_input[0] << 8) | p_input[1]);
            if ( i_pid >= MAX_PIDS ) {
               i_answer = libcLdvben50221::RET_NODATA;
            } else {
               i_answer = libcLdvben50221::RET_PID;
               i_answer_size = sizeof(libcLdvbdemux::ts_pid_info_t);
               libcLdvbdemux::demux_get_PID_info( i_pid, p_output );
            }
            break;
         }

         default:
            cLbugf(cL::dbg_dvb, "wrong command %u\n", i_command );
            i_answer = libcLdvben50221::RET_HUH;
            i_answer_size = 0;
            break;
      }

      return_answer:
      p_answer[0] = CLDVB_COMM_HEADER_MAGIC;
      p_answer[1] = i_answer;
      p_answer[2] = 0;
      p_answer[3] = 0;
      uint32_t *p_size = (uint32_t *)&p_answer[4];
      *p_size = i_answer_size + CLDVB_COMM_HEADER_SIZE;

      /*    cLbugf(cL::dbg_dvb, "answering %d to %d with size %zd\n", i_answer, i_command,
             i_answer_size ); */

#define min(a, b) (a < b ? a : b)
      ssize_t i_sended = 0;
      ssize_t i_to_send = i_answer_size + CLDVB_COMM_HEADER_SIZE;
      do {
         ssize_t i_sent = sendto( i_comm_fd, p_answer + i_sended,
               min(i_to_send, CLDVB_COMM_MAX_MSG_CHUNK), 0,
               (struct sockaddr *)&sun_client, sun_length );

         if ( i_sent < 0 ) {
            cLbugf(cL::dbg_dvb, "cannot send comm socket (%s)\n", strerror(errno) );
            break;
         }

         i_sended += i_sent;
         i_to_send -= i_sent;
      } while ( i_to_send > 0 );
#undef min
   }

   /*****************************************************************************
    * comm_Close
    *****************************************************************************/
   void comm_Close( void )
   {
      return;
      if (i_comm_fd > -1)
      {
         cLev_io_stop(libcLdvb::event_loop, &comm_watcher);
         close(i_comm_fd);
         unlink(psz_srv_socket);
      }
   }

} /* namespace libcLdvbcomm */
