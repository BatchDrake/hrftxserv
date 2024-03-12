/*
 *    hrftxserv.h: Doubly-linked lists
 *    Copyright(C) 2022 Gonzalo José Carracedo Carballal
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <stdio.h>
#include <hrftxserv.h>
#include <inttypes.h> 
#include <getopt.h>
#include <sigutils/sigutils.h>

#define HRFTXSERV_MIN_FREQ 433050000
#define HRFTXSERV_MAX_FREQ 434040000

static const struct option long_options[] = {
    {"freq-start", required_argument, NULL, 'a'},
    {"freq-end",   required_argument, NULL, 'b'},
    {"channels",   required_argument, NULL, 'n'},
    {"bw",         required_argument, NULL, 'B'},
    {"samp-rate",  required_argument, NULL, 's'},
    {"duty-cycle", required_argument, NULL, 'd'},
    {"plaintext",  no_argument,       NULL, 'p'},
    {"repeat",     required_argument, NULL, 'r'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,          0,                NULL,  0 }
};

static bool
start_server(const struct hrftxserv_params *params)
{
  hrftxserv_state_t *state = NULL;
  bool ok = false;

  fprintf(stderr, "Staring HackRF TX server\n");
  fprintf(
    stderr, 
    "  - Port range: %d - %d\n", 
    params->port_base,
    params->port_base + params->chan_count - 1);
  
  fprintf(
    stderr, 
    "  - Freq range: %g MHz - %g MHz\n", 
    1e-6 * params->freq_base,
    1e-6 * (params->freq_base + params->freq_spacing * params->chan_count));
  fprintf(
    stderr,
    "  - Bandwidth: %g kHz\n",
    1e-3 * params->bandwidth);

  fprintf(
    stderr,
    "  - Sample rate: %g Msps\n",
    params->samp_rate * 1e-6);
  
  if (params->repeat > 1)
    fprintf(
      stderr,
      "  - Symbol rate: %g baud\n",
      (float) params->samp_rate / (float) params->repeat);

  fprintf(
    stderr,
    "  - Duty cycle: %g%%\n",
    params->duty_cycle * 100);

  if (params->plaintext)
    fprintf(stderr, " - Plaintext mode is ON!\n");
  
  if ((state = hrftxserv_state_new(params)) == NULL) {
    fprintf(stderr, "hrftxserv: failed to create server\n");
    goto done;
  }
  
  fprintf(stderr, "Server started, waiting for requests...\n");

  while (hrftxserv_state_work(state));

  ok = true;

done:
  if (state != NULL)
    hrftxserv_state_destroy(state);

  return ok;
}

void
help(const char *argv0)
{
  fprintf(
      stderr,
      "%s - A HackRF TX server that prevents EM jamming\n\n",
      argv0);
  fprintf(
      stderr,
      "This tool enables controlled transmission of signals in the ISM bands\n");
  fprintf(
      stderr,
      "by listening on a certain range of TCP ports, accepting I/Q data as\n");
  fprintf(
      stderr,
      "sequences of complex float32 samples.\n\n");
  fprintf(
      stderr,
      "Usage:\n");
  fprintf(
      stderr,
      "    %s [OPTIONS]\n\n",
      argv0);

  fprintf(
      stderr,
      "Options:\n");
  fprintf(
      stderr,
      "    -a,--freq-start <f_a>    Sets the lower bound frequency to f_a (Hz)\n");
  fprintf(
      stderr,
      "    -b,--freq-end <f_b>      Sets the upper bound frequency to f_b (Hz)\n");
  fprintf(
      stderr,
      "    -n,--channels <num>      Sets the number of channels in the defined\n");
  fprintf(
      stderr,
      "                             frequency range (default: 99)\n");
  fprintf(
      stderr,
      "    -s,--samp-rate <num>     Sets the HackRF sample rate (default is\n");
  fprintf(
      stderr,
      "                             4000000 (4 M) samples per second)\n");
  fprintf(
      stderr,
      "    -p,--plaintext           Samples are plaintext and encoded as 0 or 1\n");
  fprintf(
      stderr,
      "    -B,--bw <B>              Sets the maximum channel bandwidth (default is\n");
  fprintf(
      stderr,
      "                             is 100000 Hz (100 kHz))\n");

  fprintf(
      stderr,
      "    -h,--help                This help.\n\n");
  fprintf(
    stderr,
    "(c) 2022 Gonzalo J. Carracedo <BatchDrake@gmail.com>\n");
  fprintf(
    stderr,
    "Copyrighted but free, under the terms of the GPLv3 license\n\n");
}

int
main(int argc, char **argv)
{
  struct hrftxserv_params params = hrftxserv_params_INITIALIZER;
  int option_index;
  int c;
  uint64_t freq_start = HRFTXSERV_MIN_FREQ;
  uint64_t freq_end   = HRFTXSERV_MAX_FREQ;
  uint64_t usable_range;

  params.chan_count   = 99;
  params.duty_cycle   = 0.1;
  params.samp_rate    = 4000000;
  
  if (sizeof(SUCOMPLEX) != sizeof(float complex))
    abort();

  su_lib_init();

  while ((c = getopt_long(
    argc, 
    argv,
    "a:b:n:s:B:d:r:ph",
    long_options,
    &option_index)) != -1) {
    switch (c) {
      case 'a':
        if (sscanf(optarg, "%" SCNu64, &freq_start) <= 0) {
          fprintf(
            stderr,
            "%s: invalid start frequency (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }
        break;

      case 'b':
        if (sscanf(optarg, "%" SCNu64, &freq_end) <= 0) {
          fprintf(
            stderr,
            "%s: invalid end frequency (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }
        break;

      case 'n':
        if (sscanf(optarg, "%" SCNu32, &params.chan_count) <= 0) {
          fprintf(
            stderr,
            "%s: invalid channel count (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }
        break;

      case 'r':
        if (sscanf(optarg, "%u", &params.repeat) <= 0 || params.repeat < 1) {
          fprintf(
            stderr,
            "%s: invalid sample repetition (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }
        break;

      case 'p':
        params.plaintext = true;
        break;

      case 's':
        if (sscanf(optarg, "%" SCNu32, &params.samp_rate) <= 0) {
          fprintf(
            stderr,
            "%s: invalid sample rate (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }
        break;

      case 'd':
        if (sscanf(optarg, "%g", &params.duty_cycle) <= 0) {
          fprintf(
            stderr,
            "%s: invalid duty cycle (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }

        if (params.duty_cycle <= 0 || params.duty_cycle > 100) {
          fprintf(
            stderr,
            "%s: invalid duty cycle (must be between 0 and 100)\n",
            argv[0]);
          exit(EXIT_FAILURE);
        }

        params.duty_cycle *= 1e-2;
        break;

      case 'B':
        if (sscanf(optarg, "%" SCNu64, &params.bandwidth) <= 0) {
          fprintf(
            stderr,
            "%s: invalid bandwidth (`%s')\n",
            argv[0],
            optarg);
          exit(EXIT_FAILURE);
        }
        break;
        
      case 'h':
        help(argv[0]);
        exit(EXIT_SUCCESS);
        break;

      case '?':
        fprintf(
            stderr,
            "%s: unknown option `%s'\n",
            argv[0],
            argv[optind]);
        help(argv[0]);
        exit(EXIT_FAILURE);
        break;

      case ':':
        fprintf(
            stderr,
            "%s: option `%s' expects an argument\n",
            argv[0],
            argv[optind]);
        help(argv[0]);
        exit(EXIT_FAILURE);
        break;
    }
  }

  if (argc - optind != 0) {
    fprintf(stderr, "%s: extra arguments passed after options\n", argv[0]);
    help(argv[0]);
    exit(EXIT_FAILURE);
  }

  if (freq_start >= freq_end) {
    fprintf(stderr, "%s: f_a cannot be greater than f_b\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if (freq_end - freq_start < params.bandwidth) {
    fprintf(stderr, "%s: channel bandwidth exceeds frequency range\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  usable_range = freq_end - freq_start - params.bandwidth;

  params.freq_base    = freq_start + params.bandwidth / 2;
  params.freq_spacing = usable_range / params.chan_count;

  if (!start_server(&params))
    exit(EXIT_FAILURE);

  return 0;
}
