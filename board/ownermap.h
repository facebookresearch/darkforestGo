//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _OWNERMAP_H_
#define _OWNERMAP_H_

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

void *InitOwnermap();
void FreeOwnermap(void *);

// Accumulating Ownermap
void ClearOwnermap(void *hh);
void AccuOwnermap(void *hh, const Board *board);
void GetDeadStones(void *hh, const Board *board, float ratio, Stone *livedead, Stone *group_stats);
void GetOwnermap(void *hh, float ratio, Stone *ownermap);

// Get ownermap probability.
void GetOwnermapFloat(void *hh, Stone player, float *ownermap);

// Get Trompy-Taylor score directly.
// If livedead != NULL, then livedead is a BOARD_SIZE * BOARD_SIZE array. Otherwise this output is ignored.
// If territory != NULL, then it is also a BOARD_SIZE * BOARD_SIZE array. Otherwise this output is ignored.
float GetTTScoreOwnermap(void *hh, const Board *board, Stone *livedead, Stone *territory);

// Visulize DeadStones
void ShowDeadStones(const Board *board, const Stone *stones);
void ShowStonesProb(void *hh, Stone player);

#ifdef __cplusplus
}
#endif

#endif
