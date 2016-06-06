//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 
// @author Tudor Bosman (tudorb@fb.com)

#include "event_count.h"

#include <type_traits>
#include "FollyEventCount.h"

static_assert(sizeof(EventCount) == sizeof(_folly::EventCount),
              "EventCount size mismatch");

static_assert(alignof(EventCount) == alignof(_folly::EventCount),
              "EventCount alignment mismatch");

static_assert(std::is_standard_layout<_folly::EventCount>::value,
              "_folly::EventCount must be standard layout");


static_assert(sizeof(EventCountKey) == sizeof(_folly::EventCount::Key),
              "EventCountKey size mismatch");

static_assert(alignof(EventCountKey) == alignof(_folly::EventCount::Key),
              "EventCountKey alignment mismatch");

static_assert(std::is_standard_layout<_folly::EventCount::Key>::value,
              "_folly::EventCount::Key must be standard layout");

namespace {

inline _folly::EventCount* EV(EventCount* ev) {
  return reinterpret_cast<_folly::EventCount*>(reinterpret_cast<char*>(ev));
}

inline _folly::EventCount::Key* EVK(EventCountKey* key) {
  return reinterpret_cast<_folly::EventCount::Key*>(
      reinterpret_cast<char*>(key));
}

}  // namespace

void event_count_init(EventCount* ev) {
  new (ev) _folly::EventCount();
}

void event_count_destroy(EventCount* ev) {
  EV(ev)->~EventCount();
}

void event_count_notify(EventCount* ev) {
  EV(ev)->notify();
}

void event_count_broadcast(EventCount* ev) {
  EV(ev)->notifyAll();
}

EventCountKey event_count_prepare(EventCount* ev) {
  auto key = EV(ev)->prepareWait();
  EventCountKey ek;
  memcpy(&ek, &key, sizeof(ek));
  return ek;
}

void event_count_cancel(EventCount* ev) {
  EV(ev)->cancelWait();
}

void event_count_wait(EventCount* ev, EventCountKey key) {
  EV(ev)->wait(*EVK(&key));
}

void event_count_await(EventCount* ev, EventCountWaitCallback cb, void* ctx) {
  EV(ev)->await([cb, ctx] () { return cb(ctx); });
}
