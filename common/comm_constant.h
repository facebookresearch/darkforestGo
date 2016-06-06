//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _COMM_CONSTANT_H_
#define _COMM_CONSTANT_H_

#define MOVE_NORMAL     0
#define MOVE_SIMPLE_KO  1
// Tactics moves provided by default_playout (e.g., nakade point)
// They are arranged before the actual CNN moves.
// Sometime NN missed it. (So we need to train a better model)
#define MOVE_TACTICAL   2

// Move used for life and death situations. They are more silly moves but on a local region.
#define MOVE_LD         3

#endif
