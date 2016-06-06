//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _PACKAGE_H_
#define _PACKAGE_H_

#include <stdio.h>
#include <time.h>
#include <inttypes.h>
#include "../common/common.h"
#include "../common/comm_constant.h"
#include "../board/board.h"

#define NUM_FIRST_MOVES 20
#define MAX_CUSTOM_DATA 500
#define SIG_OK 0
#define SIG_RESTART 1
#define SIG_FINISHSOON 2
#define SIG_NOPKG 3
#define SIG_ACK 100

// Several kind of messages. The first element has to be long
// Message 1: board
typedef struct {
  // sequence number.
  long seq;
  uint64_t b;
  // Send time (in microsecond).
  double t_sent;
  // Board configuration
  Board board;
} MBoard;

// Messsage 2: move information
typedef struct {
  long seq;
  uint64_t b;

  // Send time, received time and reply time.
  double t_sent, t_received, t_replied;
  char hostname[30];

  Stone player;
  BOOL error;
  char xs[NUM_FIRST_MOVES];
  char ys[NUM_FIRST_MOVES];
  float probs[NUM_FIRST_MOVES];
  // Use for types of moves, can be MOVE_SIMPLE_KO or MOVE_NORMAL
  char types[NUM_FIRST_MOVES];

  // Custom data. E.g., feature for the current board.
  char extra[MAX_CUSTOM_DATA];

  // The board hash for the board used.
  // uint64_t board_hash;

  // Score if there is any prediction.
  BOOL has_score;
  float score;
} MMove;

#endif
