//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "default_policy.h"
#include "pattern.h"
#include "assert.h"

typedef void (*PlayoutFunc)(void *h, DefPolicyMoves *m, const Region *r);

// Check whether there is any ko fight we need to take part in. (Not working now)
void check_ko_fight(void *h, DefPolicyMoves *m, const Region *r);

// Check any of our group has lib = 1, if so, try saving it by extending.
void check_our_atari(void *h, DefPolicyMoves *m, const Region *r);

// Check if any opponent group is in danger, if so, try killing it.
void check_opponent_in_danger(void *h, DefPolicyMoves *m, const Region *r);

// Check if there is any nakade point, if so, play it to kill the opponent's group.
void check_nakade(void *h, DefPolicyMoves *m, const Region *r);

// Check the 3x3 pattern matcher
void check_pattern(void *h, DefPolicyMoves *m, const Region *r);

static PlayoutFunc g_funcs[NUM_MOVE_TYPE] = {
  NULL,
  check_ko_fight,
  check_opponent_in_danger,
  check_our_atari,
  check_nakade,
  check_pattern,
  NULL,
};

// The handle for default policy.
typedef struct {
  // Pattern matcher.
  void *p;

  // Parameters
  DefPolicyParams params;
} Handle;

void *InitDefPolicy() {
  Handle *h = (Handle *)malloc(sizeof(Handle));
  assert(h);
  h->p = InitPatternDB();
  assert(h->p);
  // Set default parameters.
  InitDefPolicyParams(&h->params);
  return h;
}

// Set the inital value of default policy params.
void InitDefPolicyParams(DefPolicyParams *params) {
  memset(params, 0, sizeof(DefPolicyParams));
  // Open everything by default.
  for (int i = 0; i < NUM_MOVE_TYPE; ++i) params->switches[i] = TRUE;

  // Allow self-atari moves for groups with <= 3 stones.
  params->thres_allow_atari_stone = 3;

  // By default save all atari stones.
  params->thres_save_atari = 1;

  // Attack opponent groups with 1 libs or less, with 1 or more stones (i.e., any group).
  params->thres_opponent_libs = 1;
  params->thres_opponent_stones = 1;
}

void DefPolicyParamsPrint(void *hh) {
  Handle *h = (Handle *)hh;
  DefPolicyParams *params = &h->params;

  for (int i = 0; i < NUM_MOVE_TYPE; ++i) {
    printf("%s: %s\n", GetDefMoveType((MoveType)i), STR_BOOL(params->switches[i]));
  }
}

BOOL SetDefPolicyParams(void *hh, const DefPolicyParams *params) {
  Handle *h = (Handle *)hh;
  if (params != NULL) {
    h->params = *params;
    return TRUE;
  }
  return FALSE;
}

void DestroyDefPolicy(void *p) {
  assert(p);
  Handle *h = (Handle *)p;
  DestroyPatternDB(h->p);
  free(h);
}

void ComputeDefPolicy(void *pp, DefPolicyMoves *m, const Region *r) {
  assert(pp);
  assert(m->board);

  Handle *h = (Handle *)pp;

  // Initialize moves.
  m->num_moves = 0;

  // Loop over different type of move generators to collect moves.
  for (int i = 0; i < NUM_MOVE_TYPE; ++i) {
    if (h->params.switches[i] && g_funcs[i] != NULL) {
      g_funcs[i](h, m, r);
    }
  }
}

BOOL SampleDefPolicy(void *pp, DefPolicyMoves *ms, void *context, RandFunc rand_func,
    BOOL verbose, GroupId4 *ids, DefPolicyMove *m) {
  assert(pp);
  assert(ms);

  Handle *h = (Handle *)pp;
  if (ms->num_moves == 0) return FALSE;

  char buf[30];
  int count = 0;
  while (1) {
    // Sample distribution.
    int total = 0;
    for (unsigned int i = 0; i < ms->num_moves; i++) total += ms->moves[i].gamma;
    if (total == 0) return FALSE;

    // Random sample.
    int stab = rand_func(context, total);
    // printf("stab = %d/%d\n", stab, total);
    unsigned int i;
    for (i = 0; i < ms->num_moves; i++) {
      int gamma = ms->moves[i].gamma;
      if (stab < gamma) break;
      stab -= gamma;
    }

    assert(i < ms->num_moves);

    if (verbose) {
      printf("Sample step = %d\n", count);
      ShowBoard(ms->board, SHOW_LAST_MOVE);
      util_show_move(ms->moves[i].m, ms->board->_next_player, buf);
      printf("Type = %s, gamma = %d\n", GetDefMoveType(ms->moves[i].type), ms->moves[i].gamma);
    }

    if (ids == NULL || TryPlay2(ms->board, ms->moves[i].m, ids)) {
      *m = ms->moves[i];
      return TRUE;
    }
    // Otherwise set gamma to be zero and redo this.
    ms->moves[i].gamma = 0;
    count ++;
  }
}

BOOL SimpleSampleDefPolicy(void *pp, const DefPolicyMoves *ms, void *context, RandFunc rand_func, GroupId4 *ids, DefPolicyMove *m) {
  assert(pp);
  assert(ms);

  Handle *h = (Handle *)pp;
  if (ms->num_moves == 0) return FALSE;

  int i = rand_func(context, ms->num_moves);
  if (ids == NULL || TryPlay2(ms->board, ms->moves[i].m, ids)) {
    *m = ms->moves[i];
    return TRUE;
  }
  return FALSE;
}


// Utilities for playing default policy. Referenced from Pachi's code.
void check_ko_fight(void *h, DefPolicyMoves *m, const Region *r) {
  // Need to implement ko age.
  /*
  if (GetSimpleKoLocation(board) != M_PASS) {
  }
  */
}

// Get the move with specific structure.
static Coord get_moves_from_group(void *hh, DefPolicyMoves *m, unsigned char id, MoveType type) {
  Handle *h = (Handle *)hh;
  const Board *board = m->board;
  // Find the atari point.
  int count = 0;
  int lib_count = board->_groups[id].liberties;
  Coord last = M_PASS;
  TRAVERSE(board, id, c) {
    FOR4(c, _, cc) {
      if (board->_infos[cc].color == S_EMPTY) {
        if (h != NULL) add_move(m, c_m(cc, type));
        last = cc;
        count ++;
        if (count == lib_count) break;
      }
    } ENDFOR4
    if (count == lib_count) break;
  } ENDTRAVERSE
  return last;
}

// Check any of the opponent group has lib <= lib_thres, if so, make all the capture moves.
void check_opponent_in_danger(void *hh, DefPolicyMoves *m, const Region *r) {
  Handle *h = (Handle *)hh;
  const Board *board = m->board;
  // Loop through all groups and check.
  // Group id starts from 1.
  Stone opponent = OPPONENT(board->_next_player);
  for (int i = 1; i < board->_num_groups; ++i) {
    const Group* g = &board->_groups[i];
    if (g->color != opponent) continue;
    // If the liberties of opponent group is too many, skip.
    if (g->liberties > h->params.thres_opponent_libs) continue;
    // If #stones of opponent group is too few, skip.
    if (g->stones < h->params.thres_opponent_stones) continue;
    if (!GroupInRegion(board, i, r)) continue;

    // Find the intersections that reduces its point and save it to the move queue.
    get_moves_from_group(h, m, i, OPPONENT_IN_DANGER);
    // ShowBoard(board, SHOW_LAST_MOVE);
    // util_show_move(m, board->_next_player);
  }
}

// Check any of our group has lib = 1, if so, try saving it.
void check_our_atari(void *hh, DefPolicyMoves *m, const Region *r) {
  Handle *h = (Handle *)hh;
  const Board *board = m->board;

  for (int i = 1; i < board->_num_groups; ++i) {
    const Group* g = &board->_groups[i];
    // If the group is not in atari or its #stones are not too many, skip.
    if (g->color != board->_next_player || g->liberties != 1 || g->stones < h->params.thres_save_atari) continue;
    if (!GroupInRegion(board, i, r)) continue;

    // Find the atari point.
    Coord c = get_moves_from_group(NULL, m, i, NORMAL);
    if (c == M_PASS) {
      char buf[30];
      printf("Cannot get the atari point for group %d start with %s!\n", i, get_move_str(g->start, S_EMPTY, buf));
      ShowBoard(board, SHOW_ALL);
      DumpBoard(board);
      error("");
    }
    // If that point has liberty (or connected with another self group with > 2 liberty), take the move.
    int liberty = 0;
    int group_rescue = 0;
    FOR4(c, _, cc) {
      const Info* info = &board->_infos[cc];
      if (info->color == S_EMPTY) {
        liberty ++;
      } else if (info->color == board->_next_player) {
        if (board->_groups[info->id].liberties > 2) group_rescue ++;
      }
    } ENDFOR4
    if (liberty > 0 || group_rescue > 0) {
      add_move(m, c_m(c, OUR_ATARI));
    }
  }
}

// Get Nakade point, refactored from Pachi: pachi/tactics/nakade.c
// The goal is to find the nakade point to kill the opponent in the next move.
// [TODO]: We also need to enforce our own nakade point.
Coord nakade_point(const Board *board, Coord loc) {
	/* First, examine the nakade area. For sure, it must be at most
	 * six points. And it must be within color group(s). */
#define NAKADE_MAX 6
	Coord area[NAKADE_MAX]; int area_n = 0;

	area[area_n++] = loc;

  // Simple flood fill to find the region.
  // printf("Flood fill...\n");
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
  // printf("Compute a few statistics...\n");
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

  // printf("Anchor coordinate...\n");
	Coord coordbynei[9];
	for (int i = 0; i < area_n; i++)
		coordbynei[neighbors[i]] = area[i];

  // printf("Determine the type\n");
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

  printf("This should never happen!");
  return M_PASS;
}

// Check if there is any nakade point, if so, play it to kill the opponent's group.
void check_nakade(void *hh, DefPolicyMoves *m, const Region *r) {
  Handle *h = (Handle *)hh;
  const Board *board = m->board;
	Coord empty = M_PASS;
  if (board->_last_move == M_PASS) return;
  if (r != NULL && ! IsIn(r, board->_last_move)) return;

  FOR4(board->_last_move, _, c) {
    if (board->_infos[c].color != S_EMPTY) continue;
		if (empty == M_PASS) {
			empty = c;
			continue;
		}
		if (!NEIGHBOR8(c, empty)) {
			/* Seems like impossible nakade
			 * shape! */
			return;
		}
	} ENDFOR4

  if (empty != M_PASS) {
    Coord nakade = nakade_point(board, empty);
    if (nakade != M_PASS) {
      add_move(m, c_m(nakade, NAKADE));
    }
  }
}

// Check the 3x3 pattern matcher
void check_pattern(void *h, DefPolicyMoves *m, const Region *r) {
  Handle *handle = (Handle *)h;
  if (r != NULL && ! IsIn(r, m->board->_last_move)) return;
  CheckPatternFromLastMove(handle->p, m);
}

static unsigned int normal_rand(void *context, unsigned int max_value) {
  return rand() % max_value;
}

// The old default policy..
DefPolicyMove RunOldDefPolicy(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose) {
  const int max_iter = 100;

  if (rand_func == NULL) rand_func = normal_rand;

  AllMoves all_moves;
  GetAllEmptyLocations(board, &all_moves);
  GroupId4 ids;
  char buf[30];

  // If max_depth is < 0, run the default policy until the end of game.
  if (max_depth < 0) max_depth = 10000000;

  Coord m;
  for (int k = 0; k < max_depth; ++k) {
    if (verbose) {
      printf("Default policy: k = %d/%d, player = %d\n", k, max_depth, board->_next_player);
      ShowBoard(board, SHOW_LAST_MOVE);
      // DumpBoard(board);
    }
    // Draw a random move from the current board.
    if (all_moves.num_moves == 0) break;
    int iter = 0;
    int idx;
    do {
      iter ++;
      idx = rand_func(context, all_moves.num_moves);
      m = all_moves.moves[idx];
    } while (! TryPlay2(board, m, &ids) && iter < max_iter );

    if (iter == max_iter) break;
    if (verbose) {
      util_show_move(m, board->_next_player, buf);
    }

    Play(board, &ids);
    // Remove this move.
    all_moves.moves[idx] = all_moves.moves[--all_moves.num_moves];
  }
  DefPolicyMove move;
  move.m = board->_last_move;
  move.type = NORMAL;
  move.gamma = 0;
  move.game_ended = FALSE;
  return move;
}

// The default policy
DefPolicyMove RunDefPolicy(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose) {
  AllMoves all_moves;
  GroupId4 ids;
  char buf[30];
  int num_pass = 0;

  Handle *h = (Handle *)def_policy;

  if (rand_func == NULL) rand_func = normal_rand;

  DefPolicyMove move;
  move.m = M_PASS;
  move.type = NORMAL;
  move.gamma = 0;
  move.game_ended = FALSE;

  DefPolicyMoves m;
  m.board = board;

  if (verbose) {
    printf("Start default policy!\n");
  }
  // If max_depth is < 0, run the default policy until the end of game.
  if (max_depth < 0) max_depth = 10000000;

  for (int k = 0; k < max_depth; ++k) {
    if (verbose) {
      // printf("Default policy: k = %d/%d, player = %d\n", k, max_depth, player);
      ShowBoard(board, SHOW_ALL);
      // DumpBoard(board);
    }

    // Utilities for playing default policy. Referenced from Pachi's code.
    // printf("Start computing def policy\n");
    ComputeDefPolicy(def_policy, &m, r);

    // printf("Start sampling def policy\n");
    BOOL sample_res = SampleDefPolicy(def_policy, &m, context, rand_func, verbose, &ids, &move);
    // BOOL sample_res = SimpleSampleDefPolicy(def_policy, context, rand_func, &ids, &move);
    // printf("End sampling def policy, sample_res = %d\n", sample_res);

    if (! sample_res) {
      // Fall back to the normal mode.
      if (verbose) printf("Before find all valid moves..\n");
      FindAllCandidateMovesInRegion(board, r, board->_next_player, h->params.thres_allow_atari_stone, &all_moves);
      if (verbose) printf("After find all valid moves..\n");
      if (all_moves.num_moves == 0) {
        // No move to play, just pass.
        move.m = M_PASS;
      } else {
        // Sample one move
        int idx = rand_func(context, all_moves.num_moves);
        move.m = all_moves.moves[idx];
      }

      move.type = NORMAL;
      move.gamma = 0;
      if (! TryPlay2(board, move.m, &ids)) {
        printf("Move: x = %d, y = %d, str = %s\n", X(move.m), Y(move.m), get_move_str(move.m, board->_next_player, buf));
        printf("Move (from board) = %s\n", get_move_str(move.m, board->_next_player, buf));
        ShowBoard(board, SHOW_ALL);
        error("[%d/%d]: Move cannot be executed!", k, max_depth);
      }
    }

    if (verbose) {
      util_show_move(move.m, board->_next_player, buf);
    }

    // Keep playing (even if the game already end by two pass or a resign), until we see a consecutive two passes.
    Play(board, &ids);

    // Check if there is any consecutive two passes.
    if (move.m == M_PASS) {
      num_pass ++;
      if (num_pass == 2) break;
    }
    else num_pass = 0;
  }

  if (verbose) {
    printf("Finish default policy!\n");
  }

  if (num_pass == 2) move.game_ended = TRUE;
  return move;
}

