//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _PLAYOUT_COMMON_H_
#define _PLAYOUT_COMMON_H_

#include "../board/board.h"

#define PRINT_INFO(...) do { if (s->params.verbose >= V_INFO) { printf(__VA_ARGS__); fflush(stdout); } } while(0)
#define PRINT_CRITICAL(...) do { if (s->params.verbose >= V_CRITICAL) { printf(__VA_ARGS__); fflush(stdout); } } while(0)
#define PRINT_DEBUG(...) do { if (s->params.verbose >= V_DEBUG) { printf(__VA_ARGS__); fflush(stdout); } } while(0)

// Define Move so that we could communicate between LUA and C.
typedef struct {
  int x;
  int y;
  Coord m;
  Stone player;
  float win_rate;
  float win_games;
  int total_games;
} Move;

typedef struct {
  Move moves[MACRO_BOARD_SIZE*MACRO_BOARD_SIZE];
  int num_moves;
} Moves;

#endif
