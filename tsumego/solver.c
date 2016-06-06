//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "solver.h"
#include "rank_move.h"
#include <assert.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define MAX_DEPTH 1000

#include <inttypes.h>
#include <stdlib.h>

typedef struct {
  int64_t search_counter;
} TGState;

void InitTGState(TGState *state) {
  state->search_counter = 0;
}

typedef struct {
  Board b;
  AllMoves m;
  int next_move;
  int id;
  int alpha, beta, score;
  // The game end with decisive conclusion.
  // If decisive = TRUE, then we just skip this branch since other branches will have something better.
  BOOL decisive;

  // Save the best child move so far.
  Coord best_child_moves[MAX_DEPTH];
  int num_child_moves;
  int depth;
} State;

void InitState(const Board *board, const TGState *global_state, const TGCriterion *crit, State *s) {
  assert(s != NULL);
  if (board != NULL) CopyBoard(&s->b, board);
  s->alpha = -10000;
  s->beta = 10000;
  s->score = s->b._next_player == S_BLACK ? -10000 : 10000;

  const Region *r = &crit->region;

  // We could put some heuristics and rank them.
  GetRankedMoves(&s->b, r, -1, &s->m);

  s->next_move = 0;
  s->num_child_moves = 0;
  s->depth = 0;
  s->id = global_state->search_counter;
  s->decisive = FALSE;
}

void NextState(const TGState *global_state, const TGCriterion *crit, State *curr, State *next) {
  GroupId4 ids;
  char buf[100];

  // Next move.
  Coord m = curr->m.moves[curr->next_move++];

  // Next board.
  CopyBoard(&next->b, &curr->b);

  if (! TryPlay2(&next->b, m, &ids)) {
    printf("Illegal moves! %s", get_move_str(m, next->b._next_player, buf));
    ShowBoard(&next->b, SHOW_ALL);
    error("Move error!");
  }
  Play(&next->b, &ids);
  // ShowBoard(next_b, SHOW_ALL);
  // Then get the next possible moves.
  InitState(NULL, global_state, crit, next);

  // Set alpha and beta value.
  next->alpha = curr->alpha;
  next->beta = curr->beta;
  next->depth = curr->depth + 1;
}

void PrintState(const State *s) {
  char buf[100];
  printf("[%d]: %s player, d = %d, Alpha = %d, Beta = %d, Score = %d %s\n", s->id, (s->b._next_player == S_BLACK ? "Max" : "Min"), s->depth, s->alpha, s->beta, s->score, (s->decisive ? "" : "[undecisive]"));
  for (int i = 0; i < s->m.num_moves; ++i) {
    printf("Candidate: %s\n", get_move_str(s->m.moves[i], s->b._next_player, buf));
  }
  ShowBoard(&s->b, SHOW_LAST_MOVE);
  printf("\n");
}

void UpdateState(const State* child, State *parent) {
  // Black + (max player), White - (min player)
  if (!child->decisive) return;

  BOOL child_better = FALSE;
  if (parent->b._next_player == S_BLACK) {
    // The max player.
    /*
    printf("Max player:\n");
    printf("[%d]: parent->score = %d, parent->alpha = %d, parent->beta = %d\n", parent->id, parent->score, parent->alpha, parent->beta);
    printf("[%d]: child->score = %d, child->alpha = %d, child->beta = %d\n", child->id, child->score, child->alpha, child->beta);
    */

    if (parent->score < child->score) {
      parent->score = child->score;
      child_better = TRUE;
    }
    if (parent->alpha < child->score) parent->alpha = child->score;
  } else {
    // The min player.
    /*
    printf("Min player:\n");
    printf("[%d]: parent->score = %d, parent->alpha = %d, parent->beta = %d\n", parent->id, parent->score, parent->alpha, parent->beta);
    printf("[%d]: child->score = %d, child->alpha = %d, child->beta = %d\n", child->id, child->score, child->alpha, child->beta);
    */
    if (parent->score > child->score) {
      parent->score = child->score;
      child_better = TRUE;
    }
    if (parent->beta > child->score) parent->beta = child->score;
  }

  /*
  printf("Child:\n");
  PrintState(child);
  printf("Parent:\n");
  PrintState(parent);
  */

  if (child_better) {
    // At least one child is good.
    parent->decisive = TRUE;
    Coord m = parent->m.moves[parent->next_move - 1];
    /*
    char buf[100];
    printf("Find better move: %s, depth = %d", get_move_str(m, parent->b._next_player, buf), parent->depth);
    ShowBoard(&parent->b, SHOW_ALL);
    */

    // Save the moves..
    parent->num_child_moves = child->num_child_moves + 1;
    parent->best_child_moves[0] = m;
    for (int i = 0; i < child->num_child_moves; ++i) {
      parent->best_child_moves[i + 1] = child->best_child_moves[i];
    }
  }
}

BOOL set_win(State *s, Stone aspect) {
  if (aspect == S_WHITE) {
    // printf("White wins!\n");
    // ShowBoard(&s->b, SHOW_LAST_MOVE);
    // printf("\n\n");
    s->alpha = -10;
    s->score = -10;
    s->beta = -10;
  } else {
    s->alpha = 10;
    s->score = 10;
    s->beta = 10;
  }

  s->decisive = TRUE;
  return TRUE;
}

BOOL IfClosedSetValue(const TGCriterion *crit, State *s) {
  // Alpha beta pruning.
  if (s->alpha >= s->beta) return TRUE;

  // Check lives.
  if (OneGroupLives(&s->b, crit->target_player, &crit->region)) return set_win(s, crit->target_player);
  // Check dead.
  if (crit->target_player == S_BLACK && s->b._w_cap >= crit->dead_thres) return set_win(s, S_WHITE);
  else if (crit->target_player == S_WHITE && s->b._b_cap >= crit->dead_thres) return set_win(s, S_BLACK);

  // Undefined. Seki?
  if (s->next_move == s->m.num_moves) {
    /*
    if (abs(s->score) > 1000) {
      printf("[%d] is closed because no moves, but with undefined situation!\n", s->id);
      PrintState(s);
    }
    */
    return TRUE;
  }

  // If game is ended, e.g., two pass. then return.
  if (IsGameEnd(&s->b)) {
    /*
    if (abs(s->score) > 1000) {
      printf("[%d] is closed because the game endeds, but with undefined situation!\n", s->id);
      PrintState(s);
    }
    */

    return TRUE;
  }

  return FALSE;
}

// Solve the tsumego.
int TsumegoSearch(const Board *board, const TGCriterion *crit, AllMoves *move_seq) {
  assert(board != NULL);
  assert(crit != NULL);
  assert(move_seq != NULL);

  // Depth first search. Only need to save the stack.
  State stack[MAX_DEPTH];

  TGState global_state;
  InitTGState(&global_state);

  InitState(board, &global_state, crit, &stack[0]);
  int stack_loc = 1;
  BOOL search_complete = TRUE;

  while (stack_loc > 0) {
    State *s = &stack[stack_loc - 1];
    global_state.search_counter ++;
    if (crit->max_count > 0 && global_state.search_counter >= crit->max_count) {
      search_complete = FALSE;
      break;
    }

    BOOL closed = IfClosedSetValue(crit, s);

    if (! closed) {
      // Expand: try different moves.
      NextState(&global_state, crit, s, &stack[stack_loc ++]);
    } else {
      // Back trace.
      // PrintState(s);
      // Update the alpha beta value.
      if (stack_loc > 1) {
         UpdateState(s, &stack[stack_loc - 2]);
      }
      stack_loc --;
    }
  }

  printf("#Search = %" PRId64 " [%s], score = %d\n", global_state.search_counter, (search_complete ? "Complete" : "Incomplete"), stack[0].score);
  // Dump the best sequence.
  move_seq->num_moves = stack[0].num_child_moves;
  for (int i = 0; i < stack[0].num_child_moves; ++i) {
    move_seq->moves[i] = stack[0].best_child_moves[i];
  }
  return 0;
}

/*
      if (stack_loc > max_depth || s->m.num_moves == 0) {
        // Save the move sequence.
        move_seq->num_moves = stack_loc - 1;
        for (int i = 0; i < stack_loc - 1; ++i) {
          int move_idx = stack[i].next_move - 1;
          move_seq->moves[i] = stack[i].m.moves[move_idx];
        }
        // We only find one solution and return.
        break;
      }
*/
