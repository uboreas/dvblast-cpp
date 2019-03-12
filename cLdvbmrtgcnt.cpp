/*
 * cLdvbmrtgcnt.cpp
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
 *****************************************************************************
 * mrtg-cnt.c Handle dvb TS packets and count them for MRTG
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

#include <cLdvbmrtgcnt.h>

#include <sys/time.h>
#include <string.h>

cLdvbmrtgcnt::cLdvbmrtgcnt()
{
   this->mrtg_fh = (FILE *) 0;
   this->l_mrtg_packets = 0;            // Packets received
   this->l_mrtg_seq_err_packets = 0;    // Out of sequence packets received
   this->l_mrtg_error_packets = 0;      // Packets received with the error flag set
   this->l_mrtg_scram_packets = 0;      // Scrambled Packets received
#ifndef HAVE_CLWIN32
   this->mrtg_time.tv_sec = 0;
   this->mrtg_time.tv_usec = 0;
#endif
   cLbug(cL::dbg_high, "cLdvbmrtgcnt created\n");
}

cLdvbmrtgcnt::~cLdvbmrtgcnt()
{
   cLbug(cL::dbg_high, "cLdvbmrtgcnt deleted\n");
}

// Report the mrtg counters: bytes received, error packets & sequence errors
void cLdvbmrtgcnt::dumpCounts()
{
   unsigned int multiplier = CLDVB_MRTG_INTERVAL;
   if (this->mrtg_fh) {
      rewind(this->mrtg_fh);
      fprintf(this->mrtg_fh, "%lld %lld %lld %lld\n",
            this->l_mrtg_packets * 188 * multiplier,
            this->l_mrtg_error_packets * multiplier,
            this->l_mrtg_seq_err_packets * multiplier,
            this->l_mrtg_scram_packets * multiplier
      );
      fflush(this->mrtg_fh);
   }
}

// analyse the input block counting packets and errors
// The input is a pointer to a cLdvboutput::block_t structure, which might be a linked list
// of blocks. Each block has one TS packet.
void cLdvbmrtgcnt::mrtgAnalyse(cLdvboutput::block_t *p_ts)
{
   unsigned int i_pid;
   cLdvboutput::block_t *p_block = p_ts;

   if (this->mrtg_fh == (FILE *) 0)
      return;

   while (p_block != (cLdvboutput::block_t *) 0) {
      uint8_t *ts_packet = p_block->p_ts;

      char i_seq, i_last_seq;
      ++this->l_mrtg_packets;

      if (ts_packet[0] != 0x47) {
         ++l_mrtg_error_packets;
         p_block = p_block->p_next;
         continue;
      }

      if (ts_packet[1] & 0x80) {
         ++l_mrtg_error_packets;
         p_block = p_block->p_next;
         continue;
      }

      i_pid = (ts_packet[1] & 0x1f) << 8 | ts_packet[2];

      // Just count null packets - don't check the sequence numbering
      if (i_pid == 0x1fff) {
         p_block = p_block->p_next;
         continue;
      }

      if (ts_packet[3] & 0xc0)
         ++l_mrtg_scram_packets;

      // Check the sequence numbering
      i_seq = ts_packet[3] & 0xf;
      i_last_seq = i_pid_seq[i_pid];

      if (i_last_seq == -1) {
         // First packet - ignore the sequence
      } else
      if (ts_packet[3] & 0x10) {
         // Packet contains payload - sequence should be up by one
         if (i_seq != ((i_last_seq + 1) & 0x0f))
            ++l_mrtg_seq_err_packets;
      } else {
         // Packet contains no payload - sequence should be unchanged
         if (i_seq != i_last_seq)
            ++l_mrtg_seq_err_packets;
      }
      i_pid_seq[i_pid] = i_seq;

      // Look at next block
      p_block = p_block->p_next;
   }

   // All blocks processed. See if we need to dump the stats
   struct timeval now;
   gettimeofday(&now, (struct timezone *) 0);
   if (timercmp(&now, &this->mrtg_time, >)) {
      // Time to report the mrtg counters
      this->dumpCounts();

      // Set the timer for next time
      //
      // Normally we add the interval to the previous time so that if one
      // dump is a bit late, the next one still occurs at the correct time.
      // However, if there is a long gap (e.g. because the channel has
      // stopped for some time), then just rebase the timing to the current
      // time.  I've chosen CLDVB_MRTG_INTERVAL as the long gap - this is arbitary
      if ((now.tv_sec - this->mrtg_time.tv_sec) > CLDVB_MRTG_INTERVAL) {
         cLbugf(cL::dbg_dvb, "Dump is %d seconds late - reset timing\n", (int)(now.tv_sec - this->mrtg_time.tv_sec));
         this->mrtg_time = now;
      }
      this->mrtg_time.tv_sec += CLDVB_MRTG_INTERVAL;
   }
}

int cLdvbmrtgcnt::mrtgInit(const char *mrtg_file)
{
   if (mrtg_file == (const char *) 0)
      return -1;

   /* Open MRTG file */
   cLbugf(cL::dbg_dvb, "Opening mrtg file %s.\n", mrtg_file);
   if ((fopen(this->mrtg_fh, mrtg_file, "wb")) == (FILE *) 0) {
      cLbug(cL::dbg_dvb, "unable to open mrtg file");
      return -1;
   }
   // Initialise the file
   fprintf(this->mrtg_fh, "0 0 0 0\n");
   fflush(this->mrtg_fh);

   // Initialise the sequence numbering
   memset(&i_pid_seq[0], -1, sizeof(signed char) * CLDVB_MRTG_PIDS);

   // Set the reporting timer
   gettimeofday(&this->mrtg_time, (struct timezone *) 0);
   this->mrtg_time.tv_sec += CLDVB_MRTG_INTERVAL;

   return 0;
}

void cLdvbmrtgcnt::mrtgClose()
{
   // This is only for testing when using filetest.
   if (this->mrtg_fh) {
      this->dumpCounts();
      fclose(this->mrtg_fh);
      this->mrtg_fh = (FILE *) 0;
   }
}

