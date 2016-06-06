#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG
#include "../../board/board.h"
#include "../mq.h"
#include "2lib.h"
#include "selfatari.h"

/* Whether to avoid capturing/atariing doomed groups (this is big
 * performance hit and may reduce playouts balance; it does increase
 * the strength, but not quite proportionally to the performance). */
//#define NO_DOOMED_GROUPS


static bool
miai_2lib(Board *b, group_t group, Stone color)
{
	bool can_connect = false, can_pull_out = false;
	/* We have miai if we can either connect on both libs,
	 * or connect on one lib and escape on another. (Just
	 * having two escape routes can be risky.) We must make
	 * sure that we don't consider following as miai:
	 * X X X O
	 * X . . O
	 * O O X O - left dot would be pull-out, right dot connect */
  Coord libs[2];
  get_nlibs_of_group(b, group, 2, libs);
  Coord lib0 = libs[0];
  Coord lib1 = libs[1];

  FOR4(lib0, _, c) {
		Stone cc = board_at(b, c);
		if (cc == S_NONE && cc != board_at(b, lib1)) {
			can_pull_out = true;
		} else if (cc != color) {
			continue;
		}

    group_t cg_id = b->_infos[c].id;
		if (cg_id > 0 && cg_id != group && b->_groups[cg_id].liberties > 1)
			can_connect = true;
	} ENDFOR4

  FOR4(lib1, _, c) {
		Stone cc = board_at(b, c);
		if (c == lib0)
			continue;
		if (cc == S_NONE && can_connect) {
			return true;
		} else if (cc != color) {
			continue;
		}

    group_t cg_id = b->_infos[c].id;
		if (cg_id > 0 && cg_id != group && b->_groups[cg_id].liberties > 1)
			return (can_connect || can_pull_out);
  } ENDFOR4
	return false;
}

static bool
defense_is_hopeless(Board *b, group_t group, Stone owner,
			Stone to_play, Coord lib, Coord otherlib,
			bool use)
{
	/* If we are the defender not connecting out, do not
	 * escape with moves that do not gain liberties anyway
	 * - either the new extension has just single extra
	 * liberty, or the "gained" liberties are shared. */
	/* XXX: We do not check connecting to a short-on-liberty
	 * group (e.g. ourselves). */
  /*
	if (DEBUGL(7))
		fprintf(stderr, "\tif_check %d and defending %d and uscount %d ilcount %d\n",
			use, to_play == owner,
			neighbor_count_at(b, lib, owner),
			immediate_liberty_count(b, lib));
  */
	if (!use)
		return false;
	if (to_play == owner && neighbor_count_at(b, lib, owner) == 1) {
		if (immediate_liberty_count(b, lib) == 1)
			return true;
		if (immediate_liberty_count(b, lib) == 2 && NEIGHBOR4(lib, otherlib))
			return true;
	}
	return false;
}

void
can_atari_group(void *context, RandFunc randfunc, Board *b, group_t group, Stone owner,
		  Stone to_play, struct move_queue *q,
		  int tag, bool use_def_no_hopeless)
{
	bool have[2] = { false, false };
	bool preference[2] = { true, true };
  Coord libs[2];
  get_nlibs_of_group(b, group, 2, libs);

	for (int i = 0; i < 2; i++) {
    Coord lib = libs[i];
    assert(board_at(b, lib) == S_NONE);
    if (!board_is_valid_play(b, to_play, lib))
			continue;

    /*
		if (DEBUGL(6))
			fprintf(stderr, "- checking liberty %s of %s %s, filled by %s\n",
				coord2sstr(lib, b),
				stone2str(owner), coord2sstr(group, b),
				stone2str(to_play));
    */

		/* Don't play at the spot if it is extremely short
		 * of liberties... */
		/* XXX: This looks harmful, could significantly
		 * prefer atari to throwin:
		 *
		 * XXXOOOOOXX
		 * .OO.....OX
		 * XXXOOOOOOX */
#if 0
		if (neighbor_count_at(b, lib, stone_other(owner)) + immediate_liberty_count(b, lib) < 2)
			continue;
#endif

		/* Prevent hopeless escape attempts. */
		if (defense_is_hopeless(b, group, owner, to_play, lib,
          libs[1 - i],
          use_def_no_hopeless))
			continue;

#ifdef NO_DOOMED_GROUPS
		/* If the owner can't play at the spot, we don't want
		 * to bother either. */
		if (is_bad_selfatari(b, owner, lib))
			continue;
#endif

		/* Of course we don't want to play bad selfatari
		 * ourselves, if we are the attacker... */
		if (
#ifdef NO_DOOMED_GROUPS
		    to_play != owner &&
#endif
		    is_bad_selfatari(b, to_play, lib)) {
      /*
			if (DEBUGL(7))
				fprintf(stderr, "\tliberty is selfatari\n");
      */
			Coord coord = M_PASS;
		  group_t bygroup = 0;
			if (to_play != owner) {
				/* Okay! We are attacker; maybe we just need
				 * to connect a false eye before atari - this
				 * is very common in the corner. */
				coord = selfatari_cousin(context, randfunc, b, to_play, lib, &bygroup);
			}
			if (is_pass(coord))
        continue;
			/* Ok, connect, but prefer not to. */
			Stone byowner = b->_groups[bygroup].color;
      /*
			if (DEBUGL(7))
				fprintf(stderr, "\treluctantly switching to cousin %s (group %s %s)\n",
					coord2sstr(coord, b), coord2sstr(bygroup, b), stone2str(byowner));
      */
			/* One more thing - is the cousin sensible defense
			 * for the other group? */
			if (defense_is_hopeless(b, bygroup, byowner, to_play,
						coord, lib,
            use_def_no_hopeless))
				continue;
			lib = coord;
			preference[i] = false;

		/* By now, we must be decided we add the move to the
		 * queue!  [comment intentionally misindented] */

		}

		have[i] = true;

		/* If the move is too "lumpy", prefer the alternative:
		 *
		 * #######
		 * ..O.X.X <- always play the left one!
		 * OXXXXXX */
		if (neighbor_count_at(b, lib, to_play) + neighbor_count_at(b, lib, S_OFFBOARD) >= 3) {
      /*
			if (DEBUGL(7))
				fprintf(stderr, "\tlumpy: mine %d + edge %d\n",
					neighbor_count_at(b, lib, to_play),
					neighbor_count_at(b, lib, S_OFFBOARD));
      */
			preference[i] = false;
		}

    /*
		if (DEBUGL(6))
			fprintf(stderr, "+ liberty %s ready with preference %d\n", coord2sstr(lib, b), preference[i]);
    */

		/* If we prefer only one of the moves, pick that one. */
		if (i == 1 && have[0] && preference[0] != preference[1]) {
			if (!preference[0]) {
				if (q->move[q->moves - 1] == libs[0])
					q->moves--;
				/* ...else{ may happen, since we call
				 * mq_nodup() and the move might have
				 * been there earlier. */
			} else {
				assert(!preference[1]);
				continue;
			}
		}

		/* Tasty! Crispy! Good! */
		mq_add(q, lib, tag);
		mq_nodup(q);
	}

  /*
	if (DEBUGL(7)) {
		char label[256];
		snprintf(label, 256, "= final %s %s liberties to play by %s",
			stone2str(owner), coord2sstr(group, b),
			stone2str(to_play));
		mq_print(q, b, label);
	}
  */
}

void
group_2lib_check(void *context, RandFunc randfunc, Board *b, group_t group, Stone to_play, struct move_queue *q, int tag, bool use_miaisafe, bool use_def_no_hopeless)
{
  Stone color = b->_groups[group].color;
	assert(color != S_OFFBOARD && color != S_NONE);

  /*
	if (DEBUGL(5))
		fprintf(stderr, "[%s] 2lib check of color %d\n",
			coord2sstr(group, b), color);
  */

	/* Do not try to atari groups that cannot be harmed. */
	if (use_miaisafe && miai_2lib(b, group, color))
		return;

	can_atari_group(context, randfunc, b, group, color, to_play, q, tag, use_def_no_hopeless);

	/* Can we counter-atari another group, if we are the defender? */
	if (to_play != color)
		return;

  TRAVERSE(b, group, gg) {
    FOR4(gg, _, c) {
			if (board_at(b, c) != OPPONENT(color))
				continue;

      group_t g2_id = b->_infos[c].id;
		  Group *g2 = &b->_groups[g2_id];
			if (g2->liberties == 1) {
				/* We can capture a neighbor. */
        Coord lib = get_nlibs_of_group(b, g2_id, 1, NULL);
				mq_add(q, lib, tag);
				mq_nodup(q);
				continue;
			}
			if (g2->liberties != 2) continue;
			can_atari_group(context, randfunc, b, g2_id, OPPONENT(color), to_play, q, tag, use_def_no_hopeless);
		} ENDFOR4
	} ENDTRAVERSE
}
