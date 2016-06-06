// Pachi's moggy playout library.
#ifndef PACHI_PLAYOUT_MOGGY_H
#define PACHI_PLAYOUT_MOGGY_H

#include "../board/board.h"
#include "../board/default_policy_common.h"
#include "board_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

void *playout_moggy_init(char *arg);
void playout_moggy_destroy(void *p);
DefPolicyMove play_random_game(void *policy, void *context, RandFunc randfunc, Board *b, const Region *r, int max_depth, BOOL verbose);

void *InitPachiDefPolicy();
void RunPachiDefPolicy(void *h, const Board *board);
void FreePachiDefPolicy(void *h);

#ifdef __cplusplus
}
#endif

#endif
