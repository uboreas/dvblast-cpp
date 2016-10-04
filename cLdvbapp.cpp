/*
 * cLdvbapp.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 *****************************************************************************
 * dvblast.c
 *****************************************************************************
 * Copyright (C) 2004, 2008-2011, 2015 VideoLAN
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Andy Gatward <a.j.gatward@reading.ac.uk>
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

#include <cLdvboutput.h>
#include <cLdvbmrtgcnt.h>
#include <cLdvben50221.h>
#include <cLdvbdemux.h>
#include <cLdvbev.h>
#ifdef HAVE_CLDVBHW
#include <cLdvbdev.h>
#endif
#ifdef HAVE_CLASIHW
#include <cLdvbasi.h>
#endif
#include <cLdvbudp.h>
#include <cLdvbcomm.h>
#include <cLdvbapp.h>

#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>

namespace libcLdvbapp {

   static int i_priority = -1;
#ifdef HAVE_CLDVBHW
   static int i_dvr_buffer_size;
#endif
   char *psz_syslog_ident = NULL;
   /* TPS Input log filename */
   char * psz_mrtg_file = NULL;
   char *psz_dup_config = NULL;

   static void signal_watcher_init(struct cLev_signal *w, void *loop, cLevCB cb, int signum)
   {
      cLev_signal_init(w, cb, signum);
      cLev_signal_start(loop, w);
      cLev_unref(loop);
   }

   //struct ev_signal *
   static void sighandler(void *loop, void *p, int revents)
   {
      struct cLev_signal *w = (struct cLev_signal *)p;
      switch (w->signum) {
         case SIGINT:
         case SIGTERM:
         default:
            cLbug(cL::dbg_dvb, "Shutdown was requested.\n" );
            cLev_break(loop, 2); //EVBREAK_ALL
            break;
         case SIGHUP:
            cLbug(cL::dbg_dvb, "Configuration reload was requested.\n" );
            libcLdvbdemux::config_ReadFile();
            break;
      }
   }

#ifdef HAVE_CLDVBHW
   //struct ev_timer *
   static void quit_cb(void *loop, void *w, int revents)
   {
      cLev_break(loop, 2); //EVBREAK_ALL
   }
#endif

   int start(const char *netname, const char *proname)
   {
      struct sched_param param;
      int i_error;
      struct cLev_signal sigint_watcher, sigterm_watcher, sighup_watcher;
#ifdef HAVE_CLDVBHW
      struct cLev_timer quit_watcher;
#endif

      if ( libcLdvboutput::b_udp_global ) {
         cLbug(cL::dbg_dvb, "raw UDP output is deprecated.  Please consider using RTP.\n" );
         cLbug(cL::dbg_dvb, "for DVB-IP compliance you should use RTP.\n" );
      }

      if ( libcLdvboutput::b_epg_global && !libcLdvboutput::b_dvb_global ) {
         cLbug(cL::dbg_dvb, "turning on DVB compliance, required by EPG information\n" );
         libcLdvboutput::b_dvb_global = true;
      }

      if ((libcLdvb::event_loop = cLev_default_loop(0)) == NULL) {
         cLbug(cL::dbg_dvb, "unable to initialize libev\n" );
         return 1;
      }
      cLbug(cL::dbg_dvb, "event_loop created\n");

      memset( &libcLdvboutput::output_dup, 0, sizeof(libcLdvboutput::output_dup) );
      if ( psz_dup_config != NULL) {
         libcLdvboutput::output_config_t config;
         libcLdvboutput::config_Defaults( &config );
         if ( !libcLdvboutput::config_ParseHost( &config, psz_dup_config ) ) {
            cLbug(cL::dbg_dvb, "Invalid target address for -d switch\n" );
         } else {
            libcLdvboutput::output_Init( &libcLdvboutput::output_dup, &config );
            libcLdvboutput::output_Change( &libcLdvboutput::output_dup, &config );
         }
         libcLdvboutput::config_Free( &config );
      }

      libcLdvboutput::config_strdvb( &libcLdvboutput::network_name, netname );
      libcLdvboutput::config_strdvb( &libcLdvboutput::provider_name, proname );

      /* Set signal handlers */
      signal_watcher_init(&sigint_watcher, libcLdvb::event_loop, sighandler, SIGINT);
      signal_watcher_init(&sigterm_watcher, libcLdvb::event_loop, sighandler, SIGTERM);
      signal_watcher_init(&sighup_watcher, libcLdvb::event_loop, sighandler, SIGHUP);

      srand( time(NULL) * getpid() );

      libcLdvbdemux::demux_Open();

      // init the mrtg logfile
      libcLdvbmrtgcnt::mrtgInit(psz_mrtg_file);

      if ( i_priority > 0 ) {
         memset( &param, 0, sizeof(struct sched_param) );
         param.sched_priority = i_priority;
         if ( (i_error = pthread_setschedparam( pthread_self(), SCHED_RR, &param )) ) {
            cLbugf(cL::dbg_dvb, "couldn't set thread priority: %s\n", strerror(i_error) );
         }
      }

      libcLdvbdemux::config_ReadFile();

      if ( libcLdvbcomm::psz_srv_socket != NULL )
          libcLdvbcomm::comm_Open();

#ifdef HAVE_CLDVBHW
      if ( libcLdvbdev::i_quit_timeout_duration ) {
         cLev_timer_init(&quit_watcher, libcLdvbapp::quit_cb, libcLdvbdev::i_quit_timeout_duration / 1000000., 0);
         cLev_timer_start(libcLdvb::event_loop, &quit_watcher);
      }
#endif

      libcLdvboutput::outputs_Init();

      // main loop
      cLev_run(libcLdvb::event_loop, 0);

      libcLdvbmrtgcnt::mrtgClose();
      libcLdvboutput::outputs_Close( libcLdvboutput::i_nb_outputs );
      libcLdvbdemux::demux_Close();
      libcLdvboutput::dvb_string_clean( &libcLdvboutput::network_name );
      libcLdvboutput::dvb_string_clean( &libcLdvboutput::provider_name );

      libcLdvbcomm::comm_Close();
      libcLdvboutput::block_Vacuum();

      return 0;
   }

   void cliversion()
   {
      cLbugf(cL::dbg_dvb, "DVBlast %s (%s)\n", DVB_VERSION, DVB_VERSION_EXTRA);
   }

   int cliusage()
   {
      cliversion();
      cLbug(cL::dbg_dvb, "Usage: dvblast [-q] [-c <config file>] [-t <ttl>] [-o <SSRC IP>] "
            "[-i <RT priority>] "
#ifdef HAVE_CLASIHW
            "[-A <ASI adapter>]"
#endif
#ifdef HAVE_CLDVBHW
            "[-a <adapter>] [-n <frontend number>] [-S <diseqc>] [-k <uncommitted port>]"
            "[-f <frequency>]"
            "[-s <symbol rate>] [-v <0|13|18>] [-p] [-b <bandwidth>] [-I <inversion>] "
            "[-F <fec inner>] [-m <modulation] [-R <rolloff>] [-P <pilot>] [-K <fec lp>] "
            "[-G <guard interval>] [-H <hierarchy>] [-X <transmission>] [-O <lock timeout>] "
#endif
            "[-D [<src host>[:<src port>]@]<src mcast>[:<port>][/<opts>]*] "
            "[-u] [-w] [-U] [-L <latency>] [-E <retention>] [-d <dest IP>[<:port>][/<opts>]*] [-3] "
            "[-z] [-C [-e] [-M <network name>] [-N <network ID>]] [-T] [-j <system charset>] "
            "[-W] [-Y] [-l] [-g <logger ident>] [-Z <mrtg file>] [-V] [-h] [-B <provider_name>] "
            "[-1 <mis_id>] [-2 <size>] [-5 <DVBS|DVBS2|DVBC_ANNEX_A|DVBT|ATSC>] -y <ca_dev_number> "
            "[-J <DVB charset>] [-Q <quit timeout>] [-0 pid_mapping] [-x <text|xml>]"
            "[-6 <print period>] [-7 <ES timeout>]\n" );

      cLbug(cL::dbg_dvb, "Input:\n");
#ifdef HAVE_CLASIHW
      cLbug(cL::dbg_dvb, "  -A --asi-adapter      read packets from an ASI adapter (0-n)\n");
#endif
#ifdef HAVE_CLDVBHW
      cLbug(cL::dbg_dvb, "  -a --adapter          read packets from a Linux-DVB adapter (typically 0-n)\n");
      cLbug(cL::dbg_dvb, "  -b --bandwidth        frontend bandwith\n");
#endif
      cLbug(cL::dbg_dvb, "  -D --rtp-input        read packets from a multicast address instead of a DVB card\n");
#ifdef HAVE_CLDVBHW
      cLbug(cL::dbg_dvb, "  -5 --delsys           delivery system\n");
      cLbug(cL::dbg_dvb, "    DVBS|DVBS2|DVBC_ANNEX_A|DVBT|ATSC (default guessed)\n");
      cLbug(cL::dbg_dvb, "  -f --frequency        frontend frequency\n");
      cLbug(cL::dbg_dvb, "  -F --fec-inner        Forward Error Correction (FEC Inner)\n");
      cLbug(cL::dbg_dvb, "    DVB-S2 0|12|23|34|35|56|78|89|910|999 (default auto: 999)\n");
      cLbug(cL::dbg_dvb, "  -I --inversion        Inversion (-1 auto, 0 off, 1 on)\n");
      cLbug(cL::dbg_dvb, "  -m --modulation       Modulation type\n");
      cLbug(cL::dbg_dvb, "    DVB-C  qpsk|qam_16|qam_32|qam_64|qam_128|qam_256 (default qam_auto)\n");
      cLbug(cL::dbg_dvb, "    DVB-T  qam_16|qam_32|qam_64|qam_128|qam_256 (default qam_auto)\n");
      cLbug(cL::dbg_dvb, "    DVB-S2 qpsk|psk_8 (default legacy DVB-S)\n");
      cLbug(cL::dbg_dvb, "  -n --frontend-number <frontend number>\n");
      cLbug(cL::dbg_dvb, "  -p --force-pulse      force 22kHz pulses for high-band selection (DVB-S)\n");
      cLbug(cL::dbg_dvb, "  -P --pilot            DVB-S2 Pilot (-1 auto, 0 off, 1 on)\n");
      cLbug(cL::dbg_dvb, "  -R --rolloff          DVB-S2 Rolloff value\n");
      cLbug(cL::dbg_dvb, "    DVB-S2 35=0.35|25=0.25|20=0.20|0=AUTO (default: 35)\n");
      cLbug(cL::dbg_dvb, "  -1 --multistream-id   Set stream ID (0-255, default: 0)\n");
      cLbug(cL::dbg_dvb, "  -K --fec-lp           DVB-T low priority FEC (default auto)\n");
      cLbug(cL::dbg_dvb, "  -G --guard            DVB-T guard interval\n");
      cLbug(cL::dbg_dvb, "    DVB-T  32 (1/32)|16 (1/16)|8 (1/8)|4 (1/4)|-1 (auto, default)\n");
      cLbug(cL::dbg_dvb, "  -H --hierarchy        DVB-T hierarchy (0, 1, 2, 4 or -1 auto, default)\n");
      cLbug(cL::dbg_dvb, "  -X --transmission     DVB-T transmission (2, 4, 8 or -1 auto, default)\n");
      cLbug(cL::dbg_dvb, "  -s --symbol-rate\n");
      cLbug(cL::dbg_dvb, "  -S --diseqc           satellite number for diseqc (0: no diseqc, 1-4, A or B)\n");
      cLbug(cL::dbg_dvb, "  -k --uncommitted      port number for uncommitted diseqc (0: no uncommitted diseqc, 1-4)\n");
      cLbug(cL::dbg_dvb, "  -u --budget-mode      turn on budget mode (no hardware PID filtering)\n");
      cLbug(cL::dbg_dvb, "  -v --voltage          voltage to apply to the LNB (QPSK)\n");
      cLbug(cL::dbg_dvb, "  -w --select-pmts      set a PID filter on all PMTs (auto on, when config file is used)\n");
      cLbug(cL::dbg_dvb, "  -O --lock-timeout     timeout for the lock operation (in ms)\n");
      cLbug(cL::dbg_dvb, "  -y --ca-number <ca_device_number>\n");
      cLbugf(cL::dbg_dvb, "  -2 --dvr-buf-size <size> set the size of the DVR TS buffer in bytes (default: %d)\n", i_dvr_buffer_size);
#endif

      cLbug(cL::dbg_dvb, "Output:\n");
      cLbug(cL::dbg_dvb, "  -c --config-file <config file>\n");
      cLbug(cL::dbg_dvb, "  -C --dvb-compliance   pass through or build the mandatory DVB tables\n");
      cLbug(cL::dbg_dvb, "  -d --duplicate        duplicate all received packets to a given destination\n");
      cLbug(cL::dbg_dvb, "  -3 --passthrough      duplicate all received packets to stdout\n");
      cLbug(cL::dbg_dvb, "  -W --emm-passthrough  pass through EMM data (CA system data)\n");
      cLbug(cL::dbg_dvb, "  -Y --ecm-passthrough  pass through ECM data (CA program data)\n");
      cLbug(cL::dbg_dvb, "  -e --epg-passthrough  pass through DVB EIT schedule tables\n");
      cLbug(cL::dbg_dvb, "  -E --retention        maximum retention allowed between input and output (default: 40 ms)\n");
      cLbug(cL::dbg_dvb, "  -L --latency          maximum latency allowed between input and output (default: 100 ms)\n");
      cLbug(cL::dbg_dvb, "  -M --network-name     DVB network name to declare in the NIT\n");
      cLbug(cL::dbg_dvb, "  -N --network-id       DVB network ID to declare in the NIT\n");
      cLbug(cL::dbg_dvb, "  -B --provider-name    Service provider name to declare in the SDT\n");
      cLbug(cL::dbg_dvb, "  -o --rtp-output <SSRC IP>\n");
      cLbug(cL::dbg_dvb, "  -t --ttl <ttl>        TTL of the output stream\n");
      cLbug(cL::dbg_dvb, "  -T --unique-ts-id     generate random unique TS ID for each output\n");
      cLbug(cL::dbg_dvb, "  -U --udp              use raw UDP rather than RTP (required by some IPTV set top boxes)\n");
      cLbug(cL::dbg_dvb, "  -z --any-type         pass through all ESs from the PMT, of any type\n");
      cLbug(cL::dbg_dvb, "  -0 --pidmap <pmt_pid,audio_pid,video_pid,spu_pid>\n");

      cLbug(cL::dbg_dvb, "Misc:\n");
      cLbug(cL::dbg_dvb, "  -h --help             display this full help\n");
      cLbug(cL::dbg_dvb, "  -i --priority <RT priority>\n");
      cLbug(cL::dbg_dvb, "  -j --system-charset   character set used for printing messages (default UTF-8)\n");
      cLbug(cL::dbg_dvb, "  -J --dvb-charset      character set used in output DVB tables (default UTF-8)\n");
#ifdef HAVE_CLDVBHW
      cLbug(cL::dbg_dvb, "  -Q --quit-timeout     when locked, quit after this delay (in ms), or after the first lock timeout\n");
#endif
      cLbug(cL::dbg_dvb, "  -6 --print-period     periodicity at which we print bitrate and errors (in ms)\n");
      cLbug(cL::dbg_dvb, "  -7 --es-timeout       time of inactivy before which a PID is reported down (in ms)\n");
      cLbug(cL::dbg_dvb, "  -Z --mrtg-file <file> Log input packets and errors into mrtg-file\n");
      cLbug(cL::dbg_dvb, "  -V --version          only display the version\n");
      return 1;
   }

   int cli(int i_argc, char **pp_argv)
   {
      const char *psz_network_name = "DVBlast - http://www.videolan.org/projects/dvblast.html";
      const char *psz_provider_name = (const char *) 0;
      int c;

      if (i_argc == 1)
         return cliusage();

      /*
       * The only short options left are: 489
       * Use them wisely.
       */
      static const struct option long_options[] =
      {
            { "config-file",     required_argument, NULL, 'c' },
            { "remote-socket",   required_argument, NULL, 'r' },
            { "ttl",             required_argument, NULL, 't' },
            { "rtp-output",      required_argument, NULL, 'o' },
            { "priority",        required_argument, NULL, 'i' },
            { "adapter",         required_argument, NULL, 'a' },
            { "frontend-number", required_argument, NULL, 'n' },
            { "delsys",          required_argument, NULL, '5' },
            { "frequency",       required_argument, NULL, 'f' },
            { "fec-inner",       required_argument, NULL, 'F' },
            { "rolloff",         required_argument, NULL, 'R' },
            { "symbol-rate",     required_argument, NULL, 's' },
            { "diseqc",          required_argument, NULL, 'S' },
            { "uncommitted",     required_argument, NULL, 'k' },
            { "voltage",         required_argument, NULL, 'v' },
            { "force-pulse",     no_argument,       NULL, 'p' },
            { "bandwidth",       required_argument, NULL, 'b' },
            { "inversion",       required_argument, NULL, 'I' },
            { "modulation",      required_argument, NULL, 'm' },
            { "pilot",           required_argument, NULL, 'P' },
            { "multistream-id",  required_argument, NULL, '1' },
            { "fec-lp",          required_argument, NULL, 'K' },
            { "guard",           required_argument, NULL, 'G' },
            { "hierarchy",       required_argument, NULL, 'H' },
            { "transmission",    required_argument, NULL, 'X' },
            { "lock-timeout",    required_argument, NULL, 'O' },
            { "budget-mode",     no_argument,       NULL, 'u' },
            { "select-pmts",     no_argument,       NULL, 'w' },
            { "udp",             no_argument,       NULL, 'U' },
            { "unique-ts-id",    no_argument,       NULL, 'T' },
            { "latency",         required_argument, NULL, 'L' },
            { "retention",       required_argument, NULL, 'E' },
            { "duplicate",       required_argument, NULL, 'd' },
            { "passthrough",     no_argument,       NULL, '3' },
            { "rtp-input",       required_argument, NULL, 'D' },
            { "asi-adapter",     required_argument, NULL, 'A' },
            { "any-type",        no_argument,       NULL, 'z' },
            { "dvb-compliance",  no_argument,       NULL, 'C' },
            { "emm-passthrough", no_argument,       NULL, 'W' },
            { "ecm-passthrough", no_argument,       NULL, 'Y' },
            { "epg-passthrough", no_argument,       NULL, 'e' },
            { "network-name",    no_argument,       NULL, 'M' },
            { "network-id",      no_argument,       NULL, 'N' },
            { "system-charset",  required_argument, NULL, 'j' },
            { "dvb-charset",     required_argument, NULL, 'J' },
            { "provider-name",   required_argument, NULL, 'B' },
            { "quit-timeout",    required_argument, NULL, 'Q' },
            { "print-period",    required_argument, NULL, '6' },
            { "es-timeout",      required_argument, NULL, '7' },
            { "help",            no_argument,       NULL, 'h' },
            { "version",         no_argument,       NULL, 'V' },
            { "mrtg-file",       required_argument, NULL, 'Z' },
            { "ca-number",       required_argument, NULL, 'y' },
            { "pidmap",          required_argument, NULL, '0' },
            { "dvr-buf-size",    required_argument, NULL, '2' },
            { 0, 0, 0, 0 }
      };

      while ((c = getopt_long(i_argc, pp_argv, "c:r:t:o:i:a:n:5:f:F:R:s:S:k:v:pb:I:m:P:K:G:H:X:O:uwUTL:E:d:3D:A:zCWYeM:N:j:J:B:Q:6:7:hVZ:y:0:1:2:", long_options, NULL)) != -1) {
         switch (c) {
            case 'c':
               libcLdvbdemux::psz_conf_file = optarg;
               /*
                * When configuration file is used it is reasonable to assume that
                * services may be added/removed. If libcLdvb::b_select_pmts is not set dvblast
                * is unable to start streaming newly added services in the config.
                */
               libcLdvbdemux::b_select_pmts = 1;
               break;
            case 'r':
                libcLdvbcomm::psz_srv_socket = optarg;
                break;
            case 't':
               libcLdvboutput::i_ttl_global = strtol( optarg, NULL, 0 );
               break;
            case 'o': {
               struct in_addr maddr;
               if (!inet_aton( optarg, &maddr ))
                  return cliusage();
               memcpy( libcLdvboutput::pi_ssrc_global, &maddr.s_addr, 4 * sizeof(uint8_t) );
               break;
            }
            case 'i':
               i_priority = strtol( optarg, NULL, 0 );
               break;
#ifdef HAVE_CLDVBHW
            case 'a':
               libcLdvb::i_adapter = strtol( optarg, NULL, 0 );
               break;
            case 'y':
               libcLdvben50221::i_canum = strtol( optarg, NULL, 0 );
               break;
            case 'n':
               libcLdvbdev::i_fenum = strtol( optarg, NULL, 0 );
               break;
            case '5':
               libcLdvbdev::psz_delsys = optarg;
               break;
            case 'f':
               if (optarg && optarg[0] != '-')
                  libcLdvbdev::i_frequency = strtol( optarg, NULL, 0 );
               if ( libcLdvbdemux::pf_Open != NULL )
                  return cliusage();
               libcLdvbdemux::pf_Open = libcLdvbdev::dvb_Open;
               libcLdvbdemux::pf_Reset = libcLdvbdev::dvb_Reset;
               libcLdvbdemux::pf_SetFilter = libcLdvbdev::dvb_SetFilter;
               libcLdvbdemux::pf_UnsetFilter = libcLdvbdev::dvb_UnsetFilter;
               libcLdvben50221::pf_ResendCAPMTs = libcLdvbdemux::demux_ResendCAPMTs;
               libcLdvben50221::pf_PIDIsSelected = libcLdvbdemux::demux_PIDIsSelected;
               break;
            case 'F':
               libcLdvbdev::i_fec = strtol( optarg, NULL, 0 );
               break;
            case 'R':
               libcLdvbdev::i_rolloff = strtol( optarg, NULL, 0 );
               break;
            case 's':
               libcLdvbdev::i_srate = strtol( optarg, NULL, 0 );
               break;
            case 'S':
               libcLdvbdev::i_satnum = strtol( optarg, NULL, 16 );
               break;
            case 'k':
               libcLdvbdev::i_uncommitted = strtol( optarg, NULL, 16 );
               break;
            case 'v':
               libcLdvbdev::i_voltage = strtol( optarg, NULL, 0 );
               break;
            case 'p':
               libcLdvbdev::b_tone = 1;
               break;
            case 'b':
               libcLdvbdev::i_bandwidth = strtol( optarg, NULL, 0 );
               break;
            case 'I':
               libcLdvbdev::i_inversion = strtol( optarg, NULL, 0 );
               break;
            case 'm':
               libcLdvbdev::psz_modulation = optarg;
               break;
            case 'P':
               libcLdvbdev::i_pilot = strtol( optarg, NULL, 0 );
               break;
            case '1':
               libcLdvbdev::i_mis = strtol( optarg, NULL, 0 );
               break;
            case 'K':
               libcLdvbdev::i_fec_lp = strtol( optarg, NULL, 0 );
               break;
            case 'G':
               libcLdvbdev::i_guard = strtol( optarg, NULL, 0 );
               break;
            case 'X':
               libcLdvbdev::i_transmission = strtol( optarg, NULL, 0 );
               break;
            case 'O':
               libcLdvbdev::i_frontend_timeout_duration = strtoll( optarg, NULL, 0 ) * 1000;
               break;
            case 'H':
               libcLdvbdev::i_hierarchy = strtol( optarg, NULL, 0 );
               break;
            case 'Q':
               libcLdvbdev::i_quit_timeout_duration = strtoll( optarg, NULL, 0 ) * 1000;
               break;
            case '2':
               i_dvr_buffer_size = strtol( optarg, NULL, 0 );
               if (!i_dvr_buffer_size)
                  return cliusage();   // it exits
               /* roundup to packet size */
               i_dvr_buffer_size += TS_SIZE - 1;
               i_dvr_buffer_size /= TS_SIZE;
               i_dvr_buffer_size *= TS_SIZE;
               break;
            case 'u':
               libcLdvbdemux::b_budget_mode = 1;
               break;
            case 'w':
               libcLdvbdemux::b_select_pmts = !libcLdvbdemux::b_select_pmts;
               break;
#endif
            case 'U':
               libcLdvboutput::b_udp_global = true;
               break;
            case 'L':
               libcLdvboutput::i_latency_global = strtoll( optarg, NULL, 0 ) * 1000;
               break;
            case 'E':
               libcLdvboutput::i_retention_global = strtoll( optarg, NULL, 0 ) * 1000;
               break;
            case 'd':
               psz_dup_config = optarg;
               break;
            case '3':
               break;
            case 'D':
               libcLdvbudp::psz_udp_src = optarg;
               if ( libcLdvbdemux::pf_Open != NULL )
                  return cliusage();
               libcLdvbdemux::pf_Open = libcLdvbudp::udp_Open;
               libcLdvbdemux::pf_Reset = libcLdvbudp::udp_Reset;
               libcLdvbdemux::pf_SetFilter = libcLdvbudp::udp_SetFilter;
               libcLdvbdemux::pf_UnsetFilter = libcLdvbudp::udp_UnsetFilter;
               break;
#ifdef HAVE_CLASIHW
            case 'A':
               if ( libcLdvbdemux::pf_Open != NULL )
                  return cliusage();
               if ( strncmp(optarg, "deltacast:", 10) == 0) {
                  cLbug(cL::dbg_dvb, "DVBlast is compiled without Deltacast ASI support.\n");
                  return 1;
               } else {
                  libcLdvbasi::i_asi_adapter = strtol( optarg, NULL, 0 );
                  libcLdvbdemux::pf_Open = libcLdvbasi::asi_Open;
                  libcLdvbdemux::pf_Reset = libcLdvbasi::asi_Reset;
                  libcLdvbdemux::pf_SetFilter = libcLdvbasi::asi_SetFilter;
                  libcLdvbdemux::pf_UnsetFilter = libcLdvbasi::asi_UnsetFilter;
               }
               break;
#endif
            case 'z':
               libcLdvbdemux::b_any_type = 1;
               break;
            case 'C':
               libcLdvboutput::b_dvb_global = true;
               break;
            case 'W':
               libcLdvbdemux::b_enable_emm = true;
               break;
            case 'Y':
               libcLdvbdemux::b_enable_ecm = true;
               break;
            case 'e':
               libcLdvboutput::b_epg_global = true;
               break;
            case 'M':
               psz_network_name = optarg;
               break;
            case 'N':
               libcLdvboutput::i_network_id = strtoul( optarg, NULL, 0 );
               break;
            case 'T':
               libcLdvboutput::b_random_tsid = 1;
               break;
            case 'j':
               libcLdvb::psz_native_charset = optarg;
               break;
            case 'J':
               libcLdvboutput::psz_dvb_charset = optarg;
               break;
            case 'B':
               psz_provider_name = optarg;
               break;
            case 'g':
               psz_syslog_ident = optarg;
               break;
            case '6':
               libcLdvb::i_print_period = strtoll( optarg, NULL, 0 ) * 1000;
               break;
            case '7':
               libcLdvbdemux::i_es_timeout = strtoll( optarg, NULL, 0 ) * 1000;
               break;
            case 'V':
               cliversion();
               return 0;
               break;
            case 'Z':
               psz_mrtg_file = optarg;
               break;
            case '0': {
               /* We expect a comma separated list of numbers.
               Put them into the libcLdvb::pi_newpids array as they appear */
               char *str1;
               char *saveptr = NULL;
               char *tok = NULL;
               int i, i_newpid;
               for (i = 0, str1 = optarg; i < CLDVB_N_MAP_PIDS; i++, str1 = NULL) {
                  tok = strtok_r(str1, ",", &saveptr);
                  if ( !tok )
                     break;
                  i_newpid = strtoul(tok, NULL, 0);
                  if ( !i_newpid ) {
                     cLbug(cL::dbg_dvb, "Invalid pidmap string\n" );
                     return cliusage();
                  }
                  libcLdvbdemux::pi_newpids[i] = i_newpid;
               }
               libcLdvboutput::b_do_remap = true;
               break;
            }
            case 'h':
            default:
               return cliusage();
         }
      }
      if ( optind < i_argc || libcLdvbdemux::pf_Open == NULL )
         return cliusage();

      cliversion();
      cLbug(cL::dbg_dvb, "restarting\n" );

      return start(psz_network_name, psz_provider_name);
   }

   int run(int priority, int adapter, int freq, int srate, int volt, const char *configfile)
   {
      libcLdvbdemux::psz_conf_file = configfile;
      libcLdvbdemux::b_select_pmts = 1;
      i_priority = priority;

      libcLdvb::i_adapter = adapter;

#ifdef HAVE_CLDVBHW
      libcLdvbdev::i_frequency = freq;
      libcLdvbdemux::pf_Open = libcLdvbdev::dvb_Open;
      libcLdvbdemux::pf_Reset = libcLdvbdev::dvb_Reset;
      libcLdvbdemux::pf_SetFilter = libcLdvbdev::dvb_SetFilter;
      libcLdvbdemux::pf_UnsetFilter = libcLdvbdev::dvb_UnsetFilter;
      libcLdvben50221::pf_ResendCAPMTs = libcLdvbdemux::demux_ResendCAPMTs;
      libcLdvben50221::pf_PIDIsSelected = libcLdvbdemux::demux_PIDIsSelected;
      libcLdvbdev::i_srate = srate;
      libcLdvbdev::i_voltage = volt;
#endif

      return start("cLdvb http://kylone.com/", "Kylone");
   }

} /* namespace libcLdvbapp */
