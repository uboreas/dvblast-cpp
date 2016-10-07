/*
 * dvblastpp.cpp
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

#include <cLdvbapp.h>

int main(int argc, char **argv)
{
   cL::debug = 1;
   cL::severity = cL::dbg_all;

   cLdvbapp *pobj = new cLdvbapp();
   int i = pobj->cli(argc, argv);
   //int i = pobj->run(1, 1, 10000000, 20000000, 13, "/path/to/conf/file");
   delete(pobj);
   return i;
}

