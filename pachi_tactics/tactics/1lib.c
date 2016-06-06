#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "../../board/board.h"
#include "../mq.h"
#include "1lib.h"
#include "ladder.h"
#include "selfatari.h"

/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS
//
static bool
can_play_on_lib(Board *b, group_t group, Stone to_play)
{
	Coord capture = get_nlibs_of_group(b, group, 1, NULL);
  /*
	if (DEBUGL(6))
		fprintf(stderr, "can capture group %d (%s)?\n",
			g, coord2sstr(capture, b));
  */
	/* Does playing on the liberty usefully capture the group? */
  GroupId4 ids;
	if (TryPlay2(b, capture, &ids) && !is_bad_selfatari(b, to_play, capture))
		return true;

	return false;
}

/* For given position @c, decide if this is a group that is in danger from
 * @capturer and @to_play can do anything about it (play at the last
 * liberty to either capture or escape). */
/* Note that @to_play is important; e.g. consider snapback, it's good
 * to play at the last liberty by attacker, but not defender. */
static inline __attribute__((always_inline)) bool
capturable_group(Board *b, Stone capturer, Coord c, Stone to_play)
{
  group_t g_id = b->_infos[c].id;
	int libs = b->_groups[g_id].liberties;
	if (board_at(b, c) != OPPONENT(capturer) || libs > 1)
		return false;

	return can_play_on_lib(b, g_id, to_play);
}

bool can_countercapture(Board *b, Stone owner, group_t id, Stone to_play, struct move_queue *q, int tag) {
  // [TODO]: Need to fix this.
	//if (b->clen < 2)
  //		return false;

	unsigned int qmoves_prev = q ? q->moves : 0;
  Group *g = &b->_groups[id];

  TRAVERSE(b, id, c) {
    FOR4(c, _, cc) {
			if (!capturable_group(b, owner, c, to_play))
				continue;

			if (!q) {
				return true;
			}
			// mq_add(q, board_group_info(b, group_at(b, c)).lib[0], tag);
			mq_add(q, get_nlibs_of_group(b, id, 1, NULL), tag);
			mq_nodup(q);
    } ENDFOR4
  } ENDTRAVERSE

	bool can = q ? q->moves > qmoves_prev : false;
	return can;
}

#ifdef NO_DOOMED_GROUPS
static bool can_be_rescued(Board *b, group_t group, Stone color, int tag)
{
	/* Does playing on the liberty rescue the group? */
	if (can_play_on_lib(b, group, color))
		return true;

	/* Then, maybe we can capture one of our neighbors? */
	return can_countercapture(b, color, group, color, NULL, tag);
}
#endif

void
group_atari_check(void *context, RandFunc randfunc, unsigned int alwaysccaprate, Board *b, group_t group, Stone to_play,
                  struct move_queue *q, Coord *ladder, bool middle_ladder, int tag)
{
  Group *g = &b->_groups[group];
	Stone color = g->color;
	// Coord lib = board_group_info(b, group).lib[0];
  Coord lib = get_nlibs_of_group(b, group, 1, NULL);

	assert(color != S_OFFBOARD && color != S_NONE);
  /*
	if (DEBUGL(5))
		fprintf(stderr, "[%s] atariiiiiiiii %s of color %d\n",
		        coord2sstr(group, b), coord2sstr(lib, b), color);
  */
	assert(board_at(b, lib) == S_NONE);

	if (to_play != color) {
		/* We are the attacker! In that case, do not try defending
		 * our group, since we can capture the culprit. */
#ifdef NO_DOOMED_GROUPS
		/* Do not remove group that cannot be saved by the opponent. */
		if (!can_be_rescued(b, group, color, tag))
			return;
#endif
		if (can_play_on_lib(b, group, to_play)) {
			mq_add(q, lib, tag);
			mq_nodup(q);
		}
		return;
	}

	/* Can we capture some neighbor? */
	bool ccap = can_countercapture(b, color, group, to_play, q, tag);
	if (ccap && !ladder && alwaysccaprate > randfunc(context, 100))
		return;

	/* Otherwise, do not save kos. */
	if (g->stones == 1 && neighbor_count_at(b, lib, color) + neighbor_count_at(b, lib, S_OFFBOARD) == 4) {
		/* Except when the ko is for an eye! */
		bool eyeconnect = false;
    FORDIAG4(lib, _, c) {
			if (board_at(b, c) == S_NONE && neighbor_count_at(b, c, color) + neighbor_count_at(b, c, S_OFFBOARD) == 4) {
				eyeconnect = true;
				break;
			}
		} ENDFORDIAG4
		if (!eyeconnect)
			return;
	}

	/* Do not suicide... */
	if (!can_play_on_lib(b, group, to_play))
		return;
  /*
	if (DEBUGL(6))
		fprintf(stderr, "...escape route valid\n");
  */

	/* ...or play out ladders (unless we can counter-capture anytime). */
	if (!ccap) {
		if (is_ladder(b, lib, group, middle_ladder)) {
			/* Sometimes we want to keep the ladder move in the
			 * queue in order to discourage it. */
			if (!ladder)
				return;
			else
				*ladder = lib;
		}
	}

	mq_add(q, lib, tag);
	mq_nodup(q);
}
