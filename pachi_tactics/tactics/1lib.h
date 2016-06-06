#ifndef PACHI_TACTICS_1LIB_H
#define PACHI_TACTICS_1LIB_H

/* One-liberty tactical checks (i.e. dealing with atari situations). */

#include "../../board/board.h"
#include "../board_interface.h"
#include "../mq.h"

/* For given atari group @group owned by @owner, decide if @to_play
 * can save it / keep it in danger by dealing with one of the
 * neighboring groups. */
bool can_countercapture(Board *b, Stone owner, group_t g,
		        Stone to_play, struct move_queue *q, int tag);

/* Examine given group in atari, suggesting suitable moves for player
 * @to_play to deal with it (rescuing or capturing it). */
/* ladder != NULL implies to always enqueue all relevant moves. */
void group_atari_check(void *context, RandFunc randfunc, unsigned int alwaysccaprate, Board *b, group_t group, Stone to_play,
                       struct move_queue *q, Coord *ladder, bool middle_ladder, int tag);

#endif
