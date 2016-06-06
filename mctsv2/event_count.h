//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 
// @author Tudor Bosman (tudorb@fb.com)

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _EventCount {
  uint64_t _x;
} EventCount;

typedef struct _EventCountKey {
  uint32_t _x;
} EventCountKey;

void event_count_init(EventCount* ev);
void event_count_destroy(EventCount* ev);

void event_count_notify(EventCount* ev);
void event_count_broadcast(EventCount* ev);

EventCountKey event_count_prepare(EventCount* ev);
void event_count_cancel(EventCount* ev);
void event_count_wait(EventCount* ev, EventCountKey key);

// Wait until cb(ctx) returns non-zero
typedef int (*EventCountWaitCallback)(void*);
void event_count_await(EventCount* ev, EventCountWaitCallback cb,
                       void* ctx);

#ifdef __cplusplus
}  // extern "C"
#endif
