//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "board_interface.h"

Coord get_nlibs_of_group(const Board *b, group_t id, int k, Coord *libs) {
  if (k > 1 && libs == NULL) error("To get >= 2 libs, libs must not be NULL!");

  int count = 0;
  Coord last = M_PASS;
  int lib_count = b->_groups[id].liberties;
  if (lib_count < k) error("The liberty count is %d and cannot get %d liberty points!\n", lib_count, k);
  TRAVERSE(b, id, c) {
    FOR4(c, _, cc) {
      if (b->_infos[cc].color == S_EMPTY) {
        last = cc;
        if (libs != NULL) {
          BOOL dup = FALSE;
          for (int j = 0; j < count; ++j) {
            if (libs[j] == cc) {
              dup = TRUE;
              break;
            }
          }
          if (!dup) {
            libs[count++] = cc;
          }
        } else {
          // Do not check duplicate.
          count ++;
        }
        if (count == k) return last;
      }
    } ENDFOR4
  } ENDTRAVERSE
  error("This should never be reached!");
  return last;
}

Coord board_group_other_lib(const Board *b, group_t id, Coord to) {
  int count = 0;
  int lib_count = b->_groups[id].liberties;
  if (lib_count < 2) error("The liberty count is %d and cannot get the other liberty point!\n", lib_count);
  TRAVERSE(b, id, c) {
    FOR4(c, _, cc) {
      if (cc != to && b->_infos[cc].color == S_EMPTY) return cc;
    } ENDFOR4
  } ENDTRAVERSE
  error("This should never be reached!");
  return M_PASS;
}

BOOL check_loc_adjacent_group(const Board *b, Coord loc, group_t group) {
  // Check if loc is adjacent to the group.
  FOR4(loc, _, c) {
    if (b->_infos[c].id == group) return TRUE;
  } ENDFOR4
  return FALSE;
}

int group_stone_count(Board *b, group_t group, int max_val) {
  int stone_count = b->_groups[group].stones;
  return stone_count > max_val ? max_val : stone_count;
}

int neighbor_count_at(const Board *b, Coord c, Stone player) {
  // Count the number of stones of that color at a particular location.
  int count = 0;
  FOR4(c, _, cc) {
    if (b->_infos[cc].color == player) count ++;
  } ENDFOR4
  return count;
}

int immediate_liberty_count(const Board *b, Coord c) {
  return neighbor_count_at(b, c, S_EMPTY);
}

group_t board_get_atari_neighbor(Board *b, Coord coord, Stone group_color)
{
  FOR4(coord, _, c) {
    group_t g_id = b->_infos[c].id;
    const Group *g = &b->_groups[g_id];
    if (g_id && board_at(b, c) == group_color && g->liberties == 1)
      return g_id;
    /* We return first match. */
  } ENDFOR4
  return 0;
}

bool board_is_valid_move(const Board *b, struct move* m) {
  GroupId4 ids;
  return TryPlay(b, X(m->coord), Y(m->coord), m->color, &ids);
}

bool board_is_valid_play(const Board *b, Stone player, Coord m) {
  GroupId4 ids;
  return TryPlay(b, X(m), Y(m), player, &ids);
}

int board_play(Board *b, struct move *m) {
  GroupId4 ids;
  if (TryPlay(b, X(m->coord), Y(m->coord), m->color, &ids)) {
    Play(b, &ids);
    return 0;
  } else {
    return -1;
  }
}


