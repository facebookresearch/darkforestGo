//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _PLAYOUT_MULTITHREAD_H_
#define _PLAYOUT_MULTITHREAD_H_

#include "playout_params.h"
#include "playout_common.h"
#include "tree_search.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compose the move from x, y and player.
Move compose_move(int x, int y, Stone player);
Move compose_move2(Coord m, Stone player);

void ts_v2_print_params(void *ctx);
void ts_v2_init_params(SearchParamsV2 *params);

// Initialize the tree search, return a search handle (void *).
void* ts_v2_init(const SearchParamsV2 *params, const TreeParams *tree_params, const Board *board);

// Set the board, also can be used to restart the game.
void ts_v2_setboard(void *ctx, const Board *init_board);
void ts_v2_add_move_history(void *ctx, Coord m, Stone player, BOOL actual_play);

// Change the parameters on the fly. Be extra careful about this function.
// Depending on different setting, we might need to clear up different internal status.
BOOL ts_v2_set_params(void *ctx, const SearchParamsV2 *new_params, const TreeParams *new_tree_params);

// Set time left (in second).
// Note that this will not block all threads.
BOOL ts_v2_set_time_left(void *ctx, unsigned int time_left, unsigned int num_moves);

// Perform tree search given the current board and player.
void ts_v2_search_start(void *h);
void ts_v2_search_stop(void *h);

// Turn on all threads. Return true if succeed.
// It will not clear all statistics.
void ts_v2_thread_on(void *h);
void ts_v2_thread_off(void *h);

// Return the best move as a result of the current search.
// Move_seq must not be NULL and will store the move sequence (if l&d mode is on).
Move ts_v2_pick_best(void *h, AllMoves *move_seq, const Board *verify_board);

// Peek the topk move and save it to moves.
void ts_v2_peek(void *h, int topk, Moves *moves, const Board *verify_board);

// Output the current tree to a json file.
void ts_v2_tree_to_json(void *h, const char *json_prefix);

// Output the feature to a text file, one feature a line. L&D mode only.
void ts_v2_tree_to_feature(void *ctx, const char *feature_prefix);

// Once a move is picked, prune the tree accordingly.
void ts_v2_prune_opponent(void *ctx, Coord m);
void ts_v2_prune_ours(void *ctx, Coord m);

// Undo the most recent pass. If no pass is the recent pass, do nothing.
int ts_v2_undo_pass(void *h, const Board *before_board);

// Free tree search handle.
void ts_v2_free(void *h);

#ifdef __cplusplus
}
#endif

#endif
