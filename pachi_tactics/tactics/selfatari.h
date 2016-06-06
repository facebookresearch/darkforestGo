#ifndef PACHI_TACTICS_SELFATARI_H
#define PACHI_TACTICS_SELFATARI_H

/* A fairly reliable elf-atari detector. */

#include "../../board/board.h"

/* Check if this move is undesirable self-atari (resulting group would have
 * only single liberty and not capture anything; ko is allowed); we mostly
 * want to avoid these moves. The function actually does a rather elaborate
 * tactical check, allowing self-atari moves that are nakade, eye falsification
 * or throw-ins. */
static bool is_bad_selfatari(Board *b, Stone color, Coord to);

/* Move (color, coord) is a selfatari; this means that it puts a group of
 * ours in atari; i.e., the group has two liberties now. Return the other
 * liberty of such a troublesome group (optionally stored at *bygroup)
 * if that one is not a self-atari.
 * (In case (color, coord) is a multi-selfatari, consider a randomly chosen
 * candidate.) */
Coord selfatari_cousin(void *context, RandFunc randfunc, Board *b, Stone color, Coord coord, group_t *bygroup);


bool is_bad_selfatari_slow(Board *b, Stone color, Coord to);
static inline bool
is_bad_selfatari(Board *b, Stone color, Coord to)
{
	/* More than one immediate liberty, thumbs up! */
	if (immediate_liberty_count(b, to) > 1)
		return false;

	return is_bad_selfatari_slow(b, color, to);
}

#endif
