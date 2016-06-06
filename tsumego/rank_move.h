//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _RANK_MOVE_H_
#define _RANK_MOVE_H_

#include "../board/board.h"
#include <stdio.h>

void GetRankedMoves(const Board* board, Stone defender, const Region *r, int max_num_moves, AllMoves *all_moves);

// Save the candidate move to file.
// The feature for each move are printed in one line, separated by comma.
// Usually good move with score 1, bad move with score 0.
void SaveMoveFeatureName(FILE *fp);
BOOL SaveMoveWithFeature(const Board *board, Stone defender, Coord m, int score, FILE *fp);

#endif
