//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "tree_search_internal.h"
#include "playout_common.h"
#include "playout_callbacks.h"
#include "../board/board.h"
#include "../pachi_tactics/moggy.h"
#include "../board/default_policy.h"
#include "../tsumego/rank_move.h"
#include "../board/pattern_v2.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

/*
static void show_all_moves(const Board *board, const AllMoves *all_moves) {
  // Debug: dump all moves.
  ShowBoard(board, SHOW_LAST_MOVE);
  fprintf(stderr,"\n===========Potential moves ================:\n");
  for (int i = 0; i < all_moves->num_moves; ++i) {
    fprintf(stderr,"%s ", get_move_str(all_moves->moves[i], board->_next_player));
  }
  fprintf(stderr,"\n===========End Potential moves================\n");
  fflush(stdout);
}
*/

static void show_all_cnn_moves(const TreeBlock *bl, Stone curr_player) {
  char buf[30];
  fprintf(stderr,"==== CNN Move for b = %lx, seq = %ld, status = %d ====\n", (uint64_t)bl, bl->cnn_data.seq, bl->cnn_data.evaluated);
  for (int i = 0; i < bl->n; ++i) {
      fprintf(stderr,"%s [%f] ", get_move_str(bl->data.moves[i], curr_player, buf), bl->cnn_data.confidences[i]);
  }
  fprintf(stderr,"\n==== End CNN Move ==========\n");
  fflush(stdout);
}

static const char *def_policy_str(int def_policy_choice) {
  switch (def_policy_choice) {
    case DP_SIMPLE: return "SIMPLE";
    case DP_PACHI:  return "PACHI";
    case DP_V2:     return "PATTERN_V2";
    default:        return "";
  }
}

static void block_all_receivers(TreeHandle *s) {
  if (s->params.use_async && ! s->common_params->cpu_only) {
    for (int i = 0; i < s->params.num_receiver; ++i) {
      ReceiverParams *rp = &s->move_params[i];
      pthread_mutex_lock(&rp->lock);
    }
  }
}

static void resume_all_receivers(TreeHandle *s) {
  if (s->params.use_async && ! s->common_params->cpu_only) {
    for (int i = 0; i < s->params.num_receiver; ++i) {
      ReceiverParams *rp = &s->move_params[i];
      pthread_mutex_unlock(&rp->lock);
    }
  }
}

// Basic functions to lock.
static int block_all_threads(TreeHandle *s, BOOL print_and_reset_all_stats) {
  int res = THREAD_ALREADY_BLOCKED;

  int blocking_number = __sync_fetch_and_add(&s->all_threads_blocking_count, 1);
  if (blocking_number == 0) {
    // Just block. wait until all threads done.
    // If blocking_number > 0, then all the threads are already blocked. So don't wait.
    // Whenever we update the board, trigger the semaphone.
    // Wait until all threads are blocked.
    sem_wait(&s->sem_all_threads_blocked);
    // Block all the receivers as well
    block_all_receivers(s);
    res = THREAD_NEW_BLOCKED;
    PRINT_INFO("Thread newly blocked!\n");
  } else {
    PRINT_INFO("Thread already blocked!\n");
  }

  if (! print_and_reset_all_stats || s->all_stats_cleared) return res;

  // Reset and print all statistics
  int cnn_send_infunc = 0;
  int cnn_send_attempt = 0;
  int cnn_send_success = 0;
  int use_ucb = 0;
  int use_cnn = 0;
  int use_async = 0;
  int max_depth = 0;
  int leaf_expanded = 0;
  int num_expand_failed = 0;
  int num_policy_failed = 0;
  int preempt_playout_count = 0;
  for (int i = 0; i < s->params.num_tree_thread; ++i) {
    ThreadInfo *info = &s->infos[i];
    leaf_expanded += info->leaf_expanded;
    num_expand_failed += info->num_expand_failed;
    num_policy_failed += info->num_policy_failed;
    cnn_send_infunc += info->cnn_send_infunc;
    cnn_send_attempt += info->cnn_send_attempt;
    cnn_send_success += info->cnn_send_success;
    use_ucb += info->use_ucb;
    use_cnn += info->use_cnn;
    use_async += info->use_async;
    preempt_playout_count += info->preempt_playout_count;
    if (max_depth < info->max_depth) max_depth = info->max_depth;
    /*
       PRINT_INFO("Thread [%d]: #expanded = %d, #policy_failed = %d, #expand_failed = %d, infunc = %d, attempt = %d, success = %d, #ucb = %d, #cnn = %d, max_depth = %d\n",
       i, info->leaf_expanded, info->num_policy_failed, info->num_expand_failed,
       info->cnn_send_infunc, info->cnn_send_attempt, info->cnn_send_success,
       info->use_ucb, info->use_cnn, info->max_depth);
       */

    info->leaf_expanded = 0;
    info->num_expand_failed = 0;
    info->num_policy_failed = 0;
    info->cnn_send_infunc = 0;
    info->cnn_send_attempt = 0;
    info->cnn_send_success = 0;
    info->use_ucb = 0;
    info->use_cnn = 0;
    info->use_async = 0;
    info->max_depth = 0;
    info->counter = 0;
    info->preempt_playout_count = 0;
  }

  PRINT_INFO("Stats: leaf_expanded = %d, #policy_failed = %d, #expand_failed = %d, #preempt_playout_count = %d\n",
      leaf_expanded, num_policy_failed, num_expand_failed, preempt_playout_count);
  PRINT_INFO("Stats [Send] infunc = %d, attempt = %d, success = %d\n", cnn_send_infunc, cnn_send_attempt, cnn_send_success);
  PRINT_INFO("Stats [Policy] use_ucb = %d, use_cnn = %d, use_async = %d\n", use_ucb, use_cnn, use_async);
  fprintf(stderr,"p->root->data.stats[0].total: %d, #rollout: %d, #cnn: %d, max_depth: %d\n", s->p.root->data.stats[0].total, s->rollout_count, s->dcnn_count, max_depth);

  // Clear up the model.
  if (s->params.use_online_model) {
    fprintf(stderr,"Online model average error = %f, count = %d\n", s->model_acc_err / s->model_count_err, s->model_count_err);
    memset(s->model_weights, 0, sizeof(s->model_weights));
    s->model_bias = 0;
    s->model_acc_err = 0;
    s->model_count_err = 0;
  }

  memset(s->move_scores_black, 0, sizeof(s->move_scores_black));
  memset(s->move_scores_white, 0, sizeof(s->move_scores_white));

  s->rollout_count = 0;
  s->dcnn_count = 0;
  s->prev_dcnn_count = 0;

  // Check "search complete semaphore", clear it if necessary.
  int sem_value;
  sem_getvalue(&s->sem_search_complete, &sem_value);
  fprintf(stderr,"Semaphore value: %d\n", sem_value);
  if (sem_value > 0) sem_wait(&s->sem_search_complete);
  s->flag_search_complete = SC_NOT_YET;
  s->all_stats_cleared = TRUE;

  // All threads are blocked. Now do your stuff.
  return res;
}

static int resume_all_threads(TreeHandle *s) {
  // If all threads have been resumed, then no need to resume.
  int blocking_number = __atomic_load_n(&s->all_threads_blocking_count, __ATOMIC_ACQUIRE);
  // If all threads are already running, return.
  if (blocking_number == 0) {
    PRINT_INFO("Threads already resumed.\n");
    return THREAD_ALREADY_RESUMED;
  }

  // Otherwise things are blocked.
  blocking_number = __sync_add_and_fetch(&s->all_threads_blocking_count, -1);
  // If all threads are still blocked, then no need to resume.
  if (blocking_number > 0) {
    PRINT_INFO("Threads still blocked.\n");
    return THREAD_STILL_BLOCKED;
  }

  // Resume all receivers.
  resume_all_receivers(s);

  // Reset the timestamp.
  long curr_time = time(NULL);
  s->ts_search_start = curr_time;

  for (int i = 0; i < s->params.num_tree_thread; ++i) {
    if (sem_post(&s->sem_all_threads_unblocked) < 0) {
      error("sem_post return error!!\n");
    }
    __sync_fetch_and_add(&s->threads_count, -1);
  }
  s->all_stats_cleared = FALSE;

  PRINT_INFO("Threads newly resumed, ts_search_start = %ld\n", curr_time);
  return THREAD_NEW_RESUMED;
}

// Call from search threads to notify some condition is met.
static BOOL send_search_complete(TreeHandle *s, int complete_reason) {
  BOOL sent = FALSE;
  pthread_mutex_lock(&s->mutex_search_complete);
  // Time to send the semaphore if it is not sent yet.
  if (s->flag_search_complete == SC_NOT_YET) {
    sem_post(&s->sem_search_complete);
    sent = TRUE;
    s->flag_search_complete = complete_reason;
  }
  pthread_mutex_unlock(&s->mutex_search_complete);
  return sent;
}

// This function should be called when all threads are running.
// It blocks the current threads until the condition is met from search threads.
static void wait_search_complete(TreeHandle *s) {
  // Wait until the condition is met.
  sem_wait(&s->sem_search_complete);
  if (s->params.verbose >= V_INFO) {
    const char *reason = NULL;
    switch (s->flag_search_complete) {
      case SC_NOT_YET:
        reason = "SC_NOT_YET";
        break;
      case SC_TIME_OUT:
        reason = "SC_TIME_OUT";
        break;
      case SC_DCNN_ROLLOUT_REACHED:
        reason = "SC_DCNN_ROLLOUT_REACHED";
        break;
      case SC_TOTAL_ROLLOUT_REACHED:
        reason = "SC_TOTAL_ROLLOUT_REACHED";
        break;
      case SC_NO_NEW_DCNN_EVAL:
        reason = "SC_NO_NEW_DCNN_EVAL";
        break;
      case SC_SINGLE_MOVE_RETURN:
        reason = "SC_SINGLE_MOVE_RETURN";
        break;
      case SC_NO_VALID_MOVE:
        reason = "SC_NO_VALID_MOVE";
        break;
      case SC_TIME_LEFT_CLOSE:
        reason = "SC_TIME_LEFT_CLOSE";
        break;
      case SC_TIME_HEURISTIC_STAGE1:
        reason = "SC_TIME_HEURISTIC_STAGE1";
        break;
      case SC_TIME_HEURISTIC_STAGE2:
        reason = "SC_TIME_HEURISTIC_STAGE2";
        break;
      case SC_TIME_HEURISTIC_STAGE3:
        reason = "SC_TIME_HEURISTIC_STAGE3";
        break;
      case SC_TIME_HEURISTIC_STAGE4:
        reason = "SC_TIME_HEURISTIC_STAGE4";
        break;
      default:
        fprintf(stderr,"Error! unknown flag_search_complete = %d\n", s->flag_search_complete);
        error("");
    }

    fprintf(stderr,"Search Complete. Reason: %s\n", reason);
  }
}

// This function should be called when all threads are blocked.
static void prepare_search_complete(TreeHandle *s) {
  s->flag_search_complete = SC_NOT_YET;
}

static void *threaded_move_receiver(void *ctx) {
  ReceiverParams *rp = (ReceiverParams *)ctx;
  TreeHandle *s = rp->s;

  PRINT_DEBUG("In move receiver, id = %d\n", rp->receiver_id);
  // receive move.
  MMove mmove;
  short indices_map[BOUND_COORD];
  unsigned long seed = rp->receiver_id + 26712;
  // Simple buffer for moves.
  while (1) {
    memset(&mmove, 0, sizeof(mmove));
    if (s->receiver_done) {
      // Clean up all messages in the queue, once done, quit.
      rp->cnn_move_discarded += s->callbacks.callback_receiver_discard_move(s->callbacks.context, rp->receiver_id);
      break;
    }

    // Receive move.
    if (! s->callbacks.callback_receive_move(s->callbacks.context, rp->receiver_id, &mmove)) continue;

    rp->cnn_move_received ++;
    // Invalid sequence.
    if (mmove.seq == 0) continue;

    // This is a pointer that we could use. Therefore, any node that has not be evaluated will never be deleted by tree_simple_free_except.
    // Since MCTS will simply not pick node with bl->n = 0. In conclusion, we don't need a lock here.
    TreeBlock *bl = (TreeBlock *)mmove.b;

    // Statistics.
    // Since parameter might change at any time, we need to read them atomatically.
    const int verbose = __atomic_load_n(&s->params.verbose, __ATOMIC_ACQUIRE);
    if (verbose >= V_DEBUG || bl == TP_NULL) {
      double t_received_board = mmove.t_received - mmove.t_sent;
      double t_replied = mmove.t_replied - mmove.t_received;
      double t_received_move = wallclock() - mmove.t_replied;
      fprintf(stderr,"Received move: b = %lx, hostname = %s, board[send2rcv] = %lf, rcv2reply = %lf, move[send2rcv] = %lf\n", mmove.b, mmove.hostname, t_received_board, t_replied, t_received_move);
      fflush(stdout);
    }

    // fprintf(stderr,"Package received. b = %d, seq = %ld\n", mmove.b, mmove.seq);
    // Check if the move is valid.
    if (bl == NULL) {
      if (verbose >= V_INFO) {
        fprintf(stderr,"Should never receive move instructions from b = NULL!\n");
        fflush(stdout);
      }
      continue;
    }

    // Lock if the main threads want it to stop here.
    if (s->params.use_async)
      pthread_mutex_lock(&rp->lock);

    // If seq does not match, we skip this move.
    if (mmove.seq != s->seq) {
      rp->cnn_move_seq_mismatched ++;
      if (s->params.use_async)
        pthread_mutex_unlock(&rp->lock);
      continue;
    }

    /*
    if (bl->board_hash != mmove.board_hash) {
      rp->cnn_move_board_hash_mismatched ++;
      pthread_mutex_unlock(&rp->lock);
      continue;
    }
    */

    // We receive a move, then we should register it to the tree.
    float accumulated = 0.0;
    memset(indices_map, 0xff, sizeof(short) * BOUND_COORD);

    unsigned char cnn_evaluated = __sync_fetch_and_add(&bl->cnn_data.evaluated, 0);
    if (! TEST_BIT(cnn_evaluated, BIT_CNN_SENT)) {
      error("For a block that receives CNN prediction, its SENT bit must be set. block = %u, status = %d", mmove.b, cnn_evaluated);
    }
    if (TEST_BIT(cnn_evaluated, BIT_CNN_RECEIVED)) {
      // In synced version, each node should be evaluated precisely once.
      error("The block should not receive CNN information twice! block = %u, status = %d", mmove.b, cnn_evaluated);
    }

    rp->cnn_move_valid ++;
    // Get the parameters atomically.
    const float rcv_acc_prob_thres = __atomic_load_n(&s->params.rcv_acc_percent_thres, __ATOMIC_ACQUIRE) / 100.0;
    const int rcv_min_num_move = __atomic_load_n(&s->params.rcv_min_num_move, __ATOMIC_ACQUIRE);
    const int rcv_max_num_move = __atomic_load_n(&s->params.rcv_max_num_move, __ATOMIC_ACQUIRE);

    // First add existing moves if there is any.
    int n = bl->n;
    for (int i = 0; i < n; ++i) {
      indices_map[bl->data.moves[i]] = i;
    }

    int count = 0;
    for (int i = 0; i < NUM_FIRST_MOVES; ++i) {
      if (accumulated >= rcv_acc_prob_thres && i >= rcv_min_num_move) break;
      if (count >= rcv_max_num_move) break;
      // We only pick first BLOCK_SIZE moves.
      if (n >= BLOCK_SIZE) break;
      // note the coordinates in mmove are 1-based due to lua convension.
      Coord m = GetCoord(mmove.xs[i] - 1, mmove.ys[i] - 1);
      if (m != M_PASS) {
        // set the corresponding bit to be one. note that there is no race condition since before the
        // final .evaluated bit is set, other thread will not use it.
        // fprintf(stderr,"%s ", get_move_str(m, mmove.player));

        // bl->data.moves[bl->n] = m;
        // bl->cnn_data.confidences[bl->n] = mmove.probs[i];
        int idx = indices_map[m];
        if (idx < 0) {
          // newly added.
          idx = n;
          bl->data.moves[n] = m;
          indices_map[m] = n ++;
        }

        // Initialize the node.
        bl->cnn_data.confidences[idx] = mmove.probs[i];
        bl->cnn_data.types[idx] = mmove.types[i];
        bl->cnn_data.ps[idx].b = 10;
        bl->cnn_data.ps[idx].w = 10;
        // Without any online prediction, we just assume the prior is 0.5
        bl->data.opp_preds[idx] = 0.5;

        // Random n.
        bl->data.stats[i].total = s->params.num_virtual_games;
        bl->data.stats[i].black_win = fast_random(&seed, s->params.num_virtual_games);

        // A small fix: it seems that if CNN could play ko, it will play it with 0.9x confidence, which does not make sense.
        // So if the move is KO, we will just skip the accumulation so that other moves can also be considered.
        //if (mmove.types[i] != MOVE_SIMPLE_KO) accumulated += mmove.probs[i];
        //*disable the ko check by Yan, 12/24/2015
        accumulated += mmove.probs[i];
        count ++;
      }
    }

    // We don't need the lock here.
    // __atomic_store_n(&bl->cnn_data.seq, mmove.seq, __ATOMIC_RELAXED);
    bl->cnn_data.seq = mmove.seq;
    // bl->player = mmove.player;
    //
    // If we want to use extra information, copy them to the tree block.
    // [FIXME]: Do we need atomic operation?
    if (s->params.use_online_model) {
      bl->extra = (char *)malloc(MAX_CUSTOM_DATA);
      // It will be freed when the tree node is freed.
      memcpy(bl->extra, mmove.extra, MAX_CUSTOM_DATA);
    }

    bl->has_score = mmove.has_score;
    bl->score = mmove.score;
    // finally open the bit.
    //set_bit(bl->cnn_data.evaluated, bit_cnn_received);
    __atomic_store_n(&bl->n, n, __ATOMIC_RELAXED);
    cnn_data_set_evaluated_bit(&bl->cnn_data, BIT_CNN_RECEIVED);

    if (s->params.use_async)
      pthread_mutex_unlock(&rp->lock);

    __sync_fetch_and_add(&s->dcnn_count, 1);
    /*
    if (bl->n == 0) {
      float accumulated = 0.0;
      char notation = '*';
      for (int i = 0; i < NUM_FIRST_MOVES; ++i) {
        if (accumulated >= rcv_acc_prob_thres) notation = ' ';
        fprintf(stderr,"[%d]: x:  %d, y: %d, conf = %f [%c]\n", i, mmove.xs[i], mmove.ys[i], mmove.probs[i], notation);
        accumulated += mmove.probs[i];
      }
      PRINT_CRITICAL("cnn returned for b = %lx, seq = %ld, id = %u, %d moves are ready.\n", (uint64_t)bl, mmove.seq, ID(bl), bl->n);
    }
    */
  }
  return NULL;
}


// ====================== Main function ======================
void tree_search_init_params(TreeParams *params) {
  memset(params, 0, sizeof(TreeParams));

  // Set a few default parameters.
  params->max_depth_default_policy = 100000;
  params->verbose = V_INFO;
  params->num_tree_thread = 16;
  params->num_receiver = 4;
  params->sigma = 0.05;
  params->use_sigma_over_n = FALSE;
  params->use_async = FALSE;
  params->fast_rollout_max_move = 10;

  // No time limit by default.
  params->time_limit = 0;

  params->num_rollout = 1000;
  params->num_dcnn_per_move = 1000;
  params->num_rollout_per_move = 1000;
  params->min_rollout_peekable = 20000;

  params->expand_n_thres = 0;

  params->rcv_acc_percent_thres = 80;
  params->rcv_max_num_move = 5;
  params->rcv_min_num_move = 1;
  params->decision_mixture_ratio = 0.0;

  params->use_pondering = FALSE;
  params->single_move_return = FALSE;
  params->default_policy_choice = DP_PACHI;
  // Only useful for v2.
  params->default_policy_sample_topn = -1;
  params->default_policy_temperature = 1.0;
  params->life_and_death_mode = FALSE;
  params->use_tsumego_dcnn = FALSE;

  params->use_rave = FALSE;

  params->use_online_model = FALSE;
  params->online_model_alpha = 0.0;
  params->online_prior_mixture_ratio = 0.0;
  params->use_cnn_final_score = FALSE;
  params->min_ply_to_use_cnn_final_score = 0;
  params->final_mixture_ratio = 0.0;

  params->num_virtual_games = 0;
  params->percent_playout_in_expansion = 0;
  params->num_playout_per_rollout = 1;
  params->use_old_uct = FALSE;
}

void tree_search_print_params(void *ctx) {
  if (ctx == NULL) {
    fprintf(stderr,"Cannot print search params since ctx is NULL\n");
    return;
  }
  TreeHandle *s = (TreeHandle *)ctx;
  const TreeParams *params = &s->params;

  // Print all search parameters.
  fprintf(stderr,"Verbose: %d\n", params->verbose);
  fprintf(stderr,"#Threads: %d\n", params->num_tree_thread);
  fprintf(stderr,"#Receivers: %d\n", params->num_receiver);
  if (params->num_virtual_games == 0) fprintf(stderr,"Sigma: %.2f, over n: %s\n", params->sigma, STR_BOOL(params->use_sigma_over_n));
  else fprintf(stderr,"#Virtual games: %d\n", params->num_virtual_games);
  fprintf(stderr,"Async mode: %s\n", STR_BOOL(params->use_async));
  fprintf(stderr,"RAVE: %s\n", STR_BOOL(params->use_rave));
  fprintf(stderr,"UCT: %s\n", params->use_old_uct ? "old" : "PUCT");
  fprintf(stderr,"num_rollout: %d\n", params->num_rollout);
  fprintf(stderr,"num_rollout_per_move: %d\n", params->num_rollout_per_move);
  fprintf(stderr,"num_playout_per_rollout: %d\n", params->num_playout_per_rollout);
  fprintf(stderr,"num_rollout_peekable: %d\n", params->min_rollout_peekable);
  fprintf(stderr,"num_dcnn_per_move: %d\n", params->num_dcnn_per_move);
  fprintf(stderr,"rcv_acc_percent_thres: %d\n", params->rcv_acc_percent_thres);
  fprintf(stderr,"rcv_max_num_move: %d\n", params->rcv_max_num_move);
  fprintf(stderr,"rcv_min_num_move: %d\n", params->rcv_min_num_move);
  fprintf(stderr,"expand_n_thres: %d\n", params->expand_n_thres);
  fprintf(stderr,"decision_mixture_ratio: %.1f\n", params->decision_mixture_ratio);
  fprintf(stderr,"Use pondering: %s\n", STR_BOOL(params->use_pondering));
  fprintf(stderr,"Time limit: %ld\n", params->time_limit);
  fprintf(stderr,"%% of threads running playout when expanding node: %d\n", params->percent_playout_in_expansion);
  if (params->use_cnn_final_score) {
    fprintf(stderr,"Minimal ply for cnn final score: %d\n", params->min_ply_to_use_cnn_final_score);
    fprintf(stderr,"Final mixture ratio: %f\n", params->final_mixture_ratio);
    fprintf(stderr,"Final score = final_mixture_ratio * win_rate_prediction + (1.0 - final_mixture_ratio) * playout_result.\n");
  }
  fprintf(stderr,"single_move_return: %s\n", STR_BOOL(params->single_move_return));
  fprintf(stderr,"default_policy: %s [%d, T: %.3lf]\n", def_policy_str(params->default_policy_choice), params->default_policy_sample_topn, params->default_policy_temperature);
  if (params->life_and_death_mode) {
    fprintf(stderr,"Life and death mode. Use tsumego_dcnn: %s, Region: [%d, %d, %d, %d]\n",
        STR_BOOL(params->use_tsumego_dcnn), params->ld_region.left, params->ld_region.top, params->ld_region.right, params->ld_region.bottom);
  }
  if (params->use_online_model) {
    fprintf(stderr,"Online model alpha: %f, mixture ratio: %f\n", params->online_model_alpha, params->online_prior_mixture_ratio);
  }
}

// Set all parameters.
// =========================== Set Callbacks ===========================
static void internal_set_params(TreeHandle *s, const TreeParams *new_params) {
  s->params = *new_params;
  // Set callbacks accordingly.
  if (s->params.life_and_death_mode) {
    s->callback_def_policy = NULL;
    s->callback_policy = ld_policy;
    s->callback_expand = (s->params.use_tsumego_dcnn ? tsumego_dcnn_leaf_expansion : tsumego_rule_leaf_expansion);
    s->callback_compute_score = threaded_compute_score;
    s->callback_backprop = threaded_run_tsumego_bp;
  } else {
    switch(s->params.default_policy_choice) {
      case DP_SIMPLE:
        s->callback_def_policy = RunDefPolicy;
        break;
      case DP_PACHI:
        s->callback_def_policy = play_random_game;
        break;
      case DP_V2:
        s->callback_def_policy = fast_rollout_def_policy;
        break;
    }

    s->callback_policy = s->params.use_async ? async_policy : cnn_policy;
    s->callback_expand = dcnn_leaf_expansion;
    s->callback_compute_score = threaded_compute_score;
    s->callback_backprop = threaded_run_bp;
  }
}

// =================================== Main thread for Monte Carlo Tree Search ===============================
void thread_callback_blocks_init(TreePool *p, TreeBlock *b, void *context, void *context2) {
  // fprintf(stderr,"Before we do anything...\n");
  // fflush(stdout);
  ThreadInfo *info = (ThreadInfo *)context;
  TreeHandle *s = info->s;

  // fprintf(stderr,"Pass the type conversion...info = %lx\n", (uint64_t)info);
  if (info == NULL) error("ThreadInfo cannot be NULL!");
  // fflush(stdout);
  // Set the board hash.
  const Board *board = (const Board *)context2;
  // Set the board hash.
  // b->board_hash = board->hash;
  // Call the expand function.
  s->callback_expand(info, board, b);
}

#define EXPAND_SUCCESS            0
#define EXPAND_FAILED             1
#define EXPAND_OTHER_FIRST        2
#define EXPAND_OTHER_EXPANDING    3

// Expand the leaf
int expand_leaf(ThreadInfo* info, TreeBlock *parent, BlockOffset parent_offset, const Board* board, BOOL wait_until_expansion_finished, TreeBlock **c) {
  // expand the current tree.
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  PRINT_DEBUG("New node. Parent id = %u, parent_offset = %u, expansion = %u, cnn.evaluated = %u\n",
      ID(parent), parent_offset, (unsigned int)parent->expansion, (unsigned int)parent->cnn_data.evaluated);

  int res = wait_until_expansion_finished ? tree_simple_begin_expand(parent, parent_offset, c) : tree_simple_begin_expand_nowait(parent, parent_offset, c);
  switch (res) {
    case EXPAND_STATUS_FIRST:
      // We take the lead and set up the node
      PRINT_DEBUG("We take the lead and set up the node: b2 = %u, b2_offset = %u, expansion = %u, cnn.evaluated = %u\n",
          ID(parent), parent_offset, (unsigned int)parent->expansion, (unsigned int)parent->cnn_data.evaluated);

      // fprintf(stderr,"info = %lx, board = %lx, p = %lx, parent = %lx, parent_offset = %d\n", (uint64_t)info, (uint64_t)board, (uint64_t)p, (uint64_t)parent, parent_offset);
      *c = tree_simple_g_alloc(p, (void *)info, (void *)board, thread_callback_blocks_init, parent, parent_offset);
      if (*c == TP_NULL) {
        fprintf(stderr,"allocation error, output TP_NULL!\n");
        error("");
      }
      info->leaf_expanded ++;
      PRINT_DEBUG("New leaf created!, leaf_expanded = %d\n", info->leaf_expanded);
      return EXPAND_SUCCESS;

    case EXPAND_STATUS_EXPANDING:
      PRINT_DEBUG("Other threads is creating the leaf. b2 = %u, b2_offset = %u, expansion = %u, cnn.evaluated = %u\n",
          ID(parent), parent_offset, (unsigned int)parent->expansion, (unsigned int)parent->cnn_data.evaluated);
      return EXPAND_OTHER_EXPANDING;

    case EXPAND_STATUS_DONE:
      return EXPAND_OTHER_FIRST;
  }
  return EXPAND_FAILED;
}

#define THRES_PLY_DCNN_NOT_EVAL       400
#define MAX_ALLOWABLE_NODCNN_EVAL     5

// Return true if we need to exit the loop.
static inline BOOL threaded_block_if_needed(void *ctx) {
  ThreadInfo *info = (ThreadInfo *)ctx;
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  // If all_threads_blocking is true, block here until the tree is updated.
  int blocking_number = __atomic_load_n(&s->all_threads_blocking_count, __ATOMIC_ACQUIRE);
  if (blocking_number > 0) {
    int count = __sync_add_and_fetch(&s->threads_count, 1);
    if (count == 1) {
      fprintf(stderr,"First thread blocked at %lf\n", wallclock());
    }
    if (count == s->params.num_tree_thread) {
      fprintf(stderr,"Last thread blocked at %lf\n", wallclock());
      sem_post(&s->sem_all_threads_blocked);
    }
    sem_wait(&s->sem_all_threads_unblocked);
  }
  if (s->search_done) return TRUE;
  return FALSE;
}

static inline void normal_time_control(ThreadInfo *info, long time_elapsed) {
  TreeHandle *s = info->s;
  const int first_round_ramp = 20;

  // Check if time limit is reached.
  if (s->params.time_limit > 0) {
    // Time control is checked every 10 rollouts.
    float time_limit = (s->board._ply < first_round_ramp ? s->board._ply * s->params.time_limit / first_round_ramp : s->params.time_limit);
    if (time_elapsed > time_limit) {
      send_search_complete(s, SC_TIME_OUT);
    } else {
      unsigned int time_left = __atomic_load_n(&s->common_params->time_left, __ATOMIC_ACQUIRE);
      if (time_left > 0 && time_elapsed > time_left / 2) {
        send_search_complete(s, SC_TIME_LEFT_CLOSE);
      }
    }
  }
}

static inline void heuristic_time_control(ThreadInfo *info, long time_elapsed) {
  TreeHandle *s = info->s;
  // Whether to use heuristic time manager. If so, then (total time is info->common_params->heuristic_tm_total_time)
  // 1. Gradually add more time per move for the first 30 moves (ply < 60). (1 sec -> 15 sec, 15s * 30 / 2 = 225 s)
  // 2. Keep 15 sec move, 31-100 move ( 70 * 15sec = 1050 s)
  // 3. Decrease the time linearly, 101-130 (30 * 15/2 = 225s)
  // 4. Once we enter the region that time_left < 120s, try 1sec per move.
  // If the total time is x, then the max_time_spent y could be computed as:
  //     y * ply1 / 4 + (ply2 - ply1) / 2 * y + y * (ply3 - ply2) / 4  < alpha * x
  //     y * [ -ply1/4 + ply2/4 + ply3 / 4] < alpha * x
  //     y < 4 * alpha * x / (ply2 + ply3 - ply1)
  // For x = 1800sec, we have y = 4 * alpha * 1800 / (200 + 260 - 60) = 12.5 sec.
  if (s->board._ply < THRES_PLY1) {
    if (time_elapsed >= s->board._ply * s->common_params->max_time_spent / THRES_PLY1) {
      send_search_complete(s, SC_TIME_HEURISTIC_STAGE1);
    }
  } else if (s->board._ply < THRES_PLY2) {
    if (time_elapsed >= s->common_params->max_time_spent) {
      send_search_complete(s, SC_TIME_HEURISTIC_STAGE2);
    }
  } else {
    unsigned int time_left = __atomic_load_n(&s->common_params->time_left, __ATOMIC_ACQUIRE);

    if (time_left > 0 && time_left < THRES_TIME_CLOSE && time_elapsed >= s->common_params->min_time_spent) {
      send_search_complete(s, SC_TIME_LEFT_CLOSE);
    } else if (s->board._ply < THRES_PLY3) {
      float time_limit = (THRES_PLY3 - s->board._ply) * s->common_params->max_time_spent;
      time_limit /= (THRES_PLY3 - THRES_PLY2);
      if (time_elapsed >= time_limit) {
        // fprintf(stderr,"time_limit = %f, time_elapsed = %ld, max_time_spent = %lf, min_time_spent = %lf\n", time_limit, time_elapsed, s->common_params->max_time_spent, s->common_params->min_time_spent);
        send_search_complete(s, SC_TIME_HEURISTIC_STAGE3);
      }
    } else {
      if (time_elapsed >= s->common_params->min_time_spent) send_search_complete(s, SC_TIME_HEURISTIC_STAGE4);
    }
  }
}

static inline void threaded_if_search_complete(void *ctx) {
  ThreadInfo *info = (ThreadInfo *)ctx;
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  if (p->root == NULL) error("Root cannot be NULL!");

  // Check whether the search is completed.
  TreeBlock *first_child = p->root->children[0].child;
  // Time control.
  if (info->counter % 10 == 0) {
    long curr_time = 0;
    long genmove_start = __atomic_load_n(&s->ts_search_genmove_called, __ATOMIC_ACQUIRE);
    if (genmove_start > 0) {
      curr_time = time(NULL);
      long time_elapsed = curr_time - genmove_start;

      if (s->common_params->heuristic_tm_total_time > 0) {
        heuristic_time_control(info, time_elapsed);
      } else {
        normal_time_control(info, time_elapsed);
      }
    }

    // Check if no more dcnn is evaluated (which happens when the game is near to the end).
    if (s->board._ply > THRES_PLY_DCNN_NOT_EVAL) {
      if (! s->common_params->cpu_only) {
        if (curr_time == 0) curr_time = time(NULL);
        // Check if the dcnn_count is not updated for a while, if so, then we also complete the search.
        int dcnn_count = __sync_fetch_and_add(&s->dcnn_count, 0);
        int prev_dcnn_count = __sync_fetch_and_add(&s->prev_dcnn_count, 0);
        long search_start = __atomic_load_n(&s->ts_search_start, __ATOMIC_ACQUIRE);

        if (prev_dcnn_count == dcnn_count && curr_time - search_start > MAX_ALLOWABLE_NODCNN_EVAL) {
          send_search_complete(s, SC_NO_NEW_DCNN_EVAL);
        } else {
          // Update prev_dcnn_count.
          __atomic_store_n(&s->prev_dcnn_count, dcnn_count, __ATOMIC_RELAXED);
        }
      }
    }
  }

  // Check if the condition is met and we send the message that search is complete.
  int num_branch = p->root->data.stats[0].total;
  if (num_branch >= s->params.num_rollout) {
    BOOL rollout_count_passed = __sync_fetch_and_add(&s->rollout_count, 0) >= s->params.num_rollout_per_move;
    BOOL dcnn_count_passed = s->common_params->cpu_only || __sync_fetch_and_add(&s->dcnn_count, 0) >= s->params.num_dcnn_per_move;
    if (rollout_count_passed && dcnn_count_passed) {
      send_search_complete(s, SC_TOTAL_ROLLOUT_REACHED);
    }
  }
  // Condition 2: If single_move_return is true and
  // 1. the root has only one candidate move,
  // 2. We are not in the pondering mode
  // Then search complete immediately.
  if (s->params.single_move_return && ! s->is_pondering) {
    if (first_child != NULL) {
      int nchild = __atomic_load_n(&first_child->n, __ATOMIC_ACQUIRE);
      if (nchild == 1) {
        if (send_search_complete(s, SC_SINGLE_MOVE_RETURN)) fprintf(stderr,"One child in the root, No need to do search.\n");
      }
    }
  }
}

static inline TreeBlock *threaded_expand_root_if_needed(void *ctx) {
  ThreadInfo *info = (ThreadInfo *)ctx;
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  const Board *board_init = &s->board;
  if (p->root == NULL) error("Root cannot be null!");
  TreeBlock *b = p->root->children[0].child;
  if (b == TP_NULL) {
    PRINT_DEBUG("p->root->children[0] is TP_NULL! Need to reallocate.\n");
    TreeBlock *c;
    int res = expand_leaf(info, p->root, 0, board_init, TRUE, &c);
    b = c;

    if (res == EXPAND_SUCCESS) PRINT_DEBUG("Finish creating leaf...\n");
    else if (res == EXPAND_FAILED) error("Failed to expand the leaf...\n");
    else if (res == EXPAND_OTHER_FIRST) PRINT_DEBUG("Other thread has expanded it for us..\n");
    else error("Unknown expand_leaf return value!\n");
  }

  if (s->params.use_async && ! s->common_params->cpu_only) cnn_data_wait_until_evaluated_bit(&b->cnn_data, BIT_CNN_RECEIVED);

  return b;
}

static void *threaded_expansion(void *ctx) {
  ThreadInfo *info = (ThreadInfo *)ctx;
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  // Copy a new board. No pointer in board.
  Board board, board2;
  GroupId4 ids;
  char buf[30];
  PRINT_DEBUG("Start expansion\n");

  for (;;) {
    if (threaded_block_if_needed(ctx)) break;
    threaded_if_search_complete(ctx);
    TreeBlock *b = threaded_expand_root_if_needed(ctx);
    info->counter ++;

    BlockOffset child_offset;

    CopyBoard(&board, &s->board);
    // Random traverse down the tree and expand a node
    BOOL leaf_expanded = FALSE;
    // Whether the board is pointing towards the child node.
    BOOL board_on_child = FALSE;
    // PRINT_DEBUG("---Start playout %d/%d ---\n", i, info->s->num_rollout_per_thread);
    // fprintf(stderr,"---Start playout %d/%d ---\n", i, info->s->num_rollout_per_thread);
    int depth = 0;
    while (1) {
      // Pick a random child.
      if (b == TP_NULL) error("We should never visit TP_NULL.");
      // tree_simple_show_block(p, b);
      TreeBlock *c;

      // In sync mode, dcnn information has to be ready before the node is used.
      // In async mode, dcnn information is not necessarily ready.
      if (! s->params.use_async) {
        unsigned char cnn_evaluated = __sync_fetch_and_add(&b->cnn_data.evaluated, 0);
        if (!TEST_BIT(cnn_evaluated, BIT_CNN_RECEIVED)) {
          error("Wrong! CNN information for id = %d is not received.", ID(b));
          return NULL;
        }
      }
      // Run the CNN policy.
      BOOL policy_success = s->callback_policy(info, b, &board, &child_offset, &c);

      if (! policy_success) {
        info->num_policy_failed ++;
        break;
      }

      // Get the move.
      Coord m = b->data.moves[child_offset];
      // if (m == M_PASS) error("No move should be PASS!! (Except from p->root)");

      if (! TryPlay2(&board, m, &ids)) {
        fprintf(stderr,"============= ErrorMessage =================\n");
        ShowBoard(&board, SHOW_LAST_MOVE);
        fprintf(stderr,"\n");
        fprintf(stderr,"Depth = %d\n", depth);
        tree_simple_show_block(b);
        show_all_cnn_moves(b, board._next_player);
        error("The play %s should never fail!", get_move_str(m, board._next_player, buf));
      }

      // ShowBoard(&board, SHOW_LAST_MOVE);
      // fprintf(stderr,"Current move: %s\n", get_move_str(m, curr_player, buf));
      Play(&board, &ids);

      if (leaf_expanded) {
        board_on_child = TRUE;
        break;
      }

      // Expand it or go downward
      // fprintf(stderr,"[Explore %d]: pick idx = %d/%d, block_parent = %d, child b = %d\n", b, child_idx, total_children, b2, c);
      if (c != TP_NULL) {
        b = c;
      } else {
        if (b->data.stats[child_offset].total < s->params.expand_n_thres) {
          // Insufficient statistics, stop the expansion.
          board_on_child = TRUE;
          break;
        }

        BOOL wait_until_cnn_return = (thread_rand(info, 100) >= s->params.percent_playout_in_expansion ? TRUE : FALSE);
        if (! wait_until_cnn_return) info->preempt_playout_count ++;

        int ret = expand_leaf(info, b, child_offset, &board, wait_until_cnn_return, &c);
        BOOL time_to_break = FALSE;
        switch (ret) {
          case EXPAND_FAILED:
            info->num_expand_failed ++;
            error("Node expansion failed!");
            // break;
          case EXPAND_SUCCESS:
            // Now node.
            b = c;
            leaf_expanded = TRUE;
          case EXPAND_OTHER_EXPANDING:
            // For this, don't waste time waiting. We just break and run the playout.
            time_to_break = TRUE;
            break;
          case EXPAND_OTHER_FIRST:
            // For EXPAND_OTHER_FIRST, we didn't do anything, just let it go.
            b = c;
            break;
        }

        if (time_to_break) break;
      }
      depth ++;
    }

    if (depth > info->max_depth) info->max_depth = depth;

    // Step 2, playout from current board and curr_player.
    PRINT_DEBUG("Default policy...\n");
    int end_ply = board._ply;
    float aver_black_moku = 0.0;
    if (s->callback_def_policy != NULL && ! s->params.life_and_death_mode) {
      for (int i = 0; i < s->params.num_playout_per_rollout; ++i) {
        CopyBoard(&board2, &board);
        s->callback_def_policy(s->def_policy, info, thread_rand, &board2, NULL, s->params.max_depth_default_policy, FALSE);
        aver_black_moku += s->callback_compute_score(info, &board2);
      }
      aver_black_moku /= s->params.num_playout_per_rollout;
    }

    PRINT_DEBUG("Back propagation ...\n");
    s->callback_backprop(info, aver_black_moku, board._next_player, end_ply, board_on_child, child_offset, b);

    // Add the total rollout_count count.
    __sync_fetch_and_add(&s->rollout_count, 1);
    // fprintf(stderr,"---End playout %d/%d [Round %d]---\n", i, K, round);
    //
    // tree_pool_check(p);
  }
  return NULL;
}

// ===================== Public APIS ===============================================
BOOL tree_search_set_params(void *ctx, const TreeParams *new_params) {
  if (ctx == NULL || new_params == NULL) return FALSE;
  TreeHandle *s = (TreeHandle *)ctx;

  fprintf(stderr,"Set_params! And block all threads!\n");
  block_all_threads(s, TRUE);

  fprintf(stderr,"Change params!\n");
  internal_set_params(s, new_params);

  // Reset the seq number
  unsigned long new_seq = time(NULL);
  s->seq = (new_seq > s->seq ? new_seq : s->seq + 1);

  fprintf(stderr,"Set_params! And resume all threads!\n");
  resume_all_threads(s);
  return TRUE;
}

void *tree_search_init(const SearchParamsV2 *common_params, const SearchVariants *common_variants, const ExCallbacks *callbacks, const TreeParams *params, const Board *init_board) {
  TreeHandle *s = (TreeHandle *)malloc(sizeof(TreeHandle));
  if (s == NULL) error("initialize searchhandle failed!");
  if (common_params == NULL) error("Common params are not set!");
  if (common_variants == NULL) error("Common variants are not set!");
  if (callbacks == NULL) error("Callbacks are not set!");

  internal_set_params(s, params);
  s->common_params = common_params;
  s->common_variants = common_variants;
  s->callbacks = *callbacks;

  PRINT_INFO("Initialization: #tree_thread = %d\n", s->params.num_tree_thread);
  PRINT_INFO("Initialize Tree Pool\n");

  if (s->params.num_tree_thread == 0) error("#Tree thread cannot be zero!");
  if (s->params.num_receiver == 0) error("#Num of receivers cannot be zero!");

  tree_simple_pool_init(&s->p);
  s->search_done = FALSE;
  s->receiver_done = FALSE;

  PRINT_INFO("Initialize the sender/receiver. #gpu = %d.\n", s->params.num_receiver);
  // Threads that receives referenced moves from CNN player (another process, communication via message queue).
  // These threads are always there until the search system is destoryed.
  if (! s->common_params->cpu_only) {
    // For global server, we need to set num_receiver to 1.
    s->move_receivers = (pthread_t *)malloc(sizeof(pthread_t) * s->params.num_receiver);
    s->move_params = (ReceiverParams *)malloc(sizeof(ReceiverParams) * s->params.num_receiver);

    PRINT_INFO("Initialize Move Receiver...\n");
    for (int i = 0; i < s->params.num_receiver; ++i) {
      ReceiverParams *rp = &s->move_params[i];

      memset(rp, 0, sizeof(ReceiverParams));
      rp->s = s;
      rp->receiver_id = i;
      pthread_mutex_init(&rp->lock, NULL);
      pthread_create(&s->move_receivers[i], NULL, threaded_move_receiver, rp);
    }
  }

  // Initialize thread-related variables.
  s->explorers = (pthread_t *)malloc(sizeof(pthread_t) * s->params.num_tree_thread);
  s->infos = (ThreadInfo *)malloc(sizeof(ThreadInfo) * s->params.num_tree_thread);

  // Init the fast rollout policy.
  if (s->params.use_async) {
    s->fast_rollout_policy = InitPatternV2(s->params.pattern_filename, NULL, FALSE);
    PatternV2PrintStats(s->fast_rollout_policy);
  }

  // Initialize default policy.
  PRINT_INFO("Initialize default policy...\n");
  switch (s->params.default_policy_choice) {
    case DP_SIMPLE:
      s->def_policy = InitDefPolicy();
      // Change some parameters.
      /*
      DefPolicyParams def_params;
      InitDefPolicyParams(&def_params);
      def_params.switches[OPPONENT_IN_DANGER] = TRUE;
      def_params.switches[OUR_ATARI] = TRUE;
      def_params.switches[NAKADE] = TRUE;
      def_params.switches[PATTERN] = FALSE;
      SetDefPolicyParams(s->def_policy, &def_params);
      */
      break;

    case DP_PACHI:
      s->def_policy = playout_moggy_init(NULL);
      break;
    case DP_V2:
      s->def_policy = InitPatternV2(s->params.pattern_filename, NULL, FALSE);
      assert(s->def_policy);
      PatternV2SetSampleParams(s->def_policy, s->params.default_policy_sample_topn, s->params.default_policy_temperature);
      PatternV2PrintStats(s->def_policy);
      break;
    default:
      fprintf(stderr,"Unknown default policy choice: %d\n", s->params.default_policy_choice);
      error("");
  }

  PRINT_INFO("Initialize tree threads...\n");
  for (int i = 0; i < s->params.num_tree_thread; ++i) {
    // Initialize search info for each thread.
    ThreadInfo *info = &s->infos[i];
    memset(info, 0, sizeof(ThreadInfo));
    info->s = s;
    info->ex_id = i % s->params.num_receiver;
    // info->seed = 26224 + rand();
    info->seed = 26225 + i;
  }

  // Initialize the internal board
  if (init_board == NULL) {
    ClearBoard(&s->board);
  } else {
    CopyBoard(&s->board, init_board);
  }

  // Finally return the handle.
  return s;
}

void tree_search_free(void *ctx) {
  if (ctx == NULL) return;
  TreeHandle *s = (TreeHandle *)ctx;

  // Stop all receiving process...
  if (! s->common_params->cpu_only) {
    s->receiver_done = TRUE;
    int cnn_move_received = 0;
    int cnn_move_valid = 0;
    int cnn_move_discarded = 0;
    int cnn_move_board_hash_mismatched = 0;
    int cnn_move_seq_mismatched = 0;

    PRINT_INFO("Stopping all receivers...\n");

    for (int i = 0; i < s->params.num_receiver; ++i) {
      pthread_join(s->move_receivers[i], NULL);
      ReceiverParams *rp = &s->move_params[i];

      cnn_move_received += rp->cnn_move_received;
      cnn_move_valid += rp->cnn_move_valid;
      cnn_move_discarded += rp->cnn_move_discarded;
      cnn_move_board_hash_mismatched += rp->cnn_move_board_hash_mismatched;
      cnn_move_seq_mismatched += rp->cnn_move_seq_mismatched;

      PRINT_INFO("Stats [Receive][%d]: received = %d, valid = %d, discarded = %d, board_hash_mismatched = %d, seq_mismatched = %d\n", i,
          rp->cnn_move_received, rp->cnn_move_valid, rp->cnn_move_discarded, rp->cnn_move_board_hash_mismatched, rp->cnn_move_seq_mismatched);
      pthread_mutex_destroy(&rp->lock);
    }
    PRINT_INFO("Stats [Receive]: received = %d, valid = %d, discarded = %d, board_hash_mismatched = %d, seq_mismatched = %d\n",
        cnn_move_received, cnn_move_valid, cnn_move_discarded, cnn_move_board_hash_mismatched, cnn_move_seq_mismatched);
    free(s->move_receivers);
    free(s->move_params);
  }

  // Release default policies.
  switch (s->params.default_policy_choice) {
    case DP_SIMPLE:
      DestroyDefPolicy(s->def_policy);
      break;
    case DP_PACHI:
      playout_moggy_destroy(s->def_policy);
      break;
    case DP_V2:
      DestroyPatternV2(s->def_policy);
      break;
  }

  if (s->params.use_async && s->fast_rollout_policy != NULL) DestroyPatternV2(s->fast_rollout_policy);

  // Free threads and related stuff.
  free(s->explorers);
  free(s->infos);

  // Finally free s itself.
  tree_simple_pool_free(&s->p);
  free(s);
}

void tree_search_start(void *ctx) {
  if (ctx == NULL) {
    error("ctx cannot be NULL!");
  }

  TreeHandle *s = (TreeHandle *)ctx;
  s->search_done = FALSE;

  // Initialize the semaphore for signaling.
  s->all_threads_blocking_count = 0;
  s->threads_count = 0;
  s->all_stats_cleared = FALSE;
  sem_init(&s->sem_all_threads_blocked, 0, 0);
  sem_init(&s->sem_all_threads_unblocked, 0, 0);

  // Initialize search complete signal.
  s->flag_search_complete = SC_NOT_YET;
  sem_init(&s->sem_search_complete, 0, 0);
  pthread_mutex_init(&s->mutex_search_complete, NULL);

  // Initialize online model mutex.
  pthread_mutex_init(&s->mutex_online_model, NULL);

  // Set sequence.
  s->seq = time(NULL);
  PRINT_INFO("Current sequence = %ld\n", s->seq);

  // Start all search threads.
  for (int i = 0; i < s->params.num_tree_thread; ++i) {
    ThreadInfo *info = &s->infos[i];

    // Initialize all the counters.
    info->num_policy_failed = 0;
    info->num_expand_failed = 0;
    info->leaf_expanded = 0;
    info->cnn_send_infunc = 0;
    info->cnn_send_attempt = 0;
    info->cnn_send_success = 0;
    info->use_ucb = 0;
    info->use_cnn = 0;
    info->use_async = 0;
    info->preempt_playout_count = 0;
    info->max_depth = 0;

    // fprintf(stderr,"Starting thread = %d, #rollout = %d\n", i, infos[i].num_rollout_per_thread);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1048576);

    pthread_create(&s->explorers[i], &attr, threaded_expansion, info);
  }
}

void tree_search_stop(void *ctx) {
  // Never join since we will let it run forever.
  TreeHandle *s = (TreeHandle *)ctx;

  // Get the final statistics.
  block_all_threads(s, TRUE);
  s->search_done = TRUE;
  // Make sure all thread resumes.
  while (resume_all_threads(s) != THREAD_NEW_RESUMED);

  // Wait until all thread joins.
  PRINT_INFO("Wait for all threads to join...\n");
  for (int i = 0; i < s->params.num_tree_thread; ++i) {
    pthread_join(s->explorers[i], NULL);
  }

  if (! s->common_params->cpu_only) {
    s->callbacks.callback_receiver_restart(s->callbacks.context);
  }

  // Destroy the semaphones.
  PRINT_INFO("Destroy semaphones...\n");
  sem_destroy(&s->sem_search_complete);
  pthread_mutex_destroy(&s->mutex_search_complete);

  sem_destroy(&s->sem_all_threads_blocked);
  sem_destroy(&s->sem_all_threads_unblocked);

  pthread_mutex_destroy(&s->mutex_online_model);

  PRINT_INFO("Search Stopped!\n");
}

void tree_search_thread_off(void *ctx) {
  if (ctx == NULL) return;
  TreeHandle *s = (TreeHandle *)ctx;
  int res = block_all_threads(s, FALSE);
  if (res == THREAD_NEW_BLOCKED) {
    fprintf(stderr,"All threads blocked!\n");
  } else {
    fprintf(stderr,"Threads already blocked!\n");
  }
}

void tree_search_thread_on(void *ctx) {
  if (ctx == NULL) return;
  TreeHandle *s = (TreeHandle *)ctx;
  int res = resume_all_threads(s);
  if (res == THREAD_NEW_RESUMED) {
    fprintf(stderr,"All threads resumed!\n");
  } else if (res == THREAD_ALREADY_RESUMED) {
    fprintf(stderr,"Threads are alredy running!\n");
  } else {
    fprintf(stderr,"Threads are still blocking\n");
  }
}

BOOL tree_search_reset_tree(void *ctx) {
  TreeHandle *s = (TreeHandle *)ctx;

  block_all_threads(s, TRUE);

  s->is_pondering = FALSE;
  // Reset the seq number
  unsigned long new_seq = time(NULL);
  s->seq = (new_seq > s->seq ? new_seq : s->seq + 1);

  // Free the tree.
  tree_simple_free_except(&s->p, TP_NULL);

  resume_all_threads(s);
  return TRUE;
}

BOOL tree_search_set_board(void *ctx, const Board *new_board) {
  TreeHandle *s = (TreeHandle *)ctx;

  block_all_threads(s, TRUE);

  // Initialize the internal board
  if (new_board == NULL) {
    ClearBoard(&s->board);
  } else {
    CopyBoard(&s->board, new_board);
  }

  s->is_pondering = FALSE;
  // Reset the seq number
  unsigned long new_seq = time(NULL);
  s->seq = (new_seq > s->seq ? new_seq : s->seq + 1);

  // Free the tree.
  tree_simple_free_except(&s->p, TP_NULL);

  resume_all_threads(s);
  return TRUE;
}

// ============================ Visualization code ================================
Coord pick_best(const TreeHandle *s, const TreeBlock *b, Stone player, float *highest_score, float *win_rate, TreeBlock **best_cursor) {
  // get the child_idx that is (1) not empty and (2) is most visited.
  Coord best_m = M_PASS;
  *highest_score = -1.0;

  // Return the best move if the tree is empty.
  if (b == TP_NULL || b->n == 0) return best_m;

  for (int i = 0; i < b->n; ++i) {
    Coord m = b->data.moves[i];

    // If this is self-atari, skip this move!
    // UPDATE: we trust the decision made by MCTS/CNN. Sometime a self atari is a good move.
    // if (IsSelfAtari(&s->board, &ids, m, player)) continue;

    int this_n = b->data.stats[i].total + 1;
    float win = b->data.stats[i].black_win;
    unsigned int n_parent = b->parent->data.stats[b->parent_offset].total;
    if (player == S_WHITE) win = this_n - win;

    if (this_n == 0) error("This_n cannot be zero!");
    float winning_rate = ( (float)win + 0.5 ) / this_n;

    // Pick the most visited move.
    // At the beginning of the game, the tree could be very flat, since no move is particularly good in search.
    // So the move to be picked could be very random. In this case, we pick the move with the highest cnn_confidence.
    // float score = this_n + s->params.decision_mixture_ratio * n_parent * b->cnn_data.confidences[i]; // * winning_rate;
    float score = this_n;
    /*
    if (s->params.verbose >= V_DEBUG) {
      fprintf(stderr,"[%s]: n = %d, win = %d, winrate = %f, score = %f, cnn_conf = %f\n",
          get_move_str(m, player), this_n, win, winning_rate, score, bl->cnn_data.confidences[cursor.i]);
    }
    */

    if (score > *highest_score) {
      *highest_score = score;
      *win_rate = winning_rate;
      best_m = m;
      if (best_cursor != NULL) {
        *best_cursor = b->children[i].child;
      }
    }
  }
  return best_m;
}

BOOL pick_best_n(const TreeHandle *s, const TreeBlock *b, Stone player, Moves *moves) {
  // Return if the tree is empty.
  if (b == TP_NULL || b->n == 0) return FALSE;

  // Currently we use bubblesort on topk O(kn), but we could do better.
  for (int i = 0; i < b->n; ++i) {
    Coord m = b->data.moves[i];

    int this_n = b->data.stats[i].total + 1;
    float win = b->data.stats[i].black_win;
    unsigned int n_parent = b->parent->data.stats[b->parent_offset].total;
    if (player == S_WHITE) win = this_n - win;

    if (this_n == 0) error("This_n cannot be zero!");

    moves->moves[i].m = m;
    moves->moves[i].x = X(m);
    moves->moves[i].y = Y(m);
    moves->moves[i].player = player;
    moves->moves[i].win_games = win;
    moves->moves[i].total_games = this_n;
    moves->moves[i].win_rate = ( (float)win + 0.5 ) / this_n;
  }

  // Bubble sort.
  for (int i = 0; i < moves->num_moves; ++i) {
    for (int j = i + 1; j < b->n; ++j) {
      if (moves->moves[i].total_games < moves->moves[j].total_games) {
        Move tmp = moves->moves[j];
        moves->moves[j] = moves->moves[i];
        moves->moves[i] = tmp;
      }
    }
  }
  return TRUE;
}

static void show_picked_move_cnn_impl(const TreeHandle *s, TreeBlock *b, Stone player, int space) {
  if (b == TP_NULL) return;

  char buf[30], buf2[100], buf3[100];
  const int span = 3;
  char *space_str = (char *)malloc(span * space + 1);
  for (int i = 0; i < space; ++i)  {
    space_str[i * span] = '|';
    for (int j = 1; j < span; ++j) {
      space_str[i * span + j] = ' ';
    }
  }
  space_str[space * span] = 0;

  // Traverse twice, (1) pick the best one (2) visualize it.
  float highest_score, win_rate;
  Coord chosen = pick_best(s, b, player, &highest_score, &win_rate, NULL);

  for (int i = 0; i < b->n; ++i) {
    Coord m = b->data.moves[i];
    float cnn_conf = b->cnn_data.confidences[i];
    float fast_conf = b->cnn_data.fast_confidences[i];
    // Make it redicted win rate from our perspective.
    float online_pred = 1.0 - b->data.opp_preds[i];
    int this_n = b->data.stats[i].total;

    float win = b->data.stats[i].black_win;
    if (player == S_WHITE) win = this_n - win;

    float winning_rate = win / (this_n + 1e-8);

    Stone terminal_status = S_EMPTY;
    const char *status_str = "leaf";

    if (b->children[i].child != NULL) {
      TreeBlock *child = b->children[i].child;
      terminal_status = child->terminal_status;
      status_str = tree_simple_get_status_str(child->cnn_data.evaluated);
    }

    char picked = chosen == m ? '*' : ' ';
    char the_type = b->cnn_data.types[i];
    const char *type_str = NULL;
    switch(the_type) {
      case MOVE_SIMPLE_KO:
        type_str = "KO"; break;
      case MOVE_TACTICAL:
        type_str = "TA"; break;
      case MOVE_NORMAL:
        type_str = "NM"; break;
      case MOVE_LD:
        type_str = "LD"; break;
    }
    // Decide whether we need to print out child info.
    const TreeBlock *ch = b->children[i].child;
    if (ch == NULL) buf2[0] = 0;
    else sprintf(buf2, "   ,b = %lx, seq = %ld", (uint64_t)ch, ch->cnn_data.seq);

    if (! b->has_score) buf3[0] = 0;
    else sprintf(buf3, "pred_black_score = %f", b->score);

    if (s->params.life_and_death_mode) {
     // if (this_n > 0) {
        fprintf(stderr,"%s[%s]%c: b: %d, w: %d, n: %d, pred: %.3f, terminal: %s %s\n", space_str, get_move_str(m, player, buf), picked, b->cnn_data.ps[i].b, b->cnn_data.ps[i].w, this_n, online_pred, STR_STONE(terminal_status), buf2);
     // }
    } else {
      fprintf(stderr,"%s[%s]%c: %.3f (%.2f/%d), %s, %s, cnn = %.3f, fast_cnn = %.3f, pred = %.3f, terminal = %s %s%s\n", space_str, get_move_str(m, player, buf), picked, winning_rate, win, this_n, type_str, status_str, cnn_conf, fast_conf, online_pred, STR_STONE(terminal_status), buf3, buf2);
    }

    if (chosen == m) {
      show_picked_move_cnn_impl(s, b->children[i].child, OPPONENT(player), space + 1);
    }
  }

  free(space_str);
}

void tree_search_print_tree(void *ctx) {
  TreeHandle *s = (TreeHandle *)ctx;
  TreePool *p = &s->p;

  block_all_threads(s, FALSE);

  if (p->root == NULL || p->root->children[0].child == NULL) {
    resume_all_threads(s);
    return;
  }

  TreeBlock *b = p->root->children[0].child;

  // print the address b and seq number.
  fprintf(stderr,"b = %lx, seq = %ld\n", (uint64_t)b, s->seq);
  show_picked_move_cnn_impl(s, b, s->board._next_player, 0);
  fprintf(stderr,"Ply: %d, ld_mode: %s, def_policy: %s [%d, T: %.3lf], Async: %s, CPU_ONLY: %s, online: %s, cnn_final_score: %s, min_ply_use_final: %d, final_mixture_ratio: %.1f, num_playout_per_rollout: %d\n",
      s->board._ply, STR_BOOL(s->params.life_and_death_mode), def_policy_str(s->params.default_policy_choice), s->params.default_policy_sample_topn,
      s->params.default_policy_temperature, STR_BOOL(s->params.use_async), STR_BOOL(s->common_params->cpu_only), STR_BOOL(s->params.use_online_model),
      STR_BOOL(s->params.use_cnn_final_score), s->params.min_ply_to_use_cnn_final_score, s->params.final_mixture_ratio, s->params.num_playout_per_rollout);

  resume_all_threads(s);
}

static BOOL get_ld_best_seq(TreeHandle *s, TreeBlock *b, float *best_score, TreeBlock **best_child, AllMoves* all_moves) {
  // Get the best sequence for life and death problem.
  // Return TRUE if the ld problem has been solved.
  if (s == NULL || all_moves == NULL) return FALSE;

  // [FIXME]: Is that safe?
  all_moves->board = &s->board;
  all_moves->num_moves = 0;

  char buf[30];

  BOOL solved = FALSE;

  // Trace the tree.
  TreeBlock *curr = b;
  Stone player = s->board._next_player;

  while (curr != NULL) {
    // Find the best child.
    TreeBlock *next = NULL;
    int min_val = MAX_PROVE_NUM;
    Coord m = M_PASS;

    if (player == S_BLACK) {
      for (int i = 0; i < curr->n; ++i) {
        if (curr->cnn_data.ps[i].b < min_val) {
          min_val = curr->cnn_data.ps[i].b;
          next = curr->children[i].child;
          m = curr->data.moves[i];
        }
      }
    } else {
      for (int i = 0; i < curr->n; ++i) {
        if (curr->cnn_data.ps[i].w < min_val) {
          min_val = curr->cnn_data.ps[i].w;
          next = curr->children[i].child;
          m = curr->data.moves[i];
        }
      }
    }

    // Then go to the next tree block.
    if (all_moves->num_moves == 0) {
      if (min_val == 0) solved = TRUE;
      *best_child = next;
    }
    all_moves->moves[all_moves->num_moves++] = m;
    curr = next;
  }

  return solved;
}

void tree_search_to_json(void *ctx, const Move *prev_moves, int num_prev_moves, const char *output_filename) {
  if (ctx == NULL) error("ctx cannot be NULL!");
  if (output_filename == NULL) error("output_filename cannot be NULL!");

  // Save the current tree.
  TreeHandle *s = (TreeHandle *)ctx;
  TreePool *p = &s->p;

  block_all_threads(s, FALSE);

  FILE *fp = fopen(output_filename, "w");
  if (fp == NULL) error("Cannot open file = %s!\n", output_filename);
  // Write down all previous moves.
  fprintf(fp, "{\n");

  if (prev_moves != NULL) {
    fprintf(fp, "\"prev_moves\": [\n");
    // Put all the previous moves.
    for (int i = 0; i < num_prev_moves; ++i) {
      fprintf(fp, "  {\"x\" : %d, \"y\" : %d, \"player\" : %d }", prev_moves[i].x, prev_moves[i].y, prev_moves[i].player);
      if (i < num_prev_moves - 1) fprintf(fp, ",");
      fprintf(fp, "\n");
    }
    fprintf(fp, "],\n");
  }

  // Dump the tree.
  fprintf(fp, "\"tree\":\n");
  tree_simple_print_out_cnn(fp, p);
  fprintf(fp, "}\n");
  fclose(fp);

  resume_all_threads(s);
}

static void tree_dump_feature_impl(TreeHandle *s, const Board *board, TreeBlock *bl, FILE *fp) {
  if (bl == NULL) return;

  Stone player = board->_next_player;
  int count = 0;
  GroupId4 ids;

  for (int i = 0; i < bl->n; ++i) {
    // Check the zero moves.
    // Balance the positive and negative samples.
    int n = (player == S_WHITE ? bl->cnn_data.ps[i].w : bl->cnn_data.ps[i].b);
    if (n == 0) {
      SaveMoveWithFeature(board, s->params.defender, bl->data.moves[i], 1, fp);
      /*
      fprintf(stderr,"Positive moves: b = %lx, seq = %ld\n", (uint64_t)bl->children[i].child, bl->children[i].child == NULL ? 0 : bl->children[i].child->cnn_data.seq);
      Board b2;
      CopyBoard(&b2, board);
      TryPlay2(&b2, bl->data.moves[i], &ids);
      Play(&b2, &ids);
      ShowBoard(&b2, SHOW_LAST_MOVE);
      */
      // SaveMoveWithFeature(board, s->params.defender, bl->data.moves[i], 1, stdout);
      count ++;
    }
  }

  // Then generate the same amount of negative samples from positive ones.
  for (int i = 0; i < bl->n; ++i) {
    // Balance the positive and negative samples.
    if (count <= 0) break;
    int n = (player == S_WHITE ? bl->cnn_data.ps[i].w : bl->cnn_data.ps[i].b);
    if (n != 0) {
      SaveMoveWithFeature(board, s->params.defender, bl->data.moves[i], 0, fp);
      count --;
    }
  }

  for (int i = 0; i < bl->n; ++i) {
    // Then we need to go down and iterate.
    if (bl->children[i].child == NULL) continue;
    Board b2;
    CopyBoard(&b2, board);
    TryPlay2(&b2, bl->data.moves[i], &ids);
    Play(&b2, &ids);
    tree_dump_feature_impl(s, &b2, bl->children[i].child, fp);
  }
}

void tree_search_to_feature(void *ctx, const char *output_filename) {
  // Save the good node of the tree to the text file.
  if (ctx == NULL) error("ctx cannot be NULL!");
  if (output_filename == NULL) error("output_filename cannot be NULL!");

  TreeHandle *s = (TreeHandle *)ctx;
  TreePool *p = &s->p;

  // Save the current tree.
  block_all_threads(s, FALSE);

  if (p->root == NULL) error("Root cannot be NULL!");
  if (! s->params.life_and_death_mode || s->params.defender == S_EMPTY) return;

  FILE *fp = fopen(output_filename, "w");
  if (fp == NULL) error("Cannot open file = %s!\n", output_filename);

  SaveMoveFeatureName(fp);
  // Write down the features for the node with 0 prove numbers (They are obviously good moves).
  tree_dump_feature_impl(s, &s->board, p->root->children[0].child, fp);
  fclose(fp);

  resume_all_threads(s);
}

// Actual pick the move and play it in the internal board.
static void prune_actual_pickmove(TreeHandle *s, Coord m, TreeBlock *child_left) {
  TreePool *p = &s->p;
  GroupId4 ids;
  char buf[30];

  // Free all except for the branch starting with the move.
  // if b == 0 and i == 0, then we essentially free everything.
  tree_simple_free_except(p, child_left);

  PRINT_INFO("Remove move %s\n", get_move_str(m, s->board._next_player, buf));
  // update the internal board in p.
  if (! TryPlay2(&s->board, m, &ids)) {
    ShowBoard(&s->board, SHOW_LAST_MOVE);
    error("Cannot play the internal board! move = %s", get_move_str(m, s->board._next_player, buf));
  }

  Play(&s->board, &ids);
}

void tree_search_prune_opponent(void *ctx, Coord m) {
  if (ctx == NULL) error("ctx cannot be NULL!");

  TreeHandle *s = (TreeHandle *)ctx;
  TreePool *p = &s->p;
  char buf[30];

  block_all_threads(s, TRUE);

  // If the child to be expand is TP_NULL, we first need to expand it and then pick the move we want.
  // This happens if the opponent picks the unexpected move which we didn't expand.
  PRINT_DEBUG("play_multithread:prune_tree starts\n");
  TreeBlock * b = p->root->children[0].child;
  PRINT_DEBUG("b = %u\n", ID(b));

  BOOL move_picked = FALSE;
  // Pick the child that has the move.
  if (b != NULL) {
    PRINT_INFO("Pick the child that has the move...\n");
    for (int i = 0; i < b->n; ++i) {
      PRINT_DEBUG("Check move: b = %u, i = %d, m = %s ", ID(b), i, get_move_str(b->data.moves[i], s->board._next_player, buf));
      PRINT_DEBUG(" [target = %s]\n", get_move_str(m, s->board._next_player, buf));
      if (b->data.moves[i] == m) {
        prune_actual_pickmove(s, m, b->children[i].child);
        move_picked = TRUE;
      }
    }
  }

  // Otherwise we prune the entire tree.
  // This could be because the opponent picks an unusual move, or pick M_PASS/M_RESIGN
  // which are not in the legal move list
  if (! move_picked) {
    PRINT_INFO("The chosen move [%s] is not in the list, prune the entire tree..\n", get_move_str(m, s->board._next_player, buf));
    prune_actual_pickmove(s, m, TP_NULL);
  }

  if (s->params.verbose >= V_DEBUG) {
    fprintf(stderr,"Check the pool...\n");
    tree_simple_pool_check(p);
  }

  // After opponent prune, it is not considered as pondering.
  s->is_pondering = FALSE;

  // Update the sequence number.
  unsigned long new_seq = time(NULL);
  s->seq = (new_seq > s->seq ? new_seq : s->seq + 1);

  resume_all_threads(s);
  return;
}

BOOL tree_search_undo_pass(void *ctx, const Board *before_board) {
  if (ctx == NULL) {
    error("ctx cannot be NULL!");
  }

  TreeHandle *s = (TreeHandle *)ctx;
  block_all_threads(s, TRUE);

  // We don't need to do anything for the tree, since the tree is already cleaned up by the Previous PASS.
  // If the previous move is not pass, then UndoPass returns FALSE and nothing happens.
  // So this is not really an undo, since the previous tree is already deleted.
  // fprintf(stderr,"Perform undo pass... next_player = %d\n", s->board._next_player);
  // fprintf(stderr,"last4 = %d, last3 = %d, last2 = %d, last = %d\n", s->board._last_move4, s->board._last_move3, s->board._last_move2, s->board._last_move);
  BOOL res = UndoPass(&s->board);
  if (res && before_board != NULL) {
    // Recover _last_move4
    s->board._last_move4 = before_board->_last_move4;
  }

  // We also need to clear the tree.
  tree_simple_free_except(&s->p, TP_NULL);

  resume_all_threads(s);
  // fprintf(stderr,"After undo pass... next_player = %d\n", s->board._next_player);
  // fprintf(stderr,"last4 = %d, last3 = %d, last2 = %d, last = %d\n", s->board._last_move4, s->board._last_move3, s->board._last_move2, s->board._last_move);
  return res;
}

BOOL tree_search_peek(void *ctx, Moves *moves, const Board *verify_board) {
  // Peek the current search thread and return top k moves.
  // topk is stored in all_moves->num_moves;
  if (ctx == NULL) error("ctx cannot be NULL!");
  if (moves == NULL) error("move_seq cannot be zero!");

  TreeHandle *s = (TreeHandle *)ctx;
  TreePool *p = &s->p;
  Stone player = s->board._next_player;

  // This approach only works in pondering mode.
  if (! s->params.use_pondering) {
    printf("Warning: tree_search_peek only works in pondering mode.\n");
    return FALSE;
  }

  if (s->params.life_and_death_mode) {
    printf("Warning: tree_search_peek has not been implemented in life_and_death mode.\n");
    return FALSE;
  }

  // If we just start simulation, we should just wait..
  int total_simulation;
  do {
    total_simulation = __atomic_load_n(&p->root->data.stats[0].total, __ATOMIC_ACQUIRE);
  } while (total_simulation < s->params.min_rollout_peekable);

  // Then we block all threads and read the results.
  block_all_threads(s, TRUE);

  if (p->root == NULL) error("Root should not be null!\n");

  // Check if the board is right.
  if (verify_board != NULL) {
    // If the two boards are not the same, error!
    if (!CompareBoard(&s->board, verify_board)) {
      printf("Internal Board:\n");
      ShowBoard(&s->board, SHOW_ALL);
      printf("External Board:\n");
      ShowBoard(verify_board, SHOW_ALL);
      error("The two boards are not the same!\n");
    }
  }

  TreeBlock *b = p->root->children[0].child;
  pick_best_n(s, b, player, moves);

  // Resume the search.
  resume_all_threads(s);

  return TRUE;
}

Move tree_search_pick_best(void *ctx, AllMoves *all_moves, const Board *verify_board) {
  if (ctx == NULL) error("ctx cannot be NULL!");
  if (all_moves == NULL) error("move_seq cannot be zero!");

  TreeHandle *s = (TreeHandle *)ctx;
  TreePool *p = &s->p;
  Stone player = s->board._next_player;
  char buf[30];

  // Set the search_time starting point. The time control will be based on this number.
  // Use atomic store since pondering may be opened.
  long curr_time = time(NULL);
  __atomic_store_n(&s->ts_search_genmove_called, curr_time, __ATOMIC_RELAXED);
  PRINT_INFO("ts_genmove_called: %ld\n", curr_time);

  // If no pondering, we only resume the threads here.
  // Note that the first time resume_all_threads(s) will do nothing, but that does not matter.
  if (! s->params.use_pondering) {
    PRINT_INFO("Start search within tree_search_pick_best...\n");
    resume_all_threads(s);
  }

  // Wait until the condition is met.
  wait_search_complete(s);

  // Then we block all threads and read the results.
  block_all_threads(s, TRUE);

  if (p->root == NULL) error("Root should not be null!\n");

  // Check if the board is right.
  if (verify_board != NULL) {
    // If the two boards are not the same, error!
    if (!CompareBoard(&s->board, verify_board)) {
      fprintf(stderr,"Internal Board:\n");
      ShowBoard(&s->board, SHOW_ALL);
      fprintf(stderr,"External Board:\n");
      ShowBoard(verify_board, SHOW_ALL);
      error("The two boards are not the same!\n");
    }
  }

  // once the search is done, pick the most frequent one.
  // The root should never be NULL.
  TreeBlock *b = p->root->children[0].child;

  Coord best_m;
  TreeBlock *best_child = TP_NULL;
  all_moves->num_moves = 0;
  float best_score = 0.0;
  float win_rate = 0.5;

  if (s->params.life_and_death_mode) {
    BOOL solved = get_ld_best_seq(s, b, &best_score, &best_child, all_moves);
    if (all_moves->num_moves == 0) error("Error in solving L&D problem! return zero-length move sequence!\n");
    if (solved) {
      PRINT_INFO("The L&D problem has been solved!\n");
      win_rate = 1.0;
    }
    best_m = all_moves->moves[0];
  } else {
    best_m = pick_best(s, b, player, &best_score, &win_rate, &best_child);
  }

  // Prepare for next search.
  prepare_search_complete(s);

  Move res = { .x = X(best_m), .y = Y(best_m), .m = best_m, .player = player, .win_rate = win_rate };
  return res;
}

void tree_search_prune_ours(void *ctx, Coord m) {
  // tree_search_prune will always set is_pondering to be FALSE.
  // It will be set to TRUE afterwards if pondering is allowed.
  // If use_pondering is FALSE, then is_pondering is always FALSE.
  if (ctx == NULL) error("ctx cannot be NULL!");
  TreeHandle *s = (TreeHandle *)ctx;

  tree_search_prune_opponent(s, m);

  // If use pondering, then we start the search right now.
  // Note that ts_search_genmove_called will be 0 until pick_best is called (formally the time is ticking).
  if (s->params.use_pondering) {
    PRINT_INFO("Ponder on. Start search now...\n");
    s->is_pondering = TRUE;
    s->ts_search_genmove_called = 0;
    resume_all_threads(s);
  }
}
