//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 


/* From pachi with modification so that it works for our board definition
 */

/* Heuristical playout (and tree prior) policy modelled primarily after
 * the description of the Mogo engine. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "../board/board.h"
#include "../board/pattern.h"
#include "board_interface.h"
#include "moggy.h"
#include "./tactics/1lib.h"
#include "./tactics/2lib.h"
#include "./tactics/nlib.h"
#include "./tactics/ladder.h"
#include "./tactics/nakade.h"
#include "./tactics/selfatari.h"

// #define PLDEBUGL(n) DEBUGL_(p->debug_level, n)

/* In case "seqchoose" move picker is enabled (i.e. no "fullchoose"
 * parameter passed), we stochastically apply fixed set of decision
 * rules in given order.
 *
 * In "fullchoose" mode, we instead build a move queue of variously
 * tagged candidates, then consider a probability distribution over
 * them and pick a move from that. */

/* Move queue tags. Some may be even undesirable - these moves then
 * receive a penalty; penalty tags should be used only when it is
 * certain the move would be considered anyway. */
enum mq_tag {
	MQ_KO = 0,
	MQ_LATARI,
	MQ_L2LIB,
#define MQ_LADDER MQ_L2LIB /* XXX: We want to fit in char still! */
	MQ_LNLIB,
	MQ_PAT3,
	MQ_GATARI,
	MQ_JOSEKI,
	MQ_NAKADE,
	MQ_MAX
};

struct playout_policy;
struct playout_setup {
};

typedef Coord (*playoutp_choose)(struct playout_policy *playout_policy, struct playout_setup *playout_setup, Board *b, Stone to_play);
typedef bool (*playoutp_permit)(struct playout_policy *playout_policy, Board *b, struct move *m);

typedef struct playout_policy {
  int debug_level;
  playoutp_choose choose;
  playoutp_permit permit;

  // For actual playout data structure.
  void *data;

  // For multithread context.
  void *context;

  // For random function
  RandFunc rand_func;
} PlayoutPolicy;

/* Note that the context can be shared by multiple threads! */
struct moggy_policy {
	unsigned int lcapturerate, atarirate, nlibrate, ladderrate, capturerate, patternrate, korate, josekirate, nakaderate, eyefixrate;
	unsigned int selfatarirate, eyefillrate, alwaysccaprate;
	unsigned int fillboardtries;
	int koage;
	/* Whether to look for patterns around second-to-last move. */
	bool pattern2;
	/* Whether, when self-atari attempt is detected, to play the other
	 * group's liberty if that is non-self-atari. */
	bool selfatari_other;
	/* Whether to read out ladders elsewhere than near the board
	 * in the playouts. Note that such ladder testing is currently
	 * a fairly expensive operation. */
	bool middle_ladder;

	/* 1lib settings: */
	/* Whether to always pick from moves capturing all groups in
	 * global_atari_check(). */
	bool capcheckall;
	/* Prior stone weighting. Weight of each stone between
	 * cap_stone_min and cap_stone_max is (assess*100)/cap_stone_denom. */
	int cap_stone_min, cap_stone_max;
	int cap_stone_denom;

	/* 2lib settings: */
	bool atari_def_no_hopeless;
	bool atari_miaisafe;

	/* nlib settings: */
	int nlib_count;

	// struct joseki_dict *jdict;
	// struct pattern3s patterns;
  void *pattern_matcher;

	/* Gamma values for queue tags - correspond to probabilities. */
	/* XXX: Tune. */
	bool fullchoose;
	double mq_prob[MQ_MAX], tenuki_prob;
};

static inline bool
test_pattern3_here(struct playout_policy *p, Board *b, struct move *m, bool middle_ladder, double *gamma)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
	/* Check if 3x3 pattern is matched by given move... */
  hash3_t pattern = GetHash(b, m->coord);
  int integer_gamma = -1;
  if (!QueryPatternDB(pp->pattern_matcher, pattern, m->color, &integer_gamma))
		return false;
	/* ...and the move is not obviously stupid. */
	if (is_bad_selfatari(b, m->color, m->coord))
		return false;
	/* Ladder moves are stupid. */
	group_t atari_neighbor = board_get_atari_neighbor(b, m->coord, m->color);
	if (atari_neighbor && is_ladder(b, m->coord, atari_neighbor, middle_ladder)
	    && !can_countercapture(b, b->_groups[atari_neighbor].color, atari_neighbor, m->color, NULL, 0))
		return false;
	//fprintf(stderr, "%s: %d (%.3f)\n", coord2sstr(m->coord, b), (int) pi, pp->pat3_gammas[(int) pi]);
	if (gamma) *gamma = integer_gamma / 100.0;
	return true;
}

static void
apply_pattern_here(struct playout_policy *p, Board *b, Coord c, Stone color, struct move_queue *q, fixp_t *gammas)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
	struct move m2 = { .coord = c, .color = color };
	double gamma;
	if (!is_pass(c) && !is_resign(c) && board_is_valid_move(b, &m2) && test_pattern3_here(p, b, &m2, pp->middle_ladder, &gamma)) {
		mq_gamma_add(q, gammas, c, gamma, 1<<MQ_PAT3);
	}
}

/* Check if we match any pattern around given move (with the other color to play). */
static void
apply_pattern(struct playout_policy *p, Board *b, struct move *m, struct move *mm, struct move_queue *q, fixp_t *gammas)
{
	/* Suicides do not make any patterns and confuse us. */
	if (board_at(b, m->coord) == S_NONE || board_at(b, m->coord) == S_OFFBOARD)
		return;

	FOR8(m->coord, _, c) {
		apply_pattern_here(p, b, c, stone_other(m->color), q, gammas);
	} ENDFOR8

	if (mm) { /* Second move for pattern searching */
		FOR8(mm->coord, _, c) {
			if (NEIGHBOR8(m->coord, c))
				continue;
			apply_pattern_here(p, b, c, stone_other(m->color), q, gammas);
		} ENDFOR8
	}

  /*
	if (PLDEBUGL(5))
		mq_gamma_print(q, gammas, b, "Pattern");
  */
}

// Remove joseki check for now
/*
static void
joseki_check(struct playout_policy *p, Board *b, Stone to_play, struct move_queue *q)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
	if (!pp->jdict)
		return;

	for (int i = 0; i < 4; i++) {
		hash_t h = b->qhash[i] & joseki_hash_mask;
		Coord *cc = pp->jdict->patterns[h].moves[to_play];
		if (!cc) continue;
		for (; !is_pass(*cc); cc++) {
			if (coord_quadrant(*cc, b) != i)
				continue;
			if (board_is_valid_play(b, to_play, *cc))
				continue;
			mq_add(q, *cc, 1<<MQ_JOSEKI);
		}
	}

	if (q->moves > 0 && PLDEBUGL(5))
		mq_print(q, b, "Joseki");
}
*/

static void
global_atari_check(struct playout_policy *p, Board *b, Stone to_play, struct move_queue *q)
{
  // May affect performance.
	//if (b->clen == 0)
  //		return;

	struct moggy_policy *pp = (struct moggy_policy *)p->data;
	if (pp->capcheckall) {
    // When loop through groups, always starting from 1.
		for (int g = 1; g < b->_num_groups; g++) {
      // group_t gg = b->_groups[b->c[g]
      if (b->_groups[g].color == to_play) continue;
			group_atari_check(p->context, p->rand_func, pp->alwaysccaprate, b, g, to_play, q, NULL, pp->middle_ladder, 1<<MQ_GATARI);
    }
    /*
		if (PLDEBUGL(5))
			mq_print(q, b, "Global atari");
    */
		if (pp->fullchoose)
			return;
	}

	// int g_base = fast_random(b->clen);
  int g_base = p->rand_func(p->context, b->_num_groups - 1) + 1;
	for (int g = g_base; g < b->_num_groups; g++) {
    if (b->_groups[g].color == to_play) continue;
		group_atari_check(p->context, p->rand_func, pp->alwaysccaprate, b, g, to_play, q, NULL, pp->middle_ladder, 1<<MQ_GATARI);
		if (q->moves > 0) {
			/* XXX: Try carrying on. */
      /*
			if (PLDEBUGL(5))
				mq_print(q, b, "Global atari");
      */
			if (pp->fullchoose)
				return;
		}
	}
	for (int g = 1; g < g_base; g++) {
    if (b->_groups[g].color == to_play) continue;
		group_atari_check(p->context, p->rand_func, pp->alwaysccaprate, b, g, to_play, q, NULL, pp->middle_ladder, 1<<MQ_GATARI);
		if (q->moves > 0) {
			/* XXX: Try carrying on. */
      /*
			if (PLDEBUGL(5))
				mq_print(q, b, "Global atari");
      */
			if (pp->fullchoose)
				return;
		}
	}
}

static void
local_atari_check(struct playout_policy *p, Board *b, struct move *m, struct move_queue *q)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;

	/* Did the opponent play a self-atari? */
  group_t g_id = b->_infos[m->coord].id;
  Group *g = &b->_groups[g_id];
	if (g->liberties == 1) {
		group_atari_check(p->context, p->rand_func, pp->alwaysccaprate, b, g_id, OPPONENT(m->color), q, NULL, pp->middle_ladder, 1<<MQ_LATARI);
	}

	FOR4(m->coord, _, c) {
    group_t gg_id = b->_infos[c].id;
		if (gg_id == 0 || b->_groups[gg_id].liberties != 1)
			continue;
		group_atari_check(p->context, p->rand_func, pp->alwaysccaprate, b, gg_id, OPPONENT(m->color), q, NULL, pp->middle_ladder, 1<<MQ_LATARI);
	} ENDFOR4

  /*
	if (PLDEBUGL(5))
		mq_print(q, b, "Local atari");
  */
}

static void
local_ladder_check(struct playout_policy *p, Board *b, struct move *m, struct move_queue *q)
{
  group_t group = b->_infos[m->coord].id;
  Group *g = &b->_groups[group];
	if (g->liberties != 2) {
		return;
  }

  // Find
  Coord libs[2];
  get_nlibs_of_group(b, group, 2, libs);

	for (int i = 0; i < 2; i++) {
		Coord chase = libs[i];
		Coord escape = libs[1 - i];
		if (wouldbe_ladder(b, group, escape, chase, g->color))
			mq_add(q, chase, 1<<MQ_LADDER);
	}
  /*
	if (q->moves > 0 && PLDEBUGL(5))
		mq_print(q, b, "Ladder");
  */
}

static void
local_2lib_check(struct playout_policy *p, Board *b, struct move *m, struct move_queue *q)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
  group_t group = b->_infos[m->coord].id;
	group_t group2 = 0;

	/* Does the opponent have just two liberties? */
	if (b->_groups[group].liberties == 2) {
		group_2lib_check(p->context, p->rand_func, b, group, OPPONENT(m->color), q, 1<<MQ_L2LIB, pp->atari_miaisafe, pp->atari_def_no_hopeless);
#if 0
		/* We always prefer to take off an enemy chain liberty
		 * before pulling out ourselves. */
		/* XXX: We aren't guaranteed to return to that group
		 * later. */
		if (q->moves)
			return q->move[fast_random(q->moves)];
#endif
	}

	/* Then he took a third liberty from neighboring chain? */
	FOR4(m->coord, _, c) {
		group_t g = b->_infos[c].id;
		if (g == 0 || g == group || g == group2 || b->_groups[g].liberties != 2)
			continue;
		group_2lib_check(p->context, p->rand_func, b, g, OPPONENT(m->color), q, 1<<MQ_L2LIB, pp->atari_miaisafe, pp->atari_def_no_hopeless);
		group2 = g; // prevent trivial repeated checks
	} ENDFOR4

  /*
	if (PLDEBUGL(5))
		mq_print(q, b, "Local 2lib");
  */
}

static void
local_nlib_check(struct playout_policy *p, Board *b, struct move *m, struct move_queue *q)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
	Stone color = OPPONENT(m->color);

	/* Attacking N-liberty groups in general is probably
	 * not feasible. What we are primarily concerned about is
	 * counter-attacking groups that have two physical liberties,
	 * but three effective liberties:
	 *
	 * . O . . . . #
	 * O O X X X X #
	 * . X O O X . #
	 * . X O . O X #
	 * . X O O . X #
	 * # # # # # # #
	 *
	 * The time for this to come is when the opponent took a liberty
	 * of ours, making a few-liberty group. Therefore, we focus
	 * purely on defense.
	 *
	 * There is a tradeoff - down to how many liberties we need to
	 * be to start looking? nlib_count=3 will work for the left black
	 * group (2lib-solver will suggest connecting the false eye), but
	 * not for top black group (it is too late to start playing 3-3
	 * capturing race). Also, we cannot prevent stupidly taking an
	 * outside liberty ourselves; the higher nlib_count, the higher
	 * the chance we withstand this.
	 *
	 * However, higher nlib_count means that we will waste more time
	 * checking non-urgent or alive groups, and we will play silly
	 * or wasted moves around alive groups. */

	group_t group2 = 0;
  FOR8(m->coord, _, c) {
		group_t g = b->_infos[c].id;
		if (g == 0 || group2 == g || board_at(b, c) != color)
			continue;
    int libs = b->_groups[g].liberties;
		if (libs < 3 || libs > pp->nlib_count)
			continue;
		group_nlib_defense_check(p->context, p->rand_func, b, g, color, q, 1<<MQ_LNLIB);
		group2 = g; // prevent trivial repeated checks
	} ENDFOR8;

  /*
	if (PLDEBUGL(5))
		mq_print(q, b, "Local nlib");
  */
}

static Coord
nakade_check(struct playout_policy *p, Board *b, struct move *m, Stone to_play)
{
	Coord empty = M_PASS;
	FOR4(m->coord, _, c) {
		if (board_at(b, c) != S_NONE)
			continue;
		if (empty == M_PASS) {
			empty = c;
			continue;
		}
		if (! NEIGHBOR8(c, empty)) {
			/* Seems like impossible nakade
			 * shape! */
			return M_PASS;
		}
	} ENDFOR4
	assert(empty != M_PASS);

	Coord nakade = nakade_point(b, empty, OPPONENT(to_play));
  /*
	if (PLDEBUGL(5) && !is_pass(nakade))
		fprintf(stderr, "Nakade: %s\n", coord2sstr(nakade, b));
  */
	return nakade;
}

static void
eye_fix_check(struct playout_policy *p, Board *b, struct move *m, Stone to_play, struct move_queue *q)
{
	/* The opponent could have filled an approach liberty for
	 * falsifying an eye like these:
	 *
	 * # # # # # #    X . X X O O  last_move == 1
	 * X X 2 O 1 O    X X 2 O 1 O  => suggest 2
	 * X . X X O .    X . X X O .
	 * X X O O . .    X X O O . O
	 *
	 * This case seems pretty common (e.g. Zen-Ishida game). */

	/* Iterator for walking coordinates in a clockwise fashion
	 * (nei8 jumps "over" the middle point, inst. of "around). */
  int size = MACRO_BOARD_EXPAND_SIZE;
	int nei8_clockwise[10] = { -size-1, 1, 1, size, size, -1, -1, -size, -size, 1 };

	/* This is sort of like a cross between foreach_diag_neighbor
	 * and foreach_8neighbor. */
	Coord c = m->coord;
	for (int dni = 0; dni < 8; dni += 2) {
		// one diagonal neighbor
		Coord c0 = c + nei8_clockwise[dni];
		// adjecent staight neighbor
		Coord c1 = c0 + nei8_clockwise[dni + 1];
		// and adjecent another diagonal neighbor
		Coord c2 = c1 + nei8_clockwise[dni + 2];

		/* The last move must have a pair of unfriendly diagonal
		 * neighbors separated by a friendly stone. */
		//fprintf(stderr, "inv. %s(%s)-%s(%s)-%s(%s), imm. libcount %d\n", coord2sstr(c0, b), stone2str(board_at(b, c0)), coord2sstr(c1, b), stone2str(board_at(b, c1)), coord2sstr(c2, b), stone2str(board_at(b, c2)), immediate_liberty_count(b, c1));
		if ((board_at(b, c0) == to_play || board_at(b, c0) == S_OFFBOARD)
		    && board_at(b, c1) == m->color
		    && (board_at(b, c2) == to_play || board_at(b, c2) == S_OFFBOARD)
		    /* The friendly stone then must have an empty neighbor... */
		    /* XXX: This works only for single stone, not e.g. for two
		     * stones in a row */
		    && immediate_liberty_count(b, c1) > 0) {
			FOR4(c1, _, c) {
				if (c == m->coord || board_at(b, c) != S_NONE)
					continue;
				/* ...and the neighbor must potentially falsify
				 * an eye. */
				Coord falsifying = c;
        FORDIAG4(falsifying, _, c) {
					if (board_at(b, c) != S_NONE)
						continue;
					if (! IsEye(b, c, to_play))
						continue;
					/* We don't care about eyes that already
					 * _are_ false (board_is_false_eyelike())
					 * but that can become false. Therefore,
					 * either ==1 diagonal neighbor is
					 * opponent's (except in atari) or ==2
					 * are board edge. */
					Coord falsified = c;
					int color_diag_libs[S_MAX] = {0};
					FORDIAG4(falsified, _, c) {
						if (board_at(b, c) == m->color && group_at(b, c)->liberties == 1) {
							/* Suggest capturing a falsifying stone in atari. */
              group_t gg = b->_infos[c].id;
              Coord libb = get_nlibs_of_group(b, gg, 1, NULL);
							mq_add(q, libb, 0);
						} else {
							color_diag_libs[board_at(b, c)]++;
						}
					} ENDFORDIAG4
					if (color_diag_libs[m->color] == 1 || (color_diag_libs[m->color] == 0 && color_diag_libs[S_OFFBOARD] == 2)) {
						/* That's it. Fill the falsifying
						 * liberty before it's too late! */
						mq_add(q, falsifying, 0);
					}
				} ENDFORDIAG4
			} ENDFOR4
		}

		c = c1;
	}

  /*
	if (q->moves > 0 && PLDEBUGL(5))
		mq_print(q, b, "Eye fix");
  */
}

Coord
fillboard_check(struct playout_policy *p, Board *b)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;

  AllMoves moves;
  FindAllValidMoves(b, b->_next_player, &moves);

	unsigned int fbtries = moves.num_moves / 8;
	if (pp->fillboardtries < fbtries)
		fbtries = pp->fillboardtries;

	for (unsigned int i = 0; i < fbtries; i++) {
		Coord coord = moves.moves[p->rand_func(p->context, moves.num_moves)];
		if (immediate_liberty_count(b, coord) != 4)
			continue;
		FORDIAG4(coord, _, c) {
			if (board_at(b, c) != S_NONE)
				goto next_try;
		} ENDFORDIAG4;
		return coord;
next_try:
		;
	}
	return M_PASS;
}

Coord
playout_moggy_seqchoose(struct playout_policy *p, struct playout_setup *s, Board *b, Stone to_play)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
  struct move last_move = { .coord = b->_last_move, .color = OPPONENT(b->_next_player) };
  struct move last_move2 = { .coord = b->_last_move2, .color = b->_next_player };

  /*
	if (PLDEBUGL(5))
		board_print(b, stderr);
  */

	/* Ko fight check */
  /*
	if (!is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage
	    && pp->korate > p->rand_func(p->context, 100)) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			return b->last_ko.coord;
	}
  */

	/* Local checks */
	if (!is_pass(b->_last_move)) {
		/* Local group in atari? */
		if (pp->lcapturerate > p->rand_func(p->context, 100)) {
			struct move_queue q;  q.moves = 0;
      // printf("Perform local capture check\n");
			local_atari_check(p, b, &last_move, &q);
			if (q.moves > 0) {
        // printf("local capture\n");
				return mq_pick(p->context, p->rand_func, &q);
      }
		}

		/* Local group trying to escape ladder? */
		if (pp->ladderrate > p->rand_func(p->context, 100)) {
			struct move_queue q; q.moves = 0;
      // printf("Perform local ladder check\n");
			local_ladder_check(p, b, &last_move, &q);
			if (q.moves > 0) {
        // printf("local ladder\n");
				return mq_pick(p->context, p->rand_func, &q);
      }
		}

		/* Local group can be PUT in atari? */
		if (pp->atarirate > p->rand_func(p->context, 100)) {
			struct move_queue q; q.moves = 0;
      // printf("Perform local 2lib check\n");
			local_2lib_check(p, b, &last_move, &q);
			if (q.moves > 0) {
        // printf("local put to atari\n");
				return mq_pick(p->context, p->rand_func, &q);
      }
		}

		/* Local group reduced some of our groups to 3 libs? */
		if (pp->nlibrate > p->rand_func(p->context, 100)) {
			struct move_queue q; q.moves = 0;
      // printf("Perform local nlib check\n");
			local_nlib_check(p, b, &last_move, &q);
			if (q.moves > 0) {
        // printf("local nlib check\n");
				return mq_pick(p->context, p->rand_func, &q);
      }
		}

		/* Some other semeai-ish shape checks */
		if (pp->eyefixrate > p->rand_func(p->context, 100)) {
			struct move_queue q; q.moves = 0;
      // printf("Perform local eye fix check\n");
			eye_fix_check(p, b, &last_move, to_play, &q);
			if (q.moves > 0) {
        // printf("local eye fix\n");
				return mq_pick(p->context, p->rand_func, &q);
      }
		}

		/* Nakade check */
		if (pp->nakaderate > p->rand_func(p->context, 100)
		    && immediate_liberty_count(b, b->_last_move) > 0) {
      // printf("Perform local nakade check\n");
			Coord nakade = nakade_check(p, b, &last_move, to_play);
			if (!is_pass(nakade)) {
        // printf("local nakade\n");
				return nakade;
      }
		}

		/* Check for patterns we know */
		if (pp->patternrate > p->rand_func(p->context, 100)) {
			struct move_queue q; q.moves = 0;
			fixp_t gammas[MQL];
      struct move *mm2 = (pp->pattern2 && !is_pass(b->_last_move2) && b->_last_move2 != M_RESIGN) ? &last_move2 : NULL;
      // printf("Perform local pattern check\n");
			apply_pattern(p, b, &last_move, mm2, &q, gammas);
			if (q.moves > 0) {
        // printf("Local pattern\n");
				return mq_gamma_pick(p->context, p->rand_func, &q, gammas);
      }
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > p->rand_func(p->context, 100)) {
		struct move_queue q; q.moves = 0;
    // printf("Perform global capture check\n");
		global_atari_check(p, b, to_play, &q);
		if (q.moves > 0) {
      // printf("Global atari\n");
			return mq_pick(p->context, p->rand_func, &q);
    }
	}

	/* Joseki moves? */
  /*
	if (pp->josekirate > p->rand_func(p->context, 100)) {
		struct move_queue q; q.moves = 0;
		joseki_check(p, b, to_play, &q);
		if (q.moves > 0)
			return mq_pick(p->context, p->rand_func, &q);
	}
  */

	/* Fill board */
	if (pp->fillboardtries > 0) {
    // printf("Fill board check...\n");
		Coord c = fillboard_check(p, b);
		if (!is_pass(c)) {
      // printf("Global board_filling\n");
			return c;
    }
	}

	return M_PASS;
}

/* Pick a move from queue q, giving different likelihoods to moves
 * based on their tags. */
Coord
mq_tagged_choose(struct playout_policy *p, Board *b, Stone to_play, struct move_queue *q)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;

	/* First, merge all entries for a move. */
	/* We use a naive O(N^2) since the average length of the queue
	 * is about 1.4. */
	for (unsigned int i = 0; i < q->moves; i++) {
		for (unsigned int j = i + 1; j < q->moves; j++) {
			if (q->move[i] != q->move[j])
				continue;
			q->tag[i] |= q->tag[j];
			q->moves--;
			q->tag[j] = q->tag[q->moves];
			q->move[j] = q->move[q->moves];
		}
	}

	/* Now, construct a probdist. */
	fixp_t total = 0;
	fixp_t pd[q->moves];
	for (unsigned int i = 0; i < q->moves; i++) {
		double val = 1.0;
		assert(q->tag[i] != 0);
		for (int j = 0; j < MQ_MAX; j++)
			if (q->tag[i] & (1<<j)) {
				//fprintf(stderr, "%s(%x) %d %f *= %f\n", coord2sstr(q->move[i], b), q->tag[i], j, val, pp->mq_prob[j]);
				val *= pp->mq_prob[j];
			}
		pd[i] = val;
		total += pd[i];
	}
	total += double_to_fixp(pp->tenuki_prob);

	/* Finally, pick a move! */
	fixp_t stab = fast_irandom(p->context, p->rand_func, total);
  /*
	if (PLDEBUGL(5)) {
		fprintf(stderr, "Pick (total %.3f stab %.3f): ", fixp_to_double(total), fixp_to_double(stab));
		for (unsigned int i = 0; i < q->moves; i++) {
			fprintf(stderr, "%s(%x:%.3f) ", coord2sstr(q->move[i], b), q->tag[i], fixp_to_double(pd[i]));
		}
		fprintf(stderr, "\n");
	}
  */
	for (unsigned int i = 0; i < q->moves; i++) {
		//fprintf(stderr, "%s(%x) %f (%f/%f)\n", coord2sstr(q->move[i], b), q->tag[i], fixp_to_double(stab), fixp_to_double(pd[i]), fixp_to_double(total));
		if (stab < pd[i])
			return q->move[i];
		stab -= pd[i];
	}

	/* Tenuki. */
	assert(stab < double_to_fixp(pp->tenuki_prob));
	return M_PASS;
}

Coord
playout_moggy_fullchoose(struct playout_policy *p, struct playout_setup *s, Board *b, Stone to_play)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
	struct move_queue q; q.moves = 0;
  struct move last_move = { .coord = b->_last_move, .color = OPPONENT(b->_next_player) };
  struct move last_move2 = { .coord = b->_last_move2, .color = b->_next_player };

  /*
	if (PLDEBUGL(5))
		board_print(b, stderr);
  */

	/* Ko fight check */
  /*
	if (pp->korate > 0 && !is_pass(b->last_ko.coord) && is_pass(b->ko.coord)
	    && b->moves - b->last_ko_age < pp->koage) {
		if (board_is_valid_play(b, to_play, b->last_ko.coord)
		    && !is_bad_selfatari(b, to_play, b->last_ko.coord))
			mq_add(&q, b->last_ko.coord, 1<<MQ_KO);
	}
  */

	/* Local checks */
	if (!is_pass(b->_last_move)) {
		/* Local group in atari? */
		if (pp->lcapturerate > 0)
			local_atari_check(p, b, &last_move, &q);

		/* Local group trying to escape ladder? */
		if (pp->ladderrate > 0)
			local_ladder_check(p, b, &last_move, &q);

		/* Local group can be PUT in atari? */
		if (pp->atarirate > 0)
			local_2lib_check(p, b, &last_move, &q);

		/* Local group reduced some of our groups to 3 libs? */
		if (pp->nlibrate > 0)
			local_nlib_check(p, b, &last_move, &q);

		/* Some other semeai-ish shape checks */
		if (pp->eyefixrate > 0)
			eye_fix_check(p, b, &last_move, to_play, &q);

		/* Nakade check */
		if (pp->nakaderate > 0 && immediate_liberty_count(b, last_move.coord) > 0) {
			Coord nakade = nakade_check(p, b, &last_move, to_play);
			if (!is_pass(nakade))
				mq_add(&q, nakade, 1<<MQ_NAKADE);
		}

		/* Check for patterns we know */
		if (pp->patternrate > 0) {
			fixp_t gammas[MQL];
      struct move *mm2 = (pp->pattern2 && b->_last_move2 != M_PASS && b->_last_move2 != M_RESIGN) ? &last_move2 : NULL;
			apply_pattern(p, b, &last_move, mm2, &q, gammas);
			// FIXME: Use the gammas.
		}
	}

	/* Global checks */

	/* Any groups in atari? */
	if (pp->capturerate > 0)
		global_atari_check(p, b, to_play, &q);

	/* Joseki moves? */
  /*
	if (pp->josekirate > 0)
		joseki_check(p, b, to_play, &q);
  */

#if 0
	/* Average length of the queue is 1.4 move. */
	printf("MQL %d ", q.moves);
	for (unsigned int i = 0; i < q.moves; i++)
		printf("%s ", coord2sstr(q.move[i], b));
	printf("\n");
#endif

	if (q.moves > 0)
		return mq_tagged_choose(p, b, to_play, &q);

	/* Fill board */
	if (pp->fillboardtries > 0) {
		Coord c = fillboard_check(p, b);
		if (!is_pass(c))
			return c;
	}

	return M_PASS;
}

bool
playout_moggy_permit(struct playout_policy *p, Board *b, struct move *m)
{
	struct moggy_policy *pp = (struct moggy_policy *)p->data;

	/* The idea is simple for now - never allow self-atari moves.
	 * They suck in general, but this also permits us to actually
	 * handle seki in the playout stage. */
  bool selfatari;
	if (p->rand_func(p->context, 100) >= pp->selfatarirate) {
		if (PLDEBUGL(5))
			fprintf(stderr, "skipping sar test\n");
		goto sar_skip;
	}
	selfatari = is_bad_selfatari(b, m->color, m->coord);
	if (selfatari) {
    /*
		if (PLDEBUGL(5))
			fprintf(stderr, "__ Prohibiting self-atari %s %s\n",
				stone2str(m->color), coord2sstr(m->coord, b));
    */
		if (pp->selfatari_other) {
			/* Ok, try the other liberty of the atari'd group. */
			coord_t c = selfatari_cousin(p->context, p->rand_func, b, m->color, m->coord, NULL);
			if (is_pass(c)) return false;
      /*
			if (PLDEBUGL(5))
				fprintf(stderr, "___ Redirecting to other lib %s\n",
					coord2sstr(c, b));
      */
			m->coord = c;
			return true;
		}
		return false;
	}
sar_skip:

	/* Check if we don't seem to be filling our eye. This should
	 * happen only for false eyes, but some of them are in fact
	 * real eyes with diagonal filled by a dead stone. Prefer
	 * to counter-capture in that case. */
  BOOL eyefill;
	if (p->rand_func(p->context, 100) >= pp->eyefillrate) {
		if (PLDEBUGL(5))
			fprintf(stderr, "skipping eyefill test\n");
		goto eyefill_skip;
	}
	eyefill = board_is_eyelike(b, m->coord, m->color);
	if (eyefill) {
    Coord cc;
    Coord libs[2];

    FORDIAG4(m->coord, _, c) {
			if (board_at(b, c) != stone_other(m->color))
				continue;
      group_t g_id = b->_infos[c].id;
      int nlibs = b->_groups[g_id].liberties;
			switch (nlibs) {
        case 1: /* Capture! */
          cc = get_nlibs_of_group(b, g_id, 1, NULL);
          if (PLDEBUGL(5))
            fprintf(stderr, "___ Redirecting to capture %s\n",
                coord2sstr(cc, b));
          m->coord = cc;
          return true;
        case 2: /* Try to switch to some 2-lib neighbor. */
          get_nlibs_of_group(b, g_id, 2, libs);
          for (int i = 0; i < 2; i++) {
            coord_t l = libs[i];
            if (board_is_one_point_eye(b, l, board_at(b, c)))
              continue;
            if (is_bad_selfatari(b, m->color, l))
              continue;
            m->coord = l;
            return true;
          }
          break;
			}
		} ENDFORDIAG4
	}

eyefill_skip:
	return true;
}

void *playout_moggy_init(char *arg)
{
	struct playout_policy *p = (struct playout_policy *)calloc(1, sizeof(*p));
	struct moggy_policy *pp = (struct moggy_policy *)calloc(1, sizeof(*pp));
	p->data = pp;
	p->choose = playout_moggy_seqchoose;
  p->permit = playout_moggy_permit;

  // Initialize pattern matcher.
  pp->pattern_matcher = InitPatternDB();

	/* These settings are tuned for 19x19 play with several threads
	 * on reasonable time limits (i.e., rather large number of playouts).
	 * XXX: no 9x9 tuning has been done recently. */
	int rate = board_large(b) ? 80 : 90;

	pp->patternrate = pp->eyefixrate = 100;
	pp->lcapturerate = 90;
	pp->atarirate = pp->josekirate = -1U;
	pp->nakaderate = 60;
	pp->korate = 40; pp->koage = 4;
	pp->alwaysccaprate = 40;
	pp->eyefillrate = 60;
	pp->nlibrate = 25;

	/* selfatarirate is slightly special, since to avoid playing some
	 * silly move that stays on the board, it needs to block it many
	 * times during a simulation - we'd like that to happen in most
	 * simulations, so we try to use a very high selfatarirate.
	 * XXX: Perhaps it would be better to permanently ban moves in
	 * the current simulation after testing them once.
	 * XXX: We would expect the above to be the case, but since some
	 * unclear point, selfatari 95 -> 60 gives a +~50Elo boost against
	 * GNUGo.  This might be indicative of some bug, FIXME bisect? */
	pp->selfatarirate = 60;
	pp->selfatari_other = true;

	pp->pattern2 = true;

	pp->cap_stone_min = 2;
	pp->cap_stone_max = 15;
	pp->cap_stone_denom = 200;

	pp->atari_def_no_hopeless = !board_large(b);
	pp->atari_miaisafe = true;
	pp->nlib_count = 4;

	/* C is stupid. */
	double mq_prob_default[MQ_MAX];
  mq_prob_default[MQ_KO] = 6.0;
  mq_prob_default[MQ_NAKADE] = 5.5;
  mq_prob_default[MQ_LATARI] = 5.0;
  mq_prob_default[MQ_L2LIB] = 4.0;
  mq_prob_default[MQ_LNLIB] = 3.5;
  mq_prob_default[MQ_PAT3] = 3.0;
  mq_prob_default[MQ_GATARI] = 2.0;
  mq_prob_default[MQ_JOSEKI] = 1.0;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ":");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug") && optval) {
				p->debug_level = atoi(optval);
			} else if (!strcasecmp(optname, "lcapturerate") && optval) {
				pp->lcapturerate = atoi(optval);
			} else if (!strcasecmp(optname, "ladderrate") && optval) {
				/* Note that ladderrate is considered obsolete;
				 * it is ineffective and superseded by the
				 * prune_ladders prior. */
				pp->ladderrate = atoi(optval);
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				pp->atarirate = atoi(optval);
			} else if (!strcasecmp(optname, "nlibrate") && optval) {
				pp->nlibrate = atoi(optval);
			} else if (!strcasecmp(optname, "capturerate") && optval) {
				pp->capturerate = atoi(optval);
			} else if (!strcasecmp(optname, "patternrate") && optval) {
				pp->patternrate = atoi(optval);
			} else if (!strcasecmp(optname, "selfatarirate") && optval) {
				pp->selfatarirate = atoi(optval);
			} else if (!strcasecmp(optname, "eyefillrate") && optval) {
				pp->eyefillrate = atoi(optval);
			} else if (!strcasecmp(optname, "korate") && optval) {
				pp->korate = atoi(optval);
			} else if (!strcasecmp(optname, "josekirate") && optval) {
				pp->josekirate = atoi(optval);
			} else if (!strcasecmp(optname, "nakaderate") && optval) {
				pp->nakaderate = atoi(optval);
			} else if (!strcasecmp(optname, "eyefixrate") && optval) {
				pp->eyefixrate = atoi(optval);
			} else if (!strcasecmp(optname, "alwaysccaprate") && optval) {
				pp->alwaysccaprate = atoi(optval);
			} else if (!strcasecmp(optname, "rate") && optval) {
				rate = atoi(optval);
			} else if (!strcasecmp(optname, "fillboardtries")) {
				pp->fillboardtries = atoi(optval);
			} else if (!strcasecmp(optname, "koage") && optval) {
				pp->koage = atoi(optval);
			} else if (!strcasecmp(optname, "pattern2")) {
				pp->pattern2 = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "selfatari_other")) {
				pp->selfatari_other = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "capcheckall")) {
				pp->capcheckall = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "cap_stone_min") && optval) {
				pp->cap_stone_min = atoi(optval);
			} else if (!strcasecmp(optname, "cap_stone_max") && optval) {
				pp->cap_stone_max = atoi(optval);
			} else if (!strcasecmp(optname, "cap_stone_denom") && optval) {
				pp->cap_stone_denom = atoi(optval);
			} else if (!strcasecmp(optname, "atari_miaisafe")) {
				pp->atari_miaisafe = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "atari_def_no_hopeless")) {
				pp->atari_def_no_hopeless = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "nlib_count") && optval) {
				pp->nlib_count = atoi(optval);
			} else if (!strcasecmp(optname, "middle_ladder")) {
				pp->middle_ladder = optval && *optval == '0' ? false : true;
			} else if (!strcasecmp(optname, "fullchoose")) {
				pp->fullchoose = true;
				p->choose = optval && *optval == '0' ? playout_moggy_seqchoose : playout_moggy_fullchoose;
			} else if (!strcasecmp(optname, "mqprob") && optval) {
				/* KO%LATARI%L2LIB%LNLIB%PAT3%GATARI%JOSEKI%NAKADE */
				for (int i = 0; *optval && i < MQ_MAX; i++) {
					pp->mq_prob[i] = atof(optval);
					optval += strcspn(optval, "%");
					if (*optval) optval++;
				}
			}
		}
	}
	if (pp->lcapturerate == -1U) pp->lcapturerate = rate;
	if (pp->atarirate == -1U) pp->atarirate = rate;
	if (pp->nlibrate == -1U) pp->nlibrate = rate;
	if (pp->capturerate == -1U) pp->capturerate = rate;
	if (pp->patternrate == -1U) pp->patternrate = rate;
	if (pp->selfatarirate == -1U) pp->selfatarirate = rate;
	if (pp->eyefillrate == -1U) pp->eyefillrate = rate;
	if (pp->korate == -1U) pp->korate = rate;
	if (pp->josekirate == -1U) pp->josekirate = rate;
	if (pp->ladderrate == -1U) pp->ladderrate = rate;
	if (pp->nakaderate == -1U) pp->nakaderate = rate;
	if (pp->eyefixrate == -1U) pp->eyefixrate = rate;
	if (pp->alwaysccaprate == -1U) pp->alwaysccaprate = rate;

	return p;
}

void playout_moggy_destroy(void *h) {
  struct playout_policy *p = (struct playout_policy *)h;
	struct moggy_policy *pp = (struct moggy_policy *)p->data;
  DestroyPatternDB(pp->pattern_matcher);
  free(pp);
  free(p);
}

static bool board_try_random_move(Board *b, Coord coord, Stone color, struct playout_policy *policy)
{
  struct move m = { .coord = coord, .color = color };
  /*
	if (DEBUGL(6))
		fprintf(stderr, "trying random move %d: %d,%d %s %d\n", f, coord_x(*coord, b), coord_y(*coord, b), coord2sstr(*coord, b), board_is_valid_move(b, &m));
  */
	if (unlikely(board_is_one_point_eye(b, coord, color)) /* bad idea to play into one, usually */
		|| !board_is_valid_move(b, &m)
		|| (policy->permit && !policy->permit(policy, b, &m)))
		return false;

  return likely(board_play(b, &m) >= 0);
}

static void
board_play_random(Board *b, Stone color, coord_t *coord, struct playout_policy *policy)
{
  // Get all moves.
  AllMoves moves;
  FindAllValidMoves(b, b->_next_player, &moves);

  int base, f;
	if (unlikely(moves.num_moves == 0))
		goto pass;

	base = policy->rand_func(policy->context, moves.num_moves);
	for (f = base; f < moves.num_moves; f++) {
		if (board_try_random_move(b, moves.moves[f], color, policy)) {
      *coord = moves.moves[f];
			return;
    }
  }
	for (f = 0; f < base; f++) {
		if (board_try_random_move(b, moves.moves[f], color, policy)) {
      *coord = moves.moves[f];
			return;
    }
  }

pass:
	*coord = M_PASS;
	struct move m = { M_PASS, color };
	board_play(b, &m);
}

static coord_t play_random_move(Board *b, Stone color, struct playout_policy *policy)
{
	coord_t coord = policy->choose(policy, NULL, b, color);

	if (is_pass(coord)) {
play_random:
		/* Defer to uniformly random move choice. */
		/* This must never happen if the policy is tracking
		 * internal board state, obviously. */
    // printf("Play random move...\n");
		board_play_random(b, color, &coord, policy);

	} else {
		struct move m;
		m.coord = coord; m.color = color;
		if (board_play(b, &m) < 0) {
      /*
			if (PLDEBUGL(4)) {
				fprintf(stderr, "Pre-picked move %d,%d is ILLEGAL:\n",
					coord_x(coord, b), coord_y(coord, b));
				board_print(b, stderr);
			}
      */
			goto play_random;
		}
	}

	return coord;
}

static unsigned int local_fast_random(void *context, unsigned int num_max) {
  return rand() % num_max;
}

DefPolicyMove play_random_game(void *pp, void *context, RandFunc randfunc, Board *b, const Region *r, int max_depth, BOOL verbose) {
  PlayoutPolicy* policy = (PlayoutPolicy *)pp;
  if (randfunc == NULL) {
    policy->context = NULL;
    policy->rand_func = local_fast_random;
  } else {
    policy->context = context;
    policy->rand_func = randfunc;
  }

  // printf("Start pachi default policy!\n");

  // assert(setup && policy);
  if (max_depth < 0) max_depth = 1000;
  Stone color = b->_next_player;

  char buf[100];
  int passes = is_pass(b->_last_move) && b->_ply >= 2;

  coord_t coord;

  while (max_depth-- && passes < 2) {
    coord = play_random_move(b, color, policy);

    if (verbose) {
      printf("Move = %s\n", get_move_str(coord, color, buf));
      ShowBoard(b, SHOW_LAST_MOVE);
      printf("\n");
    }

    /*
    if (PLDEBUGL(7)) {
      fprintf(stderr, "%s %s\n", stone2str(color), coord2sstr(coord, b));
      if (PLDEBUGL(8))
        board_print(b, stderr);
    }
    */

    if (unlikely(is_pass(coord))) {
      passes++;
    } else {
      passes = 0;
    }

    /*
       if (amafmap) {
       assert(amafmap->gamelen < MAX_GAMELEN);
       amafmap->is_ko_capture[amafmap->gamelen] = board_playing_ko_threat(b);
       amafmap->game[amafmap->gamelen++] = coord;
       }
       */
    color = stone_other(color);
  }
  // printf("Finish default policy!\n");
  DefPolicyMove move;
  move.m = coord;
  move.gamma = 0;
  move.type = NORMAL;
  move.game_ended = (passes == 2 ? TRUE : FALSE);
  return move;
}
