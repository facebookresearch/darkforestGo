//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _DEFAULT_POLICY_COMMON_
#define _DEFAULT_POLICY_COMMON_

#include "board.h"

// #define SHOW_PROMPT
typedef enum { NORMAL = 0, KO_FIGHT, OPPONENT_IN_DANGER, OUR_ATARI, NAKADE, PATTERN, NO_MOVE, NUM_MOVE_TYPE } MoveType;

typedef struct {
  Coord m;
  int gamma;
  MoveType type;
  BOOL game_ended;
} DefPolicyMove;

// A queue for adding candidate moves.
typedef struct {
  const Board *board;
  // Move sequence.
  DefPolicyMove moves[MACRO_BOARD_SIZE*MACRO_BOARD_SIZE];
  int num_moves;
} DefPolicyMoves;

// Get a constant string that describes the type of default policy.
const char *GetDefMoveType(MoveType type);

// Simple constructor of default policy move.
DefPolicyMove c_mg(Coord m, MoveType t, int gamma);
DefPolicyMove c_m(Coord m, MoveType t);

// Add moves to def policy queue.
void add_move(DefPolicyMoves *m, DefPolicyMove move);

#endif
