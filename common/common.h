//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

double __attribute__ ((noinline)) wallclock(void);
uint64_t __attribute__ ((noinline)) wallclock64();

#ifdef __cplusplus
}
#endif

#define S_EMPTY 0
#define S_BLACK 1
#define S_WHITE 2
#define S_OFF_BOARD 3

typedef unsigned short Coord;
typedef unsigned char Stone;
typedef unsigned char BOOL;
//static const BOOL TRUE = 1;
//static const BOOL FALSE = 0;
#define TRUE 1
#define FALSE 0

#define STR_BOOL(s) ((s) ? "true" : "false")
#define STR_STONE(s) ((s) == S_BLACK ? "B" : ((s) == S_WHITE ? "W" : "U"))

#define timeit { \
  double __start = wallclock(); \

#define endtime \
  double __duration = wallclock() - __start; \
  printf("Time spent = %lf\n", __duration); \
}

#define endtime2(t) \
  t = wallclock() - __start; \
}

/*
#define timeit { \
  struct timespec __start, __finish; \
  clock_gettime(CLOCK_MONOTONIC, &__start);

#define endtime \
  clock_gettime(CLOCK_MONOTONIC, &__finish); \
  double __elapsed = (__finish.tv_sec - __start.tv_sec); \
  __elapsed += (__finish.tv_nsec - __start.tv_nsec) / 1000000000.0; \
  printf("Time spent = %f\n", __elapsed); \
}

#define endtime2(t) \
  clock_gettime(CLOCK_MONOTONIC, &__finish); \
  double __elapsed = (__finish.tv_sec - __start.tv_sec); \
  __elapsed += (__finish.tv_nsec - __start.tv_nsec) / 1000000000.0; \
  t = __elapsed; \
}
*/

typedef unsigned int (* RandFunc)(void *context, unsigned int max_value);
typedef float (* RandFuncF)(void *context, float max_value);

void dbg_printf(const char *format, ...);
void error(const char *format, ...);

extern inline float load_atomic_float(const float *loc) {
  // sizeof(float) == sizeof(int)
  const int *p = (const int *)loc;
  int val = __atomic_load_n(p, __ATOMIC_ACQUIRE);
  void *pp1 = (void *)&val;
  const float *pp2 = (const float *)pp1;
  return *pp2;
}

extern inline void save_atomic_float(float v, float *loc) {
  // sizeof(float) == sizeof(int)
  int val;
  void *pp1 = (void *)&val;
  float *pp2 = (float *)pp1;
  *pp2 = v;
  __atomic_store_n((int *)loc, val, __ATOMIC_RELAXED);
}

extern inline void inc_atomic_float(float *loc, float inc) {
  int *p = (int *)loc;
  int val = __atomic_load_n(p, __ATOMIC_ACQUIRE);
  void *pp1 = (void *)&val;
  float *pp2 = (float *)pp1;
  *pp2 += inc;
  __atomic_store_n(p, val, __ATOMIC_RELAXED);
}

// ============================== Utility =====================================
// You need have own random generator, the official one (rand()) has built-in thread lock.
extern inline uint16_t fast_random(unsigned long *pmseed, unsigned int max) {
  unsigned long hi, lo;
  lo = 16807 * (*pmseed & 0xffff);
  hi = 16807 * (*pmseed >> 16);
  lo += (hi & 0x7fff) << 16;
  lo += hi >> 15;
  *pmseed = (lo & 0x7fffffff) + (lo >> 31);
  return ((*pmseed & 0xffff) * max) >> 16;
}

// Generate a number for uint64_t.
extern inline uint64_t fast_random64(uint64_t *pmseed) {
  uint64_t hi, lo;
  uint64_t v = 0;
  for (int i = 0; i < 4; ++i) {
    lo = 16807 * (*pmseed & 0xffff);
    hi = 16807 * (*pmseed >> 16);
    lo += (hi & 0x7fff) << 16;
    lo += hi >> 15;
    *pmseed = (lo & 0x7fffffff) + (lo >> 31);
    v <<= 16;
    v |= *pmseed & 0xffff;
  }
  return v;
}

#endif
