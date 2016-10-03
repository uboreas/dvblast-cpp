/*
 * cLdvbapp.h
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

#ifndef CLDVBAPP_H_
#define CLDVBAPP_H_

#include <cLdvbcore.h>

namespace libcLdvbapp {

   extern int cli(int i_argc, char **pp_argv);
   extern int run(int priority, int adapter, int freq, int srate, int volt, const char *configfile);

} /* namespace libcLdvbapp */

#endif /*CLDVBAPP_H_*/
