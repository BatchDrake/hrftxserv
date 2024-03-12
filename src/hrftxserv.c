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
#include <string.h>

#include <hrftxserv.h>
#include <ctype.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#ifndef __FILENAME__
#  define __FILENAME__ "hrftxserv.c"
#endif /* __FILENAME__ */

/******************************* TX Task *************************************/
CONSTRUCTOR(hrftxserv_tx_task, const char *name, uint64_t freq, const char *path)
{
  bool ok = false;

  memset(self, 0, sizeof(hrftxserv_tx_task_t));

  self->freq     = freq;
  TRY(self->name = strdup(name));
  TRY(self->file = strdup(path));

  ok = true;

done:
  if (!ok)
    hrftxserv_tx_task_finalize(self);

  return ok;
}

DESTRUCTOR(hrftxserv_tx_task)
{
  if (self->file != NULL)
    free(self->file);

  if (self->name != NULL)
    free(self->name);
}

INSTANCER(hrftxserv_tx_task, const char *name, uint64_t freq, const char *path)
{
  hrftxserv_tx_task_t *new = NULL;

  ALLOCATE_FAIL(new, hrftxserv_tx_task_t);

  CONSTRUCT_FAIL(hrftxserv_tx_task, new, name, freq, path);

  return new;

fail:
  if (new != NULL)
    free(new);

  return NULL;
}

COLLECTOR(hrftxserv_tx_task)
{
  DESTRUCT(hrftxserv_tx_task, self);
  free(self);
}

/****************************** Task list ************************************/
METHOD(
  hrftxserv_task_list, 
  bool, push_task, const char *name, uint64_t freq, const char *file)
{
  hrftxserv_tx_task_t *task = NULL;
  bool mutex_acquired = false;
  bool ok = false;
  
  MAKE(task, hrftxserv_tx_task, name, freq, file);

  TRY(pthread_mutex_lock(&self->mutex) == 0);
  mutex_acquired = true;

  list_insert_head((void **) &self->head, task);

  if (self->tail == NULL)
    self->tail = task;

  task = NULL;

  (void) pthread_cond_signal(&self->cond_var);

  ok = true;

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->mutex);

  if (task != NULL)
    hrftxserv_tx_task_destroy(task);

  return ok;
}

METHOD(hrftxserv_task_list, hrftxserv_tx_task_t *, pop_task)
{
  hrftxserv_tx_task_t *task = NULL;
  bool mutex_acquired = false;

  TRY(pthread_mutex_lock(&self->mutex) == 0);
  mutex_acquired = true;

  do {
    task = self->tail;

    if (task != NULL) {
      self->tail = LIST_PREV(task);
      list_remove_element((void **) &self->head, task);
    } else {
      pthread_cond_wait(&self->cond_var, &self->mutex);
    }
  } while (task == NULL);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->mutex);

  return task;
}

CONSTRUCTOR(hrftxserv_task_list)
{
  bool ok = false;

  memset(self, 0, sizeof(hrftxserv_task_list_t));

  TRYC(pthread_mutex_init(&self->mutex, NULL));
  self->initialized = true;

  TRYC(pthread_cond_init(&self->cond_var, NULL));
  self->cond_init = true;

  ok = true;

done:
  if (!ok)
    hrftxserv_task_list_finalize(self);

  return ok;
}

DESTRUCTOR(hrftxserv_task_list)
{
  struct hrftxserv_tx_task *task, *tmp;

  if (self->cond_init)
    pthread_cond_destroy(&self->cond_var);

  if (self->initialized)
    pthread_mutex_destroy(&self->mutex);

  FOR_EACH_SAFE(task, tmp, self->head)
    hrftxserv_tx_task_destroy(task);
}

/***************************** Client object *******************************/
static inline uint16_t
su_complex_to_hackrf(SUCOMPLEX v)
{
  float r, i;
  int8_t r8, i8;
  
  r = SU_C_REAL(v);
  i = SU_C_IMAG(v);

  if (r > 1.)
    r = 1;
  else if (r < -1.)
    r = -1;

  if (i > 1.)
    i = 1;
  else if (i < -1.)
    i = -1;

  r8 = 0x00 ^ (int) floor(127. * r);
  i8 = 0x00 ^ (int) floor(127. * i);

  return (((int16_t) r8) << 8) | (int16_t) (i8 & 0xff);
}


static SUBOOL 
hrftxserv_listener_client_on_data(
      const struct sigutils_specttuner_channel *channel,
      void *privdata,
      const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
      SUSCOUNT size)
{
  hrftxserv_listener_client_t *self = 
    (hrftxserv_listener_client_t *) privdata;
  SUSCOUNT i;
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < size; ++i)
    self->hackrf_samples[i] = su_complex_to_hackrf(self->gain * data[i]);
  
  TRY(write(self->ofd, self->hackrf_samples, size * sizeof(uint16_t)) > sizeof(uint16_t));

  ok = SU_TRUE;

done:
  return ok;
}

INSTANCER(
  hrftxserv_listener_client,
  struct hrftxserv_listener *owner,
  const char *name,
  bool plaintext,
  unsigned int repeat,
  uint64_t freq,
  int fd)
{
  hrftxserv_listener_client_t *new = NULL;
  struct sigutils_specttuner_params params =
    sigutils_specttuner_params_INITIALIZER;

  struct sigutils_specttuner_channel_params cparams =
    sigutils_specttuner_channel_params_INITIALIZER;

  ALLOCATE_FAIL(new, hrftxserv_listener_client_t);

  new->fd        = -1;
  new->ofd       = -1;
  new->freq      = freq;
  new->repeat    = repeat;
  new->plaintext = plaintext;

  ALLOCATE_MANY_FAIL(new->read_buffer, HRFTXSERV_BUFFER_SIZE, uint8_t);
  TRY_FAIL(new->name = strdup(name));
  
  if (access(HRFTXSERV_TEMP_FOLDER, F_OK) == -1) {
    if (mkdir(HRFTXSERV_TEMP_FOLDER, 0700) == -1) {
      ERROR("Failed to create temporary directory: %s\n", strerror(errno));
      goto fail;
    }
  }

  TRY_FAIL(new->path = strbuild("%s/signalXXXXXX", HRFTXSERV_TEMP_FOLDER));
  TRYC_FAIL(new->ofd = mkstemp(new->path));

  new->fd = fd;
  new->owner = owner;

  params.window_size = HRFTXSERV_FFT_SIZE;
  MAKE_FAIL(new->stuner, su_specttuner, &params);

  cparams.f0       = 0;
  cparams.bw = 
    2 * PI 
    * SU_ASFLOAT(owner->owner->params.bandwidth) 
    / SU_ASFLOAT(owner->owner->params.samp_rate);
  cparams.guard    = 2 * PI / cparams.bw;
  cparams.privdata = new;
  cparams.on_data  = hrftxserv_listener_client_on_data;

  TRY_FAIL(new->schannel = su_specttuner_open_channel(new->stuner, &cparams));

  new->gain = 64;

  return new;

fail:
  if (new != NULL)
    hrftxserv_listener_client_destroy(new);

  return new;
}

COLLECTOR(hrftxserv_listener_client)
{
  if (self->read_buffer != NULL)
    free(self->read_buffer);

  if (self->name != NULL)
    free(self->name);

  if (self->stuner != NULL)
    su_specttuner_destroy(self->stuner);

  if (self->ofd != -1)
    close(self->ofd);

  if (self->path != NULL)
    free(self->path);

  if (self->fd != -1)
    close(self->fd);

  free(self);
}

METHOD(
  hrftxserv_listener_client,
  bool,
  write,
  const void *data,
  size_t size)
{
  bool ok = false;
  const uint8_t *buffer = (const uint8_t *) data;
  uint8_t input;
  SUSCOUNT j;

  while (size-- > 0) {
    input = *buffer++;
    
    for (j = 0; j < self->repeat; ++j) {
      if (self->plaintext) {
        if (input == '0') {
          self->as_samples[self->read_ptr / sizeof(SUCOMPLEX)] = 0;
          self->read_ptr += sizeof(SUCOMPLEX);
        } else if (input == '1') {
          self->as_samples[self->read_ptr / sizeof(SUCOMPLEX)] = 1.;
          self->read_ptr += sizeof(SUCOMPLEX);
        } else if (!isspace(input)) {
          fprintf(stderr, "Unknown char `%d'\n", input);
        }
      } else {
        self->read_buffer[self->read_ptr++] = input;
      }

      if (self->read_ptr == HRFTXSERV_BUFFER_SIZE) {
        self->read_ptr = 0;

        if (self->plaintext) {
          TRY(
            hrftxserv_listener_client_on_data(
              self->schannel,
              self,
              self->as_samples,
              HRFTXSERV_FFT_SIZE));
        } else {
          TRY(
            su_specttuner_feed_bulk(
              self->stuner,
              self->as_samples,
              HRFTXSERV_FFT_SIZE));
        }
      }
    }
  }

  ok = true;

done:
  return ok;
}

METHOD(
  hrftxserv_listener_client,
  bool,
  complete)
{
  bool ok = false;
  unsigned int i;
  unsigned int times = 
    self->owner->owner->params.samp_rate * 5e-2 / HRFTXSERV_FFT_SIZE;

  if (self->read_ptr > 0) {
    memset(
      self->read_buffer + self->read_ptr, 
      0, 
      HRFTXSERV_BUFFER_SIZE - self->read_ptr);

    TRY(
      su_specttuner_feed_bulk(
        self->stuner,
        self->as_samples,
        HRFTXSERV_FFT_SIZE));
  }

  memset(self->read_buffer, 0, HRFTXSERV_BUFFER_SIZE);
  for (i = 0; i < times; i++)
    write(self->ofd, self->read_buffer, HRFTXSERV_BUFFER_SIZE);

  ok = true;

done:
  return ok;
}

/****************************** Listener state ********************************/
static struct pollfd *
hrftxserv_listener_recreate_fds(hrftxserv_listener_t *self)
{
  static struct pollfd *fds = NULL;
  unsigned int i, p = 0;

  ALLOCATE_MANY(fds, 1 + self->port_count + self->client_count, struct pollfd);

  /* Put cancel pipe */
  fds[p].events = POLLIN;
  fds[p].fd     = self->cancel_pipe[0];
  ++p;

  /* Put socket listeners */
  for (i = 0; i < self->port_count; ++i) {
    fds[p].events = POLLIN;
    fds[p].fd     = self->sfd_list[i];
    ++p;
  }

  /* Put all clients */
  for (i = 0; i < self->client_count; ++i) {
    fds[p].events = POLLIN | POLLHUP | POLLERR;
    if (self->client_list[i] != NULL) {
      fds[p].fd = self->client_list[i]->fd;
    } else {
      /* Dummy entry */
      fds[p].fd = self->cancel_pipe[0];
    }
  }

done:
  return fds;
}

static bool
hrftxserv_listener_register_client(
  hrftxserv_listener_t *self,
  const char *name,
  uint64_t freq,
  int fd)
{
  hrftxserv_listener_client_t *client = NULL;
  struct pollfd *new_pollfd = NULL;
  bool ok = false;

  MAKE(
    client,
    hrftxserv_listener_client,
    self,
    name,
    self->plaintext,
    self->repeat,
    freq,
    fd);
  
  fd = -1;

  TRYC(PTR_LIST_APPEND_CHECK(self->client, client));
  client = NULL;
  
  TRY(new_pollfd = hrftxserv_listener_recreate_fds(self));

  if (self->pollfds != NULL)
    free(self->pollfds);

  self->pollfds = new_pollfd;

  ok = true;

done:
  if (fd != -1)
    close(fd);

  if (client != NULL)
    free(client);
    
  return ok;
}

static bool
hrftxserv_listener_unregister_client(hrftxserv_listener_t *self, int index)
{
  struct pollfd *new_pollfd = NULL;
  bool ok = false;

  if (self->client_list[index] == NULL)
    goto done;

  hrftxserv_listener_client_destroy(self->client_list[index]);
  self->client_list[index] = NULL;

  TRY(new_pollfd = hrftxserv_listener_recreate_fds(self));

  if (self->pollfds != NULL)
    free(self->pollfds);

  self->pollfds = new_pollfd;

  ok = true;

done:   
  return ok;
}


static void *
hrftxserv_listener_thread(void *opaque)
{
  hrftxserv_listener_t *self = (hrftxserv_listener_t *) opaque;
  unsigned int fd_count, i;
  struct sockaddr_in cli_addr;
  socklen_t cli_len;
  ssize_t got;
  uint64_t freq;
  struct hostent *ent;
  char *hostname;
  int new_fd;
  char b;
  bool ok = false;
  
  for (;;) {
    fd_count = 1 + self->port_count + self->client_count;

    TRYC(poll(self->pollfds, fd_count, -1));

    /* Cancel */
    if (self->pollfds[0].revents & POLLIN) {
      read(self->cancel_pipe[0], &b, 1);
      break;
    }

    /* Check for new clients */
    for (i = 0; i < self->port_count; ++i) {
      if (self->pollfds[i + 1].revents & POLLIN) {
        new_fd = accept(
          self->sfd_list[i],
          (struct sockaddr *) &cli_addr,
          &cli_len);

        if (new_fd == -1) {
          ERROR("accept(): %s\n", strerror(errno));
          continue;
        }

        ent = gethostbyaddr(
          &cli_addr.sin_addr.s_addr,
          sizeof (struct in_addr),
          AF_INET);

        if (ent != NULL && ent->h_name != NULL)
          hostname = ent->h_name;
        else
          hostname = inet_ntoa(cli_addr.sin_addr);
        freq   = self->base_freq + i * self->freq_spacing;
        
        printf(
          "[%15s] New client at %g MHz\n",
          hostname,
          freq * 1e-6);

        TRY(hrftxserv_listener_register_client(self, hostname, freq, new_fd));
      }
    }

    /* Check for data */
    for (i = 0; i < self->client_count; ++i) {
      if (self->client_list[i] != NULL) {
        if (self->pollfds[i + 1 + self->port_count].revents & POLLIN) {
          /* Data in this client */
          got = read(
            self->client_list[i]->fd,
            self->read_buffer,
            HRFTXSERV_BUFFER_SIZE);
          if (got < 1) {
            TRY(
              hrftxserv_listener_client_complete(self->client_list[i]));

            TRY(
              hrftxserv_task_list_push_task(
                &self->owner->task_list,
                self->client_list[i]->name,
                self->client_list[i]->freq, 
                self->client_list[i]->path));

            TRY(hrftxserv_listener_unregister_client(self, i));
          } else {
            TRY(
              hrftxserv_listener_client_write(
                self->client_list[i],
                self->read_buffer,
                got));
          }
        } else if (self->pollfds[i + 1 + self->port_count].revents 
          & (POLLHUP | POLLERR)) {
          TRY(
            hrftxserv_listener_client_complete(self->client_list[i]));

          TRY(
            hrftxserv_task_list_push_task(
              &self->owner->task_list,
              self->client_list[i]->name,
              self->client_list[i]->freq, 
              self->client_list[i]->path));

          TRY(hrftxserv_listener_unregister_client(self, i));
        }
      }
    }
  }

  ok = true;

done:
  if (!ok) {
    ERROR("Listener thread finished prematurely\n");
    ERROR(
      "  Affected ports: %u - %u\n", 
      self->port_base, 
      self->port_base + self->port_count - 1);

    ERROR(
      "  Affected frequencies: %g MHz - %g MHz\n", 
      self->base_freq * 1e-6, 
      (self->base_freq + self->port_count * self->freq_spacing) * 1e-6);
  }

  self->cancelled = true;

  return NULL;
}

/* Creating a listener involves:
 * 
 * 1. Creating N sockets
 * 2. Creating a cancel pipe
 * 3. Initializing the pollfds
 * 4. Creating a controlling thread
 */

INSTANCER(
  hrftxserv_listener,
  struct hrftxserv_state *state,
  uint16_t base_port,
  unsigned int count,
  uint64_t base_freq,
  uint64_t spacing,
  bool     plaintext,
  unsigned repeat)
{
  hrftxserv_listener_t *new = NULL;
  unsigned int i;
  int flag = 1;
  struct sockaddr_in listen_addr;

  ALLOCATE_FAIL(new, hrftxserv_listener_t);

  new->base_freq    = base_freq;
  new->freq_spacing = spacing;
  new->port_base    = base_port;
  new->port_count   = count;
  new->plaintext    = plaintext;
  new->repeat       = repeat;

  new->cancel_pipe[0] = -1;
  new->cancel_pipe[1] = -1;

  new->owner = state;

  ALLOCATE_MANY_FAIL(new->sfd_list, count, int);
  ALLOCATE_MANY_FAIL(new->read_buffer, HRFTXSERV_BUFFER_SIZE, uint8_t);

  for (i = 0; i < count; ++i)
    new->sfd_list[i] = -1;

  TRYC_FAIL(pipe(new->cancel_pipe));

  memset(&listen_addr, 0, sizeof (struct sockaddr_in));

  listen_addr.sin_family = AF_INET;
  listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  for (i = 0; i < count; ++i) {
    TRYC_FAIL(new->sfd_list[i] = socket(AF_INET, SOCK_STREAM, 0));
    TRYC_FAIL(
      setsockopt(
        new->sfd_list[i],
        SOL_SOCKET,
        SO_REUSEADDR,
        &flag,
        sizeof(int)));
    
    listen_addr.sin_port = htons(new->port_base + i);

    if (bind(
      new->sfd_list[i], 
      (struct sockaddr *) &listen_addr, 
      sizeof(struct sockaddr_in)) == -1) {
        ERROR("Failed to bind port %d: %s\n", new->port_base + i, strerror(errno));
        goto fail;
      }

    TRYC_FAIL(listen(new->sfd_list[i], 15));
  }

  TRY_FAIL(new->pollfds = hrftxserv_listener_recreate_fds(new));

  TRYC_FAIL(pthread_create(&new->thread, NULL, hrftxserv_listener_thread, new));
  new->thread_init = true;

  return new;

fail:
  if (new != NULL)
    hrftxserv_listener_destroy(new);

  return NULL;
}

COLLECTOR(hrftxserv_listener)
{
  char b = 1;
  unsigned int i;

  if (self->thread_init) {
    if (self->cancel_pipe[1] != -1)
      write(self->cancel_pipe[1], &b, 1);

    pthread_join(self->thread, NULL);
  }

  if (self->read_buffer != NULL)
    free(self->read_buffer);

  if (self->sfd_list != NULL)
    free(self->sfd_list);

  if (self->pollfds != NULL)
    free(self->pollfds);

  if (self->client_list != NULL) {
    for (i = 0; i < self->client_count; ++i)
      if (self->client_list[i] != NULL)
        hrftxserv_listener_client_destroy(self->client_list[i]);

    free(self->client_list);
  }

  free(self);
}

/**************************** Application state *******************************/
METHOD(hrftxserv_state, bool, work)
{
  hrftxserv_tx_task_t *task = NULL;
  struct stat sbuf;
  float wait;
  bool ok = false;
  float len;
  char *command = NULL;
  uint64_t samples;
  int ret;

  task = hrftxserv_task_list_pop_task(&self->task_list);

  if (task != NULL) {
    if (stat(task->file, &sbuf) != -1) {
      samples = sbuf.st_size / sizeof (uint16_t);
      len = samples / (float) self->params.samp_rate;

      printf(
        "[%15s] TX on %6g MHz (%g ms)\n",
        task->name,
        task->freq * 1e-6,
        len * 1e3);

      TRY(
        command = strbuild(
          "hackrf_transfer -f %llu -x 47 -a 1 -p 0 -s %d -n %llu -t %s > /dev/null 2> /dev/null",
          task->freq,
          self->params.samp_rate,
          samples,
          task->file));
      ret = system(command);
      if (ret != 0) {
        fprintf(
          stderr,
          "\033[1;33m[%15s] warning:\033[0m hackrf_transfer returned status code %d\n",
          task->name,
          ret);
        fprintf(
          stderr,
          "\033[1;33m[%15s] warning:\033[0m Command: %s\n", task->name, command);
      }

      wait = len * (1 - self->params.duty_cycle) / self->params.duty_cycle;
      printf(
        "[%15s] Waiting %g s to honour %g%% duty cycle\n",
        task->name,
        wait,
        100 * self->params.duty_cycle);
      
      usleep(wait * 1e6);
    }
  }

  ok = true;

done:
  if (command != NULL)
    free(command);

  if (task != NULL) {
    unlink(task->file);
    hrftxserv_tx_task_destroy(task);
  }
  return ok;
}

COLLECTOR(hrftxserv_state)
{
  unsigned int i;

  for (i = 0; i < self->listener_count; ++i)
    if (self->listener_list[i] != NULL)
      hrftxserv_listener_destroy(self->listener_list[i]);

  if (self->listener_list != NULL)
    free(self->listener_list);

  hrftxserv_task_list_finalize(&self->task_list);

  free(self);
}

INSTANCER(hrftxserv_state, const struct hrftxserv_params *params)
{
  hrftxserv_state_t *new = NULL;
  hrftxserv_listener_t *listener = NULL;

  unsigned int ncpu = sysconf(_SC_NPROCESSORS_ONLN);
  unsigned int ports_per_thread;
  unsigned int port_count;
  unsigned int p = 0;
  unsigned int i;

  ALLOCATE_FAIL(new, hrftxserv_state_t);

  CONSTRUCT_FAIL(hrftxserv_task_list, &new->task_list);

  new->params = *params;

  if (params->chan_count < ncpu) {
    ncpu = params->chan_count;
    ports_per_thread = 1;
  } else {
    ports_per_thread = 
      params->chan_count / ncpu + !!(params->chan_count % ncpu);
  }

  /* Create all listeners */
  for (i = 0; i < ncpu; ++i) {
    port_count = MIN(params->chan_count - p, ports_per_thread);

    MAKE_FAIL(
      listener, 
      hrftxserv_listener,
      new,
      params->port_base + i * ports_per_thread,
      port_count,
      params->freq_base + i * ports_per_thread * params->freq_spacing,
      params->freq_spacing,
      params->plaintext,
      params->repeat);

    TRYC_FAIL(PTR_LIST_APPEND_CHECK(new->listener, listener));

    listener = NULL;
  }

  return new;

fail:
  if (listener != NULL)
    hrftxserv_listener_destroy(listener);

  if (new != NULL)
    hrftxserv_state_destroy(new);

  return NULL;
}
