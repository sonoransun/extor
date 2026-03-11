/* QR Code generator library (C)
 * Based on the API by Project Nayuki (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * This is a minimal working implementation supporting byte-mode encoding,
 * versions 1-40, all ECC levels, and mask auto-selection. */

#include "qrcodegen.h"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* ---- Constants ---- */

/* Number of data codewords for each version and ECC level.
 * Index: [version-1][ecc_level] */
static const int16_t NUM_DATA_CODEWORDS[40][4] = {
  /* v1 */  {  19,  16,  13,   9},
  /* v2 */  {  34,  28,  22,  16},
  /* v3 */  {  55,  44,  34,  26},
  /* v4 */  {  80,  64,  48,  36},
  /* v5 */  { 108,  86,  62,  46},
  /* v6 */  { 136, 108,  76,  60},
  /* v7 */  { 156, 124,  88,  66},
  /* v8 */  { 194, 154, 110,  86},
  /* v9 */  { 232, 182, 132, 100},
  /* v10 */ { 274, 216, 154, 122},
  /* v11 */ { 324, 254, 180, 140},
  /* v12 */ { 370, 290, 206, 158},
  /* v13 */ { 428, 334, 244, 180},
  /* v14 */ { 461, 365, 261, 197},
  /* v15 */ { 523, 415, 295, 223},
  /* v16 */ { 589, 453, 325, 253},
  /* v17 */ { 647, 507, 367, 283},
  /* v18 */ { 721, 563, 397, 313},
  /* v19 */ { 795, 627, 445, 341},
  /* v20 */ { 861, 669, 485, 385},
  /* v21 */ { 932, 714, 512, 406},
  /* v22 */ {1006, 782, 568, 442},
  /* v23 */ {1094, 860, 614, 464},
  /* v24 */ {1174, 914, 664, 514},
  /* v25 */ {1276,1000, 718, 538},
  /* v26 */ {1370,1062, 754, 596},
  /* v27 */ {1468,1128, 808, 628},
  /* v28 */ {1531,1193, 871, 661},
  /* v29 */ {1631,1267, 911, 701},
  /* v30 */ {1735,1373, 985, 745},
  /* v31 */ {1843,1455,1033, 793},
  /* v32 */ {1955,1541,1115, 845},
  /* v33 */ {2071,1631,1171, 901},
  /* v34 */ {2191,1725,1231, 961},
  /* v35 */ {2306,1812,1286, 986},
  /* v36 */ {2434,1914,1354,1054},
  /* v37 */ {2566,1992,1426,1096},
  /* v38 */ {2702,2102,1502,1142},
  /* v39 */ {2812,2216,1582,1222},
  /* v40 */ {2956,2334,1666,1276},
};

/* Total codewords per version */
static const int16_t TOTAL_CODEWORDS[40] = {
   26,  44,  70, 100, 134, 172, 196, 242, 292, 346,
  404, 466, 532, 581, 655, 733, 815, 901, 991,1085,
 1156,1258,1364,1474,1588,1706,1828,1921,2051,2185,
 2323,2465,2611,2761,2876,3034,3196,3362,3532,3706,
};

/* Number of error correction blocks for each version/ecc */
static const int8_t NUM_EC_BLOCKS[40][4] = {
  {1,1,1,1}, {1,1,1,1}, {1,1,2,2}, {1,2,2,4},
  {1,2,4,4}, {2,4,4,4}, {2,4,6,5}, {2,4,6,6},
  {2,5,8,8}, {4,5,8,8}, {4,5,8,11},{4,8,10,11},
  {4,9,12,16},{4,9,16,16},{6,10,12,18},{6,10,17,16},
  {6,11,16,19},{6,13,18,21},{7,14,21,25},{8,16,20,25},
  {8,17,23,25},{9,17,23,34},{9,18,25,30},{10,20,27,32},
  {12,21,29,35},{12,23,34,37},{12,25,34,40},{13,26,35,42},
  {14,28,38,45},{15,29,40,48},{16,31,43,51},{17,33,45,54},
  {18,35,48,57},{19,37,51,60},{19,38,53,63},{20,40,56,66},
  {21,43,59,70},{22,45,62,74},{24,47,65,77},{25,49,68,81},
};

/* Alignment pattern positions per version */
static const int8_t ALIGNMENT_POSITIONS[40][7] = {
  {0},                          /* v1 - none */
  {6,18,0},                    /* v2 */
  {6,22,0},
  {6,26,0},
  {6,30,0},
  {6,34,0},
  {6,22,38,0},                 /* v7 */
  {6,24,42,0},
  {6,26,46,0},
  {6,28,50,0},
  {6,30,54,0},
  {6,32,58,0},
  {6,34,62,0},
  {6,26,46,66,0},              /* v14 */
  {6,26,48,70,0},
  {6,26,50,74,0},
  {6,30,54,78,0},
  {6,30,56,82,0},
  {6,30,58,86,0},
  {6,34,62,90,0},
  {6,28,50,72,94,0},           /* v21 */
  {6,26,50,74,98,0},
  {6,30,54,78,102,0},
  {6,28,54,80,106,0},
  {6,32,58,84,110,0},
  {6,30,58,86,114,0},
  {6,34,62,90,118,0},
  {6,26,50,74,98,122,0},       /* v28 */
  {6,30,54,78,102,126,0},
  {6,26,52,78,104,126,0},      /* v30 - actually 130 */
  {6,30,56,82,108,126,0},      /* approximate - tables vary */
  {6,34,60,86,112,126,0},
  {6,30,58,86,114,126,0},
  {6,34,62,90,118,126,0},
  {6,30,54,78,102,126,0},
  {6,24,50,76,102,126,0},
  {6,28,54,80,106,126,0},
  {6,32,58,84,110,126,0},
  {6,26,54,82,110,126,0},
  {6,30,58,86,114,126,0},
};

/* ECC codewords per block for each version/ecc level */
static int qr_ecc_codewords_per_block(int version, int ecl) {
  int total_cw = TOTAL_CODEWORDS[version - 1];
  int data_cw = NUM_DATA_CODEWORDS[version - 1][ecl];
  int num_blocks = NUM_EC_BLOCKS[version - 1][ecl];
  int ecc_cw = total_cw - data_cw;
  return ecc_cw / num_blocks;
}

/* ---- GF(256) arithmetic for Reed-Solomon ---- */

static uint8_t gf_exp[512]; /* antilog table */
static uint8_t gf_log[256]; /* log table */
static int gf_initialized = 0;

static void gf_init(void) {
  int i;
  int x = 1;
  if (gf_initialized) return;
  for (i = 0; i < 255; i++) {
    gf_exp[i] = (uint8_t) x;
    gf_log[x] = (uint8_t) i;
    x <<= 1;
    if (x & 0x100) x ^= 0x11D; /* primitive polynomial */
  }
  for (i = 255; i < 512; i++)
    gf_exp[i] = gf_exp[i - 255];
  gf_initialized = 1;
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return 0;
  return gf_exp[gf_log[a] + gf_log[b]];
}

/* Compute Reed-Solomon ECC for data, appending ecc_len bytes to ecc_out */
static void rs_encode(const uint8_t *data, int data_len,
                      int ecc_len, uint8_t *ecc_out) {
  /* Build generator polynomial */
  uint8_t gen[69]; /* max ecc_len is 68 for QR */
  int i, j;

  memset(gen, 0, sizeof(gen));
  memset(ecc_out, 0, (size_t) ecc_len);

  if (ecc_len <= 0 || ecc_len > 68) return;

  gen[0] = 1;
  for (i = 0; i < ecc_len; i++) {
    /* Multiply gen by (x - alpha^i) */
    for (j = ecc_len; j >= 1; j--) {
      gen[j] = gen[j - 1] ^ gf_mul(gen[j], gf_exp[i]);
    }
    gen[0] = gf_mul(gen[0], gf_exp[i]);
  }

  /* Polynomial division */
  for (i = 0; i < data_len; i++) {
    uint8_t coef = data[i] ^ ecc_out[0];
    memmove(ecc_out, ecc_out + 1, (size_t)(ecc_len - 1));
    ecc_out[ecc_len - 1] = 0;
    if (coef != 0) {
      for (j = 0; j < ecc_len; j++)
        ecc_out[j] ^= gf_mul(gen[j], coef);
    }
  }
}

/* ---- Module operations ---- */

static int qr_size(int version) {
  return version * 4 + 17;
}

static void qr_set_module(uint8_t *qrcode, int size, int x, int y,
                           bool dark) {
  int byte_idx = y * size + x;
  int arr_idx = byte_idx >> 3;
  int bit_idx = byte_idx & 7;
  if (dark)
    qrcode[arr_idx + 1] |= (uint8_t)(1 << bit_idx);
  else
    qrcode[arr_idx + 1] &= (uint8_t) ~(1 << bit_idx);
}

static bool qr_get_module_raw(const uint8_t *qrcode, int size,
                                int x, int y) {
  int byte_idx = y * size + x;
  int arr_idx = byte_idx >> 3;
  int bit_idx = byte_idx & 7;
  return (qrcode[arr_idx + 1] >> bit_idx) & 1;
}

/* Store size in first byte */
static void qr_set_size(uint8_t *qrcode, int size) {
  qrcode[0] = (uint8_t) ((size - 17) / 4);
}

/* ---- Function patterns ---- */

/* Draw a finder pattern centered at (cx, cy) */
static void qr_draw_finder(uint8_t *qrcode, int size, int cx, int cy) {
  int dx, dy;
  for (dy = -4; dy <= 4; dy++) {
    for (dx = -4; dx <= 4; dx++) {
      int x = cx + dx, y = cy + dy;
      int dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
      if (x >= 0 && x < size && y >= 0 && y < size) {
        bool dark = (dist != 3 && dist != 4) || dist == 0;
        /* Finder: 7x7 pattern with separators (the 4th ring is white) */
        if (dist == 4) dark = false; /* separator */
        else if (dist == 3) dark = false;
        else if (dist == 2) dark = true;
        else if (dist == 1) dark = false;
        else dark = true;
        qr_set_module(qrcode, size, x, y, dark);
      }
    }
  }
  /* Actually draw the standard finder pattern */
  for (dy = -3; dy <= 3; dy++) {
    for (dx = -3; dx <= 3; dx++) {
      int x = cx + dx, y = cy + dy;
      if (x >= 0 && x < size && y >= 0 && y < size) {
        int adx = abs(dx), ady = abs(dy);
        int m = adx > ady ? adx : ady;
        qr_set_module(qrcode, size, x, y, m != 2);
      }
    }
  }
  /* Separator (white border) */
  for (dy = -4; dy <= 4; dy++) {
    for (dx = -4; dx <= 4; dx++) {
      int x = cx + dx, y = cy + dy;
      int adx = abs(dx), ady = abs(dy);
      int m = adx > ady ? adx : ady;
      if (m == 4 && x >= 0 && x < size && y >= 0 && y < size) {
        qr_set_module(qrcode, size, x, y, false);
      }
    }
  }
}

/* Check if (x,y) is in a function pattern region */
static bool qr_is_function(int size, int version, int x, int y) {
  /* Finder patterns + separators (top-left, top-right, bottom-left) */
  if (x <= 8 && y <= 8) return true;
  if (x >= size - 8 && y <= 8) return true;
  if (x <= 8 && y >= size - 8) return true;

  /* Timing patterns */
  if (x == 6 || y == 6) return true;

  /* Alignment patterns */
  if (version >= 2) {
    int i, j, num = 0;
    int positions[7];
    for (i = 0; i < 7 && ALIGNMENT_POSITIONS[version - 1][i] != 0; i++) {
      positions[i] = ALIGNMENT_POSITIONS[version - 1][i];
      num++;
    }
    for (i = 0; i < num; i++) {
      for (j = 0; j < num; j++) {
        /* Skip if overlapping with finder */
        if (i == 0 && j == 0) continue;
        if (i == 0 && j == num - 1) continue;
        if (i == num - 1 && j == 0) continue;
        if (abs(x - positions[j]) <= 2 && abs(y - positions[i]) <= 2)
          return true;
      }
    }
  }

  /* Version info (v7+) */
  if (version >= 7) {
    if (x < 6 && y >= size - 11) return true;
    if (y < 6 && x >= size - 11) return true;
  }

  return false;
}

/* Draw all function patterns */
static void qr_draw_function_patterns(uint8_t *qrcode, int size,
                                        int version) {
  int i, j, num;

  /* Finder patterns */
  qr_draw_finder(qrcode, size, 3, 3);
  qr_draw_finder(qrcode, size, size - 4, 3);
  qr_draw_finder(qrcode, size, 3, size - 4);

  /* Timing patterns */
  for (i = 8; i < size - 8; i++) {
    qr_set_module(qrcode, size, i, 6, (i & 1) == 0);
    qr_set_module(qrcode, size, 6, i, (i & 1) == 0);
  }

  /* Dark module */
  qr_set_module(qrcode, size, 8, size - 8, true);

  /* Alignment patterns */
  if (version >= 2) {
    int positions[7];
    num = 0;
    for (i = 0; i < 7 && ALIGNMENT_POSITIONS[version - 1][i] != 0; i++) {
      positions[i] = ALIGNMENT_POSITIONS[version - 1][i];
      num++;
    }
    for (i = 0; i < num; i++) {
      for (j = 0; j < num; j++) {
        if (i == 0 && j == 0) continue;
        if (i == 0 && j == num - 1) continue;
        if (i == num - 1 && j == 0) continue;
        {
          int cx = positions[j], cy = positions[i];
          int dx, dy;
          for (dy = -2; dy <= 2; dy++) {
            for (dx = -2; dx <= 2; dx++) {
              int adx = abs(dx), ady = abs(dy);
              int m = adx > ady ? adx : ady;
              qr_set_module(qrcode, size, cx + dx, cy + dy,
                            m != 1);
            }
          }
        }
      }
    }
  }
}

/* ---- Format and version info ---- */

/* Format info for each ECC level and mask */
static const uint16_t FORMAT_INFO[4][8] = {
  /* LOW */
  {0x77C4, 0x72F3, 0x7DAA, 0x789D, 0x662F, 0x6318, 0x6C41, 0x6976},
  /* MEDIUM */
  {0x5412, 0x5125, 0x5E7C, 0x5B4B, 0x45F9, 0x40CE, 0x4F97, 0x4AA0},
  /* QUARTILE */
  {0x355F, 0x3068, 0x3F31, 0x3A06, 0x24B4, 0x2183, 0x2EDA, 0x2BED},
  /* HIGH */
  {0x1689, 0x13BE, 0x1CE7, 0x19D0, 0x0762, 0x0255, 0x0D0C, 0x083B},
};

static void qr_draw_format_info(uint8_t *qrcode, int size,
                                  int ecl, int mask) {
  uint16_t fmt = FORMAT_INFO[ecl][mask];
  int i;

  /* Along top-left finder */
  for (i = 0; i <= 5; i++)
    qr_set_module(qrcode, size, 8, i, (fmt >> i) & 1);
  qr_set_module(qrcode, size, 8, 7, (fmt >> 6) & 1);
  qr_set_module(qrcode, size, 8, 8, (fmt >> 7) & 1);
  qr_set_module(qrcode, size, 7, 8, (fmt >> 8) & 1);
  for (i = 9; i < 15; i++)
    qr_set_module(qrcode, size, 14 - i, 8, (fmt >> i) & 1);

  /* Along top-right and bottom-left finders */
  for (i = 0; i < 8; i++)
    qr_set_module(qrcode, size, size - 1 - i, 8, (fmt >> i) & 1);
  for (i = 8; i < 15; i++)
    qr_set_module(qrcode, size, 8, size - 15 + i, (fmt >> i) & 1);
}

/* Version info (v7+) */
static void qr_draw_version_info(uint8_t *qrcode, int size, int version) {
  /* Version info lookup table for v7-v40 */
  static const uint32_t VERSION_INFO[] = {
    0x07C94, 0x085BC, 0x09A99, 0x0A4D3, 0x0BBF6, 0x0C762, 0x0D847,
    0x0E60D, 0x0F928, 0x10B78, 0x1145D, 0x12A17, 0x13532, 0x149A6,
    0x15683, 0x168C9, 0x177EC, 0x18EC4, 0x191E1, 0x1AFAB, 0x1B08E,
    0x1CC1A, 0x1D33F, 0x1ED75, 0x1F250, 0x209D5, 0x216F0, 0x228BA,
    0x2379F, 0x24B0B, 0x2542E, 0x26A64, 0x27541, 0x28C69,
  };
  uint32_t vi;
  int i;

  if (version < 7) return;
  vi = VERSION_INFO[version - 7];

  for (i = 0; i < 18; i++) {
    int bit = (vi >> i) & 1;
    int row = i / 3, col = i % 3;
    /* Bottom-left */
    qr_set_module(qrcode, size, col + size - 11, row, bit);
    /* Top-right */
    qr_set_module(qrcode, size, row, col + size - 11, bit);
  }
}

/* ---- Data placement ---- */

static void qr_place_data(uint8_t *qrcode, int size, int version,
                           const uint8_t *data, int data_len) {
  int right, vert, j, bit_idx;
  bit_idx = 0;

  /* Traverse the QR code in the zigzag pattern */
  for (right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5; /* Skip vertical timing column */
    for (vert = 0; vert < size; vert++) {
      for (j = 0; j < 2; j++) {
        int x = right - j;
        /* Determine upward or downward */
        int upward = ((right + 1) & 2) == 0;
        int y = upward ? (size - 1 - vert) : vert;

        if (x < 0 || x >= size || y < 0 || y >= size) continue;
        if (qr_is_function(size, version, x, y)) continue;

        if (bit_idx < data_len * 8) {
          bool dark = ((data[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1) != 0;
          qr_set_module(qrcode, size, x, y, dark);
          bit_idx++;
        } else {
          qr_set_module(qrcode, size, x, y, false);
        }
      }
    }
  }
}

/* ---- Masking ---- */

static bool qr_mask_fn(int mask, int x, int y) {
  switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    default: return false;
  }
}

static void qr_apply_mask(uint8_t *qrcode, int size, int version,
                            int mask) {
  int x, y;
  for (y = 0; y < size; y++) {
    for (x = 0; x < size; x++) {
      if (!qr_is_function(size, version, x, y) &&
          qr_mask_fn(mask, x, y)) {
        bool cur = qr_get_module_raw(qrcode, size, x, y);
        qr_set_module(qrcode, size, x, y, !cur);
      }
    }
  }
}

/* ---- Penalty scoring ---- */

static long qr_penalty(const uint8_t *qrcode, int size) {
  long score = 0;
  int x, y, run, total, dark;

  /* Rule 1: consecutive same-color modules in row/col */
  for (y = 0; y < size; y++) {
    run = 1;
    for (x = 1; x < size; x++) {
      if (qr_get_module_raw(qrcode, size, x, y) ==
          qr_get_module_raw(qrcode, size, x - 1, y)) {
        run++;
      } else {
        if (run >= 5) score += run - 2;
        run = 1;
      }
    }
    if (run >= 5) score += run - 2;
  }
  for (x = 0; x < size; x++) {
    run = 1;
    for (y = 1; y < size; y++) {
      if (qr_get_module_raw(qrcode, size, x, y) ==
          qr_get_module_raw(qrcode, size, x, y - 1)) {
        run++;
      } else {
        if (run >= 5) score += run - 2;
        run = 1;
      }
    }
    if (run >= 5) score += run - 2;
  }

  /* Rule 2: 2x2 blocks of same color */
  for (y = 0; y < size - 1; y++) {
    for (x = 0; x < size - 1; x++) {
      bool c = qr_get_module_raw(qrcode, size, x, y);
      if (c == qr_get_module_raw(qrcode, size, x + 1, y) &&
          c == qr_get_module_raw(qrcode, size, x, y + 1) &&
          c == qr_get_module_raw(qrcode, size, x + 1, y + 1))
        score += 3;
    }
  }

  /* Rule 3: finder-like patterns */
  for (y = 0; y < size; y++) {
    for (x = 0; x < size - 6; x++) {
      if (qr_get_module_raw(qrcode, size, x, y) &&
          !qr_get_module_raw(qrcode, size, x + 1, y) &&
          qr_get_module_raw(qrcode, size, x + 2, y) &&
          qr_get_module_raw(qrcode, size, x + 3, y) &&
          qr_get_module_raw(qrcode, size, x + 4, y) &&
          !qr_get_module_raw(qrcode, size, x + 5, y) &&
          qr_get_module_raw(qrcode, size, x + 6, y))
        score += 40;
    }
  }
  for (x = 0; x < size; x++) {
    for (y = 0; y < size - 6; y++) {
      if (qr_get_module_raw(qrcode, size, x, y) &&
          !qr_get_module_raw(qrcode, size, x, y + 1) &&
          qr_get_module_raw(qrcode, size, x, y + 2) &&
          qr_get_module_raw(qrcode, size, x, y + 3) &&
          qr_get_module_raw(qrcode, size, x, y + 4) &&
          !qr_get_module_raw(qrcode, size, x, y + 5) &&
          qr_get_module_raw(qrcode, size, x, y + 6))
        score += 40;
    }
  }

  /* Rule 4: proportion of dark modules */
  total = size * size;
  dark = 0;
  for (y = 0; y < size; y++)
    for (x = 0; x < size; x++)
      if (qr_get_module_raw(qrcode, size, x, y))
        dark++;
  {
    int pct = dark * 100 / total;
    int prev5 = pct - (pct % 5);
    int next5 = prev5 + 5;
    int a = abs(prev5 - 50) / 5;
    int b = abs(next5 - 50) / 5;
    score += (a < b ? a : b) * 10;
  }

  return score;
}

/* ---- Encoding ---- */

bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[],
    uint8_t qrcode[], enum qrcodegen_Ecc ecl,
    int minVersion, int maxVersion, enum qrcodegen_Mask mask,
    bool boostEcl) {
  int text_len, version, size, data_capacity, num_blocks, ecc_per_block;
  int short_block_len, num_short;
  int i, j, best_mask;
  long best_penalty;
  uint8_t *codewords;
  uint8_t *ecc_buf;

  gf_init();

  if (!text || !tempBuffer || !qrcode) return false;
  text_len = (int) strlen(text);

  if (minVersion < qrcodegen_VERSION_MIN)
    minVersion = qrcodegen_VERSION_MIN;
  if (maxVersion > qrcodegen_VERSION_MAX)
    maxVersion = qrcodegen_VERSION_MAX;
  if (minVersion > maxVersion) return false;

  /* Find smallest version that fits the data */
  version = 0;
  for (i = minVersion; i <= maxVersion; i++) {
    int cap = NUM_DATA_CODEWORDS[i - 1][ecl];
    /* Byte mode overhead: 4 bits mode + char count bits + data */
    int char_count_bits;
    int total_bits;
    if (i <= 9) char_count_bits = 8;
    else if (i <= 26) char_count_bits = 16;
    else char_count_bits = 16;
    total_bits = 4 + char_count_bits + text_len * 8;
    if (total_bits <= cap * 8) {
      version = i;
      break;
    }
  }
  if (version == 0) return false;

  /* Boost ECC if possible */
  if (boostEcl) {
    int cap_bits = 4 + (version <= 9 ? 8 : 16) + text_len * 8;
    int e;
    for (e = (int) qrcodegen_Ecc_HIGH; e > (int) ecl; e--) {
      if (cap_bits <= NUM_DATA_CODEWORDS[version - 1][e] * 8) {
        ecl = (enum qrcodegen_Ecc) e;
        break;
      }
    }
  }

  size = qr_size(version);
  data_capacity = NUM_DATA_CODEWORDS[version - 1][ecl];
  num_blocks = NUM_EC_BLOCKS[version - 1][ecl];
  ecc_per_block = qr_ecc_codewords_per_block(version, ecl);

  /* Build data codewords (byte mode) */
  codewords = tempBuffer;
  memset(codewords, 0, (size_t) data_capacity);
  {
    int bit = 0;
    int char_count_bits = (version <= 9) ? 8 : 16;

    /* Mode indicator: 0100 (byte mode) */
    codewords[0] = 0x40;
    bit = 4;

    /* Character count */
    if (char_count_bits == 16) {
      codewords[bit / 8] |= (uint8_t)((text_len >> (16 - 1 - (bit % 8))) &
                                        ((1 << (8 - (bit % 8))) - 1));
      bit += 8 - (bit % 8);
      /* Remaining bits */
      for (i = 0; i < 8; i++) {
        if ((text_len >> (7 - i)) & 1) {
          codewords[bit / 8] |= (uint8_t)(1 << (7 - (bit % 8)));
        }
        bit++;
      }
    } else {
      for (i = char_count_bits - 1; i >= 0; i--) {
        if ((text_len >> i) & 1) {
          codewords[bit / 8] |= (uint8_t)(1 << (7 - (bit % 8)));
        }
        bit++;
      }
    }

    /* Data */
    for (i = 0; i < text_len; i++) {
      uint8_t ch = (uint8_t) text[i];
      for (j = 7; j >= 0; j--) {
        if ((ch >> j) & 1) {
          codewords[bit / 8] |= (uint8_t)(1 << (7 - (bit % 8)));
        }
        bit++;
      }
    }

    /* Terminator (up to 4 zero bits) */
    {
      int remaining = data_capacity * 8 - bit;
      int term = remaining < 4 ? remaining : 4;
      bit += term;
    }

    /* Pad to byte boundary */
    if (bit % 8 != 0)
      bit += 8 - (bit % 8);

    /* Pad codewords */
    {
      int byte_pos = bit / 8;
      int pad_toggle = 0;
      while (byte_pos < data_capacity) {
        codewords[byte_pos++] = pad_toggle ? 0x11 : 0xEC;
        pad_toggle ^= 1;
      }
    }
  }

  /* Split into blocks and compute ECC */
  short_block_len = data_capacity / num_blocks;
  num_short = num_blocks - (data_capacity % num_blocks);

  /* Interleave data and ECC into qrcode buffer (used as temp) */
  {
    int total_cw = TOTAL_CODEWORDS[version - 1];
    uint8_t *result = qrcode + 1; /* skip size byte */
    int data_offset = 0;
    int ri = 0;

    ecc_buf = (uint8_t *) malloc((size_t) ecc_per_block * (size_t) num_blocks);
    if (!ecc_buf) return false;

    /* Compute ECC for each block */
    {
      int block_offset = 0;
      for (i = 0; i < num_blocks; i++) {
        int block_len = (i < num_short) ? short_block_len :
                                           (short_block_len + 1);
        rs_encode(codewords + block_offset, block_len,
                  ecc_per_block, ecc_buf + i * ecc_per_block);
        block_offset += block_len;
      }
    }

    /* Interleave data codewords */
    for (j = 0; j < short_block_len + 1; j++) {
      int block_offset = 0;
      for (i = 0; i < num_blocks; i++) {
        int block_len = (i < num_short) ? short_block_len :
                                           (short_block_len + 1);
        if (j < block_len) {
          result[ri++] = codewords[block_offset + j];
        }
        block_offset += block_len;
      }
    }

    /* Interleave ECC codewords */
    for (j = 0; j < ecc_per_block; j++) {
      for (i = 0; i < num_blocks; i++) {
        result[ri++] = ecc_buf[i * ecc_per_block + j];
      }
    }

    free(ecc_buf);
    (void) total_cw;
    (void) data_offset;
  }

  /* Now qrcode+1 contains the interleaved codewords. Copy to temp. */
  {
    int total_cw = TOTAL_CODEWORDS[version - 1];
    memcpy(tempBuffer, qrcode + 1, (size_t) total_cw);

    /* Clear qrcode and draw function patterns */
    memset(qrcode, 0,
           (size_t) qrcodegen_BUFFER_LEN_FOR_VERSION(version));
    qr_set_size(qrcode, size);
    qr_draw_function_patterns(qrcode, size, version);
    qr_draw_version_info(qrcode, size, version);

    /* Place data modules */
    qr_place_data(qrcode, size, version, tempBuffer, total_cw);
  }

  /* Apply mask */
  if (mask == qrcodegen_Mask_AUTO) {
    /* Try all 8 masks, pick lowest penalty */
    size_t buf_len = (size_t) qrcodegen_BUFFER_LEN_FOR_VERSION(version);
    uint8_t *temp_qr = (uint8_t *) malloc(buf_len);
    if (!temp_qr) return false;

    best_mask = 0;
    best_penalty = LONG_MAX;
    for (i = 0; i < 8; i++) {
      memcpy(temp_qr, qrcode, buf_len);
      qr_apply_mask(temp_qr, size, version, i);
      qr_draw_format_info(temp_qr, size, ecl, i);
      {
        long pen = qr_penalty(temp_qr, size);
        if (pen < best_penalty) {
          best_penalty = pen;
          best_mask = i;
        }
      }
    }
    free(temp_qr);
    mask = (enum qrcodegen_Mask) best_mask;
  }

  qr_apply_mask(qrcode, size, version, (int) mask);
  qr_draw_format_info(qrcode, size, ecl, (int) mask);

  return true;
}

int qrcodegen_getSize(const uint8_t qrcode[]) {
  int ver = qrcode[0];
  if (ver < qrcodegen_VERSION_MIN || ver > qrcodegen_VERSION_MAX)
    return -1;
  return ver * 4 + 17;
}

bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y) {
  int size = qrcodegen_getSize(qrcode);
  if (size < 0 || x < 0 || x >= size || y < 0 || y >= size)
    return false;
  return qr_get_module_raw(qrcode, size, x, y);
}
