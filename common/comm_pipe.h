//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _COMM_PIPE_H_
#define _COMM_PIPE_H_

#define PIPE_READ 0
#define PIPE_WRITE 1

typedef struct {
  int fd;
  char filename[1000];
  int is_server;
} Pipe;

// Create pipe, if create_pipe == 0, then load the fid from an existing file.
int PipeInit(const char *filename, int create_pipe, Pipe *p);

// Nonblocking read/write. return -1 if failed, else return 0
int PipeRead(Pipe *p, void *buffer, int size);
int PipeWrite(Pipe *p, void *buffer, int size);

void PipeClose(Pipe *p);

#endif
