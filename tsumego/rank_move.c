//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "rank_move.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct {
  BOOL kosumi;
  BOOL hane;
  BOOL jump;
  BOOL keima;
  BOOL connect;
  BOOL cut;
  BOOL touch;
  BOOL extend;
  BOOL make_or_break_eye;
  BOOL kill_enemy;
  BOOL enemy_atari;
} Connection;

typedef struct {
  Coord m;
  int liberty;
  Connection connection;
} Move;

static float connection_scoring(const Connection *c) {
  int shape_score = c->hane + c->kosumi + c->jump + c->keima;
  int cut_connect_score = c->cut + c->connect + c->touch + c->extend;
  int enemy_score = c->kill_enemy + c->enemy_atari;
  int eye_score = c->make_or_break_eye;
  return shape_score * 10 + cut_connect_score * 20 + enemy_score * 15 + eye_score * 30;
}

static int move_ranking(const void *a, const void *b) {
  const Move *aa = (const Move *)a;
  const Move *bb = (const Move *)b;
  return connection_scoring(&bb->connection) - connection_scoring(&aa->connection);
}

static int save_features(const Connection *c, float *feature) {
  // A bunch of zeros and ones.
  int i = 0;
  feature[i++] = c->kosumi;
  feature[i++] = c->hane;
  feature[i++] = c->jump;
  feature[i++] = c->keima;
  feature[i++] = c->connect;
  feature[i++] = c->cut;
  feature[i++] = c->touch;
  feature[i++] = c->extend;
  feature[i++] = c->make_or_break_eye;
  feature[i++] = c->kill_enemy;
  feature[i++] = c->enemy_atari;
  return i;
}

#define LOCAL_RADIUS 2
#define abs(a) ((a) >= 0 ? (a) : (-a))

void SaveMoveFeatureName(FILE *fp) {
  fprintf(fp, "@relation NextMovePrediction\n");

  for (int i = -LOCAL_RADIUS; i <= LOCAL_RADIUS; ++i) {
    char sign_i = i < 0 ? 'n' : 'p';
    for (int j = -LOCAL_RADIUS; j <= LOCAL_RADIUS; ++j) {
      char sign_j = j < 0 ? 'n' : 'p';
      fprintf(fp, "@attribute loc_%c%d_%c%d numeric\n", sign_i, abs(i), sign_j, abs(j));
    }
  }
  fprintf(fp, "@attribute kosumi numeric\n");
  fprintf(fp, "@attribute hane numeric\n");
  fprintf(fp, "@attribute jump numeric\n");
  fprintf(fp, "@attribute keima numeric\n");
  fprintf(fp, "@attribute connect numeric\n");
  fprintf(fp, "@attribute cut numeric\n");
  fprintf(fp, "@attribute touch numeric\n");
  fprintf(fp, "@attribute extend numeric\n");
  fprintf(fp, "@attribute make_or_break_eye numeric\n");
  fprintf(fp, "@attribute kill_enemy numeric\n");
  fprintf(fp, "@attribute enemy_atari numeric\n");

  fprintf(fp, "@attribute target {0, 1}\n\n");

  fprintf(fp, "@data\n");
}

static int save_local_region(const Board *board, Coord c, int radius, float *feature) {
  int counter = 0;
  for (int i = X(c) - radius; i <= X(c) + radius; ++i) {
    for (int j = Y(c) - radius; j <= Y(c) + radius; ++j) {
      float f = 0;
      if (i < 0 || j < 0 || i >= BOARD_SIZE || j >= BOARD_SIZE) f = S_OFF_BOARD;
      else {
        Stone s = board->_infos[c].color;
        if (s == board->_next_player) f = 1;
        else if (s == OPPONENT(board->_next_player)) f = 2;
        else f = 0;
      }

      feature[counter++] = f;
    }
  }
  return counter;
}

static inline void check_connection(const Board *board, Stone defender, const GroupId4 *ids, Connection *conn) {
#define defender_color(c) (board->_infos[(c)].color == defender)
#define defender_or_wall_color(c) (board->_infos[(c)].color == defender || board->_infos[(c)].color == S_OFF_BOARD)
#define empty_color(c) (board->_infos[(c)].color == S_EMPTY)
#define nonempty_color(c) (board->_infos[(c)].color != S_EMPTY)
#define opp_color(c) (board->_infos[(c)].color == OPPONENT(board->_next_player))
  Coord m = ids->c;

  BOOL el = empty_color(L(m));
  BOOL et = empty_color(T(m));
  BOOL er = empty_color(R(m));
  BOOL eb = empty_color(B(m));

  BOOL elt = empty_color(LT(m));
  BOOL elb = empty_color(LB(m));
  BOOL ert = empty_color(RT(m));
  BOOL erb = empty_color(RB(m));

  BOOL ol = opp_color(L(m));
  BOOL ot = opp_color(T(m));
  BOOL _or = opp_color(R(m));
  BOOL ob = opp_color(B(m));

  BOOL slt = defender_color(LT(m));
  BOOL srt = defender_color(RT(m));
  BOOL slb = defender_color(LB(m));
  BOOL srb = defender_color(RB(m));

  BOOL swlt = defender_or_wall_color(LT(m));
  BOOL swrt = defender_or_wall_color(RT(m));
  BOOL swlb = defender_or_wall_color(LB(m));
  BOOL swrb = defender_or_wall_color(RB(m));

  conn->kosumi = ((slt && el && et) || (srt && er && et) || (slb && el && eb) || (srb && er && eb)) ? TRUE : FALSE;
  conn->hane = ((slt && ((el && ot) || (et && ol))) || (srt && ((er && ot) || (et && _or))) || (slb && ((el && ob) || (eb && ol))) || (srb && ((er && ob) || (eb && _or)))) ? TRUE : FALSE;
  conn->jump = ((el && elt && elb && defender_color(LL(m)))
             || (et && elt && ert && defender_color(TT(m)))
             || (er && ert && erb && defender_color(RR(m)))
             || (eb && elb && erb && defender_color(BB(m)))) ? TRUE : FALSE;

  BOOL l_keima = el && empty_color(LL(m)) && ((defender_color(T(LL(m))) && elt) || (defender_color(B(LL(m))) && elb));
  BOOL r_keima = er && empty_color(RR(m)) && ((defender_color(T(RR(m))) && ert) || (defender_color(B(RR(m))) && erb));
  BOOL t_keima = et && empty_color(TT(m)) && ((defender_color(L(TT(m))) && elt) || (defender_color(R(TT(m))) && ert));
  BOOL b_keima = eb && empty_color(BB(m)) && ((defender_color(L(BB(m))) && elb) || (defender_color(R(BB(m))) && erb));
  conn->keima = l_keima || r_keima || t_keima || b_keima;

  conn->make_or_break_eye =
          (el && defender_or_wall_color(LL(m)) && swlt && swlb)
       || (er && defender_or_wall_color(RR(m)) && swrt && swrb)
       || (et && defender_or_wall_color(TT(m)) && swlt && swrt)
       || (eb && defender_or_wall_color(BB(m)) && swlb && swrb) ? TRUE : FALSE;

  // Check connection.
  int self_group_count = 0;
  int enemy_group_count = 0;
  int min_self_liberty = 10000;
  int min_enemy_liberty = 10000;
  for (int i = 0; i < 4; ++i) {
    if (ids->ids[i] == 0) continue;
    if (ids->colors[i] == OPPONENT(board->_next_player)) {
      enemy_group_count ++;
      min_enemy_liberty = min(min_enemy_liberty, ids->group_liberties[i]);
      short lib = ids->group_liberties[i];
      if (lib == 1) conn->kill_enemy = TRUE;
      else if (lib == 2) conn->enemy_atari = TRUE;
    } else {
      self_group_count ++;
      min_self_liberty = min(min_self_liberty, ids->group_liberties[i]);
    }
  }

  if (self_group_count >= 2 && min_self_liberty <= 2) conn->connect = TRUE;
  if (enemy_group_count >= 2 && min_enemy_liberty <= 2) conn->cut = TRUE;
  if (self_group_count >= 1 && enemy_group_count >= 1) conn->touch = TRUE;
  if (self_group_count == 1 && min_self_liberty == 1 && ids->liberty >= 2) conn->extend = TRUE;

#undef defender_color
#undef empty_color
#undef opp_color
#undef defender_or_wall_color
}

// Find moves with ranks. Assuming the space is sufficient.
static void FindMovesWithRank(const Board *board, Stone defender, const Region *r, Move *moves, int *num_moves) {
  GroupId4 ids;
  Coord c;
  *num_moves = 0;
  for (int x = r->left; x < r->right; ++x) {
    for (int y = r->top; y < r->bottom; ++y) {
      c = OFFSETXY(x, y);
      if (! TryPlay2(board, c, &ids)) continue;

      // Save the move and its properties.
      Move m;
      m.liberty = ids.liberty;
      m.m = c;

      // Check if there is any connection to existing stones. (kosumi/hane or jump)
      check_connection(board, defender, &ids, &m.connection);
      if (connection_scoring(&m.connection) > 0) {
        moves[(*num_moves)++] = m;
      }
    }
  }
}

BOOL SaveMoveWithFeature(const Board *board, Stone defender, Coord c, int target, FILE *fp) {
  GroupId4 ids;
  float feature[100];
  if (! TryPlay2(board, c, &ids)) return FALSE;

  // Save the move and its properties.
  Connection conn;

  // Check if there is any connection to existing stones. (kosumi/hane or jump)
  check_connection(board, defender, &ids, &conn);

  // print out the local 5x5 region.
  float *f = feature;
  f += save_local_region(board, c, LOCAL_RADIUS, f);
  // Print out all features.
  f += save_features(&conn, f);

  for (int j = 0; j < f - feature; ++j) {
    fprintf(fp, "%.2f,", feature[j]);
  }
  fprintf(fp, "%d\n", target);

  return TRUE;
}

void GetRankedMoves(const Board* board, Stone defender, const Region *r, int max_num_moves, AllMoves *all_moves) {
  Move moves[BOARD_SIZE * BOARD_SIZE];
  int num_moves;
  FindMovesWithRank(board, defender, r, moves, &num_moves);

  qsort(moves, num_moves, sizeof(Move), move_ranking);

  // printf("============= InitState =====================\n");
  // ShowBoard(&s->b, SHOW_LAST_MOVE);
  char buf[100];
  // printf("Num of moves: %d\n", num_moves);
  all_moves->board = board;
  if (max_num_moves < 0) max_num_moves = num_moves + 1;
  for (int i = 0; i < min(num_moves, max_num_moves - 1); ++i) {
    const Move *mm = &moves[i];
    // printf("%s: lib %d, kill_enemy: %s, reduce_enemy_liberty: %s\n", get_move_str(mm->m, s->b._next_player, buf) , mm->liberty, (mm->kill_enemy ? "yes" : "no"), (mm->reduce_enemy_liberty ? "yes" : "no"));
    all_moves->moves[i] = mm->m;
  }
  // printf("=========== End InitState ===================\n");
  all_moves->num_moves = min(num_moves, max_num_moves - 1);

  // Add pass here.
  if (all_moves->num_moves == 0) all_moves->moves[all_moves->num_moves++] = M_PASS;
}
