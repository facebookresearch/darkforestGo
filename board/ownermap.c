//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "ownermap.h"
// Ownermap
typedef struct {
  // Ownermap
  int total_ownermap_count;
  // Histogram. S_EMPTY (S_UNKNOWN), S_BLACK, S_WHITE, S_OFF_BOARD (S_DAME)
  int ownermap[MACRO_BOARD_SIZE][MACRO_BOARD_SIZE][4];
} Handle;

void *InitOwnermap() {
  Handle *hh = (Handle *)malloc(sizeof(Handle));
  if (hh == NULL) error("Ownermap handle cannot be initialized.");
  return hh;
}

void FreeOwnermap(void *hh) {
  free(hh);
}

void ClearOwnermap(void *hh) {
  Handle *h = (Handle *)hh;
  // Ownermap
  h->total_ownermap_count = 0;
  memset(h->ownermap, 0, sizeof(h->ownermap));
}

void AccuOwnermap(void *hh, const Board *board) {
  // Accumulate the ownermap with the board situation.
  // Usually the current board situation is after the default policy is applied.
  Handle *h = (Handle *)hh;
  // Accumulate the ownermap
  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);
      Stone s = board->_infos[c].color;
      if (s == S_EMPTY) {
        s = GetEyeColor(board, c);
      }
      h->ownermap[i][j][s] ++;
    }
  }
  h->total_ownermap_count ++;
}

float OwnermapFloatOne(Handle *h, int i, int j, Stone player) {
  return ((float) h->ownermap[i][j][player]) / h->total_ownermap_count;
}

Stone OwnermapJudgeOne(Handle *h, int i, int j, float ratio) {
  int empty = h->ownermap[i][j][S_EMPTY];
  int black = h->ownermap[i][j][S_BLACK];
  int white = h->ownermap[i][j][S_WHITE];
  int n = h->total_ownermap_count;

  int thres = (int)(n * ratio);

  if (empty >= thres) return S_DAME;
  if (empty + black >= thres) return S_BLACK;
  if (empty + white >= thres) return S_WHITE;
  return S_UNKNOWN;
}

void GetDeadStones(void *hh, const Board *board, float ratio, Stone *livedead, Stone *group_stats) {
  // Threshold the ownermap and determine.
  Handle *h = (Handle *)hh;

  Stone *internal_group_stats = NULL;

  if (group_stats == NULL) {
    internal_group_stats = (Stone *)malloc(board->_num_groups * sizeof(Stone));
    group_stats = internal_group_stats;
  }

  memset(group_stats, S_EMPTY, board->_num_groups * sizeof(Stone));

  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);
      Stone s = board->_infos[c].color;
      Stone owner = OwnermapJudgeOne(h, i, j, ratio);

      // printf("owner at (%d, %d) = %d\n", i, j, owner);
      short id = board->_infos[c].id;
      if (owner == S_UNKNOWN) {
        group_stats[id] = s | S_UNKNOWN;
      } else if (! (group_stats[id] & S_UNKNOWN)) {
        // The group has deterministic state or empty.
        Stone stat = s;
        if (owner == s) stat |= S_ALIVE;
        else if (owner == OPPONENT(s)) stat |= S_DEAD;
        else stat |= S_UNKNOWN;

        if (group_stats[id] == S_EMPTY) group_stats[id] = stat;
        else if (group_stats[id] != stat) group_stats[id] = s | S_UNKNOWN;
      }
    }
  }
  // Once we get the group stats, we thus can fill the ownermap.
  if (livedead != NULL) {
    // Zero out everything else.
    memset(livedead, S_EMPTY, BOARD_SIZE * BOARD_SIZE * sizeof(Stone));
    for (int i = 1; i < board->_num_groups; ++i) {
      TRAVERSE(board, i, c) {
        livedead[EXPORT_OFFSET(c)] = group_stats[i];
      } ENDTRAVERSE
    }
  }

  if (internal_group_stats != NULL) free(internal_group_stats);
}

void GetOwnermap(void *hh, float ratio, Stone *ownermap) {
  // Threshold the ownermap and determine.
  Handle *h = (Handle *)hh;

  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);
      ownermap[EXPORT_OFFSET(c)] = OwnermapJudgeOne(h, i, j, ratio);
    }
  }
}

void GetOwnermapFloat(void *hh, Stone player, float *ownermap) {
  Handle *h = (Handle *)hh;
  for (int i = 0; i < BOARD_SIZE; ++i) {
    for (int j = 0; j < BOARD_SIZE; ++j) {
      Coord c = OFFSETXY(i, j);
      ownermap[EXPORT_OFFSET(c)] = OwnermapFloatOne(h, i, j, player);
    }
  }
}

float GetTTScoreOwnermap(void *hh, const Board *board, Stone *livedead, Stone *territory) {
  Stone group_stats[MAX_GROUP];
  GetDeadStones(hh, board, 0.5, livedead, group_stats);
  return GetTrompTaylorScore(board, group_stats, territory);
}

void ShowDeadStones(const Board *board, const Stone *stones) {
  // Show the board with ownership
  char buf[2000];
  int len = 0;
  len += sprintf(buf + len, "   A B C D E F G H J K L M N O P Q R S T\n");
  char stone[3];
  stone[2] = 0;
  for (int j = BOARD_SIZE - 1; j >= 0; --j) {
    len += sprintf(buf + len, "%2d ", j + 1);
    for (int i = 0; i < BOARD_SIZE; ++i) {
      Coord c = OFFSETXY(i, j);
      Stone s = board->_infos[c].color;
      if (HAS_STONE(s)) {
        char ss = (s == S_BLACK ? 'X' : 'O');
        // Make it lower case if the stone are dead.
        Stone stat = stones[EXPORT_OFFSET(c)];
        if (stat & S_DEAD) ss |= 0x20;
        stone[0] = ss;
        stone[1] = ( (stat & S_UNKNOWN) ? '?' : (c == board->_last_move ? ')' : ' '));

      } else if (s == S_EMPTY) {
        if (STAR_ON19(i, j))
          strcpy(stone, "+ ");
        else
          strcpy(stone, ". ");
      } else strcpy(stone, "# ");
      len += sprintf(buf + len, stone);
    }
    len += sprintf(buf + len, "%d\n", j + 1);
  }
  len += sprintf(buf + len, "   A B C D E F G H J K L M N O P Q R S T");
  // Finally print
  printf(buf);
}

void ShowStonesProb(void *hh, Stone player) {
  // Show the board with ownership
  char buf[20000];
  int len = 0;
  Handle *h = (Handle *)hh;

  const char *prompt = "   A      B      C      D      E      F      G      H      J      K      L      M      N      O      P      Q      R      S      T\n";
  len += sprintf(buf + len, prompt);
  for (int j = BOARD_SIZE - 1; j >= 0; --j) {
    len += sprintf(buf + len, "%2d ", j + 1);
    for (int i = 0; i < BOARD_SIZE; ++i) {
      Coord c = OFFSETXY(i, j);
      float val = OwnermapFloatOne(h, i, j, player);
      len += sprintf(buf + len, "%.3f  ", val);
    }
    len += sprintf(buf + len, "%d\n", j + 1);
  }
  len += sprintf(buf + len, prompt);
  // Finally print
  printf(buf);
}


