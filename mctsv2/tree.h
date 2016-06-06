//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _TREE_H_
#define _TREE_H_

// Simple tree. (No sibiling pointers)
#include <stdio.h>
#include <stdint.h>
#include "../common/common.h"
#include "../common/comm_constant.h"
#include "event_count.h"

// Statistics at each tree node.
typedef struct {
  float black_win;
  int total;
} Stat;

#define MAX_PROVE_NUM 0x7fffffff
#define INIT_PROVE_NUM 100000

typedef struct {
  // To prove b/w is winning, how many evaluations the search should do.
  int b, w;
} ProveNumber;

// Basic block structure in tree nodes.
// If it is too big, we waste memory for monte-carlo tree search.
// If it is too small, we have overhead in traversing/addition/deletion.
#define BLOCK_SIZE 32
typedef unsigned char BlockOffset;
typedef unsigned char BlockLength;
// Each bit indicates the status of one child. On our server, sizeof(unsigned long) = 8 and thus we have a maximum of 64 bits.
typedef unsigned long BlockBits;

#define BIT(k) (1ULL << (k))
#define TEST_BIT(e, k) (e & BIT(k))
#define SET_BIT(e, k) e |= BIT(k)
#define RESET_BIT(e, k) e &= ~BIT(k)

// ==== Game specific data =======
typedef struct {
   // Game specific data
  unsigned char player;
  Coord moves[BLOCK_SIZE];
  Stat stats[BLOCK_SIZE];
  Stat rave_stats[BLOCK_SIZE];
  // win rate online prediction from opponent perspective.
  float opp_preds[BLOCK_SIZE];
} GameData;

#define BIT_CNN_TRY_SEND 0
#define BIT_CNN_SENT 1
#define BIT_CNN_RECEIVED 2
#define BIT_CNN_NUM_BITS 3

typedef struct {
  // Whether this situation is not evaluated/pending/evaluated by deepnets.
  // Only the lowest two bits are used.
  // When bit 0 is 1, we have tried to send this node for evaluation.
  // When bit 1 is 1, we have successfully sent this node for evaluation.
  // When bit 2 is 1, this node has been evaluated and all data structures are now ready to use.
  unsigned char evaluated;
  // The sent/received sequence number from CNN server.
  long seq;
  // Deepnet confidences.
  // Type of moves. Some moves might not come from CNN (e.g., tactical moves).
  // See ../common/package.h for definition of move types.
  char types[BLOCK_SIZE];
  // confidences given in fast rollout.
  float fast_confidences[BLOCK_SIZE];
  float confidences[BLOCK_SIZE];
  // prove, disprove numbers, used for tsumego search.
  ProveNumber ps[BLOCK_SIZE];
  EventCount event_counts[BIT_CNN_NUM_BITS];
} CNNData;

BOOL cnn_data_get_evaluated_bit(const CNNData* data, unsigned char bit);
void cnn_data_set_evaluated_bit(CNNData* data, unsigned char bit);
void cnn_data_clear_evaluated_bit(CNNData* data, unsigned char bit);
BOOL cnn_data_fetch_set_evaluated_bit(CNNData* data, unsigned char bit);

unsigned char cnn_data_wait_until_evaluated_bit(CNNData* data,
                                                unsigned char bit);
unsigned char cnn_data_load_evaluated(CNNData* data);

struct TreeBlock_;

typedef struct ChildInfo_ {
  struct TreeBlock_* child;
  EventCount event_count;
} ChildInfo;

typedef struct TreeBlock_ {
  // Parent node.
  struct TreeBlock_ *parent;

  // Now the block needs an id (starting from 1)
  unsigned int id;

  // The block offset for the parent.
  BlockOffset parent_offset;

  // #valid elements for this block.
  BlockLength n;

  // Whether this node is a terminal node (is_terminal != S_EMPTY) and should not be expanded further.
  // Usually the node is a terminal node when n = 0 (no move is valid), but in life and death problem, a node might be
  // terminal when the opponent builds two eyes, or failed to build two eyes, etc.
  Stone terminal_status;

  // The board hash for this node.
  // uint64_t board_hash;

  // Many children
  ChildInfo children[BLOCK_SIZE];

  // Bit used for tree expansion. 0 = no expansion is happening, 1 = expansion is happening and/or expansion is complete.
  // It is also used for checking the first nonleaf child.
  BlockBits expansion;

  // Finally the data used for games.
  GameData data;

  // CNN data
  CNNData cnn_data;

  // Additional data used for online models.
  char *extra;

  // Score (always from black perspective), if there is any.
  BOOL has_score;
  float score;
} TreeBlock;

typedef TreeBlock * TreeBlockPtr;

// For simple tree, we allocate/delete the memory on the fly.
#define TP_NULL 0

typedef struct {
  // All things starts from 1. 0 is reserved for null pointer.
  // blocks[TP_ROOT] is always the root for the main tree (so that root can collect the overall statistics)
  // blocks[TP_FREE_START + i] is always the root for the i-th freed tree. We keep a set of free trees to make allocation parallelable.
  TreeBlock *root;

  // Some statistics.
  int64_t ever_allocated;
  int allocated;
  int freed;
} TreePool;

// Initialize tree pool
void tree_simple_pool_init(TreePool *p);

#define NUM_NONLEAF(bl) __builtin_popcountl((bl)->expansion)
#define FIRST_NONLEAF(bl) (((bl)->expansion == 0) ? BLOCK_SIZE : __builtin_ctzl((bl)->expansion))
#define ID(bl) ((bl) == TP_NULL ? 0 : (bl)->id)

// Display the content of the block.
void tree_simple_show_block(const TreeBlock *bl);

// Allocate multiple blocks with a given function as an allocator. Used for multithreading.
typedef void (* FuncSimpleInitBlocks)(TreePool *p, TreeBlock *bl, void *context, void *context2);
TreeBlock *tree_simple_g_alloc(TreePool *p, void *context, void *context2, FuncSimpleInitBlocks func_init, TreeBlock *parent, BlockOffset parent_offset);

// Free the children of node, except for the child specified by b2 and offset.
void tree_simple_free_except(TreePool *p, TreeBlock *except);

// Free the tree_pool
void tree_simple_pool_free(TreePool* p);

// Debugging tools.
void tree_simple_pool_check(const TreePool *p);

// Get status string.
const char *tree_simple_get_status_str(unsigned char evaluated);

// Print out the current visible tree into file.
typedef void (* FuncSimpleTreeVisitor)(void *context, const TreeBlock *bl, int depth);
void tree_simple_visitor_cnn(void *context, const TreeBlock *bl, int depth);

// Example usage:
//   tree_print_out(filename, p, tree_child_picker_cnn, tree_visitor_cnn);
// Here all the void * pointer should FILE *, since LUA does not recognize it, we need to change it to void *
void tree_simple_print_out(void *fp, const TreePool *p, FuncSimpleTreeVisitor visitor);
void tree_simple_print_out_cnn(void *fp, const TreePool *p);

#define EXPAND_STATUS_FIRST       0
#define EXPAND_STATUS_EXPANDING   1
#define EXPAND_STATUS_DONE        2

// Begin expanding the idx'th child of bl. If another thread is expanding it,
// wait until the expansion is complete and return the child with EXPAND_STATUS_DONE.
// Otherwise, mark us as expanding it, and return child = NULL with EXPAND_STATUS_FIRST.
// If child returns NULL, you must complete the expansion using tree_simple_g_alloc.
int tree_simple_begin_expand(TreeBlock* bl, BlockOffset idx, TreeBlock **child);

// Begin expanding the idx'th child of bl. If another thread is expanding it, the return EXPAND_STATUS_EXPANDING with *child = NULL
// If no one is expanding it and we are the first, then return EXPAND_STATUS_FIRST with *child = NULL (only in this case we need to allocate the child).
// If the node is already expanded, then return EXPAND_STATUS_DONE with *child = the expanded child.
int tree_simple_begin_expand_nowait(TreeBlock* bl, BlockOffset idx, TreeBlock **child);
// #define tree_error printf
//
#endif
