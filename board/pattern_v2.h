//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _PATTERN_V2_H_
#define _PATTERN_V2_H_

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PV_NORMAL 1
#define PV_INFO   2
#define PV_DEBUG  3

typedef struct {
  int verbose;
  int cnt_threshold;
  double learning_rate;
  // Batch size.
  int batch_size;
  int sample_from_topn;

  // whether we need to enter the training mode.
  // Deprecated.
  BOOL training_capacity;

  BOOL prior_nakade;
  BOOL prior_neighbor;
  BOOL prior_resp;

  // Find the move that save the group if the last move causes our group to be in atari.
  BOOL prior_save_atari;

  // Find the kill move if the last move causes the opponent group to be in atari.
  BOOL prior_kill_other;

  // Put last group into atari.
  BOOL prior_put_group_to_atari;

  // Global prior.
  BOOL prior_global;

  // Ko prior (if the ko has been recently played and is eligible to play, play it).
  BOOL prior_ko;

  // Eye prior (if there is a move to make eye or falsify eye, play it.)
  BOOL prior_eye;
} PatternV2Params;

#define SAMPLE_HEAP      0
#define SAMPLE_RANDOM    1
#define SAMPLE_TOPN      2
#define SAMPLE_MUST_MOVE 3

#define PRIOR_STATUS_NOT_SET     -1
#define PRIOR_STATUS_NORMAL       0
#define PRIOR_STATUS_RECOMPUTE_Z  1
#define PRIOR_STATUS_PASS_RESIGN  2

typedef struct {
  Coord m;
  Stone player;
  double prob;
  int topn;
  int counter;
  int type;
  int heap_size;
  double total_prob;
} MoveExt;

typedef struct {
  MoveExt *moves;
  int num_moves;
} AllMovesExt;

typedef char SingleComment[5000];

typedef struct {
  SingleComment *comments;
  int num_comments;
} AllMovesComments;

typedef struct {
  char name[100];
  double sum_loglikelihood;
  int sum_top1;
  int n_selected_moves;
  int n_all_moves;
  int n_games;
  int n_recompute_Z;

  double total_duration;
  // Only used for policy gradient.
  int sum_result_correct;
  int n_pg_iterations;
} PerfSummary;

#define NUM_STATS_TOPN  20

typedef struct {
  char name[100];
  // Statistics for sampling.
  // num_topn[0] is the times to sample randomly.
  int num_topn[NUM_STATS_TOPN];
  int num_counters[NUM_STATS_TOPN];
  int n_recompute_Z;
  int n;
  int max_counter;
  double total_duration;
} SampleSummary;

typedef struct {
  // Komi and handi together.
  float komi;
  // Which rule we use.
  int rule;
  // Which player has won the game.
  Stone player_won;
  // The board situation we want to simulate from.
  const Board *board;
  // #num of iterations we want to simulate.
  int iterations;
} GameScoring;

void InitPerfSummary(PerfSummary *perf_summary);
void CombinePerfSummary(PerfSummary *dst, const PerfSummary *src);
void PrintPerfSummary(const PerfSummary *perf_summary);

void InitSampleSummary(SampleSummary *sample_summary);
void CombineSampleSummary(SampleSummary *dst, const SampleSummary *src);
void PrintSampleSummary(const SampleSummary *sample_summary);

void *PatternV2InitGradients();
void PatternV2DestroyGradients(void *grad);

// Set current board. The board will be copied into the context.
void *PatternV2InitBoardExtra(void *h, const Board *board);
void PatternV2DestroyBoardExtra(void *board_extra);

// Play the move to get the next state.
void PatternV2PlayMove2(void *board_extra, const GroupId4 *ids);
BOOL PatternV2PlayMove(void *board_extra, Coord m, Stone player);

// Sample the move. This must be very fast..
void PatternV2Sample2(void *board_extra, void *context, RandFunc randfunc, GroupId4 *ids, MoveExt *move_ext);
void PatternV2Sample(void *board_extra, GroupId4 *ids, MoveExt *move_ext);
void PatternV2SampleTopn(void *be, int n, void *context, RandFunc randfunc, GroupId4 *ids, MoveExt *move_ext);
void PatternV2SampleMany(void *be, AllMovesExt *all_moves, AllMovesComments *all_comments, SampleSummary *summary);
// Get approximate top n. Return the actual #moves that have been saved.
// If append_with_random_move is TRUE, then we just fill the rest with random valid moves.
int PatternV2GetApproxTopn(void *be, int num_moves, Coord *moves, float *confidences, BOOL append_with_random_move);
int PatternV2GetTopn(void *be, int n, Coord *moves, float *confidences, BOOL fill_with_random_move);

// Sample interface, sample topn or sample the entire heap.
void PatternV2SampleInterface(void *be, void *context, RandFunc randfunc, GroupId4 *ids, MoveExt *move_ext);

// Sample until the board is filled.
void PatternV2SampleUntil(void *be, void *context, RandFunc randfunc, AllMovesExt *moves, SampleSummary *summary);
void PatternV2SampleUntilSingleThread(void *be, AllMovesExt *moves, SampleSummary *summary);

AllMovesExt *InitAllMovesExt(int num_moves);
AllMovesComments *InitAllMovesComments(int num_moves);
void DestroyAllMovesExt(AllMovesExt *h);
void DestroyAllMovesComments(AllMovesComments *h);

// New pattern map.
// Simple hash library.
// 1. No hash collision.
// 2. A blooming filter to make sure a pattern is saved only if it has been seen twice.
//
// Load the pattern file and their weights (if pattern file is not NULL).
void PatternV2DefaultParams(PatternV2Params *params);
void *InitPatternV2(const char *pattern_file, const PatternV2Params *params, BOOL init_empty_if_load_failed);
void PatternV2UpdateParams(void *ctx, const PatternV2Params *params);
const PatternV2Params *PatternV2GetParams(void *ctx);

BOOL LoadPatternV2(void *ctx, const char *filename);
BOOL SavePatternV2(void *ctx, const char *filename);

// Harvest pattern around the move.
BOOL PatternV2Harvest(void *h, void *board_extra, Coord m);
void PatternV2HarvestMany(void *h, void *board_extra, const AllMovesExt *all_moves);

void PatternV2StartTraining(void *h);

// Set sampling parameters.
//   topn: only sample from topn moves.
//   T:    use temperature, exp(xx/T) as prob-odd.
void PatternV2SetSampleParams(void *ctx, int topn, double T);
void PatternV2SetVerbose(void *ctx, int verbose);

#define TRAINING_POSITIVE 1
#define TRAINING_EVALONLY 0
#define TRAINING_NEGATIVE -1

// void PatternV2TrainMany(void *h, void *board_extra, const AllMovesExt *all_moves, int black_training_type, int white_training_type, PerfSummary *summary);
void PatternV2TrainManySaveGradients(void *board_extra, void *grads, const AllMovesExt *all_moves, int black_training_type, int white_training_type, PerfSummary *summary);
void PatternV2TrainPolicyGradient(void *h, void *grads, const GameScoring *scoring, BOOL training, SampleSummary *sample_summary, PerfSummary *perf_summary);
void PatternV2UpdateWeightsAndCleanGradients(void *hh, void *g);

const Board *PatternV2GetBoard(void *be);

// Visualization
void PatternV2PrintStats(void *h);
void PatternV2BoardExtraPrintStats(void *board_extra);
BOOL PatternV2BoardExtraCheck(void *board_extra);
const char *PatternV2BoardExtraDumpInfo(void *be, int max_heap_size);

// Destroy pattern map.
void DestroyPatternV2(void *h);

#ifdef __cplusplus
}
#endif

#endif
