/*
 * cLdvbdevc.h
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

#ifndef CLDVBDEVC_H_
#define CLDVBDEVC_H_

extern "C" {
#if DVBAPI_VERSION >= 505
   extern struct dtv_property *info_cmdargs;
   extern struct dtv_properties info_cmdseq;
   extern struct dtv_property *enum_cmdargs;
   extern struct dtv_properties enum_cmdseq;
#endif
   extern struct dtv_property *dvbs_cmdargs;
   extern struct dtv_properties dvbs_cmdseq;
   extern struct dtv_property *dvbs2_cmdargs;
   extern struct dtv_properties dvbs2_cmdseq;
   extern struct dtv_property *dvbc_cmdargs;
   extern struct dtv_properties dvbc_cmdseq;
   extern struct dtv_property *dvbt_cmdargs;
   extern struct dtv_properties dvbt_cmdseq;
   extern struct dtv_property *atsc_cmdargs;
   extern struct dtv_properties atsc_cmdseq;
   extern struct dtv_property *pclear;
   extern struct dtv_properties cmdclear;
}

#endif /*CLDVBDEVC_H_*/
