//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#include <math.h>
#include "playout_callbacks.h"
#include "playout_common.h"
#include "../tsumego/rank_move.h"
#include "../common/common.h"
#include "../board/pattern_v2.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

static float sigmoid(float x) {
  return 1.0 / (1.0 + exp(-x));
}

// ========================= All callback functions =======================================
//
static inline float add_uct_prior(ThreadInfo *info, float confidence, int n, int n_parent) {
  // For win rate, we need to put some DCNN prior. The prior will diminish when n is large, but remain strong when n is small.
  // float online_prior = 1.0 - bl->data.opp_preds[i];
  /*
     float winning_rate_with_prior = (s->params.decision_mixture_ratio * confidence + s->params.online_prior_mixture_ratio * online_prior) / n + winning_rate;
  // Put some noise to make things diverse a bit
  float this_score = winning_rate_with_prior + C * sqrt(log_n_parent/ n) + 2 * thread_randf(info) * info->s->params.sigma;
  */

  TreeHandle *s = info->s;
  float factor = s->params.use_old_uct ? 1.0 / n : sqrt(n_parent) / n;

  // winning_rate_with_prior += (s->params.decision_mixture_ratio * bl->cnn_data.confidences[i] + s->params.online_prior_mixture_ratio * online_prior) / n;
  float prior = s->params.decision_mixture_ratio * confidence * factor;

  // Put some noise to make things diverse a bit
  if (s->params.num_virtual_games == 0) {
    float noise = 2 * thread_randf(info) * s->params.sigma;
    if (s->params.use_sigma_over_n) prior += noise * factor;
    else prior += noise;
  }
  return prior;
}

// =================================== Policy
BOOL cnn_policy(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen) {
  TreeHandle *s = info->s;

  if (bl->terminal_status != S_EMPTY) return FALSE;
  char buf[30];

  const float C = 1.4142;
  // Find the node with highest noisy winrate score
  float best_score = -1.0;

  Stone player = board->_next_player;

  for (int i = 0; i < bl->n; ++i) {
    float black_win = ((float)bl->data.stats[i].black_win) + 0.5;
    unsigned int n = bl->data.stats[i].total + 1;
    unsigned int n_parent = bl->parent->data.stats[bl->parent_offset].total + 1;

    float log_n_parent = log2((float)n_parent);
    float winning_rate = ((float)black_win) / n;
    if (player == S_WHITE) winning_rate = 1 - winning_rate;

    // If there is rave open, add rave heuristic.
    if (s->params.use_rave) {
      float rave_winning_rate = ((float)bl->data.rave_stats[i].black_win + 0.5) / (bl->data.rave_stats[i].total + 1);
      if (player == S_WHITE) rave_winning_rate = 1 - rave_winning_rate;

      const float rave_k = 100;
      const float beta = sqrt(rave_k / (n_parent + rave_k));
      const float combined_winning_rate = winning_rate * (1 - beta) + rave_winning_rate * beta;
      PRINT_DEBUG("[%d]: %s, win_rate = %f, rave_win_rate = %f, beta = %f, combined_win_rate = %f, n_parent = %d\n",
        i, get_move_str(bl->data.moves[i], player, buf), winning_rate, rave_winning_rate, beta, combined_winning_rate, n_parent);

      winning_rate = combined_winning_rate;
    }

    float this_score = winning_rate + add_uct_prior(info, bl->cnn_data.confidences[i], n, n_parent);

    // For win rate, we need to put some DCNN prior. The prior will diminish when n is large, but remain strong when n is small.
    // float winning_rate_with_prior = (s->params.decision_mixture_ratio * bl->cnn_data.confidences[i] + s->params.online_prior_mixture_ratio * online_prior) / n + winning_rate;
    PRINT_DEBUG("[%d]: %s, score = %f, n = %d, n_parent = %d, winning_rate = %f, cnn = %f, score = %f\n",
        i, get_move_str(bl->data.moves[i], player, buf), this_score, n, n_parent, winning_rate, bl->cnn_data.confidences[i], this_score);
    //
    if (this_score > best_score) {
      best_score = this_score;
      *offset = i;
      *child_chosen = bl->children[i].child;
    }
  }
  PRINT_DEBUG("Best score = %f, best index = %d, best move = %s\n", best_score, *offset, get_move_str(bl->data.moves[*offset], player, buf));
  if (best_score < 0) {
    // No node is selected.
    return FALSE;
  }
  info->use_cnn ++;
  return TRUE;
}

// Policy for life and death mode.
BOOL ld_policy(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen) {
  // We could use prove/disprove numberf for the search.
  TreeHandle *s = info->s;
  char buf[30];

  // If it is an end state, return FALSE;
  if (bl->n == 0 || bl->terminal_status != S_EMPTY) return FALSE;

  // Find the node with highest noisy cnn score (true cnn score + some noise so that it can try something new)
  float best_score = MAX_PROVE_NUM;

  Stone player = board->_next_player;
  // float best_score = -1.0;
  *offset = BLOCK_SIZE;

  for (int i = 0; i < bl->n; ++i) {
    // Pick the one with lowest black/white number.
    const ProveNumber *pn = &bl->cnn_data.ps[i];
    int curr_pn = (player == S_BLACK ? pn->b : pn->w);
    PRINT_DEBUG("[%d]: %s, b_pn = %d, w_pn = %d\n", i, get_move_str(bl->data.moves[i], player, buf), pn->b, pn->w);
    // Curr_pn is zero means the node is solved and we don't need to expand it.
    if (curr_pn == 0) continue;
    // float this_score = winning_rate + bl->cnn_data.confidences[i] + thread_randf(info) * sigma;
    // float this_score = winning_rate + thread_randf(info);
    // We might need to put some randomness here (so that the tree expansion is faster).
    // float this_score = 1.0 - bl->data.opp_preds[i] + thread_randf(info) * info->s->params.sigma;
    if (thread_randf(info) < 0.2) continue;

    // Coord m = bl->data.moves[i];
    // int prior = (player == S_BLACK ? __atomic_load_n(&s->move_scores_black[m], __ATOMIC_ACQUIRE) : __atomic_load_n(&s->move_scores_white[m], __ATOMIC_ACQUIRE));
    // printf("[%d]: %s, b_pn = %d, w_pn = %d, prior = %d\n", i, get_move_str(bl->data.moves[i], player, buf), pn->b, pn->w, prior);
    int prior = 0;

    // The smaller the better.
    float this_score = curr_pn - prior;
    if (this_score < best_score) {
    // if (this_score < best_score) {
      best_score = this_score;
      *offset = i;
      *child_chosen = bl->children[i].child;
    }
  }
  // PRINT_DEBUG("Best pn = %f, best index = %d, best move = %s\n", lowest_pn, *offset, get_move_str(bl->data.moves[*offset], player, buf));
  PRINT_DEBUG("Best score = %f, best index = %d, best move = %s\n", best_score, *offset, get_move_str(bl->data.moves[*offset], player, buf));
  /*
  if (lowest_pn >= INIT_PROVE_NUM) {
    printf("Warning, picking very large prove number! Best pn = %d, best index = %d, best move = %s\n", lowest_pn, *offset, get_move_str(bl->data.moves[*offset], player, buf));
  }
  */

  // Nothing to choose, so return false.
  return *offset < BLOCK_SIZE ? TRUE : FALSE;
}

static BOOL send_to_cnn(ThreadInfo *info, TreeBlock *b, const Board *board);

// Hybrid policy in async mode.
// If BIT_CNN_RECEIVED is set, then we use cnn confidence.
// If BIT_CNN_RECEIVED is not set, then we use fast rollout confidence. In this case, if BIT_CNN_SENT is not set, then after policy selection, we will resend the situation to DCNN server.
BOOL async_policy(ThreadInfo *info, TreeBlock *bl, const Board *board, BlockOffset *offset, TreeBlock **child_chosen) {
  TreeHandle *s = info->s;

  if (bl->terminal_status != S_EMPTY) return FALSE;
  BOOL use_cnn_policy = cnn_data_get_evaluated_bit(&bl->cnn_data, BIT_CNN_RECEIVED);
  char buf[30];

  const float C = 1.4142;
  // Find the node with highest noisy winrate score
  float best_score = -1.0;
  Stone player = board->_next_player;

  PRINT_DEBUG("Async_policy. b = %lx, use_cnn_policy = %s, n = %d\n", (uint64_t)bl, STR_BOOL(use_cnn_policy), bl->n);
  for (int i = 0; i < bl->n; ++i) {
    float black_win = ((float)bl->data.stats[i].black_win) + 0.5;
    unsigned int n = bl->data.stats[i].total + 1;
    unsigned int n_parent = bl->parent->data.stats[bl->parent_offset].total + 1;

    float log_n_parent = log2((float)n_parent);
    float winning_rate = ((float)black_win) / n;
    if (player == S_WHITE) winning_rate = 1 - winning_rate;

    float confidence = use_cnn_policy ? bl->cnn_data.confidences[i] : bl->cnn_data.fast_confidences[i];
    float this_score = winning_rate + add_uct_prior(info, confidence, n, n_parent);

    PRINT_DEBUG("[%d]: %s, score = %f, n = %d, n_parent = %d, winning_rate = %f, conf = %f, winning_rate+prior = %f\n",
        i, get_move_str(bl->data.moves[i], player, buf), this_score, n, n_parent, winning_rate, confidence, this_score);
    //
    // float this_score = winning_rate + bl->cnn_data.confidences[i] + thread_randf(info) * sigma;
    // float this_score = winning_rate + thread_randf(info);
    if (this_score > best_score) {
      best_score = this_score;
      *offset = i;
      *child_chosen = bl->children[i].child;
    }
  }
  PRINT_DEBUG("Best score = %f, best index = %d, best move = %s\n", best_score, *offset, get_move_str(bl->data.moves[*offset], player, buf));

  if (! s->common_params->cpu_only) {
    if (use_cnn_policy) {
      info->use_cnn ++;
    } else {
      send_to_cnn(info, bl, board);
    }
  }
  info->use_async ++;

  if (best_score < 0) {
    // No node is selected.
    return FALSE;
  }

  return TRUE;
}

// Leaf expansion.
// ============================= Expansion Related ==================================
static BOOL send_to_cnn(ThreadInfo *info, TreeBlock *b, const Board *board) {
  // Send the current player to CNN for evaluation.
  // Check if someone else has sent it.
  TreeHandle *s = info->s;
  info->cnn_send_infunc ++;

  // If BIT_CNN_TRY_SEND has been set to 1, then someone else is sending the board, then we return.
  if (cnn_data_fetch_set_evaluated_bit(&b->cnn_data, BIT_CNN_TRY_SEND)) return FALSE;

  // Otherwise we got the token and send.
  if (b->cnn_data.seq == s->seq && cnn_data_get_evaluated_bit(&b->cnn_data, BIT_CNN_SENT)) {
    PRINT_DEBUG("b = %lx (%u) is already sent to the server, do not send again!\n", (uint64_t)b, ID(b));
    cnn_data_clear_evaluated_bit(&b->cnn_data, BIT_CNN_TRY_SEND);
    return FALSE;
  }

  // If the sequence number is out-of-date, we just clear it.
  if (b->cnn_data.seq != s->seq) cnn_data_clear_evaluated_bit(&b->cnn_data, BIT_CNN_SENT);

  // Send the message.
  MBoard mboard;
  mboard.b = (uint64_t) b;
  mboard.seq = s->seq;
  CopyBoard(&mboard.board, board);

/*  ShowBoard(&mboard.board, SHOW_LAST_MOVE);
  if (info->cnn_send_infunc == 2) {
    exit(0);
  }
*/
  info->cnn_send_attempt ++;

  b->cnn_data.seq = s->seq;
  cnn_data_set_evaluated_bit(&b->cnn_data, BIT_CNN_SENT);

  if (s->callbacks.callback_send_board(s->callbacks.context, info->ex_id, &mboard)) {
    // Note that in asynchronized version, Ideally, setting BIT_CNN_SENT and ExLocalClientSendBoard should be in the critical region.
    // Otherwise the board might get sent twice. But it should not harm too much.
    // Save the sent sequence.
    info->cnn_send_success ++;
    // Clear the evaluation TRY_SEND bit so that other people can enter the critical session.
    cnn_data_clear_evaluated_bit(&b->cnn_data, BIT_CNN_TRY_SEND);
    return TRUE;
  } else {
    cnn_data_clear_evaluated_bit(&b->cnn_data, BIT_CNN_SENT);
    // Release the token so that other thread can resend.
    cnn_data_clear_evaluated_bit(&b->cnn_data, BIT_CNN_TRY_SEND);
    return FALSE;
  }
}

static void fill_block_with_fast_rollout(const TreeHandle *s, const Board *board, TreeBlock *b) {
  void *be = PatternV2InitBoardExtra(s->fast_rollout_policy, board);
  b->n = PatternV2GetTopn(be, s->params.fast_rollout_max_move, b->data.moves, b->cnn_data.fast_confidences, FALSE);
  if (b->n == 0) {
    PRINT_DEBUG("Fast rollout produces zero moves! b = %lx\n", (uint64_t)b);
  }
  PatternV2DestroyBoardExtra(be);
}

BOOL dcnn_leaf_expansion(ThreadInfo *info, const Board *board, TreeBlock *b) {
  const TreeHandle *s = info->s;

  PRINT_DEBUG("About to send the current situation to CNN multiple times..\n");
  if (info == NULL) error("ThreadInfo cannot be NULL!");

  if (b == NULL) error("Tree block cannot be null!");
  if (board == NULL) error("Board cannot be null!");

  PRINT_DEBUG("About to send to board server.\n");
  if (s->params.use_async) {
    // Fill the block with fast rollout moves.
    fill_block_with_fast_rollout(s, board, b);
    if (! s->common_params->cpu_only) send_to_cnn(info, b, board);
    return TRUE;
    // send_to_cnn(info, b, board);
    // If we are in asynchronized mode and the number of attempts exceed the threshold, we leave the loop.
  } else {
    // If it is synchronized, then we need to keep sending until it is done.
    for (int i = 0; ;++i) {
      // If we send stuff successfully, we leave the loop.
      if (send_to_cnn(info, b, board)) {
        // Wait until cnn moves is returned.
        PRINT_DEBUG("Wait until CNN moves are returned..\n");
        cnn_data_wait_until_evaluated_bit(&b->cnn_data, BIT_CNN_RECEIVED);
        PRINT_DEBUG("CNN moves are returned..\n");
        return TRUE;
      }
      PRINT_DEBUG("Send failed, resend...\n");
    }
  }
  return FALSE;
}

BOOL tsumego_setup_if_closed(ThreadInfo *info, const Board *board, TreeBlock *bl) {
  const TreeHandle *s = info->s;

  Stone win_state = S_EMPTY;

  // Check if the newly created node b is an end state, if so, no need to call dcnn.
  Stone curr_player = OPPONENT(board->_next_player);

  if (curr_player == s->params.defender) {
    // Check if in the given region, whether a player has two eyes.
    // Check at least one group of player lives.
    BOOL curr_lives = OneGroupLives(board, curr_player, &s->params.ld_region);
    if (curr_lives) win_state = curr_player;
  }

  if (s->params.defender == S_BLACK && board->_w_cap >= 4) win_state = S_WHITE;
  else if (s->params.defender == S_WHITE && board->_b_cap >= 4) win_state = S_BLACK;

  // If it is a terminal state, then we can proceed.
  // Change the prove/dis_prove number, if one side lives.
  if (win_state != S_EMPTY) {
    /*
    if (win_state == S_WHITE) {
      char buf[100];
      Coord last_move = bl->parent->data.moves[bl->parent_offset];
      printf("%s Lives! Final move: %s\n", curr_player == S_BLACK ? "B" : "W", get_move_str(last_move, curr_player, buf));
      ShowBoard(board, SHOW_ALL);
      printf("\n");
    }
    */

    int b, w;
    if (win_state == S_BLACK) {
      b = 0;
      w = INIT_PROVE_NUM;
    } else {
      w = 0;
      b = INIT_PROVE_NUM;
    }
    // Update the statistics in the parent node.
    ProveNumber *pn = &bl->parent->cnn_data.ps[bl->parent_offset];
    __atomic_store_n(&pn->w, w, __ATOMIC_RELAXED);
    __atomic_store_n(&pn->b, b, __ATOMIC_RELAXED);

    bl->n = 0;
    bl->terminal_status = win_state;
    // bl->player = mmove.player;
    return TRUE;
  }
  return FALSE;
}

BOOL tsumego_dcnn_leaf_expansion(ThreadInfo *info, const Board *board, TreeBlock *bl) {
  BOOL is_closed = tsumego_setup_if_closed(info, board, bl);
  if (is_closed) {
    // Set this node to be end state.
    bl->cnn_data.seq = info->s->seq;
    cnn_data_set_evaluated_bit(&bl->cnn_data, BIT_CNN_RECEIVED);
    return TRUE;
  } else {
    // If nothing happens, call dcnn.
    return dcnn_leaf_expansion(info, board, bl);
  }
}

BOOL tsumego_rule_leaf_expansion(ThreadInfo *info, const Board *board, TreeBlock *b) {
  const TreeHandle *s = info->s;

  BOOL is_closed = tsumego_setup_if_closed(info, board, b);
  if (!is_closed) {
    // In life and death mode, we generate move by ourselves. Is that a good idea or not?
    AllMoves all_moves;
    GetRankedMoves(board, s->params.defender, &s->params.ld_region, BLOCK_SIZE, &all_moves);

    // Put all the moves into the node.
    b->n = 0;
    char buf[40];
    for (int i = 0; i < all_moves.num_moves; ++i) {
      b->data.moves[b->n] = all_moves.moves[i];
      PRINT_DEBUG("Add LD move: %s\n", get_move_str(b->data.moves[b->n], board->_next_player, buf));
      // Always set the confidence to be zero.
      b->cnn_data.confidences[b->n] = 0.0;
      b->cnn_data.types[b->n] = MOVE_LD;
      b->cnn_data.ps[b->n].b = 10;
      b->cnn_data.ps[b->n].w = 10;
      b->n ++;
    }
  }

  // We don't need the lock here.
  // __atomic_store_n(&bl->cnn_data.seq, mmove.seq, __ATOMIC_RELAXED);
  b->cnn_data.seq = s->seq;
  cnn_data_set_evaluated_bit(&b->cnn_data, BIT_CNN_RECEIVED);
  return TRUE;
}

static void update_online_model(ThreadInfo *info, Stone player, TreeBlock *b) {
  TreeHandle *s = info->s;
  TreePool *p = &s->p;
  if (b == NULL) error("update_online_model: input b cannot be NULL!");

  pthread_mutex_lock(&s->mutex_online_model);

  // Backprop from b.
  // Note that for any new tree block, its first opp_preds will be update here. (So a policy will never
  // avoid a nonleaf child just because its opp_preds is "empty").
  for (;b != p->root; b = b->parent, player = OPPONENT(player)) {
    TreeBlock * parent = b->parent;
    BlockOffset parent_offset = b->parent_offset;
    const Stat* stat = &b->parent->data.stats[parent_offset];

    if (b->extra == NULL) continue;

    // Compute online prediction.
    PRINT_DEBUG("In update_online_model, compute online prediction..\n");
    float pred = s->model_bias;
    for (int i = 0; i < BOARD_SIZE * BOARD_SIZE; i ++) {
      pred += s->model_weights[i] * b->extra[i];
    }
    pred = sigmoid(pred);
    // For the parent, it is the win rate from opponent point of view.
    b->parent->data.opp_preds[parent_offset] = pred;

    PRINT_DEBUG("In update_online_model, finish computing online prediction..\n");

    BOOL update_model = FALSE;
    float weight = 0.0;
    float target = -1.0;
    if (s->params.life_and_death_mode) {
      // if the current b/w number has zero, then it is an end state and we should update the model, otherwise pass.
      const ProveNumber *pn = &b->parent->cnn_data.ps[parent_offset];
      if (pn == NULL) error("update_online_model: pn cannot be NULL!");
      PRINT_DEBUG("In update_online_model with life_and_death_mode is on. Before we set target..\n");
      if ((pn->b == 0 && player == S_BLACK) || (pn->w == 0 && player == S_WHITE)) target = 1.0;
      if ((pn->b == 0 && player == S_WHITE) || (pn->w == 0 && player == S_BLACK)) target = 0.0;
      if (target > 0) update_model = TRUE;
      weight = 10.0;
    } else {
      // Normal mode.
      // Get actual win rate.
      float win_rate = ((float)stat->black_win) / stat->total;
      if (player == S_WHITE) win_rate = 1 - win_rate;

      update_model = stat->total > 30;
      target = win_rate;
      weight = min(stat->total, 1000);
    }

    // One must make sure that if update_model is TRUE, target must be meaningful.
    PRINT_DEBUG("In update_online_model, accumulate the error ...\n");
    float err = target - pred;
    if (target >= 0) {
      // Mean average.
      s->model_acc_err += fabs(err);
      s->model_count_err ++;
    }

    // Gradient update.
    if (target >= 0 && update_model) {
      PRINT_DEBUG("In update_online_model, update the model ...");
      // Update the weights as well.
      // y = f(w . x + b)
      // objective:  min ||y - f(w.x+b)||^2,
      //   Note that: err = y - f(w.x+b), f' = f * (1 - f)
      // grad w_i = - err * f(1-f) * x_i
      // grad b = - err * f(1-f)
      float alpha = err * pred * (1 - pred) * weight * s->params.online_model_alpha;

      for (int i = 0; i < BOARD_SIZE * BOARD_SIZE;  ++i) {
        // Add negative gradient.
        s->model_weights[i] += alpha * b->extra[i];
      }
      s->model_bias += alpha;
    }
  }

  pthread_mutex_unlock(&s->mutex_online_model);
}

float threaded_compute_score(ThreadInfo *info, const Board *board) {
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  // After playout, check the score.
  float black_count = 0.0;
  Stone win_state = S_EMPTY;
  /*
  if (s->params.life_and_death_mode) {

    BOOL curr_lives = OneGroupLives(board, OPPONENT(board->_next_player), &s->params.ld_region);
    if (curr_lives) win_state = OPPONENT(board->_next_player);
    else if (board->_w_cap >= 2) win_state = S_WHITE;

    if (win_state != S_EMPTY) {
      b->terminal_status = win_state;
      //printf("Find a terminal status = %s\n", STR_STONE(win_state));
      // ShowBoard(board, SHOW_LAST_MOVE);
    }
    else if (s->callback_def_policy != NULL) {
      // in this mode, we want to make sure either (1) one has two eyes and the game ended. or (2) not determined, then we run default policy until it is determined.
      Board b2;
      CopyBoard(&b2, board);
      while (win_state == S_EMPTY) {
        DefPolicyMove m = s->callback_def_policy(info->def_policy, info, thread_rand, &b2, &s->params.ld_region, 1, FALSE);
        if (m.m == M_PASS) break;
        // Check if in the given region, whether a player has two eyes.
        // Check at least one group of player lives.
        BOOL curr_lives = OneGroupLives(&b2, OPPONENT(b2._next_player), &s->params.ld_region);
        if (curr_lives) win_state = OPPONENT(b2._next_player);
      }
    }
  }*/

  if (win_state == S_EMPTY) {
    return (float)GetFastScore(board, s->common_params->rule);
  } else {
    return (win_state == S_BLACK ? 10 : -10);
  }
}

// =============================================== Back propagation.
void threaded_run_bp(ThreadInfo *info, float black_moku, Stone next_player, int end_ply, BOOL board_on_child, BlockOffset child_offset, TreeBlock *b) {
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  // Dynkomi is adjusted if the win rate is too high.
  float komi = s->common_params->komi + s->common_variants->dynkomi;
  float black_count_playout = sigmoid(black_moku - komi);
  float black_count;

  // If there is network that predicts moku, then we should combine the results.
  if (b->has_score && s->params.use_cnn_final_score && end_ply >= s->params.min_ply_to_use_cnn_final_score) {
    // Final score = final_mixture_ratio * win_rate_prediction + (1.0 - final_mixture_ratio) * playout_result.
    float cnn_final_playout = sigmoid(b->score - komi);
    black_count = s->params.final_mixture_ratio * cnn_final_playout + (1 - s->params.final_mixture_ratio) * black_count_playout;
  } else {
    black_count = black_count_playout;
  }

  // Rave moves encoded in the board.
  int rave_moves[BOUND_COORD];
  if (s->params.use_rave) memset(rave_moves, 0, sizeof(rave_moves));

  TreeBlock *curr;
  BlockOffset curr_offset;

  if (board_on_child) {
    curr = b;
    curr_offset = child_offset;
  } else {
    curr = b->parent;
    curr_offset = b->parent_offset;
  }

  // Backprop from b.
  while (curr != NULL) {
    Stat* stat = &curr->data.stats[curr_offset];

    // Add total first, otherwise the winning rate might go over 1.
    __sync_add_and_fetch(&stat->total, 1);
    inc_atomic_float(&stat->black_win, black_count);
    // stat->total += 1;
    // stat->black_win += black_count;
    // __sync_add_and_fetch(&stat->black_win, black_count);

    // Then update rave, if rave mode is open
    if (s->params.use_rave) {
      // Update rave moves.
      rave_moves[curr->data.moves[curr_offset]] = 1;

      // Loop over existing moves.
      for (int i = 0; i < curr->n; ++i) {
        Coord m = curr->data.moves[i];
        if (rave_moves[m]) {
          Stat *rave_stat = &curr->data.rave_stats[i];
          __sync_add_and_fetch(&rave_stat->total, 1);
          inc_atomic_float(&rave_stat->black_win, black_count);
          // __sync_add_and_fetch(&rave_stat->black_win, black_count);
        }
      }
    }

    curr_offset = curr->parent_offset;
    curr = curr->parent;
  }

  if (s->params.use_online_model) {
    // Update the online model.
    Stone player = (board_on_child ? OPPONENT(next_player) : next_player);
    update_online_model(info, player, b);
  }
}

static void update_pn(ThreadInfo *info, BOOL board_on_child, Stone player, TreeBlock *bl) {
  // End state. Either it's state is fixed, or it is not determined (in thie case prior will just work).
  if (bl->n == 0) return;

  TreeHandle *s = info->s;

  // player is the player for the tree block, i.e., the block uses player to reach all its children.
  /*
  if (player != bl->player) {
    printf("==========Error===========\n");
    ShowBoard(board, SHOW_ALL);
    printf("player from outside = %s, player in the block = %s, board_on_child = %s\n", STR_STONE(player), STR_STONE(bl->player), STR_BOOL(board_on_child));
    tree_simple_visitor_cnn(stdout, bl, 0);
    error("UpdatePN: current player is different from bl->player\n");
  }
  */
  int b, w;
  // Update the prove/disprove number.
  if (player == S_BLACK) {
    // To prove black wins, we only need to find one bl that leads to winning.
    // The prove white wins, we have to show for every black moves, w wins.
    b = MAX_PROVE_NUM;
    w = 0;
    // Can we do it in O(1)?
    for (int i = 0; i < bl->n; ++i) {
      // Note that ps[i].b, and ps[i].w have been computed in tsumego_setup_if_closed.
      int this_b = __atomic_load_n(&bl->cnn_data.ps[i].b, __ATOMIC_ACQUIRE);
      int this_w = __atomic_load_n(&bl->cnn_data.ps[i].w, __ATOMIC_ACQUIRE);

      Coord m = bl->data.moves[i];
      if (this_b == 0) {
        // Winning move, add weights.
        __sync_fetch_and_add(&s->move_scores_black[m], 1);
      } else if (this_b >= 10000) {
        __sync_fetch_and_add(&s->move_scores_black[m], -1);
      }

      b = min(b, this_b);
      w += this_w;
    }
  } else {
    // To prove white wins, we only need to find one bl that leads to winning.
    // The prove black wins, we have to show for every white moves, b wins.
    w = MAX_PROVE_NUM;
    b = 0;
    for (int i = 0; i < bl->n; ++i) {
      int this_b = __atomic_load_n(&bl->cnn_data.ps[i].b, __ATOMIC_ACQUIRE);
      int this_w = __atomic_load_n(&bl->cnn_data.ps[i].w, __ATOMIC_ACQUIRE);

      Coord m = bl->data.moves[i];
      if (this_w == 0) {
        // Winning move, add weights.
        __sync_fetch_and_add(&s->move_scores_white[m], 1);
      } else if (this_w >= 10000) {
        __sync_fetch_and_add(&s->move_scores_white[m], -1);
      }

      w = min(w, this_w);
      b += this_b;
    }
  }

  // Finally update the corresponding node in the parent field.
  TreeBlock *parent = bl->parent;
  ProveNumber *pn = &parent->cnn_data.ps[bl->parent_offset];

  __atomic_store_n(&pn->w, w, __ATOMIC_RELAXED);
  __atomic_store_n(&pn->b, b, __ATOMIC_RELAXED);

  __sync_fetch_and_add(&parent->data.stats[bl->parent_offset].total, 1);
}

void threaded_run_tsumego_bp(ThreadInfo *info, float black_moku, Stone next_player, int end_ply, BOOL board_on_child, BlockOffset child_offset, TreeBlock *b) {
  // In tsumego, there is no playout policy, and we just evaluate the curent board situation and backprop.
  TreeHandle *s = info->s;
  TreePool *p = &s->p;

  TreeBlock *curr = b;
  Stone player = next_player;
  // If the board is pointing to the child, we need to switch the current player.
  if (board_on_child) player = OPPONENT(player);

  // Backpropagation.
  while (curr->parent != NULL) {
    update_pn(info, board_on_child, player, curr);
    curr = curr->parent;
    player = OPPONENT(player);
  }

  if (s->params.use_online_model) {
    // Update the online model.
    player = board_on_child ? OPPONENT(next_player) : next_player;
    update_online_model(info, player, b);
  }
}

// Fast rollout default policy.
// For now we will just omit Region *r and max_depth, and simulate until the end of game.
DefPolicyMove fast_rollout_def_policy(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose) {
  if (verbose) printf("Init fast rollout def policy!\n");

  void *be = PatternV2InitBoardExtra(def_policy, board);
  SampleSummary summary;

  if (verbose) printf("Start sampling.\n");
  PatternV2SampleUntil(be, context, rand_func, NULL, &summary);

  if (verbose) printf("Copying final board back.\n");
  CopyBoard(board, PatternV2GetBoard(be));

  if (verbose) printf("Clean up.\n");
  PatternV2DestroyBoardExtra(be);

  // Return the last move.
  DefPolicyMove move = { .m = board->_last_move, .gamma = 0, .type = NORMAL, .game_ended = IsGameEnd(board) };
  return move;
}
