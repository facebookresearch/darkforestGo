//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _PLAYOUT_CALLBACKS_H_
#define _PLAYOUT_CALLBACKS_H_

#include "tree_search_internal.h"

// Normal mode.
BOOL cnn_policy(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen);
void threaded_run_bp(ThreadInfo *info, float black_moku, Stone next_player, int end_ply, BOOL board_on_child, BlockOffset child_offset, TreeBlock *b);
float threaded_compute_score(ThreadInfo *info, const Board *board);
BOOL dcnn_leaf_expansion(ThreadInfo *info, const Board *board, TreeBlock *b);

BOOL async_policy(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen);

// Def policy using fast rollout.
DefPolicyMove fast_rollout_def_policy(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose);

// Tsumego mode.
BOOL ld_policy(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen);
void threaded_run_tsumego_bp(ThreadInfo *info, float black_moku, Stone next_player, int end_ply, BOOL board_on_child, BlockOffset child_offset, TreeBlock *b);
BOOL tsumego_dcnn_leaf_expansion(ThreadInfo *info, const Board *board, TreeBlock *bl);
BOOL tsumego_rule_leaf_expansion(ThreadInfo *info, const Board *board, TreeBlock *b);

#endif
