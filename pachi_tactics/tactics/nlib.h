#ifndef PACHI_TACTICS_NLIB_H
#define PACHI_TACTICS_NLIB_H

/* N-liberty semeai defense tactical checks. */

#include "../../board/board.h"

struct move_queue;

void group_nlib_defense_check(void *context, RandFunc func, Board *b, group_t group, Stone to_play, struct move_queue *q, int tag);

#endif
