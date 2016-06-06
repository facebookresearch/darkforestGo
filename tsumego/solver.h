//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _SOLVER_H_
#define _SOLVER_H_

#include "../board/board.h"

// Currently we only have one type.
#define TG_LIVE_DIES 0

// Several criteriafor tsumego.
// 1. w/b lives.
//    There is one w/b group which has 2 liberties and no candidate move for b/w can touch the group.
// 2. w/b dead.
//    w/b loses too many stones (higher than the threshold).

typedef struct {
  // Goal of this search.
  // E.g., player = WHITE, then if w lives, -10, w dead, 10, otherwise keep searching.
  //       player = BLACK, then if b dead, -10, b lives, +10. otherwise keep searching.
  Stone target_player;

  // The threshold for dead.
  int dead_thres;

  Region region;

  // Maximum count of the search. -1 means infinite.
  int max_count;
} TGCriterion;

// Solve a given tsumego, located at Region, by doing an exhaustive search.
int TsumegoSearch(const Board *board, const TGCriterion *criterion, AllMoves *move_seq);

#endif
