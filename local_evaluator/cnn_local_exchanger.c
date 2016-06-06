//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "cnn_local_exchanger.h"
#include <stdio.h>
#include "../common/comm_pipe.h"
#include "../common/common.h"
#include <pthread.h>

#define TYPE_MESSAGE 3
#define M_BOARD 1
#define M_MOVE 2
#define M_CTRL 3

#define NUM_CHANNELS 4
#define PIPE_BOARD 0
#define PIPE_MOVE 1
#define PIPE_C2S 2
#define PIPE_S2C 3

#define PIPE_PREFIX "./pipe"
#define QUEUE_SIZE 10000

// Several kind of messages. The first element has to be long
// Message 1: board
#define PR_HIGHEST 100

// Exchanger. Save all the context.
typedef struct {
  Pipe channels[NUM_CHANNELS];
  // Whether we are running server.
  BOOL is_server;
  // server parameters.
  // Control flag.
  //      Bit FINISHSOON: the server should finish the computation soon.
  //      Bit GET_RESTART: we are restarting the thread.
  unsigned char ctrl_flag;
  volatile BOOL done;
  //
  pthread_t ctrl;
  // Some stats.
  int board_received;
  int move_sent;

  // Client side: wait count. #thread that are waiting on the response.
  // For each server to connect from, we have a counter (how many threads are waiting for it.)
  // If the counter reach #threads for this server, send a finishsoon command so that things could move forward.
  int wait_count;
  int wait_count_max;
  // Queue
  // Queue q;
} Exchanger;

// Message 3: control information
typedef struct {
  long seq;
  uint64_t b;
  int code;
} MCtrl;

/*
void *threaded_message(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  MBoard mboard;
  while (! ex->done) {
    if (PipeRead(&ex->channels[PIPE_BOARD], &mboard, sizeof(MBoard)) == 0) {
      // Put it to priority queue. if the queue is full, block the threaa.
      queue_push(&ex->q, &mboard, sizeof(MBoard));
    }
  }
}
*/

static inline unsigned char get_flag(Exchanger* ex) {
  return __sync_fetch_and_add(&ex->ctrl_flag, 0);
}

// For control thread, we listen to the ctrl channel and change the status of the server.
void *threaded_ctrl(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  MCtrl mctrl;
  while (! ex->done) {
    if (PipeRead(&ex->channels[PIPE_C2S], &mctrl, sizeof(MCtrl)) == 0) {
      if (mctrl.code != 0) {
        // All the flags will be reset after we call sendAck.
        printf("Get control signal. Code = %d\n", mctrl.code);
        __sync_fetch_and_or(&ex->ctrl_flag, 1 << mctrl.code);
      }
    }
    // Some sleep?
  }
  return NULL;
}

// Initialize all threads
//    pipe_path: the path of the pipe
//    id: the id of the pipe.
//    is_server: whether this opened pipe is a server.
void *ExLocalInit(const char *pipe_path, int id, BOOL is_server) {
  Exchanger *ex = (Exchanger *)malloc(sizeof(Exchanger));
  int flag = is_server ? 1 : 0;
  char buf[1000];

  for (int i = 0; i < NUM_CHANNELS; ++i) {
    sprintf(buf, "%s/%s-%d-%d", pipe_path, PIPE_PREFIX, id, i);
    if (is_server) {
      // We need to remove the file first.
      remove(buf);
    }
    if (PipeInit(buf, flag, &ex->channels[i]) == -1) {
      free(ex);
      return NULL;
    }
  }

  ex->is_server = is_server;
  ex->wait_count = 0;
  ex->wait_count_max = 0;
  if (! is_server) return ex;

  ex->ctrl_flag = 0;
  ex->done = FALSE;
  ex->move_sent = 0;
  ex->board_received = 0;
  // Initialize queue.
  // queue_init(&ex->q, QUEUE_SIZE, sizeof(MBoard));

  // For client, we don't need to do anything.
  // For server, we need to start a few threads.
  //    Message thread: get all board messages and put them into a (priority) queue.
  //    Ctrl thread: check all control messages (ctrl_c2s) and change the status of the exchanger accordingly.
  // Ack knowledge will be sent by the main thread (the main function).
  // pthread_create(&ex->message, NULL, threaded_message, ex);
  pthread_create(&ex->ctrl, NULL, threaded_ctrl, ex);

  return ex;
}

void ExLocalDestroy(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    // printf("Closing pipe = %d\n", i);
    PipeClose(&ex->channels[i]);
  }

  if (ex->is_server) {
    ex->done = TRUE;
    // Do we need to wait all the threads to terminate?
    pthread_join(ex->ctrl, NULL);
  }
  free(ex);
}

#define ARGP(a) (a), sizeof(*a)
#define ARG(a)  &(a), sizeof(a)

#define BLOCK(a) while ((a) == -1) { } return TRUE
#define RET(a) do { if ((a) == 0) return TRUE; else return FALSE; } while(0)
#define RETN(a, n) do { int __i = 0; for (__i = 0; __i < (n); ++__i) if ((a) == 0) return TRUE; return FALSE; } while(0)

// Server side, three cases
//   1. Block on message with exit value = SIG_OK on newboard.
//   2. Return immediately with exit value = SIG_RESTART
//   3. Return immediately with exit value = SIG_HIGH_PR
// If num_attempt == 0, then try indefinitely, otherwise try num_attempt.
int ExLocalServerGetBoard(void *ctx, MBoard *mboard, int num_attempt) {
  Exchanger *ex = (Exchanger *)ctx;
  int count = 0;
  while (! ex->done && (num_attempt == 0 || count < num_attempt)) {
    // Check flag.
    unsigned char flag = get_flag(ex);
    if (flag & (1 << SIG_RESTART)) {
      return SIG_RESTART;
    }
    // Otherwise get the board, if succeed, return.
    if (PipeRead(&ex->channels[PIPE_BOARD], ARGP(mboard)) == 0) {
      ex->board_received ++;
      return SIG_OK;
    } else if (flag & (1 << SIG_FINISHSOON)) {
      // If there is no board to read and we want finish soon, return immediately.
      return SIG_NOPKG;
    }
    // Do I need to put some sleep here?
    count ++;
  }

  // queue_pop(&ex->q, board, sizeof(MBoard));
  return SIG_NOPKG;
}

// Block send moves, once CNN finish evaluation.
// If done is set, don't send anything.
BOOL ExLocalServerSendMove(void *ctx, MMove *move) {
  Exchanger *ex = (Exchanger *)ctx;
  if (move->seq == 0) return FALSE;
  while (! ex->done) {
    unsigned char flag = get_flag(ex);
    if (flag & (1 << SIG_RESTART)) break;
    if (PipeWrite(&ex->channels[PIPE_MOVE], ARGP(move)) == 0) {
      ex->move_sent ++;
      return TRUE;
    }
  }
  return FALSE;
}

// Send ack for any unusual signal received.
BOOL ExLocalServerSendAckIfNecessary(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  // Clear the flag.
  unsigned char flag = get_flag(ex);
  // If the flag is not zero before cleanup, we need to send an ack (so that the client know we have received it).
  BOOL clean_flag = FALSE;
  BOOL send_ack = TRUE;
  if (flag != 0) {
    if (flag & (1 << SIG_RESTART)) {
      // Clean up the message queues.
      int num_discarded = 0;
      MBoard mboard;
      while (PipeRead(&ex->channels[PIPE_BOARD], ARG(mboard)) == 0) num_discarded ++;
      printf("#Board Discarded = %d\n", num_discarded);
      clean_flag = TRUE;
    } else if (flag & (1 << SIG_FINISHSOON)) {
      clean_flag = TRUE;
      // Do not need to send ack for FINISHSOON (No one is going to receive it).
      send_ack = FALSE;
    }
  }

  if (clean_flag) {
    printf("Summary: Board received = %d, Move sent = %d\n", ex->board_received, ex->move_sent);
    ex->board_received = 0;
    ex->move_sent = 0;

    // All states are resumed, then we clear the flag. (If we clear the flag before that, sendmove and receiveboard might run before the stats are reset).
    __sync_fetch_and_and(&ex->ctrl_flag, 0);

    // Send message.
    if (send_ack) {
      MCtrl mctrl;
      mctrl.code = SIG_ACK;
      while (! ex->done) {
        if (PipeWrite(&ex->channels[PIPE_S2C], ARG(mctrl)) == 0) {
          printf("Ack sent with previous flag = %d\n", flag);

          // Sent.
          return TRUE;
        }
      }
    }
  }
  // Not sent.
  return FALSE;
}

BOOL ExLocalServerIsRestarting(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  unsigned char flag = get_flag(ex);
  return (flag & (1 << SIG_RESTART)) ? TRUE : FALSE;
}

// ==================================== Client side ===============================================
int ExLocalClientSetMaxWaitCount(void *ctx, int n) {
  Exchanger *ex = (Exchanger *)ctx;
  int res = ex->wait_count_max;
  ex->wait_count_max = n;
  return res;
}

// Send board (not blocked)
BOOL ExLocalClientSendBoard(void *ctx, MBoard *board) {
  Exchanger *ex = (Exchanger *)ctx;
  RET(PipeWrite(&ex->channels[PIPE_BOARD], ARGP(board)));
}

// Receive move (not blocked)
BOOL ExLocalClientGetMove(void *ctx, MMove *move) {
  Exchanger *ex = (Exchanger *)ctx;
  RET(PipeRead(&ex->channels[PIPE_MOVE], ARGP(move)));
}

// Send restart signal (in block mode) once the search is over
BOOL ExLocalClientSendRestart(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  MCtrl mctrl;
  mctrl.code = SIG_RESTART;
  // Make sure it is sent.
  BLOCK(PipeWrite(&ex->channels[PIPE_C2S], ARG(mctrl)));
}

BOOL ExLocalClientIncWaitCount(void *ctx, BOOL send_if_needed) {
  Exchanger *ex = (Exchanger *)ctx;
  int curr = __sync_add_and_fetch(&ex->wait_count, 1);
  // printf("IncCount! count = %d/%d\n", curr, ex->wait_count_max);
  if (curr >= ex->wait_count_max && send_if_needed) {
    // Then we sent the finish soon signal
    // printf("Sending finishsoon message!\n");
    ExLocalClientSendFinishSoon(ctx);
    return TRUE;
  }
  return FALSE;
}

BOOL ExLocalClientDecWaitCount(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  int curr = __sync_add_and_fetch(&ex->wait_count, -1);
  // printf("DecCount! count = %d/%d\n", curr, ex->wait_count_max);
  if (curr < 0) {
    printf("Error!!! In ExLocalClientDecWaitOnCount(), count = %d < 0", curr);
    return FALSE;
  }
  return TRUE;
}

BOOL ExLocalClientSendFinishSoon(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  MCtrl mctrl;
  mctrl.code = SIG_FINISHSOON;
  // Make sure it is sent.
  BLOCK(PipeWrite(&ex->channels[PIPE_C2S], ARG(mctrl)));
}

// Blocked wait until ack is received.
BOOL ExLocalClientWaitAck(void *ctx) {
  Exchanger *ex = (Exchanger *)ctx;
  MCtrl mctrl;
  mctrl.code = SIG_RESTART;

  while (1) {
    if (PipeRead(&ex->channels[PIPE_S2C], ARG(mctrl)) == 0) {
      if (mctrl.code == SIG_ACK) break;
    }
  }
  return TRUE;
}
