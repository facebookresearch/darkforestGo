#ifndef PACHI_TACTICS_NAKADE_H
#define PACHI_TACTICS_NAKADE_H

/* Piercing eyes. */

#include "../../board/board.h"
#include "../board_interface.h"

/* Find an eye-piercing point within the @around area of empty board
 * internal to group of color @color.
 * Returns pass if the area is not a nakade shape or not internal. */
Coord nakade_point(Board *b, Coord around, Stone color);

#endif
