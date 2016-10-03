/*
 * cLdvbev.c
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

#include <cLdvbev.h>
#include <ev.h>

typedef void (*tmfcLevCB)(struct ev_loop *, struct ev_timer *, int);
typedef tmfcLevCB tmcLevCB;

typedef void (*iofcLevCB)(struct ev_loop *, struct ev_io *, int);
typedef iofcLevCB iocLevCB;

typedef void (*sigfcLevCB)(struct ev_loop *, struct ev_signal *, int);
typedef sigfcLevCB sigcLevCB;

void cLev_timer_stop(void *pel, void *pet)
{
   ev_timer_stop((struct ev_loop *)pel, (struct ev_timer *)pet);
}

void cLev_timer_set(void *pet, double after, double repeat)
{
   ev_timer_set((struct ev_timer *)pet, after, repeat);
}

void cLev_timer_start(void *pel, void *pet)
{
   ev_timer_start((struct ev_loop *)pel, (struct ev_timer *)pet);
}

void cLev_timer_init(void *pet, cLevCB cb, double after, double repeat)
{
   ev_timer_init((struct ev_timer *)pet, (tmcLevCB)cb, after, repeat);
}

void cLev_timer_again(void *pel, void *pet)
{
   ev_timer_again((struct ev_loop *)pel, (struct ev_timer *)pet);
}

void cLev_io_init(void *pew, cLevCB cb, int fd, int cmd)
{
   ev_io_init((struct ev_io *)pew, (iocLevCB)cb, fd, cmd);
}

void cLev_io_start(void *pel, void *pew)
{
   ev_io_start((struct ev_loop *)pel, (struct ev_io *)pew);
}

void cLev_io_stop(void *pel, void *pew)
{
   ev_io_stop((struct ev_loop *)pel, (struct ev_io *)pew);
}

void cLev_break(void *pel, int cmd)
{
   ev_break((struct ev_loop *)pel, cmd);
}

void cLev_signal_init(void *pes, cLevCB cb, int signum)
{
   ev_signal_init((struct ev_signal *)pes, (sigcLevCB)cb, signum);
}

void cLev_signal_start(void *pel, void *pes)
{
   ev_signal_start((struct ev_loop *)pel, (struct ev_signal *)pes);
}

void cLev_unref(void *pel)
{
   ev_unref((struct ev_loop *)pel);
}

void cLev_run(void *pel, int flags)
{
   ev_run((struct ev_loop *)pel, flags);
}

void *cLev_default_loop(unsigned int flags)
{
   return ev_default_loop(flags);
}
