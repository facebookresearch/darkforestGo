//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef _PLAYOUT_PARAMS_H_
#define _PLAYOUT_PARAMS_H_

#include "../board/board.h"
#include "../common/common.h"
#include "../common/package.h"

// Verbose level
#define V_SLIENT 0
#define V_CRITICAL 1
#define V_INFO 2
#define V_DEBUG 3

#define SERVER_LOCAL 0
#define SERVER_CLUSTER 1

#define THREAD_NEW_BLOCKED 0
#define THREAD_ALREADY_BLOCKED 1
#define THREAD_NEW_RESUMED 2
#define THREAD_ALREADY_RESUMED 3
#define THREAD_STILL_BLOCKED 4

// Choice of default policies.
#define DP_SIMPLE 0
#define DP_PACHI  1
#define DP_V2     2

// Used for maximal time spent.
#define THRES_PLY1 60
#define THRES_PLY2 200
#define THRES_PLY3 260
#define THRES_TIME_CLOSE     180
#define MIN_TIME_SPENT 1

typedef struct {
  char pipe_path[200];
  char tier_name[200];

  // Whether we use local server or global server, could be SERVER_LOCAL or SERVER_CLUSTER
  int server_type;

  // Go rule, rule = RULE_CHINESE (default) or RULE_JAPANESE
  int rule;

  // Komi (This also includes handicap).
  float komi;

  // It seems that MCTS really slacks off when the estimated win rate is too high. So we need to use dynkomi.
  // Set dynkomi_factor = 0.0 would disable dynkomi. If used, usually we set it to 1.0.
  float dynkomi_factor;

  // Verbose level.
  int verbose;

  // #gpu we used.
  int num_gpu;

  // Only use cpu-based rollout.
  BOOL cpu_only;

  // Print search tree.
  BOOL print_search_tree;

  // If total_time == 0, then do not use heuristic time management.
  // Otherwise we assume total_time is all the time (in second) we have and we need to plan for it.
  int heuristic_tm_total_time;
  // Maximum time spent and minimal time spent, will be computed when heuristic_tm_total_time is set.
  float max_time_spent, min_time_spent;

  // Set the time_left, unit is second.
  // This is a bit special since we don't need to lock all threads and resume afterwards.
  // We just need to change the number, and the threads will take care of it. If time left is 0, then there is no
  // constraints on the time left.
  unsigned int time_left;
} SearchParamsV2;

typedef struct {
  float dynkomi;
} SearchVariants;

// Parameters for each monte carlo tree search.
typedef struct {
  // The number of rollouts the root node should achieve per move.
  int num_rollout;
  // The number of rollouts each move should at least search.
  int num_rollout_per_move;

  // The number of dcnn evaluated required per move.
  int num_dcnn_per_move;

  // Expand leaf only if total >= expand_n_thres.
  int expand_n_thres;

  int verbose;

  // #move receivers.
  int num_receiver;

  int max_depth_default_policy;
  int max_send_attempts;

  // Number of CPU threads for MCTS trees.
  int num_tree_thread;

  // Whether we put noise during UCT.
  // The noise is to speed up the performance of MCTS+DCNN. If scores are deterministic, then MCTS will block on one node.
  // Which might be, actually a good thing.
  // When num_virtual_games > 0, then both sigma and sigma_over_n is not used.
  float sigma;

  // Whether we use sigma * sqrt(n_parent) / n. This will reduce sigma gradually when we are confidence on one node's win rate.
  BOOL use_sigma_over_n;

  // Decision mixture ratio between cnn_prediction_confidence and mcts count / parent count.
  // Final score = mcts_count_ratio + decision_mixture_ratio * cnn_confidence.
  float decision_mixture_ratio;

  // Receiver parameters.
  // Accumulated probability threshold (in percent).
  int rcv_acc_percent_thres;

  // Maximum number of move to pick.
  // Minimum number of move to pick.
  int rcv_max_num_move;
  int rcv_min_num_move;

  // Use pondering
  BOOL use_pondering;

  // Time limit for each move (in sec).
  long time_limit;

  // Immediate return if CNN only gives one best move.
  BOOL single_move_return;

  // Which default policy we are using.
  int default_policy_choice;
  // The name of pattern file.
  char pattern_filename[1000];
  int default_policy_sample_topn;
  double default_policy_temperature;

  // Define minimal rollout so that the search procedure can be peekable.
  int min_rollout_peekable;

  // Use RAVE heuristics.
  BOOL use_rave;

  // Use sync/async model.
  // In async model, we will use fast rollout to fill in the moves first, when DCNN move returns, we append the moves.
  // In sync model, we just wait until DCNN moves return.
  BOOL use_async;
  int fast_rollout_max_move;

  // Tsumego mode. In this mode, we focus on a small region and generate a lot of moves to determine the life and death situation.
  BOOL life_and_death_mode;

  // The rectangle used for move generation (I don't think that is a good idea, but try that for now.)
  Region ld_region;

  // Whether we use hand-crafted features or from Tsumego DCNN model.
  BOOL use_tsumego_dcnn;

  // Specify which side is the defender.
  Stone defender;

  // Build an online model for search.
  BOOL use_online_model;
  // Learning rate.
  float online_model_alpha;

  // Online prior mixture ratio when the online model is open.
  float online_prior_mixture_ratio;

  // Use the win rate prediction to replace playout.
  BOOL use_cnn_final_score;

  // We use win rate prediction for ply >= min_ply_to_use_cnn_final_score.
  int min_ply_to_use_cnn_final_score;

  // Once we use win rate prediction, the mixture ratio between win rate prediction and actual playout score.
  // Final score = final_mixture_ratio * win_rate_prediction + (1.0 - final_mixture_ratio) * playout_result.
  float final_mixture_ratio;

  // Whether we use virtual game. If num_virtual_games == 0, then we will use sigma.
  int num_virtual_games;

  // Whether we run playout if a node is waiting for expansion.
  // If it is 0, then all threads will be blocked when waiting for CNN evaluation.
  // If it is 100, then all threads will continue (run playout and restart).
  // Ideally we only want a few node to wait so that they can be the next batch to expand child nodes.
  int percent_playout_in_expansion;

  // Run a few playout and takes their mean. Default is 1, but could be higher.
  int num_playout_per_rollout;

  // Whether we use PUCT and previous UCT
  BOOL use_old_uct;
} TreeParams;

#endif

