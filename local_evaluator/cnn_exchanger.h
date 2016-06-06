//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _CNN_EXCHANGER_
#define _CNN_EXCHANGER_

#include <stdio.h>
#include "../common/comm_pipe.h"
#include "../common/common.h"

// Dummy functions for distributed version.

void *ExClientInit(const char tier_name[100]) { return NULL; }
void ExClientDestroy(void *) { }

//
int ExClientSetMaxWaitCount(void *ctx, int n) { return 0; }

// Send board (not blocked)
BOOL ExClientSendBoard(void *ctx, MBoard *board) { return TRUE; }

// Receive move (not blocked)
BOOL ExClientGetMove(void *ctx, MMove *move) { return TRUE; }

// Send restart signal (in block mode) once the search is over
BOOL ExClientSendRestart(void *ctx) { return TRUE; }

BOOL ExClientIncWaitCount(void *ctx, BOOL send_if_needed) { return TRUE; }

BOOL ExClientDecWaitCount(void *ctx) { return TRUE; }

BOOL ExClientSendFinishSoon(void *ctx) { return TRUE; }

// Blocked wait until ack is received.
BOOL ExClientWaitAck(void *ctx) { return TRUE; }

void ExClientStopReceivers(void *) { }

#endif
