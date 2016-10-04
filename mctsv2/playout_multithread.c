//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#include "playout_multithread.h"
#include "tree_search.h"
#include "../local_evaluator/cnn_local_exchanger.h"
#include "../local_evaluator/cnn_exchanger.h"

// ======================== Utilities functions =================================
Move compose_move(int x, int y, Stone player) {
  Move move = { .x = x, .y = y, .m = GetCoord(x, y), .player = player };
  return move;
}

Move compose_move2(Coord m, Stone player) {
  Move move = { .x = X(m), .y = Y(m), .m = m, .player = player };
  return move;
}

// =================================== Internal datastructure ==============================
// How many channels / GPUs we have
#define MAX_MOVE 1000

typedef struct SearchHandle {
  SearchParamsV2 params;
  // Dynamic global parameters that change over time (e.g., dynkomi).
  SearchVariants variants;
  TreeParams tree_params;

  // Internal board.
  Board board;

  // Search trees. For one search handle, we might use multiple search trees.
  void **trees;
  int num_trees;

  // CNN Servers to connect from. The number of servers should be the
  // same as the number of gpus.
  void **ex;

  // Previous moves.
  Move prev_moves[MAX_MOVE];
  int num_prev_moves;
} SearchHandle;

// Client delegates..
static void client_init(SearchHandle *s) {
  PRINT_INFO("Initialize Client...\n");
  if (s->params.server_type == SERVER_LOCAL) {
    for (int i = 0; i < s->params.num_gpu; ++i) {
      s->ex[i] = ExLocalInit(s->params.pipe_path, i, FALSE);
      if (s->ex[i] == NULL) {
        error("No CNN connection\n");
      }
    }
  } else {
    s->ex[0] = ExClientInit(s->params.tier_name);
    if (s->ex[0] == NULL) {
      error("Initializing tier [%s] failed. ", s->params.tier_name);
    }
  }
}

static void client_destroy(SearchHandle *s) {
  // Destroy stuff.
  PRINT_INFO("Free the client...\n");
  if (s->params.server_type == SERVER_LOCAL) {
    for (int i = 0; i < s->params.num_gpu; ++i) {
      ExLocalDestroy(s->ex[i]);
    }
  } else {
    ExClientDestroy(s->ex[0]);
  }
}

// Abstraction for sending the board / receiving the move.
static BOOL client_send_board(void *ctx, int i, MBoard *mboard) {
  SearchHandle *s = (SearchHandle *)ctx;
  mboard->t_sent = wallclock();
  if (s->params.server_type == SERVER_LOCAL) {
    return ExLocalClientSendBoard(s->ex[i], mboard);
  } else {
    ExClientSendBoard(s->ex[0], mboard);
    return TRUE;
  }
}

static void client_send_restart(void *ctx) {
  SearchHandle *s = (SearchHandle *)ctx;
  if (s->params.server_type == SERVER_LOCAL) {
    PRINT_INFO("Send Restart message to server...\n");
    for (int i = 0; i < s->params.num_gpu; ++i) {
      // Send restart message.
      ExLocalClientSendRestart(s->ex[i]);
    }

    PRINT_INFO("Waiting for ACK from server...\n");
    for (int i = 0; i < s->params.num_gpu; ++i) {
      // Wait until the server has finish restarting.
      ExLocalClientWaitAck(s->ex[i]);
    }
  }
}

// Return FALSE if the receiver did not get anything.
static BOOL client_receive_move(void *ctx, int i, MMove *mmove) {
  SearchHandle *s = (SearchHandle *)ctx;
  // Block read since we are in a different thread.
  if (s->params.server_type == SERVER_LOCAL) {
    return ExLocalClientGetMove(s->ex[i], mmove);
  } else {
    return ExClientGetMove(s->ex[0], mmove);
  }
}

static int client_discard_moves(void *ctx, int i) {
  SearchHandle *s = (SearchHandle *)ctx;
  MMove mmove;
  int num_discarded = 0;
  if (s->params.server_type == SERVER_LOCAL) {
    while (ExLocalClientGetMove(s->ex[i], &mmove)) num_discarded ++;
  }
  return num_discarded;
}

// ===================== APIs ==========================
void ts_v2_init_params(SearchParamsV2 *params) {
  memset(params, 0, sizeof(SearchParamsV2));
  // Set a few default parameters.
  params->server_type = SERVER_LOCAL;
  strcpy(params->pipe_path, "/data/local/go/");
  strcpy(params->tier_name, "ai.go-evaluator");
  params->verbose = V_INFO;
  params->komi = 6.5;
  params->dynkomi_factor = 0.0;
  params->num_gpu = 4;
  params->print_search_tree = FALSE;
  params->cpu_only = FALSE;
  params->rule = RULE_CHINESE;
  // No time constraint.
  params->time_left = 0;
  params->heuristic_tm_total_time = 0;
}

void ts_v2_print_params(void *ctx) {
  if (ctx == NULL) {
    fprintf(stderr,"Cannot print search params since ctx is NULL\n");
    return;
  }
  SearchHandle *s = (SearchHandle *)ctx;
  const SearchParamsV2 *params = &s->params;

  // Print all search parameters.
  fprintf(stderr," ------------ Parameters for Search -----------------\n");
  if (params->server_type == SERVER_LOCAL) {
    fprintf(stderr,"Local Pipe path: %s\n", params->pipe_path);
  } else {
    fprintf(stderr,"Server: %s\n", params->tier_name);
  }
  fprintf(stderr,"Verbose: %d\n", params->verbose);
  fprintf(stderr,"PrintSearchTree: %s\n", STR_BOOL(params->print_search_tree));
  fprintf(stderr,"#GPU: %d\n", params->num_gpu);
  fprintf(stderr,"#Use CPU rollout only: %s\n", STR_BOOL(params->cpu_only));
  fprintf(stderr,"Komi: %.1f\n", params->komi);
  fprintf(stderr,"dynkomi_factor: %.2f\n", params->dynkomi_factor);
  fprintf(stderr,"Rule: %s\n", params->rule == RULE_CHINESE ? "chinese" : "japanese");
  fprintf(stderr,"Use heuristic time management: %d, max_time_spent: %lf, min_time_spent: %lf\n",
      params->heuristic_tm_total_time, params->max_time_spent, params->min_time_spent);
  // Print the parameters of all the search trees.
  for (int i = 0; i < s->num_trees; ++i) {
    fprintf(stderr,"+++++++++++ Tree #%d ++++++++++++\n", i);
    tree_search_print_params(s->trees[i]);
    fprintf(stderr,"+++++++++++ End Tree ++++++++++++\n");
  }
  fprintf(stderr," --------- End parameters for Search --------------\n");
}

void ts_v2_thread_off(void *ctx) {
  if (ctx == NULL) return;
  SearchHandle *s = (SearchHandle *)ctx;
  for (int i = 0; i < s->num_trees; ++i) {
    tree_search_thread_off(s->trees[i]);
  }
}

void ts_v2_thread_on(void *ctx) {
  if (ctx == NULL) return;
  SearchHandle *s = (SearchHandle *)ctx;
  for (int i = 0; i < s->num_trees; ++i) {
    tree_search_thread_on(s->trees[i]);
  }
}

// ========================= initialize/destroy the structure ======================================
// this will only be called once for each game.
void* ts_v2_init(const SearchParamsV2 *params, const TreeParams *tree_params, const Board* init_board) {
  SearchHandle *s = (SearchHandle *)malloc(sizeof(SearchHandle));
  if (s == NULL) {
    fprintf(stderr,"Initialize SearchHandle failed!");
    error("");
  }
  if (tree_params == NULL) {
    fprintf(stderr,"TreeParams cannot be NULL");
    error("");
  }
  if (params == NULL) {
    fprintf(stderr,"SearchParams cannot be NULL");
    error("");
  }

  s->params = *params;
  s->tree_params = *tree_params;

  ExCallbacks cbs;
  cbs.context = s;
  cbs.callback_send_board = client_send_board;
  cbs.callback_receive_move = client_receive_move;
  cbs.callback_receiver_discard_move = client_discard_moves;
  cbs.callback_receiver_restart = client_send_restart;

  // initialize the queue for previous moves.
  s->num_prev_moves = 0;
  s->variants.dynkomi = 0.0;

  PRINT_INFO("Initialize the sender/receiver. #gpu = %d.\n", s->params.num_gpu);
  // For global server, we need to set num_gpu to 1.
  if (! params->cpu_only) {
    s->ex = (void **)malloc(sizeof(void *) * s->params.num_gpu);
    client_init(s);
  }

  // Initialize the internal board
  if (init_board == NULL) {
    ClearBoard(&s->board);
  } else {
    CopyBoard(&s->board, init_board);
  }

  // Initialize the search trees.
  s->num_trees = 1;
  s->trees = (void **)malloc(sizeof(void *) * s->num_trees);
  s->trees[0] = tree_search_init(&s->params, &s->variants, &cbs, tree_params, init_board);

  // Finally return the handle.
  return s;
}

void ts_v2_setboard(void *ctx, const Board *new_board) {
  if (ctx == NULL) return;
  SearchHandle *s = (SearchHandle *)ctx;
  // Free the tree.
  s->num_prev_moves = 0;
  s->variants.dynkomi = 0.0;

  if (new_board != NULL) {
    CopyBoard(&s->board, new_board);
  } else {
    ClearBoard(&s->board);
  }

  // Reset all the trees.
  for (int i = 0; i < s->num_trees; ++i) {
    tree_search_set_board(s->trees[i], new_board);
  }
}

BOOL ts_v2_set_params(void *ctx, const SearchParamsV2 *new_params, const TreeParams *new_tree_params) {
  if (ctx == NULL || (new_params == NULL && new_tree_params == NULL)) return FALSE;
  SearchHandle *s = (SearchHandle *)ctx;

  // A few things you cannot change on the fly.
  if (new_params != NULL && new_params->server_type != s->params.server_type) return FALSE;

  ts_v2_thread_off(ctx);

  if (new_params != NULL && new_params->komi != s->params.komi) {
      // If komi is changed, we need to clear up the search tree. Clean up relevant internal status.
      s->variants.dynkomi = 0.0;
      for (int i = 0; i < s->num_trees; ++i) {
        tree_search_reset_tree(s->trees[i]);
      }
  }

  // Set the params.
  if (new_params != NULL) {
    s->params = *new_params;
  }
  if (new_tree_params != NULL) {
    s->tree_params = *new_tree_params;
    for (int i = 0; i < s->num_trees; ++i) {
      tree_search_set_params(s->trees[i], new_tree_params);
    }
  }

  ts_v2_thread_on(ctx);
  return TRUE;
}

BOOL ts_v2_set_time_left(void *ctx, unsigned int time_left, unsigned int num_moves) {
  // Set the time left.
  if (ctx == NULL) return FALSE;
  SearchHandle *s = (SearchHandle *)ctx;

  __atomic_store_n(&s->params.time_left, time_left, __ATOMIC_RELAXED);
  return TRUE;
}

void ts_v2_add_move_history(void *ctx, Coord m, Stone player, BOOL actual_play) {
  if (ctx == NULL) return;
  SearchHandle *s = (SearchHandle *)ctx;

  if (actual_play) {
    GroupId4 ids;
    if (! TryPlay2(&s->board, m, &ids)) {
      char buf[100];
      ShowBoard(&s->board, SHOW_LAST_MOVE);
      fprintf(stderr,"Move: %s", get_move_str(m, player, buf));
      error("add_move_history: the move is not valid!");
    }
    // Play it.
    Play(&s->board, &ids);
  }

  // No need to block all threads since the printing function is only used in the main thread.
  if (s->num_prev_moves < MAX_MOVE) {
    s->prev_moves[s->num_prev_moves ++] = compose_move2(m, player);
  } else {
    error("The number of moves has exceeded the limit [%d]!", MAX_MOVE);
  }
}

void ts_v2_free(void *ctx) {
  if (ctx == NULL) return;
  SearchHandle *s = (SearchHandle *)ctx;

  if (! s->params.cpu_only) {
    // Stop all receiving process...
    if (s->params.server_type != SERVER_LOCAL) {
      ExClientStopReceivers(s->ex[0]);
    }

    client_destroy(s);
  }

  for (int i = 0; i < s->num_trees; ++i) {
    tree_search_free(s->trees[i]);
  }
  free(s->trees);

  if (! s->params.cpu_only) {
    // Free the sender/receiver. Their sizes are equal to the number of gpus we have.
    free(s->ex);
  }
}

void ts_v2_peek(void *ctx, int topk, Moves *moves, const Board *verify_board) {
  if (ctx == NULL) error("ctx cannot be NULL!");
  if (moves == NULL) error("move_seq cannot be zero!");

  SearchHandle *s = (SearchHandle *)ctx;
  Stone player = s->board._next_player;

  // Check if the board is right.
  if (verify_board != NULL) {
    // If the two boards are not the same, error!
    if (!CompareBoard(&s->board, verify_board)) {
      printf("[ts_v2_pick_best]: Internal Board:\n");
      ShowBoard(&s->board, SHOW_ALL);
      printf("[ts_v2_pick_best]: External Board:\n");
      ShowBoard(verify_board, SHOW_ALL);
      error("The two boards are not the same!\n");
    }
  }

  moves->num_moves = topk;

  double t;
  timeit
    tree_search_peek(s->trees[0], moves, verify_board);
  endtime2(t)

  char buf[100];
  printf("[ts_v2_peek] Ply: %d, Time elapsed: %lf\n", s->board._ply, t);
  for (int i = 0; i < moves->num_moves; ++i) {
    const Move *m = &moves->moves[i];
    printf("[ts_v2_peek:%d]: %s, win_rate: %f [%.2f/%d]\n", i, get_move_str(m->m, player, buf), m->win_rate, m->win_games, m->total_games);
  }
}

Move ts_v2_pick_best(void *ctx, AllMoves *all_moves, const Board *verify_board) {
  if (ctx == NULL) error("ctx cannot be NULL!");

  if (all_moves == NULL) error("move_seq cannot be zero!");

  SearchHandle *s = (SearchHandle *)ctx;
  Stone player = s->board._next_player;

  // Check if the board is right.
  if (verify_board != NULL) {
    // If the two boards are not the same, error!
    if (!CompareBoard(&s->board, verify_board)) {
      fprintf(stderr,"[ts_v2_pick_best]: Internal Board:\n");
      ShowBoard(&s->board, SHOW_ALL);
      fprintf(stderr,"[ts_v2_pick_best]: External Board:\n");
      ShowBoard(verify_board, SHOW_ALL);
      error("The two boards are not the same!\n");
    }
  }

  double t;
  Move move;
  timeit
    move = tree_search_pick_best(s->trees[0], all_moves, verify_board);
  endtime2(t)

  char buf[100];
  fprintf(stderr,"[ts_v2_pick_best] Ply: %d, Time elapsed: %lf, move = %s, win_rate = %f [%.2f/%d]\n",
      s->board._ply, t, get_move_str(move.m, player, buf), move.win_rate, move.win_games, move.total_games);

  // Change dynkomi if we are winning or losing too much.
  // When win_rate is big and we are black, increase the komi.
  // Suppose we have an initial win_rate of 0.2, then we have -0.3 * alpha for each move we pick.
  // In 100 rounds, we are going to have 30 * alpha dyn_komi, so maybe alpha = 1.0 is ok.
  float dynkomi_delta = s->params.dynkomi_factor * (move.win_rate - 0.5) * (player == S_WHITE ? -1 : 1);
  s->variants.dynkomi += dynkomi_delta;

  // If the best move is self-atari, then we need to double check whether it is valid.
  // In the long turn, we should put M_PASS as one choice in search when the game is about to end.
  // Here the hack will mislead MCTS to output very low winning rate, until we pick PASS and found that we are actually the winner.
  /*
  if (IsSelfAtari(&s->board, NULL, move.m, player, NULL) && move.win_rate < 0.1) {
    // Then we should change it to M_PASS, since we have nothing to lose.
    PRINT_INFO("MCTS choose a self-atari move [%s] with win rate = %f, Change it to M_PASS!\n", get_move_str(move.m, player, buf), move.win_rate);
    move.m = M_PASS;
    move.x = X(move.m);
    move.y = Y(move.m);
  }
  */

  // Print tree.
  // Show the picked move as comments so that we could analyze it.
  if (s->params.print_search_tree) {
    fprintf(stderr,"Best move: %s\n", get_move_str(move.m, player, buf));
    fprintf(stderr,"COMMENT\n");
    // print the address b and seq number.
    fprintf(stderr,"Ply: %d, dynkomi: %f\n", s->board._ply, s->variants.dynkomi);
    for (int i = 0; i < s->num_trees; ++i) {
      tree_search_print_tree(s->trees[i]);
    }
    fprintf(stderr,"ENDCOMMENT\n");
  }

  return move;
}

int ts_v2_undo_pass(void *ctx, const Board *before_board) {
  if (ctx == NULL) error("ctx cannot be NULL!");

  SearchHandle *s = (SearchHandle *)ctx;
  int success_count = 0;
  for (int i = 0; i < s->num_trees; ++i) {
    if (tree_search_undo_pass(s->trees[i], before_board))
      success_count ++;
  }
  return success_count;
}

void ts_v2_prune_opponent(void *ctx, Coord m) {
  if (ctx == NULL) error("ctx cannot be NULL!");

  SearchHandle *s = (SearchHandle *)ctx;

  for (int i = 0; i < s->num_trees; ++i) {
    tree_search_prune_opponent(s->trees[i], m);
  }

  ts_v2_add_move_history(s, m, s->board._next_player, TRUE);
  return;
}

void ts_v2_prune_ours(void *ctx, Coord m) {
  if (ctx == NULL) error("ctx cannot be NULL!");

  SearchHandle *s = (SearchHandle *)ctx;

  for (int i = 0; i < s->num_trees; ++i) {
    tree_search_prune_ours(s->trees[i], m);
  }

  ts_v2_add_move_history(s, m, s->board._next_player, TRUE);
  return;
}

void ts_v2_tree_to_json(void *ctx, const char *jsonfile_prefix) {
  SearchHandle *s = (SearchHandle *)ctx;

  char filename[1000];
  double t;
  for (int i = 0; i < s->num_trees; ++i) {
    timeit
    sprintf(filename, "%s-%d.json", jsonfile_prefix, i);
    tree_search_to_json(s->trees[i], s->prev_moves, s->num_prev_moves, filename);
    endtime2(t)
    fprintf(stderr,"Save %s. Time elapsed: %lf\n", filename, t);
  }
}

// Output the feature to a text file, one feature a line. L&D mode only.
void ts_v2_tree_to_feature(void *ctx, const char *feature_prefix) {
   SearchHandle *s = (SearchHandle *)ctx;

  char filename[1000];
  double t;
  for (int i = 0; i < s->num_trees; ++i) {
    timeit
    sprintf(filename, "%s-%d.txt", feature_prefix, i);
    tree_search_to_feature(s->trees[i], filename);
    endtime2(t)
    fprintf(stderr,"Save %s. Time elapsed: %lf\n", filename, t);
  }
}

void ts_v2_search_start(void *ctx) {
  // Start all trees.
  SearchHandle *s = (SearchHandle *)ctx;
  for (int i = 0; i < s->num_trees; ++i)
    tree_search_start(s->trees[i]);
}

void ts_v2_search_stop(void *ctx) {
  // Stop all trees.
  SearchHandle *s = (SearchHandle *)ctx;
  for (int i = 0; i < s->num_trees; ++i)
    tree_search_stop(s->trees[i]);
}
