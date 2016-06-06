#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "../../board/board.h"
#include "../mq.h"
#include "1lib.h"
#include "ladder.h"

#define BOARD_MAX_SIZE 19

bool
is_border_ladder(Board *b, Coord coord, Stone lcolor)
{
	int x = X(coord), y = Y(coord);

  /*
	if (DEBUGL(5))
		fprintf(stderr, "border ladder\n");
  */
	/* Direction along border; xd is horiz. border, yd vertical. */
	int xd = 0, yd = 0;
	if (b->_infos[L(coord)].color == S_OFFBOARD || b->_infos[R(coord)].color == S_OFFBOARD)
		yd = 1;
	else
		xd = 1;
	/* Direction from the border; -1 is above/left, 1 is below/right. */
	int dd = (board_atxy(b, x + yd, y + xd) == S_OFFBOARD) ? 1 : -1;
	if (DEBUGL(6))
		fprintf(stderr, "xd %d yd %d dd %d\n", xd, yd, dd);
	/* | ? ?
	 * | . O #
	 * | c X #
	 * | . O #
	 * | ? ?   */
	/* This is normally caught, unless we have friends both above
	 * and below... */
	if (board_atxy(b, x + xd * 2, y + yd * 2) == lcolor
	    && board_atxy(b, x - xd * 2, y - yd * 2) == lcolor)
		return false;

	/* ...or can't block where we need because of shortage
	 * of liberties. */
	group_t g1 = group_atxy(b, x + xd - yd * dd, y + yd - xd * dd);
	int libs1 = b->_groups[g1].liberties;
	group_t g2 = group_atxy(b, x - xd - yd * dd, y - yd - xd * dd);
	int libs2 = b->_groups[g2].liberties;
  /*
	if (DEBUGL(6))
		fprintf(stderr, "libs1 %d libs2 %d\n", libs1, libs2);
  */
	/* Already in atari? */
	if (libs1 < 2 || libs2 < 2)
		return false;

  Coord libs_g1[2], libs_g2[2];
  get_nlibs_of_group(b, g1, 2, libs_g1);
  get_nlibs_of_group(b, g2, 2, libs_g2);

	/* Would be self-atari? */
	if (libs1 < 3 && (board_atxy(b, x + xd * 2, y + yd * 2) != S_NONE || NEIGHBOR4(libs_g1[0], libs_g1[1])))
		return false;
	if (libs2 < 3 && (board_atxy(b, x - xd * 2, y - yd * 2) != S_NONE || NEIGHBOR4(libs_g2[0], libs_g2[1])))
		return false;
	return true;
}


/* This is a rather expensive ladder reader. It can read out any sequences
 * where laddered group should be kept at two liberties. The recursion
 * always makes a "to-be-laddered" move and then considers the chaser's
 * two alternatives (usually, one of them is trivially refutable). The
 * function returns true if there is a branch that ends up with laddered
 * group captured, false if not (i.e. for each branch, laddered group can
 * gain three liberties). */

static bool
middle_ladder_walk(Board *b, Board *bset, group_t laddered, Coord nextmove, Stone lcolor)
{
	assert(group_at(b, laddered)->liberties == 1);

	/* First, escape. */
  /*
	if (DEBUGL(6))
		fprintf(stderr, "  ladder escape %s\n", coord2sstr(nextmove, b));
  */
  GroupId4 ids;
  if (!TryPlay2(b, nextmove, &ids)) error("The play should never be wrong!");
  Play(b, &ids);

	// laddered = group_at(b, laddered);
  /*
	if (DEBUGL(8)) {
		board_print(b, stderr);
		fprintf(stderr, "%s c %d\n", coord2sstr(laddered, b), board_group_info(b, laddered).libs);
	}
  */

  int laddered_libs = b->_groups[laddered].liberties;

	if (laddered_libs == 1) {
    /*
		if (DEBUGL(6))
			fprintf(stderr, "* we can capture now\n");
    */
		return true;
	}
	if (laddered_libs > 2) {
    /*
		if (DEBUGL(6))
			fprintf(stderr, "* we are free now\n");
    */
		return false;
	}

  FOR4(nextmove, _, c) {
		if (board_at(b, c) == OPPONENT(lcolor) && group_at(b, c)->liberties == 1) {
			/* We can capture one of the ladder stones
			 * anytime later. */
			/* XXX: If we were very lucky, capturing
			 * this stone will not help us escape.
			 * That should be pretty rate. */
      /*
			if (DEBUGL(6))
				fprintf(stderr, "* can capture chaser\n");
      */
			return false;
		}
	} ENDFOR4

	/* Now, consider alternatives. */
	int liblist[2], libs = 0;
  Coord tmp_libs[2];
  get_nlibs_of_group(b, laddered, 2, tmp_libs);
	for (int i = 0; i < 2; i++) {
		Coord ataristone = tmp_libs[i];
		Coord escape = tmp_libs[1 - i];
		if (immediate_liberty_count(b, escape) > 2 + NEIGHBOR4(ataristone, escape)) {
			/* Too much free space, ignore. */
			continue;
		}
		liblist[libs++] = i;
	}

	/* Try out the alternatives. */
	bool is_ladder = false;
	for (int i = 0; !is_ladder && i < libs; i++) {
		Board *b2 = b;
		if (i != libs - 1) {
			b2 = bset++;
      CopyBoard(b2, b);
		}

    Coord libs_b2[2];
    get_nlibs_of_group(b2, laddered, 2, libs_b2);

		Coord ataristone = libs_b2[liblist[i]];
		// Coord escape = board_group_info(b2, laddered).lib[1 - liblist[i]];
		struct move m = { ataristone, OPPONENT(lcolor) };
    bool play_successful = TryPlay2(b2, ataristone, &ids);
    if (play_successful) Play(b2, &ids);
		/* If we just played self-atari, abandon ship. */
		/* XXX: If we were very lucky, capturing this stone will
		 * not help us escape. That should be pretty rate. */
    /*
		if (DEBUGL(6))
			fprintf(stderr, "(%d=%d) ladder atari %s (%d libs)\n", i, res, coord2sstr(ataristone, b2), board_group_info(b2, group_at(b2, ataristone)).libs);
    */
		if (play_successful && group_at(b2, ataristone)->liberties > 1) {
      Coord last_lib = get_nlibs_of_group(b2, laddered, 1, NULL);
			is_ladder = middle_ladder_walk(b2, bset, laddered, last_lib, lcolor);
    }

    /* Why we need to do deallocation?
		if (i != libs - 1) {
			board_done_noalloc(b2);
		}
    */
	}
  /*
	if (DEBUGL(6))
		fprintf(stderr, "propagating %d\n", is_ladder);
  */
	return is_ladder;
}

bool
is_middle_ladder(Board *b, Coord coord, group_t laddered, Stone lcolor)
{
	/* TODO: Remove the redundant parameters. */
	assert(group_at(b, laddered)->liberties == 1);

  Coord last_lib = get_nlibs_of_group(b, laddered, 1, NULL);
	assert(last_lib == coord);
	assert(group_at(b, laddered)->color == lcolor);

	/* If we can move into empty space or do not have enough space
	 * to escape, this is obviously not a ladder. */
	if (immediate_liberty_count(b, coord) != 2) {
    /*
		if (DEBUGL(5))
			fprintf(stderr, "no ladder, wrong free space\n");
    */
		return false;
	}

	/* A fair chance for a ladder. Group in atari, with some but limited
	 * space to escape. Time for the expensive stuff - set up a temporary
	 * board and start selective 2-liberty search. */

	Board *bset = (Board *)malloc(BOARD_MAX_SIZE * 2 * sizeof(Board));

	struct move_queue ccq = { .moves = 0 };
	if (can_countercapture(b, lcolor, laddered, lcolor, &ccq, 0)) {
		/* We could escape by countercapturing a group.
		 * Investigate. */
		assert(ccq.moves > 0);
		for (unsigned int i = 0; i < ccq.moves; i++) {
			Board b2;
			CopyBoard(&b2, b);
			bool is_ladder = middle_ladder_walk(&b2, bset, laddered, ccq.move[i], lcolor);
			// board_done_noalloc(&b2);
			if (!is_ladder) {
				free(bset);
				return false;
			}
		}
	}

	Board b2;
  CopyBoard(&b2, b);
  Coord last_lib2 = get_nlibs_of_group(&b2, laddered, 1, NULL);

	bool is_ladder = middle_ladder_walk(&b2, bset, laddered, last_lib2, lcolor);
	// board_done_noalloc(&b2);
	free(bset);
	return is_ladder;
}

bool
wouldbe_ladder(Board *b, group_t group, Coord escapelib, Coord chaselib, Stone lcolor)
{
	assert(b->_groups[group].liberties == 2);
	assert(b->_groups[group].color == lcolor);

  /*
	if (DEBUGL(6))
		fprintf(stderr, "would-be ladder check - does %s %s play out chasing move %s?\n",
			stone2str(lcolor), coord2sstr(escapelib, b), coord2sstr(chaselib, b));
  */

	if (!NEIGHBOR8(escapelib, chaselib)) {
    /*
		if (DEBUGL(5))
			fprintf(stderr, "cannot determine ladder for remote simulated stone\n");
    */
		return false;
	}

	if (neighbor_count_at(b, chaselib, lcolor) != 1 || immediate_liberty_count(b, chaselib) != 2) {
    /*
		if (DEBUGL(5))
			fprintf(stderr, "overly trivial for a ladder\n");
    */
		return false;
	}

	bool is_ladder = false;
	Board *bset = (Board *)malloc(BOARD_MAX_SIZE * 2 * sizeof(Board));
	Board b2;
	CopyBoard(&b2, b);

  GroupId4 ids;
  if (TryPlay(&b2, X(chaselib), Y(chaselib), OPPONENT(lcolor), &ids)) {
    Play(&b2, &ids);
    Coord last_lib2 = get_nlibs_of_group(&b2, group, 1, NULL);
		is_ladder = middle_ladder_walk(&b2, bset, group, last_lib2, lcolor);
  }

	// board_done_noalloc(&b2);
	free(bset);
	return is_ladder;
}
