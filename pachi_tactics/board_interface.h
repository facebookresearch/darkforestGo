//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _BOARD_INTERFACE_H_
#define _BOARD_INTERFACE_H_

#include "../board/board.h"

// Some functions and macros for pachi code.
#define group_at(b, c) (&(b)->_groups[b->_infos[(c)].id])
#define board_at(b, c) (b)->_infos[(c)].color

#define board_atxy(b, x, y) (b)->_infos[OFFSETXY((x), (y))].color
#define group_atxy(b, x, y) (b)->_infos[OFFSETXY((x), (y))].id
// board_group_info(b, g). is replaced as g->
//
#define S_OFFBOARD S_OFF_BOARD
#define S_NONE S_EMPTY
#define S_MAX 4

// Handling true/false
#define true TRUE
#define false FALSE
#define bool BOOL

typedef short group_t;
typedef Coord coord_t;

struct move {
    Coord coord;
    Stone color;
};

// Assume we have at least k slots in libs.
// Return the last libs we found. When libs == NULL, don't put libs.
// If we just want to get one, use Coord lib = get_nlib_of_group(b, group, 1, NULL);
Coord get_nlibs_of_group(const Board *b, group_t group, int k, Coord *libs);
Coord board_group_other_lib(const Board *b, group_t group, Coord to);
bool check_loc_adjacent_group(const Board *b, Coord loc, group_t group);
int group_stone_count(Board *b, group_t group, int max);

int neighbor_count_at(const Board *b, Coord c, Stone player);
int immediate_liberty_count(const Board *b, Coord c);

group_t board_get_atari_neighbor(Board *b, Coord coord, Stone group_color);
bool board_is_valid_move(const Board *b, struct move* m);
bool board_is_valid_play(const Board *b, Stone player, Coord m);
int board_play(Board *board, struct move *m);

extern inline int board_size(const Board *b) { return 19; }
extern inline bool is_pass(Coord m) { return m == M_PASS; }
extern inline bool is_resign(Coord m) { return m == M_RESIGN; }
// inline bool board_large(const Board *b) { return true; }
extern inline bool group_is_onestone(const Board *b, group_t group) { return b->_groups[group].stones == 1; }

#define PLDEBUGL(n) false
#define DEBUGL(n) false

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect((x), 0)

inline char *coord2sstr(Coord c, const Board *board) { return (char *)""; }

#define board_is_one_point_eye IsTrueEye
#define board_is_eyelike IsEye
#define stone_other OPPONENT
#define board_large(b) true

#endif

