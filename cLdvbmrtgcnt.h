/*
 * cLdvbmrtgcnt.h
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * mrtg-cnt.h
 *****************************************************************************
 * Copyright Tripleplay service 2004,2005,2011
 *
 * Author:  Andy Lindsay <a.lindsay@tripleplay-services.com>
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

#ifndef CLDVB_MRTG_CNT_H_
#define CLDVB_MRTG_CNT_H_

#include <cLdvbcore.h>
#include <cLdvboutput.h>

// Define the dump period in seconds
#define CLDVB_MRTG_INTERVAL   10

namespace libcLdvbmrtgcnt {

   extern int mrtgInit(char *mrtg_file);
   extern void mrtgClose();
   extern void mrtgAnalyse(libcLdvboutput::block_t * p_ts);

} /* namespace libcLdvbmrtgcnt */

#endif /*CLDVB_MRTG_CNT_H_*/
