//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "default_policy_common.h"

static const char *g_move_prompts[NUM_MOVE_TYPE] = {
  "NORMAL",
  "KO_FIGHT",
  "OPPONENT_IN_DANGER",
  "OUR_ATARI",
  "NAKADE",
  "PATTERN",
  "NO_MOVE"
};

const char *GetDefMoveType(MoveType type) {
  if (type < 0 || type >= NUM_MOVE_TYPE) return NULL;
  else return g_move_prompts[type];
}

DefPolicyMove c_mg(Coord m, MoveType t, int gamma) {
  DefPolicyMove move;
  move.m = m;
  move.type = t;
  move.gamma = gamma;
  move.game_ended = FALSE;
  return move;
}

DefPolicyMove c_m(Coord m, MoveType t) {
  DefPolicyMove move;
  move.m = m;
  move.type = t;
  move.gamma = 100;
  move.game_ended = FALSE;
  return move;
}

void add_move(DefPolicyMoves *m, DefPolicyMove move) {
  if (m->num_moves < MACRO_BOARD_SIZE*MACRO_BOARD_SIZE) {
    // char buf[30];
    // printf("#move = %d. Add move %s. type = %d\n", m->num_moves, get_move_str(move.m, S_EMPTY, buf), move.type);
    m->moves[m->num_moves ++] = move;
#ifdef SHOW_PROMPT
    printf(g_move_prompts[move.type]);
    printf("\n");
#endif
  } else {
    printf("#moves is out of bound!! num_moves = %d\n", m->num_moves);
    error("");
  }
}
