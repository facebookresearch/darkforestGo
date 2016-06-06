//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include "comm_pipe.h"

#define PIPE_SIZE 1048576

// Hack here
#define F_SETPIPE_SZ 1031
#define F_GETPIPE_SZ 1032

int PipeInit(const char *name, int create_pipe, Pipe *p) {
  if (strlen(name) >= sizeof(p->filename)) {
    printf("Input filename %s is too long!\n", name);
    return -1;
  }
  strcpy(p->filename, name);

  if (create_pipe) {
    mkfifo(name, 0666);
    p->is_server = 1;

    p->fd = open(name, O_RDWR);
    if (p->fd == -1) {
      printf("Cannot open pipe %s (server) !", name);
      return -1;
    }
  } else {
    // Load it from the global file.
    p->is_server = 0;
    p->fd = open(name, O_RDWR);
    if (p->fd == -1) {
      printf("Cannot open pipe %s (client) !\n", name);
      return -1;
    }
  }
  if (fcntl(p->fd, F_SETPIPE_SZ, PIPE_SIZE) != PIPE_SIZE) {
    printf("Cannot resize pipe %s to %d", name, PIPE_SIZE);
    return -1;
  }
  if (fcntl(p->fd, F_SETFL, O_NONBLOCK) == -1) {
    printf("Cannot set to nonblocking model\n");
    return -1;
  }

  return 0;
}

int PipeRead(Pipe *p, void *buffer, int size) {
  if (read(p->fd, buffer, size) == -1) {
    return -1;
  }
  return 0;
}

int PipeWrite(Pipe *p, void *buffer, int size) {
  if (write(p->fd, buffer, size) == -1) {
    return -1;
  }
  return 0;
}

void PipeClose(Pipe *p) {
  close(p->fd);
  if (p->is_server) unlink(p->filename);
}
