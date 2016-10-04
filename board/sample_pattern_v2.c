//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#include "pattern_v2.h"

void str2play(const char *str, Coord *m, Stone *player) {
  if (str[0] == 'W') *player = S_WHITE;
  else *player = S_BLACK;

  if (! strcmp(str + 2, "PASS")) {
    *m = M_PASS;
    return;
  }
  if (! strcmp(str + 2, "RESIGN")) {
    *m = M_RESIGN;
    return;
  }

  int x = str[2] - 'A';
  if (x >= 8) x --;
  int y;
  sscanf(str + 3, "%d", &y);
  y --;
  *m = OFFSETXY(x, y);
}

void simple_play(Board *b, const char *move_str) {
  GroupId4 ids;
  Coord m;
  Stone player;
  str2play(move_str, &m, &player);

  if (TryPlay(b, X(m), Y(m), player, &ids)) {
    Play(b, &ids);
  }
}

int main(int argc, char *argv[]) {
  // Load the pattern library and sample from it.
  if (argc < 5) {
    printf("Usage: sample_pattern_v2 pattern_file num_moves num_games verbose");
  }
  const char *pattern_file = argv[1];
  int num_moves, num_games, verbose;
  sscanf(argv[2], "%d", &num_moves);
  sscanf(argv[3], "%d", &num_games);
  sscanf(argv[4], "%d", &verbose);

  void *pat = InitPatternV2(pattern_file, NULL, FALSE);
  PatternV2Params params = *PatternV2GetParams(pat);
  params.verbose = verbose;
  PatternV2UpdateParams(pat, &params);

  Board b;
  ClearBoard(&b);
  simple_play(&b, "B Q4");
  simple_play(&b, "B Q16");
  simple_play(&b, "B D4");
  simple_play(&b, "B D16");
  simple_play(&b, "W F3");

  SampleSummary summary;
  AllMovesExt *move_ext = InitAllMovesExt(num_moves);

  double total_duration = 0.0;
  int total_moves = 0;

  for (int i = 0; i < num_games; ++i) {
    // Sample it.
    void *be = PatternV2InitBoardExtra(pat, &b);
    double start = wallclock();
    PatternV2SampleMany(be, move_ext, NULL, &summary);
    total_duration += wallclock() - start;
    total_moves += summary.n;

    // After sampling, send the summary.
    printf("Game %d: moves [%d], random/top-k: %d/%d/%d/%d/%d, counter: %d/%d/%d/%d/%d\n", i, summary.n,
        summary.num_topn[0], summary.num_topn[1], summary.num_topn[2], summary.num_topn[3], summary.num_topn[4],
        summary.num_counters[1], summary.num_counters[2], summary.num_counters[3], summary.num_counters[4], summary.num_counters[5]);

    PatternV2DestroyBoardExtra(be);
  }

  printf("Time: %lf usec (%lf/%d)\n", total_duration / total_moves * 1e6, total_duration, total_moves);

  DestroyAllMovesExt(move_ext);
  DestroyPatternV2(pat);
}
