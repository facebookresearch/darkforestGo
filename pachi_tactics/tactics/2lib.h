#ifndef PACHI_TACTICS_2LIB_H
#define PACHI_TACTICS_2LIB_H

/* Two-liberty tactical checks (i.e. dealing with two-step capturing races,
 * preventing atari). */

#include "../../board/board.h"
#include "../board_interface.h"

struct move_queue;

void can_atari_group(void *context, RandFunc randfunc, Board *b, group_t group, Stone owner, Stone to_play, struct move_queue *q, int tag, bool use_def_no_hopeless);
void group_2lib_check(void *context, RandFunc randfunc, Board *b, group_t group, Stone to_play, struct move_queue *q, int tag, bool use_miaisafe, bool use_def_no_hopeless);

#endif
