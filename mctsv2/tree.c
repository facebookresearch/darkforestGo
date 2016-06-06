//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "tree.h"
#include "../board/board.h"
#include <memory.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

// Initialize tree pool
void tree_simple_pool_init(TreePool *p) {
  p->root = (TreeBlock *)malloc(sizeof(TreeBlock));
  if (p->root == NULL) {
    error("Failed to malloc root!\n");
  }
  memset(p->root, 0, sizeof(TreeBlock));
  // Root always have one child.
  p->root->n = 1;

  p->allocated = 1;
  p->ever_allocated = 1;
}

// ======================
void tree_simple_show_block(const TreeBlock *bl) {
  BlockLength num_nonleaf = NUM_NONLEAF(bl);
  printf("[Block %u]: parent = %u [offset = %d], n = %d, n_nonleaf = %d\n",
      ID(bl), ID(bl->parent), bl->parent_offset, bl->n, num_nonleaf);
  // Print all the children.
  printf("Children [%d]: ", bl->n);
  for (int i = 0; i < bl->n; ++i) {
    printf("%d ", ID(bl->children[i].child));
  }
  printf("\n");
  printf("Expansion [%d]: ", num_nonleaf);
  for (int i = 0; i < BLOCK_SIZE; ++i) {
    if (TEST_BIT(bl->expansion, i)) {
      printf("%d ", i);
    }
  }
  printf("\n");
}

BOOL cnn_data_get_evaluated_bit(const CNNData* data, unsigned char bit) {
  unsigned char v = __atomic_load_n(&data->evaluated, __ATOMIC_ACQUIRE);
  return TEST_BIT(v, bit) ? TRUE : FALSE;
}

void cnn_data_set_evaluated_bit(CNNData* data, unsigned char bit) {
  __atomic_or_fetch(&data->evaluated, BIT(bit), __ATOMIC_RELEASE);
  event_count_broadcast(&data->event_counts[bit]);
}

BOOL cnn_data_fetch_set_evaluated_bit(CNNData* data, unsigned char bit) {
  unsigned char value_before = __atomic_fetch_or(&data->evaluated, BIT(bit), __ATOMIC_RELEASE);
  event_count_broadcast(&data->event_counts[bit]);
  return TEST_BIT(value_before, bit) ? TRUE : FALSE;
}

void cnn_data_clear_evaluated_bit(CNNData* data, unsigned char bit) {
  __atomic_and_fetch(&data->evaluated, ~BIT(bit), __ATOMIC_RELEASE);
  event_count_broadcast(&data->event_counts[bit]);
}

unsigned char cnn_data_wait_until_evaluated_bit(CNNData* data,
                                                unsigned char bit) {
  unsigned char v = __atomic_load_n(&data->evaluated, __ATOMIC_ACQUIRE);
  if (!TEST_BIT(v, bit)) {
    EventCount* ev = &data->event_counts[bit];
    for (;;) {
      EventCountKey ek = event_count_prepare(ev);
      v = __atomic_load_n(&data->evaluated, __ATOMIC_ACQUIRE);
      if (TEST_BIT(v, bit)) {
        event_count_cancel(ev);
        break;
      }
      event_count_wait(ev, ek);
    }
  }
  return v;
}

unsigned char cnn_data_load_evaluated(CNNData* data) {
  return __atomic_load_n(&data->evaluated, __ATOMIC_ACQUIRE);
}

static void tree_simple_alloc_assign_info(TreeBlock* bl, TreeBlock *parent, BlockOffset parent_offset) {
  memset((void *)bl, TP_NULL, sizeof(TreeBlock));
  bl->parent = parent;
  bl->parent_offset = parent_offset;
  // Number of nodes will be assigned when CNN moves arrives. bl->n = num_nodes;
  for (unsigned i = 0; i < BIT_CNN_NUM_BITS; ++i) {
    event_count_init(&bl->cnn_data.event_counts[i]);
  }
}

TreeBlock *tree_simple_g_alloc(TreePool *p, void *context, void *context2, FuncSimpleInitBlocks func_init, TreeBlock *parent, BlockOffset parent_offset) {
  TreeBlock *bl = (TreeBlock *)malloc(sizeof(TreeBlock));
  tree_simple_alloc_assign_info(bl, parent, parent_offset);

  // Set the id of the tree.
  bl->id = __sync_fetch_and_add(&p->ever_allocated, 1);
  __sync_fetch_and_add(&p->allocated, 1);

  // Initialize function if there is any.
  // Note that we should not call initialization AFTER the parent connects with the child, since by then other threads might have access to
  // this child before we set up everything.
  // printf("p = %lx, bl = %lx, context = %lx, context2 = %lx\n", (uint64_t)p, (uint64_t)bl, (uint64_t)context, (uint64_t)context2);
  // fflush(stdout);
  if (func_init != NULL) {
    func_init(p, bl, context, context2);
  }

  // Now parent should connect the head of the children.
  // Note that parent->expansion should already be set (parent->expansion is used as a simple mutex).
  SET_BIT(parent->expansion, parent_offset);
  __sync_bool_compare_and_swap(&parent->children[parent_offset].child, 0, bl);
  event_count_broadcast(&parent->children[parent_offset].event_count);
  return bl;
}

static void recursive_free(TreePool *p, TreeBlock *r) {
    // Open multithread to recursively free the tree.
    // For now just single thread
    if (r == TP_NULL) return;
    for (int i = 0; i < r->n; ++i) {
        if (r->children[i].child != TP_NULL) {
            recursive_free(p, r->children[i].child);
        }
    }
    if (r->parent != TP_NULL) {
      RESET_BIT(r->parent->expansion, r->parent_offset);
      r->parent->children[r->parent_offset].child = TP_NULL;
      event_count_destroy(&r->parent->children[r->parent_offset].event_count);
    }
    for (int j = 0; j < BIT_CNN_NUM_BITS; ++j) {
      event_count_destroy(&r->cnn_data.event_counts[j]);
    }
    if (r->extra) free(r->extra);
    free(r);
    p->allocated --;
    return;
}

// Free the children of p->root, except for the child exception.
// Connect the parent of bl with the exception child. If except == TP_NULL, remove all children.
// Then we starts a few threads to free the tree (also free bl).
void tree_simple_free_except(TreePool *p, TreeBlock *except) {
  TreeBlock *r = p->root->children[0].child;
  if (r == TP_NULL) {
    memset(&p->root->data.stats[0], 0, sizeof(Stat));
    return;
  }

  // Free the nodes.
  for (int i = 0; i < r->n; ++i) {
      if (r->children[i].child != except) {
        recursive_free(p, r->children[i].child);
      }
  }

  // Reconnect. Note this is run in single thread, so order does not matter.
  p->root->children[0].child = except;
  if (except != TP_NULL) {
    float black_win = 0.0;
    int total = 0;
    // In this case, we need to recompute the stats.
    for (int i = 0; i < except->n; ++i) {
      black_win += except->data.stats[i].black_win;
      total += except->data.stats[i].total;
    }
    p->root->data.stats[0].black_win = black_win;
    p->root->data.stats[0].total = total;

    except->parent = p->root;
    except->parent_offset = 0;
  } else {
    // Empty the child and reset the statistics.
    RESET_BIT(p->root->expansion, 0);
    memset(&p->root->data.stats[0], 0, sizeof(Stat));
  }
}

// Free the tree_pool
void tree_simple_pool_free(TreePool* p) {
    recursive_free(p, p->root);
}

// Debug code to detect any inconsistency.
static void tree_simple_check_one_block(const TreeBlock *root, const TreeBlock *bl) {
  if (bl == TP_NULL) return;
  // printf("Checking: ");
  // tree_simple_show_block(p, b);
  for (int i = 0; i < bl->n; ++i) {
    BOOL has_child = bl->children[i].child != TP_NULL;
    BOOL has_expansion = TEST_BIT(bl->expansion, i) != 0;
    if (has_child != has_expansion) error("Block [%u] at %d: child = %d while expansion = %d\n", ID(bl), i, has_child, has_expansion);
  }
  BlockBits mask = (BIT(bl->n) - 1);
  if (bl->expansion & ~mask) {
    tree_simple_show_block(bl);
    error("Block [%u] has nonzero expansion outside its size %d. Expansion = %u\n", ID(bl), bl->n, bl->expansion);
  }
  // Check the connection between its parents node and itself.
  if (bl->parent == TP_NULL) {
    if (bl != root) {
      tree_simple_show_block(bl);
      error("Except for p->root, no other node [%u] could have TP_NULL parent", ID(bl));
    }
  } else {
    // Check parents.
    const TreeBlock *parent_slot =
      bl->parent->children[bl->parent_offset].child;
    if (parent_slot != bl) {
      tree_simple_show_block(bl);
      tree_simple_show_block(bl->parent);
      error("The block [%u]'s parent[%u]' child pointer is zero!", ID(bl), ID(bl->parent));
    }

    // Check the statistics, except if the parent is root.
    float total_black_win = 0;
    int total = 0;
    for (int i = 0; i < bl->n; ++i) {
      // printf("Child %d: black_win = %.2f, total = %d\n", bl->data.stats[i].black_win, bl->data.stats[i].total);
      total_black_win += bl->data.stats[i].black_win;
      total += bl->data.stats[i].total;
    }

    int recorded_total = bl->parent->data.stats[bl->parent_offset].total;
    float recorded_black_win =  bl->parent->data.stats[bl->parent_offset].black_win;
    if (total != recorded_total) {
      error("Block %x [%u]: The computed total [%d] is different from recorded total [%d]!", (uint64_t)bl, ID(bl), total, recorded_total);
    }
    if (total_black_win != recorded_black_win) {
      error("Block %x [%u]: The computed total [%.2f] is different from recorded total [%.2f]!", (uint64_t)bl, ID(bl), total_black_win, recorded_black_win);
    }
  }
}

static void tree_simple_pool_recursive_tree_check(const TreeBlock *root, const TreeBlock *bl) {
    if (bl == TP_NULL) return;
    // Check this block.
    tree_simple_check_one_block(root, bl);

    // Do it recursively.
    for (int i = 0; i < bl->n; ++i) {
        tree_simple_check_one_block(root, bl->children[i].child);
    }
}

void tree_simple_pool_check(const TreePool *p) {
  printf("DEBUG: Checking p->root\n");
  if (p->root == TP_NULL) {
    error("ROOT cannot be TP_NULL!");
    return;
  }

  tree_simple_pool_recursive_tree_check(p->root, p->root);
  printf("DEBUG: All tree check complete!\n");
}

const char *tree_simple_get_status_str(unsigned char evaluated) {
  if (evaluated == 6) return "evaluated";
  if (evaluated == 2) return "sent";
  if (evaluated == 1) return "try_sending";
  if (evaluated == 0) return "created";
  return "unknown";
}

void tree_simple_visitor_cnn(void *context, const TreeBlock *bl, int depth) {
  FILE *fp = (FILE *)context;
  BlockOffset i = bl->parent_offset;

  const TreeBlock *parent = bl->parent;
  const Stat *s = &parent->data.stats[i];
  const CNNData *cnn = &parent->cnn_data;

  char *spaces = (char *)malloc(depth + 1);
  memset(spaces, ' ', depth);
  spaces[depth] = 0;

  float win_ratio = ((float)s->black_win) / s->total;
  char buf[30];

  // Only show black win ratio.
  fprintf(fp, "%s\"name\": \"%.1f/%.3f/%d\", \n", spaces, win_ratio * 100, s->black_win, s->total);
  fprintf(fp, "%s\"status\": \"%s\", \n", spaces, tree_simple_get_status_str(bl->cnn_data.evaluated));
  fprintf(fp, "%s\"confidence\": %f, \n", spaces, cnn->confidences[i]);
  fprintf(fp, "%s\"fast_confidence\": %f, \n", spaces, cnn->fast_confidences[i]);
  fprintf(fp, "%s\"opp_pred\": %f, \n", spaces, parent->data.opp_preds[i]);
  fprintf(fp, "%s\"terminal\": \"%s\", \n", spaces, STR_STONE(bl->terminal_status));
  fprintf(fp, "%s\"b_ptr\": \"%lx\",\n", spaces, (uint64_t)bl);
  fprintf(fp, "%s\"seq\": %ld,\n", spaces, bl->cnn_data.seq);
  fprintf(fp, "%s\"n\": %d,\n", spaces, bl->n);
  fprintf(fp, "%s\"b/w/n\": \"%d/%d/%d\",\n", spaces, parent->cnn_data.ps[i].b, parent->cnn_data.ps[i].w, s->total);
  fprintf(fp, "%s\"move_x\": %d,\n", spaces, X(parent->data.moves[i]));
  fprintf(fp, "%s\"move_y\": %d,\n", spaces, Y(parent->data.moves[i]));
  fprintf(fp, "%s\"move_str\": \"%s\",\n", spaces, get_move_str(parent->data.moves[i], S_EMPTY, buf));
  fprintf(fp, "%s\"nonleaf\": %d", spaces, NUM_NONLEAF(bl));
  free(spaces);
}

// Recursively print stuff
void tree_simple_print_out_impl(FILE* fp, const TreeBlock *bl, int depth, FuncSimpleTreeVisitor visitor) {
  if (bl == TP_NULL) return;

  char *spaces = (char *)malloc(depth + 1);
  memset(spaces, ' ', depth);
  spaces[depth] = 0;

  // Visit the current node.
  fprintf(fp, "%s{\n", spaces);
  visitor(fp, bl, depth);

  int index = 0;
  for (int i = 0; i < bl->n; ++i) {
    if (bl->children[i].child == TP_NULL) continue;
    if (index > 0) {
      fprintf(fp, ",\n");
    } else {
      fprintf(fp, ",\n%s\"children\": [\n", spaces);
    }
    tree_simple_print_out_impl(fp, bl->children[i].child, depth + 2, visitor);
    index ++;
  }
  if (index > 0) fprintf(fp, "\n%s]", spaces);
  fprintf(fp, "\n%s}", spaces);
  free(spaces);
  return;
}

void tree_simple_print_out(void *fp, const TreePool *p, FuncSimpleTreeVisitor visitor) {
  tree_simple_print_out_impl((FILE *)fp, p->root->children[0].child, 0,
                             visitor);
}

void tree_simple_print_out_cnn(void *fp, const TreePool *p) {
  tree_simple_print_out((FILE *)fp, p, tree_simple_visitor_cnn);
}

// Expand #1:
//   wait if other threads already have token. return if this thread is the first.
int tree_simple_begin_expand(TreeBlock* parent, BlockOffset parent_offset, TreeBlock **child) {
  BlockBits expansion_before = __atomic_fetch_or(
      &parent->expansion,
      BIT(parent_offset),
      __ATOMIC_ACQ_REL);
  if (!TEST_BIT(expansion_before, parent_offset)) {
    *child = NULL;
    return EXPAND_STATUS_FIRST;  // we've got to do it
  }

  ChildInfo* cinfo = &parent->children[parent_offset];
  TreeBlock* c;

  // Wait if no one has done it yet.
  if ((c = __atomic_load_n(&cinfo->child, __ATOMIC_ACQUIRE)) == NULL) {
    // We've got to wait.
    for (;;) {
      EventCountKey key = event_count_prepare(&cinfo->event_count);
      if ((c = __atomic_load_n(&cinfo->child, __ATOMIC_ACQUIRE)) != NULL) {
        event_count_cancel(&cinfo->event_count);
        break;
      }
      event_count_wait(&cinfo->event_count, key);
    }
  }

  *child = c;
  return EXPAND_STATUS_DONE;
}

// Expand #2:
//   return if other threads already have tokens and are expanding.
int tree_simple_begin_expand_nowait(TreeBlock* parent, BlockOffset parent_offset, TreeBlock **child) {
  BlockBits expansion_before = __atomic_fetch_or(
      &parent->expansion,
      BIT(parent_offset),
      __ATOMIC_ACQ_REL);

  if (!TEST_BIT(expansion_before, parent_offset)) {
    *child = NULL;
    return EXPAND_STATUS_FIRST;  // we've got to do it
  }

  ChildInfo* cinfo = &parent->children[parent_offset];

  // Wait if no one has done it yet.
  if ((*child = __atomic_load_n(&cinfo->child, __ATOMIC_ACQUIRE)) == NULL) return EXPAND_STATUS_EXPANDING;
  else return EXPAND_STATUS_DONE;
}
