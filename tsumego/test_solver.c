//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "solver.h"

Coord gtp2coord(const char *s) {
}

void PlayStones(const Board *b, const char *blacks, const char *whites) {
}

int main() {
  Board b;
  ClearBoard(&b);
  // Put stones here.
  const char *whites[] = {
    "L19", "L18", "L17", "L16", "M16", "N16", "O16",
    "P16", "R19", "R16", "R18", "S17", "S16", "T18"
  };
  const char *blacks[] = {
    "M18", "M17", "N17", "O17", "P18", "Q18", "Q17",
    "R17", "S18"
  };

  TGCriterion crit;
  TGRegion *r = &crit.region;
  GetBoardBBox(&b, &r->left, &r->top, &r->right, &r->bottom);

  // Black should live.
  crit.w_cap_upper_bound = 4;
  crit.w_crit_loc = M_PASS;

  // Get moves.
  AllMoves moves;
  TsumegoSearch(&b, &crit, &moves);
}
