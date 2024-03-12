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

#ifndef _HRFTXSERV_H
#define _HRFTXSERV_H

#include <pthread.h>
#include <stdbool.h>
#include "list.h"
#include "defs.h"
#include <math.h>
#include <complex.h>
#include <sigutils/specttuner.h>

#define HRFTXSERV_TEMP_FOLDER "upload.tmp"

/********************************** TX Task **********************************/
struct hrftxserv_tx_task {
  LINKED_LIST;

  char *name;
  uint64_t freq;
  char    *file;
};

typedef struct hrftxserv_tx_task hrftxserv_tx_task_t;

CONSTRUCTOR(hrftxserv_tx_task, const char *name, uint64_t freq, const char *path);
INSTANCER(hrftxserv_tx_task, const char *name, uint64_t freq, const char *path);

DESTRUCTOR(hrftxserv_tx_task);
COLLECTOR(hrftxserv_tx_task);

/********************************* Task list *********************************/
struct hrftxserv_task_list
{
  pthread_mutex_t           mutex;
  bool                      initialized;

  pthread_cond_t            cond_var;
  bool                      cond_init;

  struct hrftxserv_tx_task *head;
  struct hrftxserv_tx_task *tail;
};

typedef struct hrftxserv_task_list hrftxserv_task_list_t;

CONSTRUCTOR(hrftxserv_task_list);

METHOD(
  hrftxserv_task_list, 
  bool, push_task, const char *name, uint64_t freq, const char *file);
METHOD(
  hrftxserv_task_list,
  hrftxserv_tx_task_t *, pop_task);

DESTRUCTOR(hrftxserv_task_list);

struct hrftxserv_state;

/***************************** Client object *******************************/
struct hrftxserv_listener;

struct hrftxserv_listener_client {
  int fd;
  int ofd;
  char *path;
  char *name;
  unsigned int count;
  float gain;
  bool plaintext;
  unsigned int repeat;
  uint64_t freq;
  su_specttuner_t *stuner;
  su_specttuner_channel_t *schannel;

  union {
    uint8_t       *read_buffer;
    uint16_t      *hackrf_samples;
    SUCOMPLEX     *as_samples;
  };
  unsigned       read_ptr;

  struct hrftxserv_listener *owner;
};

typedef struct hrftxserv_listener_client hrftxserv_listener_client_t;

INSTANCER(
  hrftxserv_listener_client,
  struct hrftxserv_listener *,
  const char *name,
  bool plaintext,
  unsigned int repeat,
  uint64_t freq,
  int fd);

METHOD(
  hrftxserv_listener_client,
  bool,
  write,
  const void *data,
  size_t size);

METHOD(
  hrftxserv_listener_client,
  bool,
  complete);

COLLECTOR(hrftxserv_listener_client);

/**************************** Listener state *******************************/
struct pollfd;

#define HRFTXSERV_FFT_SIZE     4096
#define HRFTXSERV_BUFFER_SIZE (HRFTXSERV_FFT_SIZE * sizeof(SUCOMPLEX))

struct hrftxserv_listener {
  struct hrftxserv_state *owner;
  pthread_t thread;
  bool      thread_init;

  uint64_t base_freq;
  uint64_t freq_spacing;
  unsigned int port_base;
  unsigned int port_count;

  int cancel_pipe[2];  
  bool cancelled;
  bool plaintext;
  unsigned repeat;

  uint8_t       *read_buffer;
  uint8_t       *write_buffer;
  int           *sfd_list;
  struct pollfd *pollfds;

  PTR_LIST(hrftxserv_listener_client_t, client);
};

typedef struct hrftxserv_listener hrftxserv_listener_t;

INSTANCER(
  hrftxserv_listener,
  struct hrftxserv_state *,
  uint16_t base_port,
  unsigned int count,
  uint64_t base_freq,
  uint64_t spacing,
  bool     plaintext,
  unsigned repeat);
COLLECTOR(hrftxserv_listener);

/**************************** Application state *******************************/
struct hrftxserv_params {
  uint16_t port_base;
  uint32_t samp_rate;
  uint64_t freq_base;
  uint64_t freq_spacing;
  uint64_t bandwidth;
  uint32_t chan_count;
  float    duty_cycle;
  unsigned repeat;
  bool     plaintext;
};

#define hrftxserv_params_INITIALIZER  \
{                                     \
      10000, /* port_base */          \
    1000000, /* samp_rate */          \
  433000000, /* freq_base */          \
      12500, /* freq_spacing */       \
     100000, /* bandwidth */          \
        100, /* chan_count */         \
        0.1, /* duty cycle */         \
          1, /* repeat */             \
      false, /* plaintext */          \
}

struct hrftxserv_state {
  struct hrftxserv_params    params;
  struct hrftxserv_task_list task_list;
  
  PTR_LIST(hrftxserv_listener_t, listener);
};

typedef struct hrftxserv_state hrftxserv_state_t;

INSTANCER(hrftxserv_state, const struct hrftxserv_params *);
METHOD(hrftxserv_state, bool, work);
COLLECTOR(hrftxserv_state);

#endif /* _HRFTXSERV_H */
