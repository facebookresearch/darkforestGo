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
static BOOL check_correct = FALSE;

int seed;
void onexit() {
  printf("Random seed = %d\n", seed);
}

int main(int argc, char *argv[]) {
  int K, R;
  int nthread = 16;
  int num_gpu = 4;

  char server_type[100];
  strcpy(server_type, "local");

  if (check_correct) {
    K = 1000;
    R = 10;
  } else {
    K = 100000;
    R = 50;
  }

  SearchParamsV2 search_params;
  TreeParams tree_params;

  ts_v2_init_params(&search_params);
  tree_search_init_params(&tree_params);

  search_params.verbose = V_INFO;
  tree_params.verbose = V_INFO;
  // tree_params.verbose = V_DEBUG;
  //
  K = 1000;
  if (argc >= 2) sscanf(argv[1], "%s", server_type);
  if (argc >= 3) sscanf(argv[2], "%d", &K);
  if (argc >= 4) sscanf(argv[3], "%d", &nthread);
  if (argc >= 5) sscanf(argv[4], "%d", &num_gpu);
  if (argc >= 6) sscanf(argv[5], "%d", &R);

  if (! strcmp(server_type, "local")) {
    printf("Use local server\n");
    search_params.server_type = SERVER_LOCAL;
    search_params.num_gpu = num_gpu;
    strcpy(search_params.pipe_path, "/data/local/go/");
  } else {
    printf("Use cluster server = %s\n", server_type);
    search_params.server_type = SERVER_CLUSTER;
    search_params.num_gpu = num_gpu;
    strcpy(search_params.tier_name, server_type);
  }

  // search_params.print_search_tree = TRUE;

  tree_params.use_async = FALSE;
  search_params.cpu_only = FALSE;
  tree_params.expand_n_thres = 0;

  /*
  search_params.cpu_only = TRUE;
  tree_params.use_async = TRUE;
  tree_params.expand_n_thres = 40;
  */

  tree_params.num_rollout = K;
  tree_params.num_rollout_per_move = K;
  tree_params.num_dcnn_per_move = K;
  tree_params.num_receiver = num_gpu;
  tree_params.num_tree_thread = nthread;
  tree_params.sigma = 0.05;
  // tree_params.num_virtual_games = 10;
  tree_params.decision_mixture_ratio = 5.0;
  tree_params.rcv_max_num_move = 20;
  tree_params.use_rave = FALSE;
  tree_params.use_online_model = FALSE;
  tree_params.online_model_alpha = 0.001;
  tree_params.online_prior_mixture_ratio = 5.0;
  tree_params.rcv_acc_percent_thres = 80;
  // tree_params.sigma = 0.00;
  tree_params.use_pondering = TRUE;
  strcpy(tree_params.pattern_filename, "../models/playout-model.bin");
  tree_params.default_policy_choice = DP_V2;
  tree_params.default_policy_temperature = 0.125;

  // seed = time(NULL);
  seed = 1441648459;
  srand(seed);
  atexit(onexit);
  printf("K = %d, R = %d, nthread = %d, num_gpu = %d\n", K, R, nthread, num_gpu);

  // Random expand a few nodes.
  //
  double t;
  Board board;
  ClearBoard(&board);
  GroupId4 ids;
  AllMoves move_seq;

  void *tree_handle = ts_v2_init(&search_params, &tree_params, &board);
  ts_v2_print_params(tree_handle);
  ts_v2_search_start(tree_handle);

  char dbuf[200];
  strcpy(dbuf, "/tmp/test_playout_multithread.XXXXXX");
  if (!mkdtemp(dbuf)) {
    error("Could not create temporary directory");
  }

  printf("Saving JSON tree dumps in %s\n", dbuf);
  char* buf = dbuf + strlen(dbuf);
  *buf++ = '/';

  timeit
  for (int i = 0; i < R; ++i) {
    timeit
    // dprintf("======================= Start round %d out of %d ========================\n", i, R);
    Move m = ts_v2_pick_best(tree_handle, &move_seq, NULL);
    sprintf(buf, "mcts_tree_%d", i);
    ts_v2_tree_to_json(tree_handle, buf);
    ts_v2_prune_ours(tree_handle, m.m);

    // Save the current tree.
    // printf("Saving current tree to %s...\n", buf);
    // tree_print_out_cnn(buf, ts_get_tree_pool(tree_handle));

    if (! TryPlay(&board, m.x, m.y, board._next_player, &ids) ) error("The move given by expansion should never fail!!");
    Play(&board, &ids);

    ShowBoard(&board, SHOW_LAST_MOVE);
    printf("\n");

    if (check_correct) {
      // dprintf("======================= Finish round %d out of %d ========================\n", i, R);
      // tree_pool_check(ts_get_tree_pool(tree_handle));
    }
    endtime
  }
  endtime2(t)

  printf("Freeing\n");
  timeit
    ts_v2_search_stop(tree_handle);
    ts_v2_free(tree_handle);
  endtime

  printf("Time used for mcts = %lf\n", t);
  printf("rollout rate = %f\n", K * R / t);
  return 0;
}

