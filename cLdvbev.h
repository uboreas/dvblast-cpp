/*
 * cLdvbev.h
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

#ifndef CLDVB_EV_H_
#define CLDVB_EV_H_

typedef void (*fcLevCB)(void *, void *, int);
typedef fcLevCB cLevCB;

#ifdef __cplusplus
extern "C" {
#endif

   typedef struct cLev_timer {
         int active;
         int pending;
         int priority;
         void *data;
         cLevCB cb;
         double at;
         double repeat;
   } cLev_timer;

   typedef struct cLev_io {
         int active;
         int pending;
         int priority;
         void *data;
         cLevCB cb;
         void *next;
         int fd;
         int events;
   } cLev_io;

   typedef struct cLev_signal {
         int active;
         int pending;
         int priority;
         void *data;
         cLevCB cb;
         void *next;
         int signum;
   } cLev_signal;

   extern void cLev_timer_stop(void *pel, void *pet);
   extern void cLev_timer_set(void *pet, double after, double repeat);
   extern void cLev_timer_start(void *pel, void *pet);
   extern void cLev_timer_init(void *pet, cLevCB cb, double after, double repeat);
   extern void cLev_timer_again(void *pel, void *pet);
   extern void cLev_io_init(void *pew, cLevCB cb, int fd, int cmd);
   extern void cLev_io_start(void *pel, void *pew);
   extern void cLev_io_stop(void *pel, void *pew);
   extern void cLev_break(void *pel, int cmd);
   extern void cLev_signal_init(void *pes, cLevCB cb, int signum);
   extern void cLev_signal_start(void *pel, void *pes);
   extern void cLev_unref(void *pel);
   extern void cLev_run(void *pel, int flags);
   extern void *cLev_default_loop(unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /*CLDVB_EV_H_*/
