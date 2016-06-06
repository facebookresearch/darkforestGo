//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "moggy.h"
#include <stdlib.h>

int main() {
  char buf[100];
  // After that we use moggy to analyze it. (With default parameters).
  void* policy = playout_moggy_init(NULL);

  // Put a few random moves and ask mogo to play it out.
  AllMoves all_moves;
  GroupId4 ids;
  const int num_round = 10000;

  Board b;
  timeit
  for (int j = 0; j < num_round; j++) {
    printf("Round = %d/%d\n", j, num_round);
    ClearBoard(&b);
    //for (int i = 0; i < rand() % 100 + 1; ++i) {
    for (int i = 0; i < 0; ++i) {
      FindAllCandidateMoves(&b, b._next_player, 0, &all_moves);
      // Randomly find one move.
      int idx = rand() % all_moves.num_moves;
      if (TryPlay2(&b, all_moves.moves[idx], &ids)) {
        Play(&b, &ids);
      }
    }

    ShowBoard(&b, SHOW_LAST_MOVE);
    play_random_game(policy, NULL, NULL, &b, -1, FALSE);
    ShowBoard(&b, SHOW_LAST_MOVE);
  }
  endtime

  playout_moggy_destroy(policy);
  return 0;
}
