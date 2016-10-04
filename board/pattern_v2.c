
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//

#include "pattern_v2.h"
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <inttypes.h>
#ifndef PRIu64
#define PRIu64 "llu"
#endif

#define HASH_SIZE (1ULL << 20)
#define MASK(h) (h & (HASH_SIZE - 1))

// #define EXP(a) (1 + a)

#define PRINT_INFO(h, ...) do { if (h->params.verbose >= PV_INFO) { fprintf(stderr,__VA_ARGS__); fflush(stdout); } } while(0)
#define PRINT_DEBUG(h, ...) do { if (h->params.verbose >= PV_DEBUG) { fprintf(stderr,__VA_ARGS__); fflush(stdout); } } while(0)

const double g_beta = 0.67;
// #define EXP(a) exp(((a) * g_beta))
#define EXP(a) exp(a)
#define LOG(a) log(a)

#define W_BOUND 6.0
#define CLAMP(w) do { if ((w) > W_BOUND) w = W_BOUND; else if ((w) < -W_BOUND) w = -W_BOUND; } while(0)

// Note this works only up to 2, for 3 offsets it will wrap around.
/*
#define D12(c, cc) \
static int __deltax[13] = {0, 0,  0,  1, -1,  1, 1, -1, -1, 0,  0, 2, -2}; \
static int __deltay[13] = {0, 1, -1,  0,  0, -1, 1,  1, -1, 2, -2, 0, 0}; \
int __x0 = X(c); \
int __y0 = Y(c); \
for (int __i = 0; __i < 13; ++__i) { \
  Coord cc = OFFSETXY(__x0 + __deltax[__i], __y0 + __deltay[__i]); \
  if (cc >= BOUND_COORD) continue;

#define ENDD12 }
*/

#define NEIGHBOR_COUNT 9
static int neighbor_x[NEIGHBOR_COUNT] = {0, 0,  0,  1, -1,  1, 1, -1, -1};
static int neighbor_y[NEIGHBOR_COUNT] = {0, 1, -1,  0,  0, -1, 1,  1, -1};
#define NEI_C 0
#define NEI_B 1
#define NEI_T 2
#define NEI_R 3
#define NEI_L 4
#define NEI_RT 5
#define NEI_RB 6
#define NEI_LB 7
#define NEI_LT 8

#define D12(c, __i, cc, dir) \
int __x0 = X(c); \
int __y0 = Y(c); \
for (int __i = 0; __i < NEIGHBOR_COUNT; ++__i) { \
  Coord cc = OFFSETXY(__x0 dir neighbor_x[__i], __y0 dir neighbor_y[__i]); \
  if (cc >= BOUND_COORD) continue;

#define ENDD12 }

#define min(a, b) ((a) < (b) ? (a) : (b))
#define LOCAL_CODE(liberty, color) ( (min((liberty - 1), 3) << 2) + (color) )

static int get_hash_local_index(const Board *b, Coord cc) {
  Stone color = b->_infos[cc].color;
  short liberty = 1;
  short id = b->_infos[cc].id;
  if (color == S_BLACK || color == S_WHITE) {
    liberty = b->_groups[id].liberties;
  }
  // Return a number in [0, 15];
  return LOCAL_CODE(liberty, color);
}

#define HASH_EMPTY_LOCAL_IDX 0

static unsigned int fast_random_callback(void *ctx, unsigned int max_value) {
  unsigned long *seed = (unsigned long *)ctx;
  return fast_random(seed, max_value);
}

// A simple list data structure that can quickly check repetitves.
typedef struct {
  // A array that stores keys.
  unsigned int *keys;
  // Num of keys that we currently have.
  int n;
  // Upper bound of the #keys we could have.
  int ub_num_key;

  // Whether an item is in the list (-1 means it is not in the list), if so, where is it.
  int *keys_map;
  // Upper bound of the key. max(key) == upper_bound - 1
  int ub_key;
} RepCheckList;

RepCheckList *InitRepCheckList(int ub_num_key, int ub_key) {
  RepCheckList *l = (RepCheckList *)malloc(sizeof(RepCheckList));
  l->ub_key = ub_key;
  l->ub_num_key = ub_num_key;

  l->keys = (unsigned int *)malloc(sizeof(int) * ub_num_key);
  l->keys_map = (int *)malloc(sizeof(int) * ub_key);

  // Initialize everything to be -1.
  memset(l->keys_map, 0xff, sizeof(int) * ub_key);

  l->n = 0;
  return l;
}

void DestroyRepCheckList(RepCheckList *l) {
  free(l->keys);
  free(l->keys_map);
  free(l);
}

#define KEY_SUCCESS           0
#define KEY_NONEXISTS         1
#define KEY_EXISTS            2
#define KEY_OOB            0x101
#define KEY_FULL           0x102
#define KEY_EMPTY          0x103

static inline int RepCheckListAdd(RepCheckList *l, unsigned int key) {
  if (key >= l->ub_key) return KEY_OOB;
  if (l->keys_map[key] >= 0) return KEY_EXISTS;
  if (l->n == l->ub_num_key) return KEY_FULL;

  // fprintf(stderr,"Add key [%u] at %d\n", key, l->n);

  l->keys_map[key] = l->n;
  l->keys[l->n ++] = key;
  return KEY_SUCCESS;
}

static inline int RepCheckListRemove(RepCheckList *l, unsigned int key) {
  if (key >= l->ub_key) return KEY_OOB;
  if (l->keys_map[key] < 0) return KEY_NONEXISTS;
  if (l->n == 0) return KEY_EMPTY;

  int idx = l->keys_map[key];
  // fprintf(stderr,"Delete key [%u] at %d\n", key, idx);
  l->keys_map[key] = -1;
  l->n --;
  // Swap the deleted key with the last element.
  if (idx < l->n) {
    int k2 = l->keys[l->n];
    // fprintf(stderr,"Replace slot %d with key %d at %d/%d\n", idx, k2, l->n, l->n + 1);
    l->keys_map[k2] = idx;
    l->keys[idx] = k2;
  }
  return KEY_SUCCESS;
}

static inline unsigned int RepCheckListSize(const RepCheckList *l) { return l->n; }
static inline unsigned int RepCheckListEnumerate(const RepCheckList *l, int i) { return l->keys[i]; }

static inline BOOL RepCheckListCheck(const RepCheckList *l) {
  if (l->n < 0 || l->n >= l->ub_num_key) {
    fprintf(stderr,"l->n [%d] is out of bound [%d]\n", l->n, l->ub_num_key);
    return FALSE;
  }
  for (int i = 0; i < l->n; ++i) {
    unsigned int key = l->keys[i];
    // fprintf(stderr,"key: %d\n", key);
    if (key >= l->ub_key) {
      fprintf(stderr,"key [%d] at %d/%d is not valid [ub_key = %d]\n", key, i, l->n, l->ub_key);
      return FALSE;
    }
    if (l->keys_map[key] != i) {
      fprintf(stderr,"key [%d] at %d/%d is not consistent with key map, whose loc is [%d]\n", key, i, l->n, l->keys_map[key]);
      return FALSE;
    }
  }
  for (int i = 0; i < l->ub_key; ++i) {
    int idx = l->keys_map[i];
    if (idx == -1) continue;
    if (idx >= l->n) {
      fprintf(stderr,"The key map at %d is %d, out of bound [l->n = %d]\n", i, idx, l->n);
      return FALSE;
    }
    if (l->keys[idx] != i) {
      fprintf(stderr,"key_map [%d] at %d/%d is not consistent with keys at %d, whose key is %d\n", idx, i, l->ub_key, idx, l->keys[idx]);
      return FALSE;
    }
  }
  return TRUE;
}

static inline void RepCheckListClear(RepCheckList *l) {
  for (int i = 0; i < l->n; ++i) {
    l->keys_map[l->keys[i]] = -1;
  }
  l->n = 0;
}

// ================ AllMovesExt =====================
AllMovesExt *InitAllMovesExt(int num_moves) {
  AllMovesExt *all_moves = (AllMovesExt *)malloc(sizeof(AllMovesExt));
  all_moves->moves = (MoveExt *)malloc(num_moves * sizeof(MoveExt));
  all_moves->num_moves = num_moves;
  return all_moves;
}

void DestroyAllMovesExt(AllMovesExt *h) {
  free(h->moves);
  h->num_moves = 0;
  free(h);
}

AllMovesComments *InitAllMovesComments(int num_moves) {
  AllMovesComments *all_moves_c = (AllMovesComments *)malloc(sizeof(AllMovesComments));
  all_moves_c->comments = (SingleComment *)malloc(num_moves * sizeof(SingleComment));
  all_moves_c->num_comments = num_moves;
  return all_moves_c;
}

void DestroyAllMovesComments(AllMovesComments *h) {
  free(h->comments);
  h->num_comments = 0;
  free(h);
}

typedef struct {
  // A blooming filter to make sure a pattern is saved only if it has been seen twice.
  // Assume n_pattern = 1e8 (100m), if p = 1e-4,
  // then the best m = -n * ln (p) / (ln2)^2 / 1M / 8bit = 229 M
  // best k = -log_2(p) = 13.3
  unsigned char *bloom_filter;
  int mbit;
  uint64_t m_mask;
  int k;
  uint64_t *hash_seeds;

  uint64_t num_queries;
  uint64_t num_found;
} BloomFilter;

BloomFilter *bloom_init(int mbit, int k) {
  BloomFilter *f = (BloomFilter *)malloc(sizeof(BloomFilter));
  f->num_queries = 0;
  f->num_found = 0;
  f->mbit = mbit;
  f->m_mask = (1ULL << mbit) - 1;
  f->k = k;

  int bloom_filter_size = 1ULL << (mbit - 3);
  f->bloom_filter = (unsigned char *)malloc( bloom_filter_size * sizeof(unsigned char) );
  memset(f->bloom_filter, 0, bloom_filter_size * sizeof(unsigned char) );

  f->hash_seeds = (uint64_t *)malloc(k * sizeof(uint64_t));
  uint64_t hash_init = 124135134;
  for (int i = 0; i < k; ++i) {
    f->hash_seeds[i] = fast_random64(&hash_init);
  }
  return f;
}

void bloom_free(BloomFilter *f) {
  free(f->bloom_filter);
  free(f->hash_seeds);
  free(f);
}

static BOOL bloom_check(BloomFilter *f, uint64_t key, BOOL insert_if_not_found) {
  BOOL found = TRUE;
  for (int i = 0; i < f->k; ++i) {
    uint64_t seed = f->hash_seeds[i] ^ key;
    uint64_t idx = fast_random64(&seed) & f->m_mask;
    uint64_t offset = idx >> 3;
    unsigned char mask = (1 << (idx & 7));

    if (f->bloom_filter[offset] & mask) continue;
    found = FALSE;
    if (insert_if_not_found) {
      f->bloom_filter[offset] |= mask;
    }
  }
  f->num_queries ++;
  if (found) f->num_found ++;
  return found;
}

#define MAX_GROUP_ATARI      5
#define MAX_SELF_ATARI       5

#define T_RESP_MOVE          0
#define T_NAKADE             1
#define T_NEIGHBOR           2
#define T_SAVE_ATARI         3
#define T_KILL_GROUP         4
#define T_KILL_GROUP2        5
#define T_GLOBAL_EXTEND      6
#define T_GLOBAL_KILL        7
#define T_GLOBAL_SELF_ATARI  8
#define T_GLOBAL_ATARI       9
#define T_KO                 10
#define T_PUT_GROUP_TO_ATARI 11
#define T_PLY_POS_W          12
#define T_ABSENT_MOVE        13
#define T_SELF_ATARI         14
#define T_MAKE_EYE           15
#define T_FALSIFY_EYE        16

typedef struct {
  int id;
  char prior_name[40];
  int size;
} PriorSpec;

#define USE_EYE

static const PriorSpec g_priors[] = {
  { T_RESP_MOVE, "RESP_MOVE", 1 },
  { T_NAKADE, "NAKADE", 9 },
  { T_NEIGHBOR, "NEIGHBOR", 8 },
  { T_SAVE_ATARI, "SAVE_ATARI", 1 },
  { T_KILL_GROUP, "KILL_GROUP", 1 },
  { T_KILL_GROUP2, "KILL_GROUP2", 1 },
  { T_GLOBAL_EXTEND, "GLOBAL_EXTEND", 1 },
  { T_GLOBAL_KILL, "GLOBAL_KILL", 1 },
  { T_GLOBAL_SELF_ATARI, "GLOBAL_SELF_ATARI", 1 },
  { T_GLOBAL_ATARI, "GLOBAL_ATARI", 1 },
  // Play recent ko (for ko_age = 1-10)
  { T_KO, "KO", 10 },
  { T_PUT_GROUP_TO_ATARI, "PUT_GROUP_TO_ATARI", MAX_GROUP_ATARI },
  { T_PLY_POS_W, "PLY_POS_W", 1 },
  { T_ABSENT_MOVE, "ABSENT_MOVE", 1 },
  { T_SELF_ATARI, "SELF_ATARI", MAX_SELF_ATARI },
#ifdef USE_EYE
  { T_MAKE_EYE, "MAKE_EYE", 1},
  { T_FALSIFY_EYE, "FALSIFY_EYE", 1}
#endif
};

// Hack here. Not good.
#ifdef USE_EYE
#define LEN_PRIOR (39 + MAX_GROUP_ATARI + MAX_SELF_ATARI)
#else
#define LEN_PRIOR (37 + MAX_GROUP_ATARI + MAX_SELF_ATARI)
#endif

#define NUM_PRIOR (sizeof(g_priors)/sizeof(PriorSpec))

#define WT_RESP 0
#define WT_NORESP 1
#define WT_POS 2
#define WT_PRIOR 3
#define WT_TOTAL 4

#define STATUS_NORMAL 0
#define STATUS_BAD_MOVE 1

static const int g_w_sizes[WT_TOTAL] = { HASH_SIZE, HASH_SIZE, BOUND_COORD, LEN_PRIOR };

typedef struct {
  // Accumulated probability.
  double prob;
  // log prob due to the current move pattern.
  double logprob;
  // Total log prob due to influence (other prior)
  double prior;

  // The location of the move.
  Coord m;

  // Gradient at each move.
  double grad;

  // The heap idx.
  int heap_idx;

  // Total prior counts on this move.
  int prior_count;

  // Whether this move is added by prior, if so, should be removed when prior is removed.
  BOOL added_by_prior;

  // Status of the move, used for sampling.
  int status;
} PatternMove;

typedef struct {
  // Total priors that have been added.
  double prior;
  // The location of the move.
  Coord m;
  // The type of the weight.
  int w_type;
  // The offset.
  int w_offset;
} PriorMove;

/*
typedef struct {
  // The move that causes atari. If the atari situation is from opponent, m will be the stone close to the opponent stone.
  Coord m_self;
  // The opponent stone that causes the group to be in atari. If the group is self-atari, m_opponent = M_PASS.
  Coord m_opponent;
} AtariInfo;
  // Used to monitor groups with liberties <= 2.
  // When a move has been played,
  //  1. If we kill any enemy group, then all <=2 groups adjacent to the killed enemy group will be updated.
  //  2. If we reduce an enemy group's liberty, update the information accordingly.
  //  3. Play the move.
  //  4. Shuffle the order of group id with GetGroupReplaceSeq,
  //  5. If new <= 2 groups has been created, add.
  AtariInfo atari_infso[MAX_GROUP];
*/

// Simple hash library
typedef struct {
  // key -> weights.
  double k2w_resp[HASH_SIZE];
  double k2w_noresp[HASH_SIZE];
  int cnt_k2w_resp[HASH_SIZE];
  int cnt_k2w_noresp[HASH_SIZE];

  // Positional weights. Each position there is a preference.
  double pos_w[BOUND_COORD];

  // weights for different priors.
  int prior_offset[NUM_PRIOR];
  double prior_w[LEN_PRIOR];
  int prior_type[LEN_PRIOR];

  // Pointing to the weights.
  double *weights[WT_TOTAL];

  // Set hashes
  // {w, b, empty, offboard} x {1, 2, 3, >=3} x {#neighbor count}
  // Note that for empty location, their hash code is always 0.
  uint64_t hs[NEIGHBOR_COUNT][16];

  // The number of patterns harvested.
  uint64_t num_pattern;
  uint64_t collision;

  // Bloom filter. NULL if we don't want to harvest pattern.
  BloomFilter *filter;

  // Whether to display stuff.
  PatternV2Params params;

  // Additional params.
  double T;
} Handle;

// Gradient information.
typedef struct {
  // Gradient related structures.
  RepCheckList *checks[WT_TOTAL];
  // Allocate gradients.
  double *grads[WT_TOTAL];
} HandleGradient;

typedef struct {
  // Handle pointer.
  const Handle *h;
  // Internal board.
  Board board;
  // For each valid board location, we have a hash.
  uint64_t hashes[BOUND_COORD];

  // Data structure for incremental move sampling.
  // We have a max heap that stores the probability/coord.
  // 1. Each time we sample, we go through the heap until the prob mass is accumulated (heap is approximately sorted).
  // 2. When a new move comes,
  //    (a) update all the pattern around it,
  //    (b) for each update pattern, for each its influenced move, find their heap location, and update the heap.
  //
  // Candidate moves arranged in the same manner as on the board.
  PatternMove moves[BOUND_COORD];

  // Need to make it double, otherwise the error would accumulate.
  double total_prob;
  double total_prob_before_prior;
  int prior_status;
  // Must prior move. (e.g., nakade point).
  Coord prior_must_move;

  // Heap, each points to the index in moves.
  Coord moves_heap[MACRO_BOARD_SIZE * MACRO_BOARD_SIZE + 1];
  int heap_size;

  // Modified moves with priors.
  PriorMove prior_moves[MACRO_BOARD_SIZE * MACRO_BOARD_SIZE];
  int num_prior_moves;

  // Empty location queue.
  RepCheckList *empty_list;

  // Prior move for atari's.
  // These moves will be update in PlayMove, if any lib == 2 group can be put atari by some moves.
  // PriorMove atari_moves[MACRO_BOARD_SIZE * MACRO_BOARD_SIZE];
  // int num_atari_moves;

  // Used to check which hashes have been changed. Only used in play_move.
  BOOL changed_hashes_map[BOUND_COORD];
  Coord changed_hashes[BOUND_COORD];
  int num_changed_hashed;

  // All group ids used in PlayMove.
  RepCheckList *changed_ids;
} BoardExtra;

// --------- Heap related utilities -----------------------
static int heap_dump_one_to_buffer(const BoardExtra *h, int heap_idx, const char *prefix, char *buffer) {
  Coord m = h->moves_heap[heap_idx];
  const PatternMove *mv = &h->moves[m];
  char buf[30];
  const char *move_str = get_move_str(m, h->board._next_player, buf);

  if (prefix == NULL) prefix = "";

  int len = 0;
  len += sprintf(buffer + len, "%s Move %s at %d/%d: logprob: %lf, prior: %lf, raw-prob: %lf, total_prob: %lf, prob: %lf",
      prefix, move_str, heap_idx, h->heap_size, mv->logprob, mv->prior, mv->prob, h->total_prob, mv->prob / h->total_prob);
  if (heap_idx != mv->heap_idx) {
    len += sprintf(buffer + len, ", heap_idx from moves: %d", mv->heap_idx);
  }
  len += sprintf(buffer + len, "\n");
  return len;
}

static int heap_dump_to_buffer(const BoardExtra *h, int heap_size, char *buffer) {
  if (heap_size == -1) heap_size = h->heap_size;
  else if (heap_size > h->heap_size) heap_size = h->heap_size;
  char buf[30];
  int len = 0;
  len += sprintf(buffer + len, "--- HeapDump:\n");
  len += sprintf(buffer + len, "heap_size: %d, last_move: %s, ", h->heap_size, get_move_str(h->board._last_move, OPPONENT(h->board._next_player), buf));
  len += sprintf(buffer + len, "last_move_2: %s\n", get_move_str(h->board._last_move2, h->board._next_player, buf));
  for (int i = 1; i < heap_size; ++i) {
    len += heap_dump_one_to_buffer(h, i, NULL, buffer + len);
  }
  len += sprintf(buffer + len, "--- End HeapDump\n");
  return len;
}

static void heap_dump_one(const BoardExtra *h, int heap_idx, const char *prefix) {
  char buffer[5000];
  heap_dump_one_to_buffer(h, heap_idx, prefix, buffer);
  fprintf(stderr, buffer);
}

static void heap_dump(const BoardExtra *h, int heap_size) {
  char buffer[5000];
  heap_dump_to_buffer(h, heap_size, buffer);
  fprintf(stderr, buffer);
}

#define HEAP_DUMP(prefix, h, heap_idx) do { if (h->h->params.verbose >= PV_DEBUG) { heap_dump_one(h, heap_idx, prefix); fflush(stdout); } } while(0)

static inline void heap_swap(BoardExtra *h, int heap_idx1, int heap_idx2) {
  Coord m1 = h->moves_heap[heap_idx1];
  Coord m2 = h->moves_heap[heap_idx2];
  h->moves[m1].heap_idx = heap_idx2;
  h->moves[m2].heap_idx = heap_idx1;

  h->moves_heap[heap_idx1] = m2;
  h->moves_heap[heap_idx2] = m1;
}

/*
static void heap_dump_moves(const BoardExtra *h) {
  char buf[20];
  fprintf(stderr,"Heap Moves: \n");
  for (int i = 1; i < h->heap_size; ++i) {
    int idx = h->moves_heap[i];
    const char *mv_str = get_move_str(h->moves[idx], S_EMPTY, buf);
    fprintf(stderr,mv_str);
    if (i % 20 == 0) fprintf(stderr,"\n");
  }
  fprintf(stderr,"\n");
}
*/

#define HEAP_PROB(h, heap_idx) h->moves[h->moves_heap[(heap_idx)]].prob
#define HEAP_MOVE(h, heap_idx) &h->moves[h->moves_heap[(heap_idx)]]

// Check whether the heap is valid.
BOOL heap_check(const BoardExtra *h) {
  for (int i = 1; i < h->heap_size; ++i) {
     double curr_prob = HEAP_PROB(h, i);
     double child1 = (2*i < h->heap_size) ? HEAP_PROB(h, 2*i) : 0.0;
     double child2 = (2*i + 1 < h->heap_size) ? HEAP_PROB(h, 2*i + 1) : 0.0;
     if (curr_prob < child1 || curr_prob < child2) {
       fprintf(stderr,"Heap invalid! curr [%d/%lf] is smaller than child1 [%d/%lf] or child2 [%d/%lf]\n", i, curr_prob, 2*i, child1, 2*i+1, child2);
       heap_dump(h, -1);
       ShowBoard(&h->board, SHOW_ALL);
       return FALSE;
     }
  }
  return TRUE;
}

int heap_up(BoardExtra *h, int heap_idx) {
  // Child prob always stay the same.
  const double child_prob = HEAP_PROB(h, heap_idx);
  while (heap_idx > 1) {
    int parent = heap_idx >> 1;
    double parent_prob = HEAP_PROB(h, parent);

    if (child_prob <= parent_prob) break;
    // Swap the two element.
    heap_swap(h, heap_idx, parent);
    heap_idx = parent;
  }
  // heap_dump(h, -1);
  return heap_idx;
}

int heap_down(BoardExtra *h, int heap_idx, int heap_size) {
  // The parent prob never changed.
  double parent_prob = HEAP_PROB(h, heap_idx);
  while (1) {
    int child1 = heap_idx * 2;
    int child2 = child1 + 1;

    double c1 = -1e30, c2 = -1e30;
    if (child1 < heap_size) c1 = HEAP_PROB(h, child1);
    if (child2 < heap_size) c2 = HEAP_PROB(h, child2);

    if (parent_prob >= c1 && parent_prob >= c2) break;
    else if (parent_prob <= c2 && c1 <= c2) {
      // fprintf(stderr,"heap_size = %d, parent_prob = %lf[%d], c1 = %lf[%d], c2 = %lf[%d], pick c2\n", heap_size, parent_prob, heap_idx, c1, child1, c2, child2);
      heap_swap(h, heap_idx, child2);
      heap_idx = child2;
    } else {
      // fprintf(stderr,"heap_size = %d, parent_prob = %lf[%d], c1 = %lf[%d], c2 = %lf[%d], pick c1\n", heap_size, parent_prob, heap_idx, c1, child1, c2, child2);
      heap_swap(h, heap_idx, child1);
      heap_idx = child1;
    }
  }
  // heap_dump(h, -1);
  return heap_idx;
}

BOOL heap_check_neg_total_prob(const BoardExtra *h, const PatternMove *mv) {
  if (h->total_prob < -0.1) {
    fprintf(stderr,"Total prob cannot be negative! total_prob = %lf, mv->prob = %lf\n", h->total_prob, mv->prob);
    heap_dump_one(h, mv->heap_idx, "check_neg_total_prob");
    heap_dump(h, -1);
    return FALSE;
  } else return TRUE;
}

void heap_delete(BoardExtra *h, PatternMove *mv) {
  int heap_idx = mv->heap_idx;
  HEAP_DUMP("Delete:", h, heap_idx);

  // heap_dump_moves(h);

  if (heap_idx < 1 || heap_idx >= h->heap_size) {
    fprintf(stderr,"Delete index cannot be %d/%d!\n", heap_idx, h->heap_size);
    ShowBoard(&h->board, SHOW_LAST_MOVE);
    error("");
  }

  // fprintf(stderr,"Heap delete! heap_idx: %d/%d\n", heap_idx, h->heap_size);
  // Substracted from the influence.
  h->total_prob -= mv->prob;
  /*
  if (! heap_check_neg_total_prob(h, mv)) {
    fprintf(stderr,"In heap delete.\n");
    error("");
  }*/

  // Swap the node to be deleted with the last element, delete the last element and adjust the element accordingly.
  Coord m = mv->m;
  // Not the last element, need to swap.
  if (heap_idx < h->heap_size - 1) {
    // Swap the heap location with the last element.
    heap_swap(h, heap_idx, h->heap_size - 1);
    // up/down the entry heap_idx. If up doesnot change the location of the element, try down.
    // Here we already exclude the last element of the current heap in heap_down.
    // Othrewise it might be swapped back up.
    if (heap_up(h, heap_idx) == heap_idx)
      heap_down(h, heap_idx, h->heap_size - 1);
  }

  // Now we only need to delete the last element in the heap.
  h->heap_size --;

  // Mark the corresponding move as empty.
  h->moves[m].heap_idx = 0;

  /*
  if (h->h->params.verbose >= PV_DEBUG) {
    if (! PatternV2BoardExtraCheck(h)) error("");
    else fprintf(stderr,"heap_delete: All checks pass!\n");
  }
  */
}

void heap_add(BoardExtra *h, Coord c, double logprob) {
  // char buf[30];
  // PRINT_DEBUG(h->h, "Add move %s to heap! heap_size: %d, logprob: %lf\n", get_move_str(c, h->board._next_player, buf), h->heap_size, logprob);
  //
  if (h->h->params.verbose >= PV_DEBUG) {
    char buf[20];
    const char *mv_str = get_move_str(c, h->board._next_player, buf);
    fprintf(stderr,"Add move %s! heap_size: %d, logprob: %lf, prob: %lf, total_prob: %lf\n", mv_str, h->heap_size, logprob, EXP(logprob / h->h->T), h->total_prob);
  }
  // heap_dump_moves(h);

  if (h->heap_size == sizeof(h->moves_heap) / sizeof(Coord)) {
    // heap full, error. Dump the entire heap.
    fprintf(stderr,"Heap full!!\n");
    heap_dump(h, -1);
    error("");
  }

  PatternMove *move = &h->moves[c];
  if (move->heap_idx > 0) {
    char buf[20];
    const char *mv_str = get_move_str(c, h->board._next_player, buf);
    fprintf(stderr,"The move %s, is already added at heap_idx %d\n", mv_str, move->heap_idx);
    error("");
  }
  memset(move, 0, sizeof(PatternMove));

  h->moves_heap[h->heap_size ++] = c;

  move->heap_idx = h->heap_size - 1;
  move->logprob = logprob;
  move->prob = EXP(logprob / h->h->T);
  move->m = c;

  h->total_prob += move->prob;

  // Rearrange heap.
  heap_up(h, h->heap_size - 1);

  /*
  if (h->h->params.verbose >= PV_DEBUG) {
    if (! PatternV2BoardExtraCheck(h)) error("");
    else fprintf(stderr,"heap_add: All checks pass!\n");
  }
  */
}

void heap_recompute_prob(BoardExtra *h, PatternMove *move) {
   double old_prob = move->prob;
   double new_prob = EXP( (move->logprob + move->prior) / h->h->T );

   HEAP_DUMP("Recompute:", h, move->heap_idx);
   PRINT_DEBUG(h->h, "oldprob: %lf, newprob: %lf\n", old_prob, new_prob);

   h->total_prob -= old_prob - new_prob;
   move->prob = new_prob;

   /*
   if (! heap_check_neg_total_prob(h, move)) {
     fprintf(stderr,"oldprob: %lf, newprob: %lf\n", old_prob, new_prob);
     error("");
   }
   */

   if (old_prob < new_prob) {
     heap_up(h, move->heap_idx);
   } else {
     heap_down(h, move->heap_idx, h->heap_size);
   }
}

#define PLY_FRACTION 0.001

void simple_swap(Coord *heap, int idx1, int idx2) {
  Coord m = heap[idx1];
  heap[idx1] = heap[idx2];
  heap[idx2] = m;
}

void simple_heap_down(const BoardExtra *h, Coord *heap, int heap_size, int heap_idx) {
  // The parent prob never changed.
  double parent_prob = h->moves[heap[heap_idx]].prob;
  while (1) {
    int child1 = heap_idx * 2;
    int child2 = child1 + 1;

    double c1 = -1e30, c2 = -1e30;
    if (child1 < heap_size) c1 = h->moves[heap[child1]].prob;
    if (child2 < heap_size) c2 = h->moves[heap[child2]].prob;

    if (parent_prob >= c1 && parent_prob >= c2) break;
    else if (parent_prob <= c2 && c1 <= c2) {
      simple_swap(heap, child2, heap_idx);
      heap_idx = child2;
    } else {
      simple_swap(heap, child1, heap_idx);
      heap_idx = child1;
    }
  }
}

static BOOL get_log_prob(const BoardExtra *h, Coord c, double *logprob) {
  uint64_t v = MASK(h->hashes[c]);
  if (h->h->cnt_k2w_noresp[v] >= h->h->params.cnt_threshold) {
    if (logprob) *logprob = h->h->k2w_noresp[v] + h->h->pos_w[c] + h->board._ply * h->h->prior_w[h->h->prior_offset[T_PLY_POS_W]] * PLY_FRACTION;
    return TRUE;
  } else {
    if (logprob) *logprob = 0.0;
    return FALSE;
  }
}

static void show_hash_log_prob(const BoardExtra *h, Coord c) {
  uint64_t hash = h->hashes[c];
  uint64_t v = MASK(hash);
  int cnt = h->h->cnt_k2w_noresp[v];
  BOOL cnt_passed = cnt >= h->h->params.cnt_threshold;
  double logprob = h->h->k2w_noresp[v];
  double pos_logprob = h->h->pos_w[c];
  double move_logprob = h->board._ply * h->h->prior_w[h->h->prior_offset[T_PLY_POS_W]] * PLY_FRACTION;
  char buf[30];
  fprintf(stderr,"HashLog: Move: %s, hash: %lx, idx: %lx, cnt: %d [%s], logprob: %lf, pos_logprob: %lf, move_logprob: %lf\n",
      get_move_str(c, S_EMPTY, buf), hash, v, cnt, (cnt_passed ? "passed" : "not passed"), logprob, pos_logprob, move_logprob);
}

void heap_update(BoardExtra *h, Coord c) {
  const Handle *hh = h->h;
  PatternMove *move = &h->moves[c];

  // Do not add locations that is not valid.
  GroupId4 ids;
  if (c == M_PASS || c == M_RESIGN || ! TryPlay2(&h->board, c, &ids)) {
    // Not a valid move any more, remove the heap idx.
    if (move->heap_idx > 0) heap_delete(h, move);
    return;
  }

  double logprob;
  BOOL cnt_passed = get_log_prob(h, c, &logprob);

  if (h->h->params.verbose >= PV_DEBUG) {
    char buf[20];
    fprintf(stderr,"heap_update: Move: %s, logprob: %lf, cnt_passed: %s, idx: %d/%d\n",
        get_move_str(c, h->board._next_player, buf), logprob, (cnt_passed ? "true" : "false"), move->heap_idx, h->heap_size);
    show_hash_log_prob(h, c);
  }

  // Four cases.
  if (move->heap_idx > 0) {
    if (! cnt_passed && move->prior_count == 0) {
      heap_delete(h, move);
    } else if (move->logprob != logprob) {
      // Update the move.
      move->logprob = logprob;
      heap_recompute_prob(h, move);
    }
  } else if (cnt_passed && move->heap_idx == 0) {
    // New move, add to heap.
    // fprintf(stderr,"Update move %s! heap_size: %d, influence: %lf\n", get_move_str(c, h->board._next_player, buf), h->heap_size, logprob);
    heap_add(h, c, logprob);
  }
}

inline static double get_prior(const Handle *h, int w_type, int w_offset) {
  if (w_type < 0 || w_type >= WT_TOTAL) {
    fprintf(stderr,"w_type [%d] is out of bound [%d]\n", w_type, WT_TOTAL);
    error("");
  }
  return h->weights[w_type][w_offset];
}

inline static void add_gradient(HandleGradient *grads, int w_type, int w_offset, double delta) {
  if (w_type < 0 || w_type >= WT_TOTAL) {
    fprintf(stderr,"w_type [%d] is out of bound [%d]\n", w_type, WT_TOTAL);
    error("");
  }

  if (RepCheckListAdd(grads->checks[w_type], w_offset) == KEY_OOB) {
    fprintf(stderr,"Key %d is out of bound!\n", w_offset);
    error("");
  }
  // Record that the gradient of w_type at w_offset has been changed.
  grads->grads[w_type][w_offset] += delta;
  return;
}

//====================== Utility for prior.
BOOL heap_add_prior(BoardExtra *h, Coord c, int w_type, int w_offset, BOOL create_new) {
  // Do not deal with the locations that is not empty
  if (h->board._infos[c].color != S_EMPTY) return FALSE;

  double prior = get_prior(h->h, w_type, w_offset);

  if (h->h->params.verbose >= PV_DEBUG) {
    char buf[30];
    fprintf(stderr,"Change prior for %s! heap_size: %d, prior: %lf, create_new: %s\n",
        get_move_str(c, h->board._next_player, buf), h->heap_size, prior, (create_new ? "true" : "false"));
  }

  int heap_idx = h->moves[c].heap_idx;
  // No move and return.
  // Note this idx points to h->moves but starts from 1.
  if (heap_idx == 0) {
    if (create_new) {
      double logprob = 0.0;
      // logprob remains 0.0 if cnt does not pass the test.
      get_log_prob(h, c, &logprob);

      heap_add(h, c, logprob);
      h->moves[c].added_by_prior = TRUE;
      heap_idx = h->moves[c].heap_idx;
    } else {
      return FALSE;
    }
  }

  PriorMove *pmv = &h->prior_moves[h->num_prior_moves ++];
  pmv->m = c;
  pmv->prior = prior;
  pmv->w_type = w_type;
  pmv->w_offset = w_offset;

  PatternMove *mv = &h->moves[c];
  mv->prior += prior;
  mv->prior_count ++;

  heap_recompute_prob(h, mv);

  return TRUE;
}

// ---------------------------
// Load the pattern file and their weights (if pattern file is not NULL).
void PatternV2DefaultParams(PatternV2Params *params) {
  params->verbose = 0;
  params->cnt_threshold = 1;
  params->learning_rate = 0.001;
  params->prior_nakade = TRUE;
  params->prior_neighbor = TRUE;
  params->prior_resp = TRUE;
  params->prior_save_atari = TRUE;
  params->prior_global = FALSE;
  params->batch_size = 128;
  params->prior_ko = TRUE;
  params->prior_put_group_to_atari = TRUE;
#ifdef USE_EYE
  params->prior_eye = TRUE;
#else
  params->prior_eye = FALSE;
#endif
}

void *InitPatternV2(const char *pattern_file, const PatternV2Params *params, BOOL init_empty_if_load_failed) {
  Handle *h = (Handle *)malloc(sizeof(Handle));
  // Initialize the Zobrist hash number.
  uint64_t pmseed = 15213;
  for (int c = 0; c < NEIGHBOR_COUNT; ++c) {
    for (int i = 0; i < 16; ++i) {
      h->hs[c][i] = fast_random64(&pmseed);
    }

    // For empty location, its hash is always 0.
    h->hs[c][HASH_EMPTY_LOCAL_IDX] = 0;
  }

  if (params != NULL) {
    h->params = *params;
  }
  else {
    fprintf(stderr,"Params is NULL, set default parameters.\n");
    PatternV2DefaultParams(&h->params);
  }

  // Initialize prior.
  h->prior_offset[0] = 0;
  for (int i = 1; i < NUM_PRIOR; ++i) {
    h->prior_offset[i] = h->prior_offset[i - 1] + g_priors[i - 1].size;
    for (int j = h->prior_offset[i - 1]; j < h->prior_offset[i]; ++j) {
      h->prior_type[j] = i - 1;
    }
  }
  for (int j = h->prior_offset[NUM_PRIOR - 1]; j < LEN_PRIOR; ++j) {
    h->prior_type[j] = NUM_PRIOR - 1;
  }

  // This needs to be set manually.
  h->weights[WT_RESP] = h->k2w_resp;
  h->weights[WT_NORESP] = h->k2w_noresp;
  h->weights[WT_POS] = h->pos_w;
  h->weights[WT_PRIOR] = h->prior_w;

  h->filter = NULL;
  h->collision = 0;
  h->T = 1.0;
  if (! LoadPatternV2(h, pattern_file)) {
    if (! init_empty_if_load_failed) error("Load file %s failed, aborting...\n", pattern_file);
    // Initialize the bloom filter (m = 2G, k = 14)
    h->filter = bloom_init(31, 14);
    /*
    for (uint64_t i = 0; i < HASH_SIZE; ++i) {
      h->k2w_resp[i] = 1.0;
      h->k2w_noresp[i] = 1.0;
    }
    */
    memset(h->k2w_noresp, 0, sizeof(h->k2w_noresp));
    memset(h->k2w_resp, 0, sizeof(h->k2w_resp));
    memset(h->prior_w, 0, sizeof(h->prior_w));
    // Positional weights.
    memset(h->pos_w, 0, sizeof(h->pos_w));

    h->num_pattern = 0;
  } else {
    fprintf(stderr,"Pattern file %s loaded!\n", pattern_file);
  }
  return h;
}

void PatternV2SetSampleParams(void *ctx, int topn, double T) {
  Handle *h = (Handle *)ctx;
  h->params.sample_from_topn = topn;
  h->T = T;
}

void PatternV2SetVerbose(void *ctx, int verbose) {
  Handle *h = (Handle *)ctx;
  h->params.verbose = verbose;
}

void *PatternV2InitGradients() {
  HandleGradient *grad = (HandleGradient *)malloc(sizeof(HandleGradient));
  memset(grad, 0, sizeof(HandleGradient));
  // Gradient. Also initialize a few extra structures.
  for (int i = 0; i < WT_TOTAL; ++i) {
    int s = g_w_sizes[i];
    grad->checks[i] = InitRepCheckList(s, s);
    grad->grads[i] = (double *)malloc(sizeof(double) * s);
    memset(grad->grads[i], 0, sizeof(double) * s);
  }
  return grad;
}

const PatternV2Params *PatternV2GetParams(void *ctx) {
  Handle *h = (Handle *)ctx;
  return &h->params;
}

void PatternV2UpdateParams(void *ctx, const PatternV2Params *params) {
  Handle *h = (Handle *)ctx;
  h->params = *params;
}

static inline void hash_12d_influence_flip(Coord c, int local_idx, BoardExtra *board_extra) {
  // Flip the move at c (add/remove it), and make an influence around it.
  const Handle *h = board_extra->h;
  D12(c, idx, cc, -) {
    // We always set hs[idx][HASH_EMPTY_LOCAL_IDX] = 0, so no need to do an XOR here.
    board_extra->hashes[cc] ^= h->hs[idx][local_idx];
    // char buf[30];
    if (! board_extra->changed_hashes_map[cc]) {
      // PRINT_DEBUG(board_extra->h, "Add hash influence at %s, #changed hash = %d\n", get_move_str(cc, board_extra->board._next_player, buf), board_extra->num_changed_hashed);
      board_extra->changed_hashes[board_extra->num_changed_hashed ++] = cc;
      board_extra->changed_hashes_map[cc] = TRUE;
    }
    // else {
      // PRINT_DEBUG(board_extra->h, "Hash influence at %s is already recorded. #changed hash = %d\n", get_move_str(cc, board_extra->board._next_player, buf), board_extra->num_changed_hashed);
    // }
  } ENDD12
}

static uint64_t get_12d_hash(const BoardExtra *board_extra, Coord c) {
  const Handle *h = board_extra->h;
  const Board *b = &board_extra->board;

  /*
  char buf[30];
  fprintf(stderr,"Get12dhash at %s\n", get_move_str(c, S_EMPTY, buf));
  */

  uint64_t v = 0;
  D12(c, idx, cc, +) {
    int local_idx = get_hash_local_index(b, cc);
    // fprintf(stderr,"idx = %d, local_idx = %d, hash = %lx\n", idx, local_idx, h->hs[idx][local_idx]);
    v ^= h->hs[idx][local_idx];
  } ENDD12

  // fprintf(stderr,"Get12dhash, final hash %lx\n", v);
  return v;
}

// Set current board. The board will be copied into the context.
void *PatternV2InitBoardExtra(void *hh, const Board *board) {
  const Handle *h = (Handle *)hh;
  BoardExtra *be = (BoardExtra *)malloc(sizeof(BoardExtra));

  Board *b = &be->board;
  be->h = h;
  if (board == NULL) {
    ClearBoard(b);
  } else {
    CopyBoard(b, board);
  }
  // Setup the board extra.
  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);
      be->hashes[c] = get_12d_hash(be, c);
    }
  }
  // Setup the related data structure.
  // Slot[0] is not used.
  be->heap_size = 1;
  be->total_prob = 0.0;
  be->total_prob_before_prior = 0.0;
  be->prior_status = PRIOR_STATUS_NOT_SET;
  be->num_changed_hashed = 0;
  memset(be->changed_hashes_map, 0, sizeof(be->changed_hashes_map));
  memset(be->moves, 0, sizeof(be->moves));
  memset(be->moves_heap, 0, sizeof(be->moves_heap));

  // Initialize modified moves with priors.
  memset(be->prior_moves, 0, sizeof(be->prior_moves));
  be->num_prior_moves = 0;

  // memset(be->atari_moves, 0, sizeof(be->atari_moves));
  // be->num_atari_moves = 0;
  //
  // Initialized changed group ids.
  be->changed_ids = InitRepCheckList(MAX_GROUP, MAX_GROUP);

  // Add the move one by one.
  be->empty_list = InitRepCheckList(BOUND_COORD, BOUND_COORD);
  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);
      if (board->_infos[c].color == S_EMPTY) {
        RepCheckListAdd(be->empty_list, c);
        heap_update(be, c);
      }
    }
  }
  return be;
}

void PatternV2DestroyBoardExtra(void *board_extra) {
  BoardExtra *be = (BoardExtra *)board_extra;
  DestroyRepCheckList(be->changed_ids);
  DestroyRepCheckList(be->empty_list);
  free(be);
}

void PatternV2BoardExtraPrintStats(void *board_extra) {
  BoardExtra *be = (BoardExtra *)board_extra;
  fprintf(stderr,"---- Board Extra -----\n");
  fprintf(stderr,"#heap_size: %d, #empty: %d\n", be->heap_size, be->empty_list->n);
  ShowBoard(&be->board, SHOW_LAST_MOVE);
  fprintf(stderr,"Total prob: %lf\n", be->total_prob);
  fprintf(stderr,"-- End Board Extra ---\n");
}

void PatternV2RecomputeZ(void *be2) {
  BoardExtra *be = (BoardExtra *)be2;

  be->total_prob = 0.0;
  for (int i = 1; i < be->heap_size; ++i) {
    Coord m = be->moves_heap[i];
    PatternMove *mv = &be->moves[m];

    // Then we update it.
    be->total_prob += mv->prob;
  }
}

void PatternV2UpdateAllScores(void *be2) {
  BoardExtra *be = (BoardExtra *)be2;
  const Board *b = &be->board;

  char buf[30];
  PRINT_DEBUG(be->h, "Update weights in heap. heap_size: %d\n", be->heap_size);
  // heap_dump_moves(be);
  // Since heap order will change, we need to loop through the location.
  for (int m = 0; m < BOUND_COORD; ++m) {
    PatternMove *mv = &be->moves[m];
    if (mv->heap_idx == 0) continue;

    // Then we update it.
    // Load the logprob and update.
    if (! get_log_prob(be, m, &mv->logprob)) {
      error("UpdateWeight: move [%s] cannot be invalid\n", get_move_str(m, b->_next_player, buf));
    }

    heap_recompute_prob(be, mv);
  }

  PatternV2RecomputeZ(be);
}

BOOL PatternV2BoardExtraCheck(void *board_extra) {
  BoardExtra *be = (BoardExtra *)board_extra;
  const Board *b = &be->board;
  const Handle *h = be->h;
  char buf[100];

  // Check if the current hashes are the same as if everything is loaded from scratch.
  // Setup the board extra.
  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);

      uint64_t recomputed = get_12d_hash(be, c);
      if (be->hashes[c] != recomputed) {
        ShowBoard(b, SHOW_LAST_MOVE);
        // Something wrong.
        fprintf(stderr,"At %s: recomputed hash [%lx] is different from stored one [%lx]\n",
            get_move_str(c, S_EMPTY, buf), recomputed, be->hashes[c]);
        return FALSE;
      }
    }
  }

  if (be->heap_size < 1 || be->heap_size > BOARD_SIZE * BOARD_SIZE + 1) {
    fprintf(stderr,"Invalid heap size: %d\n", be->heap_size);
    return FALSE;
  }

  if (! heap_check(be)) return FALSE;

  int move_loc2[BOUND_COORD];
  double total_prob = 0.0;
  double total_prob_d = 0.0;

  double total_prob_sum2_saved = 0.0, total_prob_sum2_recompute = 0.0;
  double total_prob_sum2_saved_d = 0.0, total_prob_sum2_recompute_d = 0.0;
  int counted_board = 0;
  memset(move_loc2, 0, sizeof(move_loc2));

  for (int m = 0; m < BOUND_COORD; ++m) {
    const PatternMove *mv = &be->moves[m];

    total_prob_sum2_saved += mv->prob;
    total_prob_sum2_recompute += EXP((mv->logprob + mv->prior) / h->T);

    total_prob_sum2_saved_d += mv->prob;
    total_prob_sum2_recompute_d += EXP((mv->logprob + mv->prior) / h->T);

    move_loc2[m] = mv->heap_idx;
    counted_board ++;
  }

  // Check if heap is working properly.
  for (int i = 1; i < be->heap_size; ++i) {
    Coord m = be->moves_heap[i];
    const PatternMove *mv = &be->moves[m];
    const char *move_str = get_move_str(m, b->_next_player, buf);

    if (m == M_PASS || m == M_RESIGN) {
      fprintf(stderr,"Move at heap_idx %d in heap cannot be %s!\n", i, move_str);
      heap_dump((const BoardExtra *)board_extra, -1);
      return FALSE;
    }
    if (mv->heap_idx != i) {
      Coord m2 = be->moves_heap[mv->heap_idx];
      const PatternMove *mv2 = &be->moves[m2];
      fprintf(stderr,"Move %s in idx %d/%d, ", move_str, i, be->heap_size);
      fprintf(stderr,"but mv->heap_idx = %d", mv->heap_idx);
      if (mv->heap_idx > 0) {
        fprintf(stderr,", whose move is %s\n", get_move_str(m2, b->_next_player, buf));
      }
      return FALSE;
    }
    // Make it count.
    // If negative count, then
    if (move_loc2[m] < 0) {
      fprintf(stderr,"The same move [%s] was encoded more than two times. One in heap_idx %d/%d, the previous one is heap_idx %d/%d\n",
          move_str, i, be->heap_size, -move_loc2[m], be->heap_size);
    } else {
      // Meet the encoding of this move for the first time.
      move_loc2[m] = -move_loc2[m];
    }

    // Check if the heap has a move, then their key in the table has positive counts.
    double logprob = 0.0;
    if (! get_log_prob(be, m, &logprob)) {
      fprintf(stderr,"Hash count of move [%s] is below threshold [%d], while it is in the heap at %d/%d\n", move_str, h->params.cnt_threshold, i, be->heap_size);
      heap_dump(be, -1);
      ShowBoard(&be->board, SHOW_ALL);
      return FALSE;
    }
    double prob = EXP( (logprob + mv->prior) / h->T );
    if (prob != mv->prob) {
      fprintf(stderr,"Move %s at %d/%d: the prob computed [%lf] != the recorded prob [%lf], logprob (from dict) = %lf, logprob = %lf, prior = %lf\n",
          move_str, i, be->heap_size, prob, mv->prob, logprob, mv->logprob, mv->prior);
      return FALSE;
    }

    total_prob += prob;
    total_prob_d += prob;
  }
  double relative_err = fabs(total_prob - be->total_prob) / (fabs(total_prob) + 1e-3);
  if (be->heap_size > 1 && relative_err > 1e-3) {
    fprintf(stderr,"The total_prob [%lf] is not the same as recorded [%lf]!\n", total_prob, be->total_prob);
    fprintf(stderr,"total_prob_sum2_saved: %lf, total_prob_sum2_recompute: %lf, counted_board: %d, heap_size: %d\n", total_prob_sum2_saved, total_prob_sum2_recompute, counted_board, be->heap_size);
    fprintf(stderr,"total_prob_sum2_saved_d: %lf, total_prob_sum2_recompute_d: %lf\n", total_prob_sum2_saved_d, total_prob_sum2_recompute_d);
    fprintf(stderr,"total_prob_d: %lf\n", total_prob_d);
    heap_dump(be, -1);
    ShowBoard(&be->board, SHOW_ALL);
    return FALSE;
    // return TRUE;
  }

  // Check if other than the location that heap mentioned, other locations are 0.
  for (int m = 0; m < BOUND_COORD; ++m) {
    if (move_loc2[m] > 0) {
      fprintf(stderr,"Move %s is claimed to have idx %d, but the heap/moves has no such information.\n", get_move_str(m, b->_next_player, buf), move_loc2[m]);
      return FALSE;
    }
  }
  fprintf(stderr,"Total prob: %lf\n", be->total_prob);
  return TRUE;
}

BOOL PatternV2PlayMove(void *board_extra, Coord m, Stone player) {
  BoardExtra *be = (BoardExtra *)board_extra;
  GroupId4 ids;
  if (! TryPlay(&be->board, X(m), Y(m), player, &ids)) return FALSE;
  PatternV2PlayMove2(board_extra, &ids);
  return TRUE;
}

static inline void flip_hash(BoardExtra *be, short id) {
  const Board *b = &be->board;
  // If the liberties changes. Update along the group.
  TRAVERSE(b, id, c) {
    int local_idx = get_hash_local_index(b, c);
    hash_12d_influence_flip(c, local_idx, be);
  } ENDTRAVERSE
}

// Play the move to get the next state.
void PatternV2PlayMove2(void *board_extra, const GroupId4 *ids) {
  BoardExtra *be = (BoardExtra *)board_extra;
  const Handle *h = be->h;

  // if (! RepCheckListCheck(be->empty_list)) error("");
  const Board *b = &be->board;
  char buf[30];

  Stone opponent = OPPONENT(ids->player);
  // From the ids, we check the group that to be killed, and adjust the hash accordingly.
  for (int i = 0; i < 4; ++i) {
    // No suicide.
    if (ids->ids[i] == 0) continue;
    if (ids->colors[i] == opponent && ids->group_liberties[i] == 1) {
      // The enemy stone is dead after this move is played. Remove all stones for this enemy group.
      // Remove their hashes.
      flip_hash((BoardExtra *)board_extra, ids->ids[i]);

      // Add its neighbor group and add it to the empty group.
      TRAVERSE(b, ids->ids[i], cc) {
        FOR4(cc, _, ccc) {
          if (b->_infos[ccc].color == ids->player) {
            RepCheckListAdd(be->changed_ids, b->_infos[ccc].id);
          }
        } ENDFOR4
        // Add move to the empty move list.
        // fprintf(stderr,"Add empty move %s\n", get_move_str(cc, b->_next_player, buf));
        RepCheckListAdd(be->empty_list, cc);
        // if (! RepCheckListCheck(be->empty_list)) error("");
      } ENDTRAVERSE
    } else {
      // Add the id to the group.
      RepCheckListAdd(be->changed_ids, ids->ids[i]);
    }
  }

  // Clear the affected ids before we take the move.
  for (int i = 0; i < RepCheckListSize(be->changed_ids); ++i) {
    int id = RepCheckListEnumerate(be->changed_ids, i);
    flip_hash(be, id);
  }

  // if (! RepCheckListCheck(be->empty_list)) error("");

  Coord m = ids->c;

  // Actually play the move.
  PRINT_DEBUG(h, "PlayMove: %s, #empty: %d\n", get_move_str(m, b->_next_player, buf), be->empty_list->n);
  Play(&be->board, ids);

  // Recompute the affected ids after we take the move.
  // Note that the ids might change so we need to use BoardIdOld2New in be->board.
  for (int i = 0; i < RepCheckListSize(be->changed_ids); ++i) {
    unsigned char id = (unsigned char)RepCheckListEnumerate(be->changed_ids, i);
    id = BoardIdOld2New(b, id);
    // If the id is not deleted, flip its hash.
    if (id > 0) flip_hash(be, id);
  }

  RepCheckListClear(be->changed_ids);

  // Finally update the hash if the new move is a stand-alone group
  // Only in this case a new id will be created.
  if (b->_groups[b->_infos[m].id].stones == 1) {
    int local_idx = get_hash_local_index(b, m);
    hash_12d_influence_flip(m, local_idx, be);
  }
  // Remove the play from the empty list.
  RepCheckListRemove(be->empty_list, m);
  // if (! RepCheckListCheck(be->empty_list)) error("");

  // Delete the move we just take, if the move is in the candidate.
  if (be->moves[m].heap_idx > 0) heap_delete(be, &be->moves[m]);

  // Then we check all the changed hashes, and update the heap accordingly.
  PRINT_DEBUG(h, "#Changed hashes = %d\n", be->num_changed_hashed);
  for (int i = 0; i < be->num_changed_hashed; ++i) {
    Coord c = be->changed_hashes[i];
    PRINT_DEBUG(h, "[%d] Check move = %s\n", i, get_move_str(c, b->_next_player, buf));
    heap_update(be, c);
    be->changed_hashes_map[c] = FALSE;
    be->changed_hashes[i] = M_PASS;
  }

  be->num_changed_hashed = 0;
  PRINT_DEBUG(h, "PlayMove done!\n");
}

// Harvest pattern for the current location.
// The location could be b->_last_move, or could be the move that is to be make.
BOOL PatternV2Harvest(void *hh, void *be, Coord c) {
  Handle *h = (Handle *)hh;
  BoardExtra *board_extra = (BoardExtra *) be;

  const Board *b = &board_extra->board;
  if (h->filter == NULL || c == M_PASS || c == M_RESIGN) return FALSE;

  // The new pattern in hashed format.
  uint64_t v = board_extra->hashes[c];
  if (bloom_check(h->filter, v, TRUE)) {
    // If the pattern is already in the bloom filter, then we add it to the pattern library.
    int idx = MASK(v);
    if (h->cnt_k2w_noresp[idx] != 0) {
      h->collision ++;
    } else {
      h->num_pattern ++;
    }
    // Accumulate
    h->cnt_k2w_noresp[idx] ++;
    h->cnt_k2w_resp[idx] ++;
    return TRUE;
  }
  return FALSE;
}

void PatternV2HarvestMany(void *h, void *board_extra, const AllMovesExt *all_moves) {
  BoardExtra *be = (BoardExtra *)board_extra;

  // char buf[30];
  // fprintf(stderr,"HarvestMany, #moves: %d\n", all_moves->num_moves);
  for (int i = 0; i < all_moves->num_moves; ++i) {
    Coord m = all_moves->moves[i].m;
    Stone player = all_moves->moves[i].player;
    // fprintf(stderr,"Move %s\n", get_move_str(m, be->board._next_player, buf));
    PatternV2Harvest(h, board_extra, m);
    PatternV2PlayMove(board_extra, m, player);
    PatternV2Harvest(h, board_extra, m);
    if (be->h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(be)) error("");
      else fprintf(stderr,"All checks pass!\n");
    }
  }
}

void PatternV2PrintStats(void *hh) {
  Handle *h = (Handle *)hh;
  fprintf(stderr,"---- PatternV2 -----\n");
  fprintf(stderr,"#hash_size: %" PRIu64 ", NUM_PRIOR: %d, LEN_PRIOR: %d\n", (uint64_t)HASH_SIZE, (int)NUM_PRIOR, (int)LEN_PRIOR);
  fprintf(stderr,"Verbose: %d, cnt_threshold: %d, alpha: %lf, batch_size: %d, temperature: %lf, ply_fraction: %lf\n",
      h->params.verbose, h->params.cnt_threshold, h->params.learning_rate, h->params.batch_size, h->T, PLY_FRACTION);
  fprintf(stderr,"neighbor: %s, nakade: %s, resp: %s, save_atari: %s, kill_other: %s, global: %s, ko: %s, put_group_to_atari: %s, eye: %s\n",
      (h->params.prior_neighbor ? "true" : "false"),
      (h->params.prior_nakade ? "true" : "false"),
      (h->params.prior_resp ? "true" : "false"),
      (h->params.prior_save_atari ? "true" : "false"),
      (h->params.prior_kill_other ? "true" : "false"),
      (h->params.prior_global ? "true" : "false"),
      (h->params.prior_ko ? "true" : "false"),
      (h->params.prior_put_group_to_atari ? "true" : "false"),
      (h->params.prior_eye ? "true" : "false")
  );

  fprintf(stderr,"#Pattern: %" PRIu64 ", collision: %" PRIu64 "\n", h->num_pattern, h->collision);
  fprintf(stderr,"Sample from topn: %d\n", h->params.sample_from_topn);
  if (h->filter != NULL) {
    fprintf(stderr,"mbit: %d, k: %d\n", h->filter->mbit, h->filter->k);
    fprintf(stderr,"#query: %" PRIu64 ", #found: %" PRIu64 "\n", h->filter->num_queries, h->filter->num_found);
  }
  fprintf(stderr,"-- End Patternv2 ---\n");
}

void InitPerfSummary(PerfSummary *perf_summary) {
  assert(perf_summary);
  memset(perf_summary, 0, sizeof(PerfSummary));
}

void CombinePerfSummary(PerfSummary *dst, const PerfSummary *src) {
  assert(dst);
  assert(src);

  dst->n_all_moves += src->n_all_moves;
  dst->n_games += src->n_games;
  dst->n_pg_iterations += src->n_pg_iterations;
  dst->n_selected_moves += src->n_selected_moves;
  dst->sum_loglikelihood += src->sum_loglikelihood;
  dst->sum_result_correct += src->sum_result_correct;
  dst->sum_top1 += src->sum_top1;
  dst->total_duration += src->total_duration;
  dst->n_recompute_Z += src->n_recompute_Z;
}

void PrintPerfSummary(const PerfSummary *summary) {
  assert(summary);

  float n_selected = summary->n_selected_moves + 1e-6;
  float n_all_moves = summary->n_all_moves + 1e-6;
  float n_pg_iterations = summary->n_pg_iterations + 1e-6;
  float n_games = summary->n_games + 1e-6;

  float ratio_selected = ((float) n_selected) / n_all_moves;
  float aver_loglikelihood = ((float) summary->sum_loglikelihood) / n_selected;
  float aver_top1_all = ((float) summary->sum_top1) / n_all_moves;
  float aver_top1_selected = ((float) summary->sum_top1) / n_selected;
  float playout_accuracy = ((float) summary->sum_result_correct) / n_pg_iterations;

  double per_game = summary->total_duration / n_games;
  double per_move = summary->total_duration / n_selected;

  fprintf(stderr,"PerfSummary %s: #game: %d, #positions: %.2f%% (%d/%d), aver likelihood: %f, aver top1 in selection: %.2f%%, overall top1: %.2f%%, playout accuracy: %.2f%%, #recompute_Z: %d, per_game: %lf usec, per_move: %lf usec\n",
      summary->name, summary->n_games, ratio_selected * 100, summary->n_selected_moves, summary->n_all_moves,
      aver_loglikelihood, aver_top1_selected * 100, aver_top1_all * 100, playout_accuracy * 100, summary->n_recompute_Z, per_game * 1e6, per_move * 1e6);
}

void InitSampleSummary(SampleSummary *sample_summary) {
  memset(sample_summary, 0, sizeof(SampleSummary));
}

void CombineSampleSummary(SampleSummary *dst, const SampleSummary *src) {
  for (int i = 0; i < NUM_STATS_TOPN; ++i) {
    dst->num_topn[i] += src->num_topn[i];
    dst->num_counters[i] += src->num_counters[i];
  }

  dst->n += src->n;
  if (dst->max_counter < src->max_counter) dst->max_counter = src->max_counter;
  dst->total_duration += src->total_duration;
  dst->n_recompute_Z += src->n_recompute_Z;
}

void PrintSampleSummary(const SampleSummary *summary) {
  assert(summary);
  int n = summary->n;
  double per_sample = summary->total_duration / (n + 1e-6);
  fprintf(stderr,"SampleSummary %s: random = %d/%d, top1 = %d/%d, top2 = %d/%d, top3 = %d/%d, counter = %d %d %d %d %d, max = %d, #recompute_Z = %d, per_sample: %lf usec\n",
        summary->name,
        summary->num_topn[0], n, summary->num_topn[1], n, summary->num_topn[2], n, summary->num_topn[3], n,
        summary->num_counters[1], summary->num_counters[2], summary->num_counters[3],
        summary->num_counters[4], summary->num_counters[5], summary->max_counter, summary->n_recompute_Z, per_sample * 1e6);
}

BOOL add_resp_prior(BoardExtra *board_extra, Coord last) {
  const Handle *h = board_extra->h;
  uint64_t idx = MASK(board_extra->hashes[last]);
  // This pattern is not harvested.
  if (h->cnt_k2w_resp[idx] < h->params.cnt_threshold) return FALSE;

  // Check its local neighbor move. if there is any, put prior there.
  D12(last, _, cc, +) {
    // Add prior. Don't create any new move.
    heap_add_prior(board_extra, cc, WT_RESP, idx, FALSE);
  } ENDD12
  return TRUE;
}

BOOL add_global_prior(BoardExtra *board_extra) {
  // Search possible moves that will cause self-atari/atari others.
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;
  GroupId4 ids;

  int offset_global_extend = h->prior_offset[T_GLOBAL_EXTEND];
  int offset_global_kill = h->prior_offset[T_GLOBAL_KILL];
  int offset_global_self_atari = h->prior_offset[T_GLOBAL_SELF_ATARI];
  int offset_global_atari = h->prior_offset[T_GLOBAL_ATARI];

  for (int i = 1; i < b->_num_groups; ++i) {
    int lib = b->_groups[i].liberties;
    if (lib > 2) continue;

    BOOL our_group = b->_groups[i].color == b->_next_player;
    // Four cases.
    if (lib == 1) {
      Coord m;
      find_only_liberty(b, i, &m);
      if (! TryPlay2(b, m, &ids)) continue;

      if (our_group) {
        // Extend if not self atari.
        if (! IsSelfAtari(b, &ids, ids.c, ids.player, NULL)) {
          heap_add_prior(board_extra, m, WT_PRIOR, offset_global_extend, TRUE);
        }
      } else {
        // Kill move.
        heap_add_prior(board_extra, m, WT_PRIOR, offset_global_kill, TRUE);
      }
    } else if (lib == 2) {
      // 2 libs.
      Coord m[2];
      find_two_liberties(b, i, m);
      int w_idx = our_group ? offset_global_self_atari : offset_global_atari;

      for (int k = 0; k < 2; ++k) {
        if (TryPlay2(b, m[k], &ids)) {
          heap_add_prior(board_extra, m[k], WT_PRIOR, w_idx, TRUE);
        }
      }
    }
  }
  return TRUE;
}

BOOL add_neighbor_prior(BoardExtra *board_extra, Coord last) {
  // Check its local neighbor move. if there is any, put prior there.
  const Handle *h = board_extra->h;
  int offset = h->prior_offset[T_NEIGHBOR];
  FOR8(last, _, cc) {
    // Add the prior move if necessary.
    heap_add_prior(board_extra, cc, WT_PRIOR, offset, TRUE);
    offset ++;
  } ENDFOR8
  return TRUE;
}

// Get Nakade point, refactored from Pachi: pachi/tactics/nakade.c
// The goal is to find the nakade point to kill the opponent in the next move.
// [TODO]: We also need to enforce our own nakade point.
Coord nakade_point_v2(const Board *board, Coord loc, int *type) {
  /* First, examine the nakade area. For sure, it must be at most
   * six points. And it must be within color group(s). */
#define NAKADE_MAX 6
	Coord area[NAKADE_MAX]; int area_n = 0;

	area[area_n++] = loc;

  // Simple flood fill to find the region.
  // fprintf(stderr,"Flood fill...\n");
	for (int i = 0; i < area_n; i++) {
    FOR4(area[i], _, c) {
      // If that point is surrounding by our stone, return immediately.
			if (board->_infos[c].color == board->_next_player) return M_PASS;
			if (board->_infos[c].color != S_EMPTY) continue;
      BOOL dup = FALSE;
      for (int j = 0; j < area_n; j++)
        if (c == area[j]) {
          dup = TRUE;
          break;
        }
      if (dup) continue;

      if (area_n >= NAKADE_MAX) {
        /* Too large nakade area. */
        return M_PASS;
      }
      area[area_n++] = c;
		} ENDFOR4
	}

	/* We also collect adjecency information - how many neighbors
	 * we have for each area point, and histogram of this. This helps
	 * us verify the appropriate bulkiness of the shape. */
  // Compute a few statistics.
  // fprintf(stderr,"Compute a few statistics...\n");
	int neighbors[area_n]; int ptbynei[9] = {area_n, 0};
	memset(neighbors, 0, sizeof(neighbors));
	for (int i = 0; i < area_n; i++) {
		for (int j = i + 1; j < area_n; j++)
			if (NEIGHBOR4(area[i], area[j])) {
				ptbynei[neighbors[i]]--;
				neighbors[i]++;
				ptbynei[neighbors[i]]++;
				ptbynei[neighbors[j]]--;
				neighbors[j]++;
				ptbynei[neighbors[j]]++;
			}
	}

	/* For each given neighbor count, arbitrary one coordinate
	 * featuring that. */

  // fprintf(stderr,"Anchor coordinate...\n");
	Coord coordbynei[9];
	for (int i = 0; i < area_n; i++)
		coordbynei[neighbors[i]] = area[i];

  // fprintf(stderr,"Determine the type\n");
  *type = area_n;
	switch (area_n) {
		case 1: return M_PASS;
		case 2: return M_PASS;
		case 3: // assert(ptbynei[2] == 1);
			return coordbynei[2]; // middle point
		case 4: if (ptbynei[3] != 1) return M_PASS; // long line
			return coordbynei[3]; // tetris four
		case 5: if (ptbynei[3] == 1 && ptbynei[1] == 1) return coordbynei[3]; // bulky five
			if (ptbynei[4] == 1) return coordbynei[4]; // cross five
			return M_PASS; // long line
		case 6: if (ptbynei[4] == 1 && ptbynei[2] == 3)
				return coordbynei[4]; // rabbity six
			return M_PASS; // anything else
	}

  fprintf(stderr,"This should never happen!");
  return M_PASS;
}

/* Check nakade point v3 */
Coord nakade_point_v3(const Board *board, Coord loc, int *size, int *type) {
/* First, examine the nakade area. For sure, it must be at most
 * six points. And it must be within color group(s). */
#define NAKADE_MAX 6
  Coord area[NAKADE_MAX];
  //collect adjencency info.
  int trait[NAKADE_MAX];
  //collect if this point already has our stone.
  BOOL isOwn[NAKADE_MAX + 1];
  memset(trait, 0, sizeof(trait));
  memset(isOwn, FALSE, sizeof(isOwn));
  int area_n = 0;
  area[area_n++] = loc;
  // Simple flood fill to find the region.
  for (int i = 0; i < area_n; i++) {
    FOR4(area[i], _, c) {
      if (board->_infos[c].color == OPPONENT(board->_next_player) || board->_infos[c].color == S_OFF_BOARD) continue;
      trait[i] += 1;
      BOOL dup = FALSE;
      for (int j = 0; j < area_n; j++)
        if (c == area[j]) {
          dup = TRUE;
          break;
        }
      if (dup) continue;
      if (board->_infos[c].color != S_EMPTY){
        isOwn[area_n] = TRUE;
      }

      if (area_n >= NAKADE_MAX) {
        /* Too large nakade area. */
        return M_PASS;
      }
      area[area_n++] = c;
    } ENDFOR4
  }

  int minNeighbor = 4;
  int maxNeighbor = 0;
  int nakadeCounter = 0;
  for (int i = 0; i < area_n; i++) {
    if (trait[i] < minNeighbor) minNeighbor = trait[i];
    if (trait[i] > maxNeighbor) maxNeighbor = trait[i];
  }
  int numVitalPoint = 0;
  int numVitalPointOccupied = 0;
  int maxCounter = 0;
  Coord nakadePoint = M_PASS;
  Coord maxNakadePoint = M_PASS;
  for (int i = 0; i < area_n; i++) {
    if (trait[i] > minNeighbor) {
      numVitalPoint++;
      if (isOwn[i]){
        numVitalPointOccupied++;
      } else {
        // anchor nakade point..
        if (trait[i] == maxNeighbor) {
          maxNakadePoint = area[i];
          maxCounter++;
        } else {
          nakadePoint = area[i];
        }
      }
    }
  }
  *size = area_n;
#define DEAD 1
#define LIVE 2
#define SEKI 3
#define HOT_LIVE_DEAD 4
#define HOT_LIVE_SEKI 5
  //Special handling for some 4 vital points shape
  if (numVitalPoint == 4 && maxNeighbor >= 3) {
    switch(maxCounter) {
      case 0:
        *type = DEAD;
        return M_PASS;
      case 1:
        *type = HOT_LIVE_DEAD;
        return maxNakadePoint;
      default:
        *type = LIVE;
        return M_PASS;
    }
  }
  switch(numVitalPoint - numVitalPointOccupied){
    case 0:
      switch (numVitalPoint) {
        case 3: *type = SEKI;
        case 4: if (maxNeighbor == 2) *type = LIVE; else *type = DEAD;
        default: *type = DEAD;
          return M_PASS;
      }
    case 1:
      switch (numVitalPoint) {
        case 3: *type = HOT_LIVE_SEKI;
        default: *type = HOT_LIVE_DEAD;
          if (maxNakadePoint != M_PASS) return maxNakadePoint;
          else return nakadePoint;
      }
    default:
      *type = LIVE;
      return M_PASS;
  }
}

// Check if there is any nakade point, if so, play it to kill the opponent's group.
Coord check_nakade_v2(const Board *board, Coord m, int *size) {
	Coord empty = M_PASS;
  if (m == M_PASS) return M_PASS;

  FOR8(m, _, c) {
    if (board->_infos[c].color != S_EMPTY) continue;
		if (empty == M_PASS) {
			empty = c;
			continue;
		}
		// if (!NEIGHBOR8(c, empty)) {
			/* Seems like impossible nakade
			 * shape! */
			// return M_PASS;
		// }
	} ENDFOR8
  int type;
  if (empty != M_PASS) return nakade_point_v3(board, empty, size, &type);
  else return M_PASS;
}

BOOL add_nakade_prior(BoardExtra *board_extra, Coord last) {
  const Handle *h = board_extra->h;
  const Board *b = &board_extra->board;

  // Get the nakade point and add the prior there.
  int type;
  Coord p = check_nakade_v2(b, b->_last_move, &type);

  if (p != M_PASS) {
    // Add the points.
    int offset = h->prior_offset[T_NAKADE];
    heap_add_prior(board_extra, p, WT_PRIOR, offset + type, TRUE);
    board_extra->prior_must_move = p;
    return TRUE;
  }
  return FALSE;
}

BOOL add_kill_group_prior(BoardExtra *board_extra, Coord last) {
  // If there is a move that kills the last's group, put a prior.
  if (last == M_PASS) return FALSE;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;
  Coord m;
  unsigned short id = b->_infos[last].id;
  if (! find_only_liberty(b, id, &m)) return FALSE;

  GroupId4 ids;
  if (! TryPlay2(b, m, &ids)) return FALSE;

  // If #stones in the group >= 5, then we must capture it.
  // if (b->_groups[id].stones >= 5) board_extra->prior_must_move = m;

  heap_add_prior(board_extra, m, WT_PRIOR, h->prior_offset[T_KILL_GROUP], TRUE);
  return TRUE;
}

BOOL add_put_group_to_atari(BoardExtra *board_extra) {
  // If there is a move that puts the last's group to atari, put a prior.
  const Board *b = &board_extra->board;
  Coord last = b->_last_move;

  if (last == M_PASS) return FALSE;
  const Handle *h = board_extra->h;
  Coord m;

  short id = b->_infos[last].id;
  if (b->_groups[id].liberties != 2) return FALSE;

  // How many stones?
  int offset = b->_groups[id].stones;
  if (offset > MAX_GROUP_ATARI) offset = MAX_GROUP_ATARI;
  offset --;

  // 2 libs.
  Coord ms[2];
  find_two_liberties(b, id, ms);

  GroupId4 ids;
  for (int k = 0; k < 2; ++k) {
    if (TryPlay2(b, ms[k], &ids)) {
      heap_add_prior(board_extra, ms[k], WT_PRIOR, h->prior_offset[T_PUT_GROUP_TO_ATARI] + offset, TRUE);
      // Check whether the move is a self-atari move, if not, then put an additional self atari prior (depending on the size of the self-atari group).
      int num_stones = 0;
      if (IsSelfAtari(b, &ids, ids.c, ids.player, &num_stones)) {
        // Put self atari prior.
        if (num_stones > MAX_SELF_ATARI) num_stones = MAX_SELF_ATARI;
        heap_add_prior(board_extra, ms[k], WT_PRIOR, h->prior_offset[T_SELF_ATARI] + num_stones - 1, FALSE);
      }
    }
  }

  return TRUE;
}

BOOL add_kill_group_prior2(BoardExtra *board_extra, Coord last2) {
  // If there is a move that kills the last2's surrounding group, put a prior.
  if (last2 == M_PASS) return FALSE;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;
  Coord m;
  GroupId4 ids;
  int offset = h->prior_offset[T_KILL_GROUP2];
  // Check if the neighbor of the last2 has vunerable opponent groups, if so, kill it.
  FOR4(last2, _, c) {
    short id = b->_infos[c].id;
    if (id > 0 && b->_groups[id].color == OPPONENT(b->_next_player) && b->_groups[id].liberties == 1) {
      // Try to kill this group.
      if (! find_only_liberty(b, id, &m)) error("save_group_prior: this should never fail!");
      // Check whether this move is valid.
      if (! TryPlay2(b, m, &ids)) continue;

      // If #stones in the group >= 5, then we must capture it.
      // if (b->_groups[id].stones >= 5) board_extra->prior_must_move = m;

      heap_add_prior(board_extra, m, WT_PRIOR, offset, TRUE);
    }
  } ENDFOR4

  return TRUE;
}

BOOL add_save_group_prior(BoardExtra *board_extra, Coord last) {
  // If there is a move that save a neighboring group (to last) from atari, put a prior.
  if (last == M_PASS) return FALSE;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;
  Coord m;
  GroupId4 ids;
  int offset = h->prior_offset[T_SAVE_ATARI];
#define SAVE_MAX 20
  // Check if the neighbor of last has group in atari.
  BOOL visitedIds[MAX_GROUP];
  memset(visitedIds, FALSE, sizeof(visitedIds));
  FOR4(last, i, c) {
    short id = b->_infos[c].id;
    if (! G_HAS_STONE(id) || b->_groups[id].color != b->_next_player || b->_groups[id].liberties > 1) continue;

    if (visitedIds[id]) continue;
    visitedIds[id] = TRUE;

    int stones = b->_groups[id].stones;

    // First check if there are any capture move that can save this group.
    Coord area[SAVE_MAX];
    int save_n = 0;
    area[save_n++] = c;
    for (int i = 0; i < save_n; i++) {
      FOR4(area[i], _, cc) {
        if (b->_infos[cc].color == S_EMPTY || b->_infos[cc].color == S_OFF_BOARD) continue;
        // opponent stone
        if (b->_infos[cc].color == OPPONENT(b->_next_player)) {
          short oppoId = b->_infos[cc].id;
          if (visitedIds[oppoId]) continue;
          visitedIds[oppoId] = TRUE;
          if (b->_groups[oppoId].liberties == 1) {
            if (! find_only_liberty(b, oppoId, &m)) error("save_group_prior: this should never fail!");
            if (! TryPlay2(b, m, &ids)) continue;
            if (! IsSelfAtari(b, &ids, m, ids.player, NULL)) {
              // if (stones >= 5) board_extra->prior_must_move = m;
              heap_add_prior(board_extra, m, WT_PRIOR, offset, TRUE);
            }
          }
        } else {
          // our stone
          BOOL dup = FALSE;
          for (int j = 0; j < save_n; j++) {
            if (cc == area[j]){
              dup = TRUE;
              break;
            }
          }
          if (dup) continue;
          // too large group
          if (save_n >= SAVE_MAX) break;
          area[save_n++] = cc;
        }
      } ENDFOR4
    }
    // Otherwise, Try to save this group by extending.
    if (! find_only_liberty(b, id, &m)) error("save_group_prior: this should never fail!");

    // Check whether this move is self-atari.
    if (! TryPlay2(b, m, &ids)) continue;
    if (! IsSelfAtari(b, &ids, m, ids.player, NULL)) {
      // if (stones >= 5) board_extra->prior_must_move = m;
      heap_add_prior(board_extra, m, WT_PRIOR, offset, TRUE);
    }
  } ENDFOR4
  return TRUE;
}

BOOL add_ko_prior(BoardExtra *board_extra) {
  // Play ko move if the ko is relative new and playable.
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;

  // The ko is too old or no Ko available, skip.
  if (b->_ko_age > 10 || b->_simple_ko == M_PASS) return FALSE;

  // Immediate capture back would be illegal move.
  if (b->_ko_age == 0) return FALSE;

  Coord m = b->_simple_ko;
  GroupId4 ids;
  // Illegal move, return false.
  if (! TryPlay2(b, m, &ids)) return FALSE;

  // Then we can make it a candidate.
  int offset = h->prior_offset[T_KO] + b->_ko_age - 1;
  heap_add_prior(board_extra, m, WT_PRIOR, offset, TRUE);
  return TRUE;
}

#ifdef USE_EYE
BOOL add_make_eye_prior(BoardExtra *board_extra, Coord last){
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;
  Coord m;
  GroupId4 ids;
  FORDIAG4(last, _, c){
    if (IsSemiEye(b, c, b->_next_player, &m)) {
      if (!TryPlay2(b, m, &ids)) return FALSE;
      if (!IsSelfAtari(b, NULL, m, OPPONENT(b->_next_player), NULL))
        heap_add_prior(board_extra, m, WT_PRIOR, h->prior_offset[T_MAKE_EYE], TRUE);

    }
  }ENDFORDIAG4
  return TRUE;
}

BOOL add_falsify_eye_prior(BoardExtra *board_extra, Coord last){
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;
  Coord m;
  GroupId4 ids;
  FOR8(last, _, c){
    if (IsSemiEye(b, c, OPPONENT(b->_next_player), &m)) {
      if (!TryPlay2(b, m, &ids)) return FALSE;
      if (!IsSelfAtari(b, &ids, m, b->_next_player, NULL))
        heap_add_prior(board_extra, m, WT_PRIOR, h->prior_offset[T_FALSIFY_EYE], TRUE);
    }
  } ENDFOR8
  return TRUE;
}
#endif

void check_simple_semeai(BoardExtra *board_extra, Coord last) {
  // Check the simplest case. If the last move reduce any of our big (>= 5) group to <= 3 liberty, then we check if any of our neighboring opponent group has <= 3 liberty,
  // if so, play one (nonself-atari) move that reduce its liberty.
  const int our_liberty_thres = 3;
  const int our_size_thres = 10;
  const int opp_liberty_thres = 2;

  const Board *b = &board_extra->board;
  Stone player = b->_next_player;
  if (last == M_PASS || last == M_RESIGN) return;
  GroupId4 ids;

  Coord other_libs[3];
  Coord move = M_PASS;
  char buf[30];

  FOR4(last, _, c) {
      // Check if any of the four neighbors has our group
      if (b->_infos[c].color != player) continue;
      unsigned short id = b->_infos[c].id;
      if (b->_groups[id].stones < our_size_thres || b->_groups[id].liberties > our_liberty_thres) continue;

      // fprintf(stderr,"Find large group. id = %d, #stones = %d, #liberties = %d\n", id, b->_groups[id].stones, b->_groups[id].liberties);

      // Check if any of our neighboring opponent groups has <= 3 liberties.
      // If so, we try reduce its liberty.
      FOR4(c, _, cc) {
          if (b->_infos[cc].color != OPPONENT(player)) continue;
          unsigned short other_id = b->_infos[cc].id;
          if (b->_groups[other_id].liberties > opp_liberty_thres) continue;

          // fprintf(stderr,"Find opponent group. other_id = %d, #stones = %d, #liberties = %d\n", other_id, b->_groups[other_id].stones, b->_groups[other_id].liberties);

          // Try reducing its liberty.
          int counter = 0;
          TRAVERSE(b, other_id, c_in_opp_group) {
              FOR4(c_in_opp_group, _, c_potential_liberty) {
                  if (b->_infos[c_potential_liberty].color == S_EMPTY) {
                      other_libs[counter ++] = c_potential_liberty;
                  }
              } ENDFOR4
          } ENDTRAVERSE

          // Select one if it is not self atari.
          for (int i = 0; i < counter; ++i) {
            if (! TryPlay2(b, other_libs[i], &ids)) continue;
            if (IsSelfAtari(b, &ids, ids.c, ids.player, NULL)) continue;
            move = other_libs[i];
            break;
          }
      } ENDFOR4
      if (move != M_PASS) break;
  } ENDFOR4

  if (board_extra->prior_must_move == M_PASS && move != M_PASS) {
    ShowBoard(b, SHOW_ALL);
    // fprintf(stderr,"simple_semeai: %s\n", get_move_str(move, player, buf));
    board_extra->prior_must_move = move;
  }
}

// TRUE for add_prior, FALSE for remove_prior.
BOOL add_all_priors(BoardExtra *board_extra) {
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;

  Coord last = b->_last_move;

  if (h->params.verbose >= PV_DEBUG) {
    char buf[30];
    const char *move_str = get_move_str(last, board_extra->board._next_player, buf);
    fprintf(stderr,"add_prior: last move: %s\n", move_str);
  }

  board_extra->prior_status = PRIOR_STATUS_NORMAL;
  board_extra->prior_must_move = M_PASS;

  if (last == M_PASS || last == M_RESIGN) {
    board_extra->prior_status = PRIOR_STATUS_PASS_RESIGN;
    return FALSE;
  }

  // Save previous moves.
  board_extra->total_prob_before_prior = board_extra->total_prob;

  // Add priors that depend on last move.
  if (h->params.prior_resp) add_resp_prior(board_extra, last);
  if (h->params.prior_neighbor) add_neighbor_prior(board_extra, last);
  if (h->params.prior_nakade) add_nakade_prior(board_extra, last);
  if (h->params.prior_save_atari) {
    add_save_group_prior(board_extra, last);
  }
  if (h->params.prior_kill_other) {
    add_kill_group_prior(board_extra, last);
    add_kill_group_prior2(board_extra, b->_last_move2);
  }

  if (h->params.prior_ko) {
    add_ko_prior(board_extra);
  }

  if (h->params.prior_global) {
    add_global_prior(board_extra);
  }

  if (h->params.prior_put_group_to_atari) add_put_group_to_atari(board_extra);

  if (h->params.prior_eye) {
#ifdef USE_EYE
    add_make_eye_prior(board_extra, last);
    add_falsify_eye_prior(board_extra, last);
#endif
  }

  // Semeai..Disabled for now.
  // check_simple_semeai(board_extra, last);

  // If we see negative probability. we need to recompute the normalization constant.
  if (board_extra->total_prob < 1e-6) {
    PatternV2RecomputeZ(board_extra);
    board_extra->prior_status = PRIOR_STATUS_RECOMPUTE_Z;
  }

  // PRINT_DEBUG(h, "finish change_prior: last move: %s\n", move_str);
  return TRUE;
}

void update_gradient_all_priors(HandleGradient *h, BoardExtra *be) {
  for (int i = be->num_prior_moves - 1; i >= 0; --i) {
    const PriorMove *pmv = &be->prior_moves[i];
    PatternMove *mv = &be->moves[pmv->m];
    add_gradient(h, pmv->w_type, pmv->w_offset, mv->grad);
  }
}

// Remove all priors.
void remove_all_priors(BoardExtra *be) {
  const Handle *h = be->h;
  PRINT_DEBUG(h, "Remove all priors. #prior_moves: %d\n", be->num_prior_moves);
  for (int i = be->num_prior_moves - 1; i >= 0; --i) {
    const PriorMove *pmv = &be->prior_moves[i];
    PatternMove *mv = &be->moves[pmv->m];

    mv->prior_count --;
    if (mv->prior_count == 0) {
      // if (! get_log_prob(be, pmv->m, NULL)) {
      if (mv->added_by_prior) {
         // Remove the prior move if it is added by prior.
         heap_delete(be, mv);
         // Note that after removal, mv is no longer valid!
         mv = &be->moves[pmv->m];
      }
    }

    if (mv->heap_idx > 0) {
      // If not deleted, substract the prior.
      mv->prior -= pmv->prior;
      heap_recompute_prob(be, mv);
    }
  }
  be->num_prior_moves = 0;
  be->total_prob = be->total_prob_before_prior;
  PRINT_DEBUG(h, "Finish remove all priors. #prior_moves: %d\n", be->num_prior_moves);
}

/*
static unsigned int local_fast_random(void *context, unsigned int num_max) {
  return rand() % num_max;
}
*/

void PatternV2SampleMany(void *be, AllMovesExt *all_moves, AllMovesComments *comments, SampleSummary *summary) {
  if (be == NULL) { fprintf(stderr,"be cannot be NULL!"); error(""); }
  if (all_moves == NULL) { fprintf(stderr,"all_moves cannot be NULL!"); error(""); }

  BoardExtra *board_extra = (BoardExtra *) be;

  GroupId4 ids;
  int topn;
  double prob;
  int counter;
  char buf[30];

  double start = wallclock();

  const int max_heap_dumped = 10;

  if (comments != NULL) assert(comments->num_comments == all_moves->num_moves);

  int i = 0;
  for (i = 0; i < all_moves->num_moves; ++i) {
    MoveExt *move = &all_moves->moves[i];
    PatternV2Sample(be, &ids, move);

    if (summary != NULL) {
      summary->n ++;
      if (move->topn < NUM_STATS_TOPN) summary->num_topn[move->topn] ++;
      if (move->counter < NUM_STATS_TOPN) summary->num_counters[move->counter] ++;
      if (summary->max_counter < move->counter) summary->max_counter = move->counter;
      if (board_extra->prior_status == PRIOR_STATUS_RECOMPUTE_Z) summary->n_recompute_Z ++;
    }

    PatternV2PlayMove2(be, &ids);

    if (comments != NULL) {
      strcpy(comments->comments[i], PatternV2BoardExtraDumpInfo(be, max_heap_dumped));
    }

    if (IsGameEnd(&board_extra->board)) break;

    PRINT_DEBUG(board_extra->h, "Sampled move: %s, sample: %d/%d\n", get_move_str(ids.c, ids.player, buf), i, all_moves->num_moves);
    if (board_extra->h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(board_extra)) error("");
      else fprintf(stderr,"[%d/%d]: PatternV2SampleMany: After PatternV2PlayMove2: All checks pass!\n", i, all_moves->num_moves);
    }
  }

  if (summary != NULL) summary->total_duration += wallclock() - start;
  // fprintf(stderr,"Total #moves = %d\n", i);
}

void PatternV2SampleUntilSingleThread(void *be, AllMovesExt *moves, SampleSummary *summary) {
  static unsigned long seed = 13341234;
  PatternV2SampleUntil(be, &seed, fast_random_callback, moves, summary);
}

void PatternV2SampleUntil(void *be, void *context, RandFunc randfunc, AllMovesExt *moves, SampleSummary *summary) {
  if (be == NULL) { fprintf(stderr,"be cannot be NULL!"); error(""); }

  BoardExtra *board_extra = (BoardExtra *) be;
  const Handle *h = board_extra->h;

  GroupId4 ids;
  int topn;
  double prob;
  char buf[30];

  double start = wallclock();

  MoveExt move;
  int max_num_moves = 600 - board_extra->board._ply;
  if (max_num_moves < 10) max_num_moves = 10;

  int counter;
  board_extra->board._rollout_passes = 0;
  for (counter = 0; counter < max_num_moves; ++ counter) {
    if (h->params.sample_from_topn >= 1) PatternV2SampleTopn(be, h->params.sample_from_topn, context, randfunc, &ids, &move);
    else PatternV2Sample2(be, context, randfunc, &ids, &move);

    if (move.m == M_PASS) {
      if (move.player == S_BLACK) board_extra->board._rollout_passes ++;
      if (move.player == S_WHITE) board_extra->board._rollout_passes --;
    }

    if (moves != NULL && counter < moves->num_moves) {
      memcpy(&moves->moves[counter], &move, sizeof(MoveExt));
    }

    if (summary != NULL) {
      summary->n ++;
      if (move.topn < NUM_STATS_TOPN) summary->num_topn[move.topn] ++;
      if (move.counter < NUM_STATS_TOPN) summary->num_counters[move.counter] ++;
      if (summary->max_counter < move.counter) summary->max_counter = move.counter;
      if (board_extra->prior_status == PRIOR_STATUS_RECOMPUTE_Z) summary->n_recompute_Z ++;
    }

    PatternV2PlayMove2(be, &ids);

    // Update it occationally to solve the numerical instability.
    // if (counter > 0 && counter % 30 == 0) {
    // PatternV2UpdateAllScores(board_extra);
    //}

    if (IsGameEnd(&board_extra->board)) break;

    PRINT_DEBUG(h, "Sampled move: %s, sample: %d\n", get_move_str(ids.c, ids.player, buf), counter);
    if (h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(board_extra)) error("");
      else fprintf(stderr,"[%d/%d]: PatternV2SampleUntil: After PatternV2PlayMove2, all checks pass!\n", counter, max_num_moves);
    }
  }
  if (summary != NULL) summary->total_duration += wallclock() - start;
  // fprintf(stderr,"Total #moves = %d\n", counter);
}

const Board *PatternV2GetBoard(void *be) {
  BoardExtra *board_extra = (BoardExtra *) be;
  return &board_extra->board;
}

void PatternV2Sample(void *be, GroupId4 *ids, MoveExt *move_ext) {
  static unsigned long seed = 13341234;
  PatternV2SampleInterface(be, &seed, fast_random_callback, ids, move_ext);
}

/*
static BOOL reduce_self_liberty(const Board *b, GroupId4 *ids) {
  // Check if this move is not good.
  if (ids->liberty > 1) return FALSE;
  Coord m = M_PASS;
  int num_off_board = 0;
  FOR4(ids->c, _, cc) {
    if (b->_infos[cc].color == S_EMPTY) {
      m = cc;
    } else if (b->_infos[cc].color == S_OFF_BOARD) {
      num_off_board ++;
    }
  } ENDFOR4
  if (num_off_board != 1 || m == M_PASS) return FALSE;

  for (int i = 0; i < 4; ++i) {
    if (ids->ids[i] > 0 && ids->colors[i] == ids->player && ids->group_liberties[i] == 3) {
      // Check if its own liberty is also a liberty of that group.
      BOOL condition_met = FALSE;
      FOR4(m, _, cc) {
        if (b->_infos[cc].id == ids->ids[i]) {
          condition_met = TRUE;
          break;
        }
      } ENDFOR4
      if (condition_met) return TRUE;
    }
  }
  return FALSE;
}
*/

static BOOL is_good_move(const Board *b, Coord m, GroupId4 *ids) {
  // If illegal then false.
  if (! TryPlay2(b, m, ids)) return FALSE;
  //
  if (IsTrueEye(b, ids->c, ids->player)) return FALSE;
  int num_stones = 0;
  // Check if the move is self-atari
  // 1. Atari with >= 3 stones and without atari other groups (no-purpose),
  // 2. Atari with >= 5 stones.
  // If either of the condition holds, we skip this move.
  if (IsSelfAtari(b, ids, ids->c, ids->player, &num_stones)) {
    if (num_stones >= 3) return FALSE;
    /*
    if (num_stones >= 5) return FALSE;
    if (num_stones >= 3) {
      // Check if its neighbor enemy stones would be in atari.
      BOOL any_enemy_in_atari = FALSE;
      for (int i = 0; i < 4; ++i) {
        if (ids->ids[i] > 0 && ids->group_liberties[i] == 2 && ids->c == OPPONENT(ids->player)) {
          any_enemy_in_atari = TRUE;
          break;
        }
      }
      if (any_enemy_in_atari) return TRUE;
      else return FALSE;
    }
    */
  }

  // if (reduce_self_liberty(b, ids)) return FALSE;
  //
  return TRUE;
}
// #define is_good_move TryPlay2

static void sample_from_empty_locs(const BoardExtra *be, void *context, RandFunc randfunc, GroupId4 *ids) {
  Coord m;
  int n = RepCheckListSize(be->empty_list);
  const Board *b = &be->board;

  // Pick valid moves. (is this slow?)
  Coord *valid_moves = (Coord *)malloc(n * sizeof(Coord));
  int valid_n = 0;
  for (int i = 0; i < n; ++i) {
    m = (Coord)RepCheckListEnumerate(be->empty_list, i);
    if (is_good_move(b, m, ids)) {
      valid_moves[valid_n ++] = m;
    }
  }

  // Random sample valid moves.
  m = valid_n > 0 ? valid_moves[randfunc(context, valid_n)] : M_PASS;

  if (be->h->params.verbose >= PV_DEBUG) {
    char buf[30];
    fprintf(stderr,"Empty heap [ply = %d]! Random move %s...\n", b->_ply, get_move_str(m, b->_next_player, buf));
    ShowBoard(b, SHOW_LAST_MOVE);
    fprintf(stderr,"\n");
  }

  TryPlay2(b, m, ids);
  free(valid_moves);

  return;
}

void PatternV2SampleTopn(void *be, int n, void *context, RandFunc randfunc, GroupId4 *ids, MoveExt *move_ext) {
  BoardExtra *board_extra = (BoardExtra *) be;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;

  memset(move_ext, 0, sizeof(MoveExt));
  move_ext->player = b->_next_player;
  move_ext->heap_size = board_extra->heap_size;
  move_ext->counter = 0;

  add_all_priors(board_extra);

  // Get topn move.
  Coord *moves = (Coord *)malloc(sizeof(Coord) * n);
  float *confidences = (float *)malloc(sizeof(float) * n);
  n = PatternV2GetTopn(be, n, moves, confidences, FALSE);
  // fprintf(stderr,"Sample top, nbefore = %d, n = %d\n", nbefore, n);

  char buf[30];
  if (n == 0) {
    // Everything has been zeored out and we need to sample from empty locations.
    sample_from_empty_locs(board_extra, context, randfunc, ids);
    if (move_ext != NULL) {
      move_ext->m = ids->c;
      move_ext->type = SAMPLE_RANDOM;
      move_ext->topn = 0;
    }
  } else {
    // Then sample a good one.
    double total_prob = 0.0;
    for (int i = 0; i < n; ++i) {
      total_prob += confidences[i];
    }

    int max_value = 32767;
    unsigned int rand_int = randfunc(context, max_value);
    double sample = ((double)rand_int) / max_value * total_prob;
    double accu = 0;

    // fprintf(stderr,"sample = %lf\n", sample);

    for (int i = 0; i < n; ++i) {
      // fprintf(stderr,"[%d] %s, prob: %f\n", i, get_move_str(moves[i], S_EMPTY, buf), confidences[i] / total_prob);
      accu += confidences[i];
      if (accu >= sample) {
        // fprintf(stderr,"picked!\n");
        move_ext->m = moves[i];
        move_ext->prob = confidences[i] / total_prob;
        move_ext->topn = i + 1;
        break;
      }
    }

    TryPlay2(b, move_ext->m, ids);
    if (move_ext != NULL) {
      move_ext->type = SAMPLE_TOPN;
    }
  }

  // fprintf(stderr,"Top sampling: sampled move %s from %d/%d\n", get_move_str(move_ext->m, move_ext->player, buf), move_ext->topn, n);

  free(moves);
  free(confidences);

  remove_all_priors(board_extra);
}

void PatternV2SampleInterface(void *be, void *context, RandFunc randfunc, GroupId4 *ids, MoveExt *move_ext) {
  BoardExtra *board_extra = (BoardExtra *) be;
  const Handle *h = board_extra->h;

  if (h->params.sample_from_topn >= 1) PatternV2SampleTopn(be, h->params.sample_from_topn, context, randfunc, ids, move_ext);
  else PatternV2Sample2(be, context, randfunc, ids, move_ext);
}

// Sample the move from incremental structure.
void PatternV2Sample2(void *be, void *context, RandFunc randfunc, GroupId4 *ids, MoveExt *move_ext) {
  // If we already have k2w, we will sample from it.
  BoardExtra *board_extra = (BoardExtra *) be;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;

  memset(move_ext, 0, sizeof(MoveExt));
  move_ext->player = b->_next_player;
  move_ext->heap_size = board_extra->heap_size;
  move_ext->total_prob = board_extra->total_prob;

  // If there is no heap, we should just sample randomly (?) from the queue of empty locations.
  if (board_extra->heap_size == 1) {
    sample_from_empty_locs(board_extra, context, randfunc, ids);

    if (move_ext != NULL) {
      move_ext->m = ids->c;
      move_ext->type = SAMPLE_RANDOM;
      move_ext->counter = 0;
    }
    return;
  }

  add_all_priors(board_extra);
  // Check if there is a must move, if so, we just sample that move.
  if (board_extra->prior_must_move != M_PASS && TryPlay2(b, board_extra->prior_must_move, ids)) {
    // ShowBoard(b, SHOW_ALL);
    // char buf[30];
    // fprintf(stderr,"Pattern_must move is %s\n", get_move_str(board_extra->prior_must_move, b->_next_player, buf));
    if (move_ext != NULL) {
      move_ext->m = board_extra->prior_must_move;
      move_ext->type = SAMPLE_MUST_MOVE;
      move_ext->counter = 0;
    }

    // After sampling, we remove the prior back.
    remove_all_priors(board_extra);
    return;
  }

  int max_value = 32767;
  double prob_val = 0.0;
  int sample_i = 0;
  char buf[30];

  // Scan the heap.
  if (h->params.verbose >= PV_INFO) {
    fprintf(stderr,"ply = %d, heap_size = %d\n", b->_ply, board_extra->heap_size);
    ShowBoard(b, SHOW_ALL);
  }

  const int max_counter = 20;
  Coord prev_m = M_PASS;
  Coord m = M_PASS;

  int counter;

  Coord bad_moves[BOUND_COORD];
  int num_bad_moves = 0;
  double total_prob = board_extra->total_prob;

  for (counter = 0; counter < max_counter; ++ counter) {
    unsigned int rand_int = randfunc(context, max_value);
    double uniform = ((double)rand_int) / max_value;
    double sample = uniform * total_prob;
    double accu = 0;
    // Random sample one.
    // int x = randfunc(context, BOARD_SIZE);
    // int y = randfunc(context, BOARD_SIZE);
    m = M_PASS;
    // OFFSETXY(x, y);

    for (int i = 1; i < board_extra->heap_size; ++i) {
      const PatternMove *mv = HEAP_MOVE(board_extra, i);
      if (mv->status == STATUS_BAD_MOVE) continue;
      accu += mv->prob;
      if (accu >= sample) {
        m = mv->m;
        sample_i = i;
        prob_val = mv->prob / (board_extra->total_prob + 1e-8);
        break;
      }
    }
    // Check if valid.
    if (prev_m != m) {
      // fprintf(stderr,"  sampled: %s, prob: %lf\n", get_move_str(m, b->_next_player, buf), prob_val);
      prev_m = m;
      if (counter >= 1 && h->params.verbose >= PV_INFO) {
        fprintf(stderr,"  sampled: %s, prob: %lf\n", get_move_str(m, b->_next_player, buf), prob_val);
        ShowBoard(b, SHOW_ALL);
        fprintf(stderr,"\n");
      }
    }

    if (m != M_PASS) {
      if (is_good_move(b, m, ids)) break;
      else {
        // Not a good move, so we need to temporarily remove it.
        bad_moves[num_bad_moves ++] = m;
        board_extra->moves[m].status = STATUS_BAD_MOVE;
        total_prob -= board_extra->moves[m].prob;

        if (h->params.verbose >= PV_INFO) {
          fprintf(stderr,"Move %s is bad [%lf], remove it from consideration..\n",
              get_move_str(m, b->_next_player, buf), board_extra->moves[m].prob);
        }
      }
    }
  }

  // If there is no valid move, try sample randomly.
  int type = SAMPLE_HEAP;
  if (counter == max_counter) {
    if (h->params.verbose >= PV_INFO) {
      fprintf(stderr,"Sample random move..\n");
    }
    sample_from_empty_locs(board_extra, context, randfunc, ids);
    type = SAMPLE_RANDOM;
    prob_val = 0.0;
    sample_i = 0;
  }

  if (move_ext != NULL) {
    move_ext->m = ids->c;
    move_ext->type = type;
    move_ext->counter = counter;
    move_ext->prob = prob_val;
    move_ext->topn = sample_i;
  }

  for (int i = 0; i < num_bad_moves; ++i) {
    board_extra->moves[bad_moves[i]].status = STATUS_NORMAL;
  }

  // After sampling, we remove the prior back.
  remove_all_priors(board_extra);
}

// Get approximate top n choice.
int PatternV2GetApproxTopn(void *be, int n, Coord *moves, float *confidences, BOOL fill_with_random_move) {
  // If we already have k2w, we will sample from it.
  BoardExtra *board_extra = (BoardExtra *) be;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;

  add_all_priors(board_extra);

  char buf[30];
  GroupId4 ids;
  int counter = 0;

  for (int i = 1; i < board_extra->heap_size; ++i) {
    const PatternMove *mv = HEAP_MOVE(board_extra, i);
    if (is_good_move(b, mv->m, &ids)) {
      moves[counter] = mv->m;
      confidences[counter] = mv->prob / (board_extra->total_prob + 1e-6);
      counter ++;
      if (counter >= n) break;
    }
  }

  /*
  if (counter < n && fill_with_random_move) {
    sample_from_empty_locs(board_extra, context, randfunc, ids);
  }
  */

  // After sampling, we remove the prior back.
  remove_all_priors(board_extra);

  return counter;
}

// Get top n choice.
int PatternV2GetTopn(void *be, int n, Coord *moves, float *confidences, BOOL fill_with_random_move) {
  // If we already have k2w, we will sample from it.
  BoardExtra *board_extra = (BoardExtra *) be;
  const Board *b = &board_extra->board;
  const Handle *h = board_extra->h;

  add_all_priors(board_extra);

  GroupId4 ids;
  int counter = 0;

  // Get topn in heap.
  Coord heap2[MACRO_BOARD_SIZE * MACRO_BOARD_SIZE + 1];
  memcpy(heap2, board_extra->moves_heap, sizeof(heap2));

  int heap_size = board_extra->heap_size;

  const int heap_head = 1;
  while (heap_size > 1 && counter < n) {
    Coord m = heap2[heap_head];
    simple_swap(heap2, heap_head, heap_size - 1);
    heap_size --;
    simple_heap_down(board_extra, heap2, heap_size, heap_head);

    if (is_good_move(b, m, &ids)) {
      moves[counter] = m;
      confidences[counter] = board_extra->moves[m].prob / board_extra->total_prob;
      counter ++;
    }
  }

  /*
  if (counter < n && fill_with_random_move) {
    sample_from_empty_locs(board_extra, context, randfunc, ids);
  }
  */

  // After sampling, we remove the prior back.
  remove_all_priors(board_extra);

  return counter;
}

const char *PatternV2BoardExtraDumpInfo(void *be, int max_heap_dumped) {
  BoardExtra *board_extra = (BoardExtra *)be;
  const Handle *h = board_extra->h;

  add_all_priors(board_extra);

  // Dump information for prior moves.
  static char all_info[5000];
  all_info[0] = 0;
  int len = 0;

  len += heap_dump_to_buffer(board_extra, max_heap_dumped, all_info);

  char buf[30];
  len += sprintf(all_info + len, "----- Prior moves: #moves = %d--------\n", board_extra->num_prior_moves);
  for (int i = 0; i < board_extra->num_prior_moves; ++i) {
    const PriorMove *pmv = &board_extra->prior_moves[i];
    get_move_str(pmv->m, board_extra->board._next_player, buf);

    if (pmv->w_type == WT_RESP) {
      len += sprintf(all_info + len, "%d: %s, type RESP, w_offset: %d, prior: %lf\n", i, buf, pmv->w_offset, pmv->prior);
    } else {
      const int type_idx = h->prior_type[pmv->w_offset];
      len += sprintf(all_info + len, "%d: %s, type %s, offset: %d, prior: %lf\n", i, buf, g_priors[type_idx].prior_name, pmv->w_offset - h->prior_offset[type_idx], pmv->prior);
    }
  }
  len += sprintf(all_info + len, "----- End Prior moves --------\n");

  remove_all_priors(board_extra);
  return all_info;
}

// When the weights are updated, all the logprob in the heap should be updated as well.
void PatternV2UpdateWeightsAndCleanGradients(void *hh, void *g) {
  Handle *h = (Handle *)hh;
  HandleGradient *grads = (HandleGradient *)g;
  // Add all gradient to the value and clean the gradient up.
  // All w should be bounded, otherwise there will be numerical instability.
  for (int i = 0; i < WT_TOTAL; ++i) {
    double *w = h->weights[i];
    double *g = grads->grads[i];
    RepCheckList *l = grads->checks[i];
    for (int j = 0; j < RepCheckListSize(l); ++j) {
      int idx = RepCheckListEnumerate(l, j);
      w[idx] += g[idx] * h->params.learning_rate;
      CLAMP(w[idx]);
      g[idx] = 0.0;
    }
    RepCheckListClear(l);
  }
}

void debug_heap_prior(BoardExtra *be) {
  const Board *b = &be->board;
  const Handle *h = be->h;
  Handle *h_m = (Handle *)((void *)h);

  ShowBoard(&be->board, SHOW_LAST_MOVE);
  fprintf(stderr,"Dump current heap!\n");
  heap_dump(be, -1);

  int prev_verbose = h->params.verbose;
  h_m->params.verbose = PV_DEBUG;
  add_all_priors(be);
  remove_all_priors(be);
  h_m->params.verbose = prev_verbose;
  fprintf(stderr,"Dump heap again!\n");
  heap_dump(be, -1);
}

BOOL PatternV2Train(void *be2, void *g, Coord m_target, int training, int *topn, double *loglikelihood) {
  // Use the target to train the current model.
  BoardExtra *be = (BoardExtra *)be2;
  HandleGradient *grads = (HandleGradient *)g;
  const Board *b = &be->board;
  const Handle *h = be->h;

  if (grads == NULL && training != TRAINING_EVALONLY) {
    fprintf(stderr,"No gradient structure but we enter the training mode.\n");
    error("");
  }

  char buf[30];
  PRINT_INFO(h, "Train with move %s, training: %d, heap_size: %d, total_prob: %lf\n",
      get_move_str(m_target, b->_next_player, buf), training, be->heap_size, be->total_prob);

  if (be->total_prob <= 1e-6) return FALSE;

  // int ply_pos_offset = h->prior_offset[T_PLY_POS_W];
  // BOOL ply_coeff = b->_ply * PLY_FRACTION;
  //
  add_all_priors(be);

  // Compute #absent_moves AFTER add_all_priors.
  int num_absent_moves = MACRO_BOARD_SIZE * MACRO_BOARD_SIZE - be->heap_size + 1;
  int absent_w_offset = h->prior_offset[T_ABSENT_MOVE];
  double absent_prob = num_absent_moves * EXP(h->prior_w[absent_w_offset] / h->T);
  double total_prob = be->total_prob + absent_prob;
  double log_total_prob = LOG(total_prob);
  if (isnan(log_total_prob)) {
    fprintf(stderr,"Training: log_total_prob is nan! absent_prob = %lf, be->total_prob = %lf, num_absent_moves = %d, prev_total_prob = %lf\n",
        absent_prob, be->total_prob, num_absent_moves, be->total_prob_before_prior);

    remove_all_priors(be);
    debug_heap_prior(be);
    error("");
    return FALSE;
  }

  *topn = -1;

  // Loop through the heap, and change the weight accordingly.
  // Heap gives the probablity for each outcome.
  // Ideally, for all the moves without a pattern to support, their exp(w'x) = 1 and should be added to the normalization constant. We just omit it.
  for (int i = 1; i < be->heap_size; ++i) {
    Coord m = be->moves_heap[i];
    PatternMove *mv = &be->moves[m];

    double prob = mv->prob / total_prob;
    double grad;
    // Change weight accordingly.
    if (m == m_target) {
      // This is only an approximate of topn. top1 is exact.
      *loglikelihood = (mv->logprob + mv->prior) / h->T - log_total_prob;
      if (isnan(*loglikelihood)) {
        const char *move_str = get_move_str(m_target, b->_next_player, buf);
        fprintf(stderr,"nan loglikelihood! Move: %s, prob: %lf, logprob: %lf, prior: %lf, total_prob: %lf, log_total_prob: %lf\n",
            move_str, prob, mv->logprob, mv->prior, total_prob, log_total_prob);
        error("");
      }
      *topn = i;
      grad = 1 - prob;
    } else {
      grad = -prob;
    }

    if (training != TRAINING_EVALONLY) {
      if (isnan(grad)) {
        const char *move_str = get_move_str(m_target, b->_next_player, buf);
        fprintf(stderr,"grad is nan! Move: %s, prob: %lf, grad: %lf, un-normalized prob: %lf, total_prob: %lf\n", move_str, prob, grad, mv->prob, total_prob);
        error("");
      }

      if (training == TRAINING_NEGATIVE) grad = -grad;

      // Actually perform gradient update.
      uint64_t v = be->hashes[m];
      add_gradient(grads, WT_NORESP, MASK(v), grad);
      add_gradient(grads, WT_POS, m, grad);
      // add_gradient(grads, WT_PRIOR, ply_pos_offset, grad * ply_coeff);
      // This is used for updating gradient for prior moves.
      mv->grad = grad;
    }
  }

  // This function will also perform gradient descent if training is true.
  if (training != TRAINING_EVALONLY) {
    update_gradient_all_priors(grads, be);
    // Update the common weight for all the absent moves.
    double absent_ratio = absent_prob / total_prob;
    add_gradient(grads, WT_PRIOR, absent_w_offset, (*topn == -1) ? 1 - absent_ratio : -absent_ratio);
  } else {
    // If evaluate only and verbose >= 3, we also print out the heap
    if (h->params.verbose >= PV_DEBUG) {
      ShowBoard(&be->board, SHOW_LAST_MOVE);
      const char *move_str = get_move_str(m_target, b->_next_player, buf);
      fprintf(stderr,"Target move = %s, topn = %d\n", move_str, *topn);
      heap_dump(be, -1);
    }
  }

  remove_all_priors(be);

  /*
  double prob_error = be->total_prob - be->total_prob_before_prior;
  if (fabs(prob_error) / fabs(be->total_prob) > 1e-2)  {
    fprintf(stderr,"Error in PatternV2Train. Current total_prob [%lf] is different from previous prob [%lf]\n", be->total_prob, be->total_prob_before_prior);
    const char *move_str = get_move_str(m_target, b->_next_player, buf);
    fprintf(stderr,"Target move = %s, topn = %d\n", move_str, *topn);

    debug_heap_prior(be);

    fprintf(stderr,"Error in PatternV2Train. Try again: Current total_prob [%lf] is different from previous prob [%lf]\n", be->total_prob, be->total_prob_before_prior);
    error("");
  }
  */

  PRINT_DEBUG(h, "Finish train with move %s, heap_size: %d, total_prob: %lf\n", get_move_str(m_target, b->_next_player, buf), be->heap_size, total_prob);
  return TRUE;
}

void PatternV2StartTraining(void *hh) {
  Handle *h = (Handle *)hh;
  // Initialize.
  int num_noresp = 0, num_resp = 0;
  for (int i = 0; i < HASH_SIZE; ++i) {
    if (h->cnt_k2w_noresp[i] >= h->params.cnt_threshold) {
      h->k2w_noresp[i] = LOG((double)h->cnt_k2w_noresp[i]);
      CLAMP(h->k2w_noresp[i]);
      num_noresp ++;
    }
    if (h->cnt_k2w_resp[i] >= h->params.cnt_threshold) {
      h->k2w_resp[i] = LOG((double)h->cnt_k2w_resp[i]);
      CLAMP(h->k2w_resp[i]);
      num_resp ++;
    }
  }
  fprintf(stderr,"Start Training. #noresp: %d, #resp: %d\n", num_noresp, num_resp);
}

/*
void PatternV2TrainMany(void *h, void *board_extra, const AllMovesExt *all_moves, int black_training_type, int white_training_type, PerfSummary *summary) {
  BoardExtra *be = (BoardExtra *)board_extra;

  char buf[30];
  double sum_loglikelihood = 0;
  int sum_top1 = 0;
  int n = 0;
  int n_game_end = 0;
  BOOL just_updated = TRUE;

  HandleGradient *grads = (HandleGradient *)PatternV2InitGradients();

  double start = wallclock();

  for (int i = 0; i < all_moves->num_moves; ++i) {
    double loglikelihood;
    int topn = -1;
    Coord m = all_moves->moves[i].m;
    Stone player = all_moves->moves[i].player;
    int training = (player == S_BLACK ? black_training_type : white_training_type);

    if (PatternV2Train(be, grads, m, training, &topn, &loglikelihood) && topn >= 1) {
      sum_loglikelihood += loglikelihood;
      sum_top1 += (topn == 1) ? 1 : 0;
      n ++;
    }

    // [TODO]: We might have problem here.
    if (training != TRAINING_EVALONLY) {
      // Since the weights are updating, we need to sync heap logprob with the weights in Handle.
      if (i % be->h->params.batch_size == be->h->params.batch_size - 1) {
        PatternV2UpdateWeightsAndCleanGradients(h, grads);
        PatternV2UpdateAllScores(be);
        just_updated = TRUE;
      } else {
        just_updated = FALSE;
      }
    }

    // Check whether all the constraints are good.
    if (be->h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(board_extra)) error("");
      else fprintf(stderr,"[%d/%d]: PatternV2TrainMany. Before PatternV2PlayMove, All checks pass!\n", i, all_moves->num_moves);
    }

    // Play the next move.
    if (! PatternV2PlayMove(be, m, player)) {
      // If the move is not valid, skip the game and return.
      if (be->h->params.verbose >= PV_DEBUG) {
        char buf[30];
        fprintf(stderr,"Move %s is not valid?", get_move_str(m, be->board._next_player, buf));
        ShowBoard(&be->board, SHOW_ALL);
        error("");
      } else {
        break;
      }
    }

    n_game_end ++;

    // If game end, we stop.
    if (IsGameEnd(&be->board)) break;

    // Check whether all the constraints are good.
    if (be->h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(board_extra)) error("");
      else fprintf(stderr,"[%d/%d]: PatternV2TrainMany. After PatternV2PlayMove, All checks pass!\n", i, all_moves->num_moves);
    }
  }

  if (training != TRAINING_EVALONLY && ! just_updated) {
    PatternV2UpdateWeightsAndCleanGradients(h, grads);
    PatternV2UpdateAllScores(be);
  }

  PatternV2DestroyGradients(grads);

  // Finally print the statistics.
  PRINT_DEBUG(be->h, "Trained on %d/%d samples: loglikelihood = %lf, topn = %.1f%%\n", n, all_moves->num_moves, sum_loglikelihood / n, (double)sum_top1 * 100 / n);

  if (summary != NULL) {
    summary->sum_loglikelihood += sum_loglikelihood;
    summary->sum_top1 += sum_top1;
    summary->n_selected_moves += n;
    summary->n_all_moves += n_game_end;
    summary->n_games ++;
    summary->total_duration += wallclock() - start;
  }
}
*/

void PatternV2TrainManySaveGradients(void *board_extra, void *grads, const AllMovesExt *all_moves, int black_training_type, int white_training_type, PerfSummary *summary) {
  BoardExtra *be = (BoardExtra *)board_extra;

  char buf[30];
  double sum_loglikelihood = 0;
  int sum_top1 = 0;
  int n = 0;
  int n_game_end = 0;
  int n_recompute_z = 0;

  double start = wallclock();

  for (int i = 0; i < all_moves->num_moves; ++i) {
    double loglikelihood;
    int topn = -1;
    Coord m = all_moves->moves[i].m;
    Stone player = all_moves->moves[i].player;
    int training = (player == S_BLACK ? black_training_type : white_training_type);

    if (PatternV2Train(be, grads, m, training, &topn, &loglikelihood) && topn >= 1) {
      sum_loglikelihood += loglikelihood;
      sum_top1 += (topn == 1) ? 1 : 0;
      n ++;
    }

    if (be->prior_status == PRIOR_STATUS_RECOMPUTE_Z) n_recompute_z ++;

    // Check whether all the constraints are good.
    if (be->h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(board_extra)) error("");
      else fprintf(stderr,"[%d/%d] PatternV2TrainManySaveGradients: Before PatternV2PlayMove: All checks pass!\n", i, all_moves->num_moves);
    }

    // Play the next move.
    if (! PatternV2PlayMove(be, m, player)) {
      // If the move is not valid, skip the game and return.
      if (be->h->params.verbose >= PV_DEBUG) {
        char buf[30];
        fprintf(stderr,"Move %s is not valid?", get_move_str(m, be->board._next_player, buf));
        ShowBoard(&be->board, SHOW_ALL);
        error("");
      } else {
        break;
      }
    }

    // Update score..
    // PatternV2UpdateAllScores(be);

    n_game_end ++;

    // If game end, we stop.
    if (IsGameEnd(&be->board)) break;

    // Check whether all the constraints are good.
    if (be->h->params.verbose >= PV_DEBUG) {
      if (! PatternV2BoardExtraCheck(board_extra)) error("");
      else fprintf(stderr,"[%d/%d] PatternV2TrainManySaveGradients: After PatternV2PlayMove: All checks pass!\n", i, all_moves->num_moves);
    }
  }

  // Finally print the statistics.
  PRINT_DEBUG(be->h, "Trained on %d/%d samples: loglikelihood = %lf, topn = %.1f%%\n", n, all_moves->num_moves, sum_loglikelihood / n, (double)sum_top1 * 100 / n);

  if (summary != NULL) {
    summary->sum_loglikelihood += sum_loglikelihood;
    summary->sum_top1 += sum_top1;
    summary->n_selected_moves += n;
    summary->n_all_moves += n_game_end;
    summary->n_games ++;
    summary->total_duration += wallclock() - start;
    summary->n_recompute_Z += n_recompute_z;
  }
}

// Start from the current board, do a random sampling multiple times and train the model given which player has won.
// If training is FALSE, then it will just sample and report the average accuracy.
void PatternV2TrainPolicyGradient(void *h, void *grads, const GameScoring *scoring, BOOL training, SampleSummary *sample_summary, PerfSummary *perf_summary) {
  assert(scoring);
  // char buf[30];
  AllMovesExt *moves = InitAllMovesExt(MACRO_BOARD_SIZE * MACRO_BOARD_SIZE);

  int n_correct = 0;
  double start = wallclock();

  for (int i = 0; i < scoring->iterations; ++i) {
    BoardExtra *be = (BoardExtra *)PatternV2InitBoardExtra(h, scoring->board);
    // Play until the end of game and then back propagate.
    PatternV2SampleUntilSingleThread(be, moves, sample_summary);

    // Get the score.
    float score = GetFastScore(&be->board, scoring->rule) - scoring->komi;
    PatternV2DestroyBoardExtra(be);

    int black_training_sign = TRAINING_EVALONLY;
    int white_training_sign = TRAINING_EVALONLY;
    if (score > 0 && scoring->player_won == S_WHITE) {
      // Predict black win but actual white wins.
      // Penalize white move.
      white_training_sign = TRAINING_NEGATIVE;
      black_training_sign = TRAINING_NEGATIVE;
    } else if (score < 0 && scoring->player_won == S_BLACK) {
      // Predict white win but actual black wins.
      // Penalize black move.
      black_training_sign = TRAINING_NEGATIVE;
      white_training_sign = TRAINING_NEGATIVE;
    } else {
      // Reinforce both if the scoring is correct.
      black_training_sign = TRAINING_POSITIVE;
      white_training_sign = TRAINING_POSITIVE;
      n_correct ++;
    }

    if (training) {
      be = (BoardExtra *)PatternV2InitBoardExtra(h, scoring->board);
      PatternV2TrainManySaveGradients(be, grads, moves, black_training_sign, white_training_sign, perf_summary);
      PatternV2DestroyBoardExtra(be);
    }
  }

  if (perf_summary != NULL) {
    perf_summary->sum_result_correct += n_correct;
    perf_summary->n_games ++;
    perf_summary->n_pg_iterations += scoring->iterations;
    perf_summary->total_duration += wallclock() - start;
  }

  DestroyAllMovesExt(moves);
}

BOOL LoadPatternV2(void *ctx, const char *filename) {
  if (filename == NULL) {
    fprintf(stderr,"LoadPatternV2: Filename is NULL!");
    return FALSE;
  }

  Handle *h = (Handle *)ctx;
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    fprintf(stderr,"File %s cannot be opened!\n", filename);
    return FALSE;
  }

  // Load the pattern code from a binary file.
  uint64_t hash_size;
  fread(&hash_size, 1, sizeof(hash_size), fp);
  if (hash_size != HASH_SIZE) {
    fprintf(stderr,"Loaded hash size: %" PRIu64 ", hardcoded hash size: %" PRIu64 "\n", hash_size, (uint64_t)HASH_SIZE);
    error("");
  }
  // Write the hashes.
  fread(h->hs, 1, sizeof(h->hs), fp);
  fread(h->cnt_k2w_noresp, 1, sizeof(h->cnt_k2w_noresp), fp);
  fread(h->cnt_k2w_resp, 1, sizeof(h->cnt_k2w_resp), fp);
  fread(h->k2w_noresp, 1, sizeof(h->k2w_noresp), fp);
  fread(h->k2w_resp, 1, sizeof(h->k2w_resp), fp);
  fread(h->pos_w, 1, sizeof(h->pos_w), fp);

  uint64_t len_prior;
  fread(&len_prior, 1, sizeof(len_prior), fp);
  if (len_prior != LEN_PRIOR) {
    fprintf(stderr,"Loaded length of prior: %" PRIu64 ", hardcoded length of prior: %" PRIu64 "\n", len_prior, (uint64_t)LEN_PRIOR);
    error("");
  }
  fread(&h->prior_w, 1, sizeof(h->prior_w), fp);

  for (int i = 0; i < sizeof(h->k2w_resp) / sizeof(double); ++i) {
     if (fabs(h->k2w_resp[i]) > W_BOUND) {
      fprintf(stderr,"k2w_resp[%d]: %lf (out of bound, bound = %lf)\n", i, h->k2w_resp[i], W_BOUND);
      CLAMP(h->k2w_resp[i]);
      // error("");
    }
  }

  for (int i = 0; i < sizeof(h->k2w_noresp) / sizeof(double); ++i) {
     if (fabs(h->k2w_noresp[i]) > W_BOUND) {
      fprintf(stderr,"k2w_noresp[%d]: %lf (out of bound, bound = %lf)\n", i, h->k2w_noresp[i], W_BOUND);
      CLAMP(h->k2w_noresp[i]);
      // error("");
    }
  }

  for (int i = 0; i < sizeof(h->prior_w) / sizeof(double); ++i) {
    if (fabs(h->prior_w[i]) > W_BOUND) {
      fprintf(stderr,"prior_w[%d]: %lf (out of bound, bound = %lf)\n", i, h->prior_w[i], W_BOUND);
      CLAMP(h->prior_w[i]);
      // error("");
    }
  }
  for (Coord m = 0; m < BOUND_COORD; ++m) {
    if (fabs(h->pos_w[m]) > W_BOUND) {
      char buf[30];
      fprintf(stderr,"pos_w[%s]: %lf (out of bound, bound = %lf)\n", get_move_str(m, S_EMPTY, buf), h->pos_w[m], W_BOUND);
      // error("");
      CLAMP(h->pos_w[m]);
    }
  }

  fread(&h->collision, 1, sizeof(h->collision), fp);
  fread(&h->num_pattern, 1, sizeof(h->num_pattern), fp);
  fread(&h->params, 1, sizeof(h->params), fp);

  // Close the file.
  fclose(fp);
  return TRUE;
}

BOOL SavePatternV2(void *ctx, const char *filename) {
  if (filename == NULL) return FALSE;

  const Handle *h = (Handle *)ctx;
  FILE *fp = fopen(filename, "w");
  if (fp == NULL) return FALSE;

  // Save the pattern code into a binary file.
  uint64_t hash_size = HASH_SIZE;
  fwrite(&hash_size, 1, sizeof(hash_size), fp);
  // Write the hashes.
  fwrite(h->hs, 1, sizeof(h->hs), fp);
  fwrite(h->cnt_k2w_noresp, 1, sizeof(h->cnt_k2w_noresp), fp);
  fwrite(h->cnt_k2w_resp, 1, sizeof(h->cnt_k2w_resp), fp);
  fwrite(h->k2w_noresp, 1, sizeof(h->k2w_noresp), fp);
  fwrite(h->k2w_resp, 1, sizeof(h->k2w_resp), fp);
  fwrite(h->pos_w, 1, sizeof(h->pos_w), fp);

  uint64_t len_prior = LEN_PRIOR;
  fwrite(&len_prior, 1, sizeof(len_prior), fp);
  fwrite(h->prior_w, 1, sizeof(h->prior_w), fp);
  fwrite(&h->collision, 1, sizeof(h->collision), fp);
  fwrite(&h->num_pattern, 1, sizeof(h->num_pattern), fp);
  fwrite(&h->params, 1, sizeof(h->params), fp);
  // Close the file.
  fclose(fp);
  return TRUE;
}

void PatternV2DestroyGradients(void *g) {
  HandleGradient *grad = (HandleGradient *)g;

  for (int i = 0; i < WT_TOTAL; ++i) {
    if (grad->checks[i] != NULL) DestroyRepCheckList(grad->checks[i]);
    if (grad->grads[i] != NULL) free(grad->grads[i]);
  }
  free(grad);
}

// Destroy pattern map.
void DestroyPatternV2(void *ctx) {
  Handle *h = (Handle *)ctx;
  if (h->filter != NULL) bloom_free(h->filter);
  free(h);
}
