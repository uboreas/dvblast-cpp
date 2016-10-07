/*
 * cLdvbdevc.c
 * Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
 *****************************************************************************
 * dvb.c: linux-dvb input for DVBlast
 *****************************************************************************
 * Copyright (C) 2008-2010, 2015 VideoLAN
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

#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/ca.h>

#define DVBAPI_VERSION ((DVB_API_VERSION)*100+(DVB_API_VERSION_MINOR))

#if DVBAPI_VERSION < 508
#define DTV_STREAM_ID        42
#define FE_CAN_MULTISTREAM   0x4000000
#endif

#if DVBAPI_VERSION >= 505
struct dtv_property info_cmdargs[] = {
      { .cmd = DTV_API_VERSION,     .u.data = 0 },
};
struct dtv_properties info_cmdseq = {
      .num = sizeof(info_cmdargs)/sizeof(struct dtv_property),
      .props = info_cmdargs
};

struct dtv_property enum_cmdargs[] = {
      { .cmd = DTV_ENUM_DELSYS,     .u.data = 0 },
};
struct dtv_properties enum_cmdseq = {
      .num = sizeof(enum_cmdargs)/sizeof(struct dtv_property),
      .props = enum_cmdargs
};
#endif

struct dtv_property dvbs_cmdargs[] = {
      { .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBS },
      { .cmd = DTV_FREQUENCY,       .u.data = 0 },
      { .cmd = DTV_MODULATION,      .u.data = QPSK },
      { .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
      { .cmd = DTV_SYMBOL_RATE,     .u.data = 27500000 },
      { .cmd = DTV_INNER_FEC,       .u.data = FEC_AUTO },
      { .cmd = DTV_TUNE },
};
struct dtv_properties dvbs_cmdseq = {
      .num = sizeof(dvbs_cmdargs)/sizeof(struct dtv_property),
      .props = dvbs_cmdargs
};

struct dtv_property dvbs2_cmdargs[] = {
      { .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBS2 },
      { .cmd = DTV_FREQUENCY,       .u.data = 0 },
      { .cmd = DTV_MODULATION,      .u.data = PSK_8 },
      { .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
      { .cmd = DTV_SYMBOL_RATE,     .u.data = 27500000 },
      { .cmd = DTV_INNER_FEC,       .u.data = FEC_AUTO },
      { .cmd = DTV_PILOT,           .u.data = PILOT_AUTO },
      { .cmd = DTV_ROLLOFF,         .u.data = ROLLOFF_AUTO },
      { .cmd = DTV_STREAM_ID,       .u.data = 0 },
      { .cmd = DTV_TUNE },
};
struct dtv_properties dvbs2_cmdseq = {
      .num = sizeof(dvbs2_cmdargs)/sizeof(struct dtv_property),
      .props = dvbs2_cmdargs
};

struct dtv_property dvbc_cmdargs[] = {
#if DVBAPI_VERSION >= 505
{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBC_ANNEX_A },
#else
{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBC_ANNEX_AC },
#endif
{ .cmd = DTV_FREQUENCY,       .u.data = 0 },
{ .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
{ .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
{ .cmd = DTV_SYMBOL_RATE,     .u.data = 27500000 },
{ .cmd = DTV_TUNE },
};
struct dtv_properties dvbc_cmdseq = {
      .num = sizeof(dvbc_cmdargs)/sizeof(struct dtv_property),
      .props = dvbc_cmdargs
};

struct dtv_property dvbt_cmdargs[] = {
      { .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBT },
      { .cmd = DTV_FREQUENCY,       .u.data = 0 },
      { .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
      { .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
      { .cmd = DTV_BANDWIDTH_HZ,    .u.data = 8000000 },
      { .cmd = DTV_CODE_RATE_HP,    .u.data = FEC_AUTO },
      { .cmd = DTV_CODE_RATE_LP,    .u.data = FEC_AUTO },
      { .cmd = DTV_GUARD_INTERVAL,  .u.data = GUARD_INTERVAL_AUTO },
      { .cmd = DTV_TRANSMISSION_MODE,.u.data = TRANSMISSION_MODE_AUTO },
      { .cmd = DTV_HIERARCHY,       .u.data = HIERARCHY_AUTO },
      { .cmd = DTV_TUNE },
};
struct dtv_properties dvbt_cmdseq = {
      .num = sizeof(dvbt_cmdargs)/sizeof(struct dtv_property),
      .props = dvbt_cmdargs
};

/* ATSC + DVB-C annex B */
struct dtv_property atsc_cmdargs[] = {
      { .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_ATSC },
      { .cmd = DTV_FREQUENCY,       .u.data = 0 },
      { .cmd = DTV_MODULATION,      .u.data = QAM_AUTO },
      { .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
      { .cmd = DTV_TUNE },
};
struct dtv_properties atsc_cmdseq = {
      .num = sizeof(atsc_cmdargs)/sizeof(struct dtv_property),
      .props = atsc_cmdargs
};

struct dtv_property pclear[] = {
      { .cmd = DTV_CLEAR },
};

struct dtv_properties cmdclear = {
      .num = 1,
      .props = pclear
};
