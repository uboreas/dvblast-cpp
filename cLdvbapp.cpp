/*
 * cLdvbapp.cpp
 * Authors: Gokhan Poyraz <gokhan@kylone.com>
 *
 * Based on code from:
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

#ifdef HAVE_CLDVBHW
#include <cLdvbdev.h>
#endif
#ifdef HAVE_CLASIHW
#include <cLdvbasi.h>
#endif
#include <cLdvbudp.h>

#include <cLdvbcomm.h>
#include <cLdvbapp.h>

//#include <unistd.h>
//#include <string.h>
#include <signal.h>
#include <getopt.h>

cLdvbapp::cLdvbapp()
{
   this->pdemux = (cLdvbdemux *) 0;
   cLbug(cL::dbg_low, "c++ implementation\n");
   cLbug(cL::dbg_high, "cLdvbapp created\n");
}

cLdvbapp::~cLdvbapp()
{
   if (this->pdemux != (cLdvbdemux *) 0)
      delete(this->pdemux);
   cLbug(cL::dbg_high, "cLdvbapp deleted\n");
}

void cLdvbapp::sighandler(void *loop, void *p, int revents)
{
   struct cLev_signal *w = (struct cLev_signal *)p;
   cLdvbapp *pobj = (cLdvbapp *) w->data;
   switch (w->signum) {
      case SIGINT:
      case SIGTERM:
      default:
         cLbug(cL::dbg_dvb, "Shutdown was requested.\n");
         cLev_break(loop, 2); //EVBREAK_ALL
         break;
      case SIGHUP:
         cLbug(cL::dbg_dvb, "Configuration reload was requested.\n");
         pobj->pdemux->config_ReadFile();
         break;
   }
}

void cLdvbapp::cliversion()
{
   cLbugf(cL::dbg_dvb, "DVBlast %s (%s)\n", DVB_VERSION, DVB_VERSION_EXTRA);
}

int cLdvbapp::cliusage()
{
   this->cliversion();

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
         "[-6 <print period>] [-7 <ES timeout>]\n");
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
   cLbugf(cL::dbg_dvb, "  -2 --dvr-buf-size <size> set the size of the DVR TS buffer in bytes (default: %d)\n", DVB_DVR_BUFFER_SIZE);
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

int cLdvbapp::cli(int i_argc, char **pp_argv)
{
   const char *network_name = "DVBlast - http://www.videolan.org/projects/dvblast.html";
   const char *provider_name = (const char *) 0;
   int c;

   if (i_argc == 1)
      return this->cliusage();

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
         { "logger",          no_argument,       NULL, 'l' },
         { "logger-ident",    required_argument, NULL, 'g' },
         { "print",           required_argument, NULL, 'x' },
         { "quit-timeout",    required_argument, NULL, 'Q' },
         { "print-period",    required_argument, NULL, '6' },
         { "es-timeout",      required_argument, NULL, '7' },
         { "quiet",           no_argument,       NULL, 'q' },
         { "help",            no_argument,       NULL, 'h' },
         { "version",         no_argument,       NULL, 'V' },
         { "mrtg-file",       required_argument, NULL, 'Z' },
         { "ca-number",       required_argument, NULL, 'y' },
         { "pidmap",          required_argument, NULL, '0' },
         { "dvr-buf-size",    required_argument, NULL, '2' },
         { 0, 0, 0, 0 }
   };

#ifdef HAVE_CLDVBHW
   cLdvbdev *pdev = (cLdvbdev *) 0;
#endif

   const char *ostr = "q::c:r:t:o:i:a:n:5:f:F:R:s:S:k:v:pb:I:m:P:K:G:H:X:O:uwUTL:E:d:3D:A:lg:zCWYeM:N:j:J:B:x:Q:6:7:hVZ:y:0:1:2:";

   while ((c = getopt_long(i_argc, pp_argv, ostr, long_options, NULL)) != -1) {
      switch (c) {
         case 'D': {
            if (this->pdemux != (cLdvbdemux *) 0)
               return cliusage();
            cLdvbudp *pudp = new cLdvbudp();
            pudp->setsource(optarg);
            this->pdemux = (cLdvbdemux *) pudp;
            break;
         }
         case 'A': {
#ifdef HAVE_CLASIHW
            if (strncmp(optarg, "deltacast:", 10) == 0) {
#ifdef HAVE_CLASIDC
               if (this->pdemux != (cLdvbdemux *) 0)
                  return cliusage();
               cLdvbasidc *padc = new cLdvbasidc();
               padc->set_asi_adapter(strtol(optarg+10, (char **) 0, 0));
               this->pdemux = (cLdvbdemux *) padc;
#else
               cLbug(cL::dbg_low, "DVBlast is compiled without Deltacast ASI support.\n");
               return 1;
#endif
            } else {
               if (this->pdemux != (cLdvbdemux *) 0)
                  return cliusage();
               cLdvbasi *pasi = new cLdvbasi();
               pasi->set_asi_adapter(strtol(optarg, (char **) 0, 0));
               this->pdemux = (cLdvbdemux *) pasi;
            }
            break;
#else
            cLbug(cL::dbg_low, "DVBlast is compiled without ASI support.\n");
            return 1;
#endif
         }
         case 'f': {
#ifdef HAVE_CLDVBHW
            if (this->pdemux != (cLdvbdemux *) 0)
               return cliusage();
            pdev = new cLdvbdev();
            if (optarg && optarg[0] != '-')
               pdev->set_frequency(strtol(optarg, (char **) 0, 0));
            this->pdemux = (cLdvbdemux *) pdev;
#else
            cLbug(cL::dbg_low, "DVBlast is compiled without DVB support.\n");
            return 1;
#endif
            break;
         }
      }
   }

   if (this->pdemux == (cLdvbdemux *) 0)
      return this->cliusage();

#ifdef HAVE_CLDVBHW
   if (pdev != (cLdvbdev *) 0) {
      optind = 1;
      while ((c = getopt_long(i_argc, pp_argv, ostr, long_options, (int *) 0)) != -1) {
         switch (c) {
            case 'n':
               pdev->set_frontend(strtol(optarg, (char **) 0, 0));
               break;
            case '5':
               pdev->set_delivery_system(optarg);
               break;
            case 'F':
               pdev->set_fec(strtol(optarg, (char **) 0, 0));
               break;
            case 'R':
               pdev->set_rolloff(strtol(optarg, (char **) 0, 0));
               break;
            case 's':
               pdev->set_srate(strtol(optarg, (char **) 0, 0));
               break;
            case 'S':
               pdev->set_diseqc(strtol(optarg, (char **) 0, 16));
               break;
            case 'k':
               pdev->set_diseqc_port(strtol(optarg, (char **) 0, 16));
               break;
            case 'v':
               pdev->set_voltage(strtol(optarg, (char **) 0, 0));
               break;
            case 'p':
               pdev->set_fource_pulse();
               break;
            case 'b':
               pdev->set_bandwidth(strtol(optarg, (char **) 0, 0));
               break;
            case 'I':
               pdev->set_inversion(strtol(optarg, (char **) 0, 0));
               break;
            case 'm':
               pdev->set_modulation(optarg);
               break;
            case 'P':
               pdev->set_pilot(strtol(optarg, (char **) 0, 0));
               break;
            case '1':
               pdev->set_multistream_id(strtol(optarg, (char **) 0, 0));
               break;
            case 'K':
               pdev->set_lpfec(strtol(optarg, (char **) 0, 0));
               break;
            case 'G':
               pdev->set_guard_interval(strtol(optarg, (char **) 0, 0));
               break;
            case 'X':
               pdev->set_transmission(strtol(optarg, (char **) 0, 0));
               break;
            case 'O':
               pdev->set_frontend_timeout(strtoll(optarg, (char **) 0, 0) * 1000);
               break;
            case 'H':
               pdev->set_hierarchy(strtol(optarg, (char **) 0, 0));
               break;
            case '2': {
               int i = strtol(optarg, (char **) 0, 0);
               if (!i)
                  return this->cliusage();
               pdev->set_dvb_buffer_size(i);
               break;
            }
         }
      }
   }
#endif

   optind = 1;
   while ((c = getopt_long(i_argc, pp_argv, ostr, long_options, (int *) 0)) != -1) {
      switch (c) {
         case 'c':
            this->pdemux->set_configfile(optarg);
            /*
             * When configuration file is used it is reasonable to assume that
             * services may be added/removed. If b_select_pmts is not set dvblast
             * is unable to start streaming newly added services in the config.
             */
            this->pdemux->set_pid_filter();
            break;
         case 'r':
            //libcLdvbcomm::psz_srv_socket = optarg;
            break;
         case 't':
            this->pdemux->set_ttl(strtol(optarg, (char **) 0, 0));
            break;
         case 'o':
            if (!this->pdemux->set_rtpsrc(optarg))
               return this->cliusage();
            break;
         case 'i':
            this->pdemux->set_priority(strtol(optarg, (char **) 0, 0));
            break;
         case 'a':
            this->pdemux->set_adapter(strtol(optarg, (char **) 0, 0));
            break;
         case 'y':
            this->pdemux->set_cadevice(strtol(optarg, (char **) 0, 0));
            break;
         case 'u':
            this->pdemux->hw_filtering(false);
            break;
         case 'w':
            this->pdemux->set_pid_filter(!this->pdemux->get_pid_filter());
            break;
         case 'U':
            this->pdemux->set_rawudp();
            break;
         case 'L':
            this->pdemux->set_max_latency(strtoll(optarg, (char **) 0, 0) * 1000);
            break;
         case 'E':
            this->pdemux->set_max_retention(strtoll(optarg, (char **) 0, 0) * 1000);
            break;
         case 'd':
            this->pdemux->set_dupconfig(optarg);
            break;
         case 'z':
            this->pdemux->pass_all_es();
            break;
         case 'C':
            this->pdemux->set_dvb_compliance();
            break;
         case 'W':
            this->pdemux->set_pass_emm();
            break;
         case 'Y':
            this->pdemux->set_pass_ecm();
            break;
         case 'e':
            this->pdemux->set_pass_epg();
            break;
         case 'M':
            network_name = optarg;
            break;
         case 'B':
            provider_name = optarg;
            break;
         case 'N':
            this->pdemux->set_network_id(strtoul(optarg, (char **) 0, 0));
            break;
         case 'T':
            this->pdemux->set_random_tsid();
            break;
         case 'j':
            this->pdemux->set_charset(optarg);
            break;
         case 'J':
            this->pdemux->set_dvb_charset(optarg);
            break;
         case 'l':
         case 'g':
         case 'x':
            break;
         case 'Q':
            this->pdemux->set_quit_timeout(strtoll(optarg, (char **) 0, 0) * 1000);
            break;
         case '6':
            this->pdemux->set_print_period(strtoll(optarg, (char **) 0, 0) * 1000);
            break;
         case '7':
            this->pdemux->set_es_timeout(strtoll(optarg, (char **) 0, 0) * 1000);
            break;
         case 'V':
            this->cliversion();
            return 1;
            // no break
         case 'Z':
            this->pdemux->set_mrtg_file(optarg);
            break;
         case '0':
            this->pdemux->set_pid_map(optarg);
            break;
         case 'h':
            return this->cliusage();
         default:
            break;
      }
   }

   this->cliversion();
   cLbug(cL::dbg_dvb, "restarting\n");

   if (!this->pdemux->demux_Setup(cLdvbapp::sighandler, this))
      return 1;
   if (!this->pdemux->output_Setup(network_name, provider_name))
      return 1;
   this->pdemux->demux_Open();
   //if (libcLdvbcomm::psz_srv_socket != NULL)
   //   libcLdvbcomm::comm_Open();

   // main loop
   cLev_run(this->pdemux->event_loop, 0);

   this->pdemux->demux_Close();
   //libcLdvbcomm::comm_Close();

   return 0;
}

int cLdvbapp::run(int priority, int adapter, int freq, int srate, int volt, const char *configfile)
{
#ifdef HAVE_CLDVBHW
   cLdvbdev *pdev = new cLdvbdev();
   pdev->set_frequency(freq);
   pdev->set_srate(srate);
   pdev->set_voltage(volt);

   this->pdemux = (cLdvbdemux *) pdev;
   this->pdemux->set_priority(priority);
   this->pdemux->set_adapter(adapter);
   this->pdemux->set_configfile(configfile);
   this->pdemux->set_pid_filter();

   if (!this->pdemux->demux_Setup(cLdvbapp::sighandler, this))
      return 1;
   if (!this->pdemux->output_Setup("cLdvb http://kylone.com/", "Kylone"))
      return 1;
   this->pdemux->demux_Open();
   cLev_run(this->pdemux->event_loop, 0);
   this->pdemux->demux_Close();

   return 0;
#else
   cLbug(cL::dbg_low, "DVBlast is compiled without DVB support.\n");
   return 1;
#endif
}
