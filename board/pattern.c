//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 
// This file is inspired by Pachi's engine (https://github.com/pasky/pachi). 
// The main DarkForest engine (when specified with `--playout_policy v2`) does not depend on it. 
// However, the simple policy opened with `--playout_policy simple` will use this library.

#include "pattern.h"
#include "assert.h"

#define PATTERN3_HASH_BITS 19
#define PATTERN3_HASH_SIZE (1 << PATTERN3_HASH_BITS)
#define PATTERN3_HASH_MASK (PATTERN3_HASH_SIZE - 1)

// Save the original pattern (for collision) and its value.
typedef struct {
	hash3_t pattern;
	unsigned char value;
} Pattern2P;

typedef struct {
	/* In case of a collision, following hash entries are
	 * used. value==0 indicates an unoccupied hash entry. */
	/* The hash indices are zobrist hashes based on p3hashes. */
	Pattern2P hash[PATTERN3_HASH_SIZE];

  /* Zobrist hashes for the various 3x3 points. */
  /* [point][is_atari][color] */
  hash_t p3hashes[8][2][4];
} Pattern3S;

#define PAT3_N 15

static char moggy_patterns_src[PAT3_N][11] = {
	/* hane pattern - enclosing hane */	/* 0.52 */
	"XOX"
	"..."
	"???",
	/* hane pattern - non-cutting hane */	/* 0.53 */
	"YO."
	"..."
	"?.?",
	/* hane pattern - magari */		/* 0.32 */
	"XO?"
	"X.."
	"x.?",
	/* hane pattern - thin hane */		/* 0.22 */
	"XOO"
	"..."
	"?.?" "X",
	/* generic pattern - katatsuke or diagonal attachment; similar to magari */	/* 0.37 */
	".Q."
	"Y.."
	"...",
	/* cut1 pattern (kiri) - unprotected cut */	/* 0.28 */
	"XO?"
	"O.o"
	"?o?",
	/* cut1 pattern (kiri) - peeped cut */	/* 0.21 */
	"XO?"
	"O.X"
	"???",
	/* cut2 pattern (de) */			/* 0.19 */
	"?X?"
	"O.O"
	"ooo",
	/* cut keima (not in Mogo) */		/* 0.82 */
	"OX?"
	"?.O"
	"?o?", /* oo? has some pathological tsumego cases */
	/* side pattern - chase */		/* 0.12 */
	"X.?"
	"O.?"
	"##?",
	/* side pattern - block side cut */	/* 0.20 */
	"OX?"
	"X.O"
	"###",
	/* side pattern - block side connection */	/* 0.11 */
	"?X?"
	"x.O"
	"###",
	/* side pattern - sagari (SUSPICIOUS) */	/* 0.16 */
	"?XQ"
	"x.x" /* Mogo has "x.?" */
	"###" /* Mogo has "X" */,
#if 0
	/* side pattern - throw-in (SUSPICIOUS) */
	"?OX"
	"o.O"
	"?##" "X",
#endif
	/* side pattern - cut (SUSPICIOUS) */	/* 0.57 */
	"?OY"
	"Y.O"
	"###" /* Mogo has "X" */,
	/* side pattern - eye piercing:
	 * # O O O .
	 * # O . O .
	 * # . . . .
	 * # # # # # */
	/* side pattern - make eye */		/* 0.44 */
	"?X."
	"Q.X"
	"###",
#if 0
	"Oxx"
	"..."
	"###",
#endif
};

#define moggy_patterns_src_n sizeof(moggy_patterns_src) / sizeof(moggy_patterns_src[0])
#define MASK(h) ((h) & PATTERN3_HASH_MASK)

static int pat3_gammas_default[PAT3_N] = {
  52, 53, 32, 22, 37, 28, 21, 19, 82, 12, 20, 11, 16, 57, 44
};

static inline hash3_t
pattern3_reverse(hash3_t pat)
{
	/* Reverse color assignment - achieved by swapping odd and even bits */
	return ((pat >> 1) & 0x5555) | ((pat & 0x5555) << 1) | (pat & 0xf0000);
}

static inline __attribute__((const)) hash_t
hash3_to_hash(hash_t p3hashes[8][2][4], hash3_t pat)
{
  // From local board representation (pat) to Zobrist-hash.
  /* hash3_t pattern: ignore middle point, 2 bits per intersection (color)
   * plus 1 bit per each direct neighbor => 8*2 + 4 bits. Bitmap point order:
   * 7 6 5    b
   * 4   3  a   9
   * 2 1 0    8   */
  /* Value bit 0: black pattern; bit 1: white pattern */
	hash_t h = 0;
  // All Neighbor4 has ataribits >= 0.
	static const int ataribits[8] = { -1, 0, -1, 1, 2, -1, 3, -1 };
	for (int i = 0; i < 8; i++) {
		h ^= p3hashes[i][ataribits[i] >= 0 ? (pat >> (16 + ataribits[i])) & 1 : 0][(pat >> (i*2)) & 3];
	}
	return h;
}

static void
pattern_record(Pattern3S *p, int pi, char *str, hash3_t pat, int fixed_color)
{
	hash_t h = hash3_to_hash(p->p3hashes, pat);
  // Find collision. We might not need that in the future.
	while (p->hash[MASK(h)].pattern != pat && p->hash[MASK(h)].value != 0)
		h++;
#if 0
	if (h != hash3_to_hash(pat) && p->hash[MASK(h)].pattern != pat)
		fprintf(stderr, "collision of %06x: %llx(%x)\n", pat, MASK(hash3_to_hash(pat)), p->hash[MASK(hash3_to_hash(pat))].pattern);
#endif
	p->hash[MASK(h)].pattern = pat;
	p->hash[MASK(h)].value = (fixed_color ? fixed_color : 3) | (pi << 2);
	//fprintf(stderr, "[%s] %04x %d\n", str, pat, fixed_color);
}

static int
pat_vmirror(hash3_t pat)
{
	/* V mirror pattern; reverse order of 3-2-3 color chunks and
	 * 1-2-1 atari chunks */
	return ((pat & 0xfc00) >> 10) | (pat & 0x03c0) | ((pat & 0x003f) << 10)
		| ((pat & 0x80000) >> 3) | (pat & 0x60000) | ((pat & 0x10000) << 3);
}

static int
pat_hmirror(hash3_t pat)
{
	/* H mirror pattern; reverse order of 2-bit values within the chunks,
	 * and the 2-bit middle atari chunk. */
#define rev3(p) ((p >> 4) | (p & 0xc) | ((p & 0x3) << 4))
#define rev2(p) ((p >> 2) | ((p & 0x3) << 2))
	return (rev3((pat & 0xfc00) >> 10) << 10)
		| (rev2((pat & 0x03c0) >> 6) << 6)
		| rev3((pat & 0x003f))
		| ((pat & 0x20000) << 1)
		| ((pat & 0x40000) >> 1)
		| (pat & 0x90000);
#undef rev3
#undef rev2
}

static int
pat_90rot(hash3_t pat)
{
	/* Rotate by 90 degrees:
	 * 5 6 7  3     7 4 2     2
	 * 3   4 1 2 -> 6   1 -> 3 0
	 * 0 1 2  0     5 3 0     1  */
	/* I'm too lazy to optimize this :) */

	int p2 = 0;

	/* Stone info */
	int vals[8];
	for (int i = 0; i < 8; i++)
		vals[i] = (pat >> (i * 2)) & 0x3;
	int vals2[8];
	vals2[0] = vals[5]; vals2[1] = vals[3]; vals2[2] = vals[0];
	vals2[3] = vals[6];                     vals2[4] = vals[1];
	vals2[5] = vals[7]; vals2[6] = vals[4]; vals2[7] = vals[2];
	for (int i = 0; i < 8; i++)
		p2 |= vals2[i] << (i * 2);

	/* Atari info */
	int avals[4];
	for (int i = 0; i < 4; i++)
		avals[i] = (pat >> (16 + i)) & 0x1;
	int avals2[4];
	avals2[3] = avals[2];
	avals2[1] = avals[3]; avals2[2] = avals[0];
	avals2[0] = avals[1];
	for (int i = 0; i < 4; i++)
		p2 |= avals2[i] << (16 + i);

	return p2;
}

void
pattern3_transpose(hash3_t pat, hash3_t (*transp)[8])
{
	int i = 0;
	(*transp)[i++] = pat;
	(*transp)[i++] = pat_vmirror(pat);
	(*transp)[i++] = pat_hmirror(pat);
	(*transp)[i++] = pat_vmirror(pat_hmirror(pat));
	(*transp)[i++] = pat_90rot(pat);
	(*transp)[i++] = pat_90rot(pat_vmirror(pat));
	(*transp)[i++] = pat_90rot(pat_hmirror(pat));
	(*transp)[i++] = pat_90rot(pat_vmirror(pat_hmirror(pat)));
}

static void
pattern_gen(Pattern3S *p, int pi, hash3_t pat, char *src, int srclen, int fixed_color)
{
	for (; srclen > 0; src++, srclen--) {
		if (srclen == 5)
			continue;
		static const int ataribits[] = { -1, 0, -1, 1, 2, -1, 3, -1 };
		int patofs = (srclen > 5 ? srclen - 1 : srclen) - 1;
		switch (*src) {
			/* Wildcards. */
			case '?':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '?'; // for future recursions
				return;
			case 'x':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'x'; // for future recursions
				return;
			case 'o':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'o'; // for future recursions
				return;

			case 'X':
				*src = 'Y'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				if (ataribits[patofs] >= 0)
					*src = '|'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'X'; // for future recursions
				return;
			case 'O':
				*src = 'Q'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				if (ataribits[patofs] >= 0)
					*src = '@'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'O'; // for future recursions
				return;

			case 'y':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '|'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'y'; // for future recursions
				return;
			case 'q':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '@'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'q'; // for future recursions
				return;

			case '=':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'Y'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '='; // for future recursions
				return;
			case '0':
				*src = '.'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'Q'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pi, pat, src, srclen, fixed_color);
				*src = '0'; // for future recursions
				return;

			/* Atoms. */
			case '.': /* 0 */ break;
			case 'Y': pat |= S_BLACK << (patofs * 2); break;
			case 'Q': pat |= S_WHITE << (patofs * 2); break;
			case '|': assert(ataribits[patofs] >= 0);
				  pat |= (S_BLACK << (patofs * 2)) | (1 << (16 + ataribits[patofs]));
				  break;
			case '@': assert(ataribits[patofs] >= 0);
				  pat |= (S_WHITE << (patofs * 2)) | (1 << (16 + ataribits[patofs]));
				  break;
			case '#': pat |= S_OFF_BOARD << (patofs * 2); break;
		}
	}

	/* Original pattern, all transpositions and rotations */
	hash3_t transp[8];
	pattern3_transpose(pat, &transp);
	for (int i = 0; i < 8; i++) {
		/* Original color assignment */
		pattern_record(p, pi, src - 9, transp[i], fixed_color);
		/* Reverse color assignment */
		if (fixed_color)
			fixed_color = 2 - (fixed_color == 2);
		pattern_record(p, pi, src - 9, pattern3_reverse(transp[i]), fixed_color);
	}
}

static void
patterns_gen(Pattern3S *p, char src[][11], int src_n)
{
	for (int i = 0; i < src_n; i++) {
		//printf("<%s>\n", src[i]);
		int fixed_color = 0;
		switch (src[i][9]) {
			case 'X': fixed_color = S_BLACK; break;
			case 'O': fixed_color = S_WHITE; break;
		}
		// fprintf(stderr, "** %s **\n", src[i]);
		pattern_gen(p, i, 0, src[i], 9, fixed_color);
	}
}

// Main functions
void *InitPatternDB() {
  Pattern3S *p = (Pattern3S *)malloc(sizeof(Pattern3S));
  assert(p);
  memset(p, 0, sizeof(Pattern3S));

  /* tuned for 11482 collisions */
  /* XXX: tune better */
  hash_t h = 0x35373c;
  for (int i = 0; i < 8; i++) {
    for (int a = 0; a < 2; a++) {
      p->p3hashes[i][a][S_EMPTY] = (h = h * 16803-7);
      p->p3hashes[i][a][S_BLACK] = (h = h * 16805-2);
      p->p3hashes[i][a][S_WHITE] = (h = h * 16807-11);
      p->p3hashes[i][a][S_OFF_BOARD] = (h = h * 16809+7);
    }
  }

  patterns_gen(p, moggy_patterns_src, moggy_patterns_src_n);
  return p;
}

static inline char atari_atxy(const Board *b, Coord m) {
  unsigned char id = b->_infos[m].id;
  if (id > 0) {
    // There is a group.
    return b->_groups[b->_infos[m].id].liberties == 1 ? 1 : 0;
  }
  return 0;
}

/* hash3_t pattern: ignore middle point, 2 bits per intersection (color)
 * plus 1 bit per each direct neighbor => 8*2 + 4 bits. Bitmap point order:
 * 7 6 5    b
 * 4   3  a   9
 * 2 1 0    8   */
/* Value bit 0: black pattern; bit 1: white pattern */

hash3_t GetHash(const Board *b, Coord m) {
	hash3_t pat = 0;

	/* Stone info. */
#define board_atxy(__m) b->_infos[__m].color
	pat |= (board_atxy(LT(m)) << 14) | (board_atxy(T(m)) << 12) | (board_atxy(RT(m)) << 10);
	pat |= (board_atxy(L(m)) << 8) | (board_atxy(R(m)) << 6);
	pat |= (board_atxy(LB(m)) << 4) | (board_atxy(B(m)) << 2) | (board_atxy(RB(m)));
#undef board_atxy

	/* Atari info. */
 	pat |= (atari_atxy(b, T(m)) << 19);
	pat |= (atari_atxy(b, L(m)) << 18)
		| (atari_atxy(b, R(m)) << 17);
	pat |= (atari_atxy(b, B(m)) << 16);
	return pat;
}

// Check whether a move (represented by pat) with the given stone color is good.
BOOL QueryPatternDB(void *pp, hash3_t pat, Stone color, int* gamma) {
  Pattern3S *p = (Pattern3S *)pp;
	hash_t h = hash3_to_hash(p->p3hashes, pat);
  short idx = -1;
  // Deal with collisions.
	while (p->hash[MASK(h)].pattern != pat && p->hash[MASK(h)].value != 0)
		h++;
	if (p->hash[MASK(h)].value & color) {
		idx = p->hash[MASK(h)].value >> 2;
	}

  if (idx >= 0 && gamma != NULL) {
    *gamma = pat3_gammas_default[idx];
    return TRUE;
  } else {
    return FALSE;
  }
}

static void check_pattern_here(void *pp, const Board *board, Coord m, DefPolicyMoves *move_queue) {
  if (board->_infos[m].color != S_EMPTY) return;

  GroupId4 ids;
  // ShowBoard(board, SHOW_LAST_MOVE);
  // util_show_move(m, board->_next_player);
  if (! TryPlay2(board, m, &ids)) return;

  hash3_t pattern = GetHash(board, m);
  // Check whether a move (represented by pat) with the given stone color is good.
  int gamma = 0;
  if (QueryPatternDB(pp, pattern, board->_next_player, &gamma)) {
    add_move(move_queue, c_mg(m, PATTERN, gamma));
  }
}

// Check the 3x3 pattern matcher
void CheckPatternFromLastMove(void *pp, DefPolicyMoves *move_queue) {
  assert(move_queue);
  const Board *b = move_queue->board;
  Coord last = b->_last_move;
  if (last == M_PASS || last == M_RESIGN) return;
  FOR8(last, _, c) {
    check_pattern_here(pp, b, c, move_queue);
  } ENDFOR8

  Coord last2 = b->_last_move2;
  if (last2 == M_PASS || last2 == M_RESIGN) return;
  FOR8(last2, _, c) {
    if (NEIGHBOR8(c, last)) continue;
    check_pattern_here(pp, b, c, move_queue);
  } ENDFOR8
}

void DestroyPatternDB(void *p) {
  free(p);
}
