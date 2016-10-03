//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _TREE_SEARCH_H_
#define _TREE_SEARCH_H_

#include "playout_params.h"
#include "playout_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Send/Receive callback.
typedef BOOL (* func_send_board)(void *context, int, MBoard *b);
// Receive the move from exchanger.
typedef BOOL (* func_receive_move)(void *context, int, MMove *mmove);
typedef int (* func_receiver_discard_move)(void *context, int);
typedef void (* func_receiver_restart)(void *context);

typedef struct {
  void *context;
  // Callbacks.
  func_send_board callback_send_board;
  func_receive_move callback_receive_move;
  func_receiver_discard_move callback_receiver_discard_move;
  func_receiver_restart callback_receiver_restart;
} ExCallbacks;

// ================================================
// APIs for tree search.
void tree_search_init_params(TreeParams *params);

void *tree_search_init(const SearchParamsV2 *common_params, const SearchVariants *variants, const ExCallbacks *callbacks, const TreeParams *params, const Board *board_init);
void tree_search_free(void *ctx);

void tree_search_print_params(void *ctx);
BOOL tree_search_set_params(void *ctx, const TreeParams *new_params);

// Start and stop the search.
void tree_search_start(void *ctx);
void tree_search_stop(void *ctx);

// Stop and resume the threads.
void tree_search_thread_off(void *ctx);
void tree_search_thread_on(void *ctx);

// Reset the entire tree. This happens when we setboard/setkomi etc.
BOOL tree_search_reset_tree(void *ctx);
BOOL tree_search_undo_pass(void *ctx, const Board *before_board);
BOOL tree_search_set_board(void *ctx, const Board *new_board);

void tree_search_print_tree(void *ctx);

// Save the tree to json format.
void tree_search_to_json(void *ctx, const Move *prev_moves, int num_prev_moves, const char *output_filename);

// Save the tree to feature file (ARFF format).
void tree_search_to_feature(void *ctx, const char *output_filename);

// Return the best move. Remember to call tree_search_prune_ours after the decision is made.
Move tree_search_pick_best(void *ctx, AllMoves *all_moves, const Board *verify_board);

// Peek the top few moves, topk = moves->num_moves.
BOOL tree_search_peek(void *ctx, Moves *moves, const Board *verify_board);

// Prune the tree given the move.
void tree_search_prune_opponent(void *ctx, Coord m);
void tree_search_prune_ours(void *ctx, Coord m);

#ifdef __cplusplus
}
#endif

#endif
