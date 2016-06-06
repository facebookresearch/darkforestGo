//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "tree.h"
// #include "block_queue.h"
#include <stdio.h>
#include "../common/common.h"
#include <pthread.h>

#define CHECK_CORRECT

#define NUM_THREAD 16
// #define NUM_THREAD 1

typedef struct {
  pthread_mutex_t mutex_alloc[NUM_THREAD];
  // Seed shared by all threads (not a good design, but this is not important here).
  unsigned long seed;
  TreePool *p;
  int num_rollout;
  int num_rollout_per_thread;
  // Queue q;
} SearchInfo;

int thread_rand(void *context, int max_value) {
  SearchInfo *info = (SearchInfo *)context;
  return fast_random(&info->seed, max_value);
}

void init_callback(TreePool *p, TreeBlock *bl, void *context, void *context2) {
  // Here we just set the number of free to be active.
  bl->n = thread_rand(context, BLOCK_SIZE - 1) + 1;
}

void* thread_random_expansion(void *ctx) {
  // Copy a new board. No pointer in board.
  SearchInfo *info = (SearchInfo* )ctx;

  TreePool *p = info->p;

  for (int i = 0; i < info->num_rollout_per_thread; ++i) {
    // Random traverse down the tree and expand a node
    TreeBlock *b = p->root->children[0].child;
    // printf("---Start playout %d/%d [Round %d]---\n", i, K, round);
    BOOL expanded = FALSE;
    while (!expanded) {  // Once expanded, leave the loop.
      // Pick a random children.
      if (b == TP_NULL) error("We should never visit TP_NULL.");
      // tree_show_block(p, b);
      // printf("Total children = %d\n", total_children);
      BlockOffset child_idx = thread_rand(ctx, b->n);
      TreeBlock *c = b->children[child_idx].child;

      // Expand it or go downward
      // printf("[Explore %d]: pick idx = %d/%d\n", ID(b), child_idx, b->n);
      if (c == TP_NULL) {
        int res = tree_simple_begin_expand(b, child_idx, &c);
        if (c == TP_NULL) {
          // expand the current tree.
          c = tree_simple_g_alloc(p, ctx, NULL, init_callback, b, child_idx);
          if (c == TP_NULL) {
            error("allocation error, b = 0");
          }
        }
        expanded = TRUE;
      }
      b = c;
    }
    // Back prop.
    int black_count = thread_rand(ctx, 2);
    while (b != p->root) {
      TreeBlock * parent = b->parent;
      BlockOffset parent_offset = b->parent_offset;
      Stat* stat = &b->parent->data.stats[parent_offset];

      // Add total first, otherwise the winning rate might go over 1.
      // stat->total += 1;
      // stat->black_win += black_count;
      __sync_fetch_and_add(&stat->total, 1);
      inc_atomic_float(&stat->black_win, (float)black_count);
      // __sync_fetch_and_add(&stat->black_win, black_count);

      b = parent;
    }
  }
  // printf("thread done. #rollout = %d\n", info->num_rollout_per_thread);
  return NULL;
}

void *thread_tree_search_daemon(void *ctx) {
  SearchInfo *info = (SearchInfo *)ctx;
  info->num_rollout_per_thread = (info->num_rollout + NUM_THREAD - 1) / NUM_THREAD;
  info->seed = 324;

  for (int i = 0; i < NUM_THREAD; ++i) pthread_mutex_init(&info->mutex_alloc[i], NULL);
  // Initialize queue.
  // queue_init(&info->q);

  // Split the task into nthread, and make a sparate thread for tree-leaf allocation.
  pthread_t explorers[NUM_THREAD];
  TreePool *p = info->p;

  for (int i = 0; i < NUM_THREAD; ++i) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1048576);
    if (i == 0 && p->root->children[i].child == TP_NULL) {
      // We need to initialize root.
      p->root->children[i].child =
        tree_simple_g_alloc(p, info, NULL, init_callback, p->root, 0);
    }
    pthread_create(&explorers[i], &attr, thread_random_expansion, info);
  }

  // Wait until all finished.
  for (int i = 0; i < NUM_THREAD; ++i) {
    pthread_join(explorers[i], NULL);
  }

  for (int i = 0; i < NUM_THREAD; ++i) pthread_mutex_destroy(&info->mutex_alloc[i]);
  return NULL;
  // queue_release(&info->q);
}

int seed;
void onexit() {
  printf("Random seed = %d\n", seed);
}

int main() {
#ifdef CHECK_CORRECT
  const int K = 1000;
  const int R = 100;
#else
  const int K = 100000;
  const int R = 100;
#endif

  atexit(onexit);
  printf("K = %d, R = %d\n", K, R);

  TreePool p;
  timeit
    tree_simple_pool_init(&p);
  endtime

  // Random expand a few nodes.
  //
  double t;
  timeit
  for (int i = 0; i < R; ++i) {
    // printf("======================= Start round %d out of %d ========================\n", i, R);
    // random_expansion(&p, i, K);
    pthread_t daemon;
    SearchInfo info = { .p = &p, .num_rollout = K };
    pthread_create(&daemon, NULL, thread_tree_search_daemon, &info);
    pthread_join(daemon, NULL);

    // First child.
    BlockOffset offset = FIRST_NONLEAF(p.root->children[0].child);
    tree_simple_free_except(&p, p.root->children[offset].child);
#ifdef CHECK_CORRECT
    printf("======================= Finish round %d out of %d ========================\n", i, R);
    tree_simple_pool_check(&p);
#endif
  }
  endtime2(t)

  printf("Freeing tree pool\n");
  timeit
    tree_simple_pool_free(&p);
  endtime

  printf("rollout rate = %f\n", K * R / t);
  return 0;
}
