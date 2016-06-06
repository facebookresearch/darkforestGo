//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _DEFEAULT_POLICY_H_
#define _DEFEAULT_POLICY_H_

#include "default_policy_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The parameters for default policy.
typedef struct {
  BOOL switches[NUM_MOVE_TYPE];
  // Try to save our group in atari if its size is >= thres_save_atari.
  int thres_save_atari;
  // Allow self-atari move if the group size is smaller than thres_allow_atari_stone (before the new move is put).
  int thres_allow_atari_stone;
  // Reduce opponent liberties if its liberties <= thres_opponent_libs and #stones >= thres_opponent_stones.
  int thres_opponent_libs;
  int thres_opponent_stones;
} DefPolicyParams;

void *InitDefPolicy();
void DestroyDefPolicy(void *);
void DefPolicyParamsPrint(void *hh);

// Set the inital value of default policy params.
void InitDefPolicyParams(DefPolicyParams *params);

// Set policy parameters. If not called, then the default policy will use the default parameters.
BOOL SetDefPolicyParams(void *h, const DefPolicyParams *params);

// Utilities for playing default policy. Referenced from Pachi's code.
void ComputeDefPolicy(void *h, DefPolicyMoves *m, const Region *r);

// Sample the default policy, if ids != NULL, then only sample valid moves and save the ids information for the next play.
BOOL SampleDefPolicy(void *h, DefPolicyMoves *ms, void *context, RandFunc rand_func, BOOL verbose, GroupId4 *ids, DefPolicyMove *m);
BOOL SimpleSampleDefPolicy(void *h, const DefPolicyMoves *ms, void *context, RandFunc rand_func, GroupId4 *ids, DefPolicyMove *m);

// Run the default policy
DefPolicyMove RunOldDefPolicy(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose);
DefPolicyMove RunDefPolicy(void *def_policy, void *context, RandFunc rand_func, Board* board, const Region *r, int max_depth, BOOL verbose);

#ifdef __cplusplus
}
#endif

#endif
