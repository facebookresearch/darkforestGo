//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _TREE_SEARCH_INTERNAL_H_
#define _TREE_SEARCH_INTERNAL_H_

#include <pthread.h>
#include <semaphore.h>

#include "tree_search.h"
#include "tree.h"
#include "../board/default_policy_common.h"

// ========================== Data Structure =============================
struct __TreeHandle;

typedef struct {
  struct __TreeHandle *s;
  int receiver_id;

  // Notification for receivers.
  pthread_mutex_t lock;

  int cnn_move_valid;
  int cnn_move_received;
  int cnn_move_discarded;
  int cnn_move_seq_mismatched;
  int cnn_move_board_hash_mismatched;
} ReceiverParams;

// This is one for each thread.
typedef struct {
  // A pointer to search info common.
  struct __TreeHandle *s;
  // The exchanger id we should use for this thread.
  int ex_id;

  // Random seed used for this threaad.
  unsigned long seed;

  // For each thread, count #loops.
  int counter;

  // Counters.
  int num_policy_failed;
  int num_expand_failed;
  int leaf_expanded;
  int cnn_send_infunc;
  int cnn_send_attempt;
  int cnn_send_success;
  int use_ucb, use_cnn, use_async;
  int max_depth;
  // Count for preempt-expanding
  int preempt_playout_count;
} ThreadInfo;

// Some callback functions.
typedef DefPolicyMove (* func_def_policy)(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose);
typedef float (* func_compute_score)(ThreadInfo *info, const Board *board);
// When board_on_child is false, then the bl is at the same situation as board.
// When board_on_child is true, then board points to the child that has been played on the board, and child_offset is that child's offset.
typedef void (* func_back_prop)(ThreadInfo *info, float black_moku, Stone next_player, int end_ply, BOOL board_on_child, BlockOffset child_offset, TreeBlock *bl);
typedef BOOL (* func_policy)(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen);
typedef BOOL (* func_expand)(ThreadInfo *info, const Board *board, TreeBlock *b);

#define SC_NOT_YET                0
#define SC_TIME_OUT               1
#define SC_DCNN_ROLLOUT_REACHED   2
#define SC_TOTAL_ROLLOUT_REACHED  3
#define SC_NO_NEW_DCNN_EVAL       4
#define SC_SINGLE_MOVE_RETURN     5
#define SC_NO_VALID_MOVE          6
#define SC_TIME_LEFT_CLOSE        7
#define SC_TIME_HEURISTIC_STAGE1  8
#define SC_TIME_HEURISTIC_STAGE2  9
#define SC_TIME_HEURISTIC_STAGE3  10
#define SC_TIME_HEURISTIC_STAGE4  11

typedef struct __TreeHandle {
  TreeParams params;
  ExCallbacks callbacks;
  const SearchParamsV2 *common_params;
  const SearchVariants *common_variants;

  // This sequence number for this search, used for cnn communication.
  long seq;

  // The internal board.
  Board board;

  volatile BOOL search_done;
  volatile BOOL receiver_done;

  // Tree Pool used among all threads.
  TreePool p;

  // Semaphore for all threads.
  // If all_threads_blocking_count == 0, then all threads are running,
  // Each call of block_all_threads will increase it by 1, if all_threads_blocking_count > 0, then all threads are blocked.
  // Each call of resume_all_threads will decrease it by 1, if all_threads_blocking_count == 0, then all threads are resumed.
  // Call resume_all_threads when all_threads_blocking_count == 0 will have no effect.
  int all_threads_blocking_count;
  sem_t sem_all_threads_unblocked, sem_all_threads_blocked;
  int threads_count;

  // Total rollout count.
  int rollout_count;
  // Total dcnn evaluation received.
  int dcnn_count;
  int prev_dcnn_count;
  BOOL all_stats_cleared;

  // The timestamp when the search start. It will be update when resume_all_threads are called.
  long ts_search_start;

  // The timestamp when command "genmove" is called. Use for time control.
  long ts_search_genmove_called;

  // Notification with search complete signal.
  pthread_mutex_t mutex_search_complete;
  sem_t sem_search_complete;
  int flag_search_complete;

  // Callbacks.
  func_def_policy callback_def_policy;
  func_compute_score callback_compute_score;
  func_back_prop callback_backprop;
  func_policy callback_policy;
  func_expand callback_expand;

  // Threads for searching. # = number of tree threads.
  pthread_t *explorers;
  ThreadInfo *infos;

  // For default policy.
  void *def_policy;

  // Fast rollout interface.
  void *fast_rollout_policy;

  // Move receivers. # = number of gpus
  pthread_t *move_receivers;
  ReceiverParams *move_params;

  // Whether the bot is pondering. (Think when the opponent is thinking)
  BOOL is_pondering;

  // Online linear model. Weights and bias here. For now we use one float for each location of the board.
  // w . x + b will predict a score of the current board. sigmoid(w . x + b) should give the winrate between [0, 1].
  // Atomic operation is used to read/write the data.
  // We train the model online so that the best move is better than other moves.
  pthread_mutex_t mutex_online_model;
  float model_weights[MACRO_BOARD_SIZE * MACRO_BOARD_SIZE];
  float model_bias;
  float model_acc_err;
  int model_count_err;

  // Model for which move to search first.
  // If a move causes win, put positive, if a move causes loss, put negative.
  int move_scores_black[BOUND_COORD];
  int move_scores_white[BOUND_COORD];
} TreeHandle;

// Some utilities.
// ============================== Utility =====================================
extern inline unsigned int thread_rand(void *context, unsigned int max_value) {
  ThreadInfo *info = (ThreadInfo *)context;
  return fast_random(&info->seed, max_value);
}

extern inline unsigned int normal_rand(void *context, unsigned int max_value) {
  return rand() % max_value;
}

extern inline float thread_randf(ThreadInfo *info) {
  const int max_for_float = 32768;
  return ((float)fast_random(&info->seed, max_for_float)) / max_for_float;
}

#endif
