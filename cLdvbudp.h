/*
 * cLdvbudp.h
 * Copyright (C) 2016, Kylone
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
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

#ifndef CLDVBUDP_H_
#define CLDVBUDP_H_

#include <cLdvbcore.h>

namespace libcLdvbudp {

   extern void udp_Open(void);
   extern void udp_Reset(void);
   extern int udp_SetFilter(uint16_t i_pid);
   extern void udp_UnsetFilter(int i_fd, uint16_t i_pid);
   extern char *psz_udp_src;

} /* namespace libcLdvbudp */


#endif /*CLDVBUDP_H_*/
