#ifndef PACHI_TACTICS_LADDER_H
#define PACHI_TACTICS_LADDER_H

/* Reading ladders. */
#include "../../board/board.h"
#include "../board_interface.h"

/* Check if escaping on this liberty by given group in atari would play out
 * a simple ladder. */
/* Two ways of ladder reading can be enabled separately; simple first-line
 * ladders and trivial middle-board ladders. */
static bool is_ladder(Board *b, Coord coord, group_t laddered, bool test_middle);

/* Check if a 2-lib group of color @lcolor escaping at @escapelib would be
 * caught in a ladder given opponent stone at @chaselib.  */
bool wouldbe_ladder(Board *b, group_t group, Coord escapelib, Coord chaselib, Stone lcolor);

bool is_border_ladder(Board *b, Coord coord, Stone lcolor);
bool is_middle_ladder(Board *b, Coord coord, group_t group, Stone lcolor);
static inline bool
is_ladder(Board *b, Coord coord, group_t laddered, bool test_middle)
{
	Stone lcolor = b->_groups[laddered].color;

  /*
	if (DEBUGL(6))
		fprintf(stderr, "ladder check - does %s play out %s's laddered group %s?\n",
			coord2sstr(coord, b), stone2str(lcolor), coord2sstr(laddered, b));
  */

	/* First, special-case first-line "ladders". This is a huge chunk
	 * of ladders we actually meet and want to play. */
	if (neighbor_count_at(b, coord, S_OFFBOARD) == 1
	    && neighbor_count_at(b, coord, lcolor) == 1) {
		bool l = is_border_ladder(b, coord, lcolor);
		// if (DEBUGL(6)) fprintf(stderr, "border ladder solution: %d\n", l);
		return l;
	}

	bool l = test_middle && is_middle_ladder(b, coord, laddered, lcolor);
	// if (DEBUGL(6)) fprintf(stderr, "middle ladder solution: %d\n", l);
	return l;
}

#endif
