//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _CNN_LOCAL_EXCHANGER_H_
#define _CNN_LOCAL_EXCHANGER_H_

#include "../common/package.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init exchanger.
//    pipe_path: the path of the pipe
//    id: the id of the pipe.
//    is_server: whether this opened pipe is a server.
void *ExLocalInit(const char *pipe_path, int id, BOOL is_server);
void ExLocalDestroy(void *ctx);

// Server side, three cases
//   1. Block on message with exit value = SIG_OK on newboard.
//   2. Return immediately with exit value = SIG_RESTART
//   3. Return immediately with exit value = SIG_HIGH_PR
// If num_attempt == 0, then try indefinitely, otherwise try num_attempt.
int ExLocalServerGetBoard(void *ctx, MBoard *board, int num_attempt);
// Block send moves, once CNN finish evaluation.
// If done is set, don't send anything.
BOOL ExLocalServerSendMove(void *ctx, MMove *move);
// Send ack for any unusual signal received.
BOOL ExLocalServerSendAckIfNecessary(void *ctx);
// Check whether the server is restarting.
BOOL ExLocalServerIsRestarting(void *ctx);

// Client side
// Set Maximum wait count. Return the previous maximum.
int ExLocalClientSetMaxWaitCount(void *ctx, int n);
// Send board (not blocked)
BOOL ExLocalClientSendBoard(void *ctx, MBoard *board);
// Receive move (not blocked)
BOOL ExLocalClientGetMove(void *ctx, MMove *move);
// Add the wait count. If the count is >= wait_count_max (set by ExLocalClientSetWaitCount) and send_if_needed is true,
// then send SIG_FINISHSOON.
// This means that already n threads are waiting on the results, please response soon.
// Return TRUE if we have sent SIG_FINISHSOON, return FALSE if not.
BOOL ExLocalClientIncWaitCount(void *ctx, BOOL send_if_needed);
// Decrease the wait count.
// Return TRUE if we have done the operation, FALSE if the count is < 0 (this should error).
BOOL ExLocalClientDecWaitCount(void *ctx);

// Send restart signal (in block mode) once the search is over
BOOL ExLocalClientSendRestart(void *ctx);
// Send finish soon signal. Server will evaluate all current existing
// board situations and then return. It won't wait for a missing board indefinitely.
BOOL ExLocalClientSendFinishSoon(void *ctx);
// Blocked wait until ack is received.
BOOL ExLocalClientWaitAck(void *ctx);

#ifdef __cplusplus
}
#endif

#endif
