//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 
// This file is inspired by Pachi's engine (https://github.com/pasky/pachi). 
// The main DarkForest engine (when specified with `--playout_policy v2`) does not depend on it. 
// However, the simple policy opened with `--playout_policy simple` will use this library.

#ifndef _PATTERN_H_
#define _PATTERN_H_

#include <stdint.h>

#include "board.h"
#include "default_policy_common.h"

/* hash3_t pattern: ignore middle point, 2 bits per intersection (color)
 * plus 1 bit per each direct neighbor => 8*2 + 4 bits. Bitmap point order:
 * 7 6 5    b
 * 4   3  a   9
 * 2 1 0    8   */
/* Value bit 0: black pattern; bit 1: white pattern */

typedef uint64_t hash_t;
typedef uint32_t hash3_t; // 3x3 pattern hash

// Conceal all the interfaces.
void *InitPatternDB();
// The hash pattern is extracted from Board.
hash3_t GetHash(const Board *b, Coord m);
BOOL QueryPatternDB(void *pp, hash3_t pat, Stone color, int* gamma);
void DestroyPatternDB(void *);

// Get pattern moves from the board and put them to default policy move queue.
void CheckPatternFromLastMove(void *, DefPolicyMoves *m);

#endif
