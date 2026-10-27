/* FIFA STREET Recompiled - homebrew self-test program.
 *
 * Original code written for this project (MIT). It is compiled twice:
 *   1. to big-endian PowerPC (Xbox 360-like) -> packed into an XEX by mkxex
 *      -> recompiled to C++ by fsr-recomp -> compiled for the host / ARM64;
 *   2. natively for the host / ARM64.
 * Both builds must produce identical results; any difference is a
 * recompiler or runtime bug. No libc: freestanding on purpose.
 * FSR_NO_WIDE (set for the 32-bit PowerPC build and its native twin) skips
 * code that would need 64-bit atomics/__int128/libgcc helpers.
 */
#include <stdint.h>

#ifndef FSR_EXPORT
#define FSR_EXPORT
#endif
#define NOINLINE __attribute__((noinline))

/* ---------------------------------------------------------------- helpers */
static NOINLINE uint32_t mix(uint32_t h, uint32_t v) {
  h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
  return h;
}

static NOINLINE uint32_t fib(uint32_t n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static NOINLINE uint32_t crc32_bytes(const uint8_t* p, uint32_t n) {
  uint32_t table[256];
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    table[i] = c;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

static NOINLINE void sort_i32(int32_t* a, int n) {
  for (int i = 1; i < n; i++) {
    int32_t key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) { a[j + 1] = a[j]; j--; }
    a[j + 1] = key;
  }
}

/* Dense switch -> jump table (bctr dispatch in the recompiler). */
static NOINLINE int32_t opcode_vm(const uint8_t* prog, int32_t x) {
  int32_t acc = x;
  for (int pc = 0; prog[pc] != 0xFF; pc++) {
    switch (prog[pc]) {
      case 0: acc += 7; break;
      case 1: acc -= 3; break;
      case 2: acc *= 5; break;
      case 3: acc ^= 0x5A5A; break;
      case 4: acc = acc >> 1; break;
      case 5: acc = acc << 3; break;
      case 6: acc = -acc; break;
      case 7: acc = acc % 97; break;
      case 8: acc = acc / 3; break;
      case 9: acc |= 0x100; break;
      case 10: acc &= 0xFFF0; break;
      default: acc += 1; break;
    }
  }
  return acc;
}

static NOINLINE uint64_t u64_ops(uint64_t a, uint64_t b) {
  uint64_t r = a * b;
  r ^= (a << 13) | (b >> 7);
  r += (a >> 3) * 3;
  r -= b;
  r ^= r >> 29;
  r = (r << 17) | (r >> 47);
  return r;
}

static NOINLINE int32_t i32_div(int32_t a, int32_t b) { return a / b + a % b; }
static NOINLINE uint32_t u32_div(uint32_t a, uint32_t b) { return a / b + a % b; }

static NOINLINE uint32_t bits(uint32_t x) {
  uint32_t r = (uint32_t)__builtin_clz(x | 1);
  r += (x & 0xF0F0u) >> 4;
  r ^= (x >> 5) & 0x3Fu;
  r += (uint32_t)((int32_t)(x << 16) >> 24); /* arithmetic shift */
  r ^= (x << 3) | (x >> 29);                   /* rotate */
  return r;
}

static NOINLINE double dmath(double a, double b) {
  double r = a * b + a / (b + 1.5) - (a - b) * 0.25;
  r = r < 0 ? -r : r;
  r = __builtin_sqrt(r + 2.0);
  return r * 1000.0;
}

static NOINLINE float fmath(float a, float b) {
  float r = a * 0.5f + b * b - a / (b + 2.0f);
  return r > 10.0f ? r - 3.25f : r + 1.75f;
}

static NOINLINE int32_t to_int(double d) { return (int32_t)d; }
static NOINLINE double from_int(int32_t i) { return (double)i * 0.125; }

typedef int32_t (*binop_fn)(int32_t, int32_t);
static NOINLINE int32_t op_add(int32_t a, int32_t b) { return a + b; }
static NOINLINE int32_t op_sub(int32_t a, int32_t b) { return a - b; }
static NOINLINE int32_t op_mul(int32_t a, int32_t b) { return a * b; }
static NOINLINE int32_t op_xor(int32_t a, int32_t b) { return a ^ b; }
static NOINLINE int32_t apply(binop_fn f, int32_t a, int32_t b) { return f(a, b); }

struct Rec {
  int16_t a;
  uint8_t b;
  int32_t c;
  uint16_t d;
  int8_t e;
};

static NOINLINE uint32_t struct_ops(struct Rec* r, int n) {
  uint32_t h = 0;
  for (int i = 0; i < n; i++) {
    r[i].a = (int16_t)(i * -77);
    r[i].b = (uint8_t)(i * 37);
    r[i].c = i * 100003 - 5000;
    r[i].d = (uint16_t)(60000 + i);
    r[i].e = (int8_t)(-i * 3);
  }
  for (int i = 0; i < n; i++)
    h = mix(h, (uint32_t)r[i].a + r[i].b + (uint32_t)r[i].c + r[i].d + (uint32_t)r[i].e);
  return h;
}

static NOINLINE uint32_t str_hash(const char* s) {
  uint32_t h = 5381;
  while (*s) h = h * 33 + (uint8_t)*s++;
  return h;
}

static NOINLINE int32_t cmp_chain(int32_t a, uint32_t b) {
  int32_t r = 0;
  if (a < 0) r |= 1;
  if (a > 1000) r |= 2;
  if (b < 10u) r |= 4;
  if (b > 0x80000000u) r |= 8;
  if ((a == 5) || (b == 7u)) r |= 16;
  if (a <= -3 && b >= 3u) r |= 32;
  return r;
}

static NOINLINE uint32_t atomics(volatile uint32_t* p, volatile uint64_t* q) {
  uint32_t r = 0;
  for (int i = 0; i < 10; i++) r += __atomic_fetch_add(p, (uint32_t)i * 3u + 1u, __ATOMIC_SEQ_CST);
  uint32_t expect = *p;
  r ^= (uint32_t)__atomic_compare_exchange_n(p, &expect, expect ^ 0x55u, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  r += *p;
#if !defined(FSR_NO_WIDE)
  for (int i = 0; i < 4; i++) r += (uint32_t)__atomic_fetch_xor(q, 0x0123456789ull << i, __ATOMIC_SEQ_CST);
  r ^= (uint32_t)(*q >> 17);
#else
  r += 0xC0FFEEu; /* 32-bit build: no 64-bit atomics; keep result layout aligned */
  *q = 0;
#endif
  return r;
}

#if !defined(FSR_NO_WIDE)
static NOINLINE uint64_t div64(uint64_t a, uint64_t b, int64_t c, int64_t d) {
  return a / b + a % b + (uint64_t)(c / d) + (uint64_t)(c % d);
}

static NOINLINE uint32_t wide_bits(uint64_t x, int64_t s) {
  uint32_t r = (uint32_t)__builtin_clzll(x | 1);
  r += (uint32_t)(s >> 33) ^ (uint32_t)((uint64_t)s >> 51);
  uint64_t hi = (uint64_t)(((unsigned __int128)x * 0x9E3779B97F4A7C15ull) >> 64);
  r ^= (uint32_t)hi ^ (uint32_t)(hi >> 32);
  int64_t sh = (int64_t)(((__int128)s * -77777777777ll) >> 64);
  r += (uint32_t)sh;
  return r;
}
#endif

static NOINLINE double fp_misc(double a, float b) {
  double r = (double)(int64_t)(a * 1e6) * 1e-6;          /* fctidz / fcfid */
  r += (a > b) ? a - b : b - a;                            /* compares */
  float fr = b * 3.0f + (float)a;                           /* frsp */
  r += (double)fr;
  r += (double)(uint32_t)(a < 0 ? -a : a);
  return r;
}

/* ---------------------------------------------------------------- entry
 * results[] receives one value per sub-test; the return value is a hash. */
#define NRESULTS 24
FSR_EXPORT uint32_t fsr_selftest(uint32_t* results) {
  uint32_t r[NRESULTS];
  r[0] = fib(20);
  static const uint8_t msg[] = "FIFA STREET Recompiled homebrew self-test";
  r[1] = crc32_bytes(msg, sizeof(msg) - 1);
  int32_t arr[16] = {5, -3, 99, 0, -1000, 42, 7, 7, 13, -8, 2147483647, -2147483647, 1, 2, 3, 4};
  sort_i32(arr, 16);
  r[2] = 0;
  for (int i = 0; i < 16; i++) r[2] = mix(r[2], (uint32_t)arr[i]);
  static const uint8_t prog[] = {0, 2, 3, 1, 5, 4, 7, 9, 8, 6, 10, 11, 2, 2, 0, 0xFF};
  r[3] = (uint32_t)opcode_vm(prog, 12345);
  uint64_t u = u64_ops(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull);
  r[4] = (uint32_t)u;
  r[5] = (uint32_t)(u >> 32);
  r[6] = (uint32_t)i32_div(-1000003, 97) ^ (uint32_t)i32_div(77777, -13);
  r[7] = u32_div(0xFFFFFFF0u, 12345u);
  r[8] = bits(0x12345678u) ^ bits(0x80000001u);
  r[9] = (uint32_t)to_int(dmath(3.75, -1.25));
  r[10] = (uint32_t)to_int(fmath(2.5f, 3.0f) * 1000.0f);
  r[11] = (uint32_t)to_int(from_int(-123456) * 16.0);
  binop_fn ops[4] = {op_add, op_sub, op_mul, op_xor};
  int32_t acc = 17;
  for (int i = 0; i < 12; i++) acc = apply(ops[i & 3], acc, i * 31 + 5);
  r[12] = (uint32_t)acc;
  struct Rec recs[8];
  r[13] = struct_ops(recs, 8);
  r[14] = str_hash("Xenon PowerPC -> ARM64");
  r[15] = (uint32_t)cmp_chain(-5, 3) | (uint32_t)cmp_chain(5000, 0x90000000u) << 8 | (uint32_t)cmp_chain(5, 7) << 16;
  int64_t s64 = -9876543210ll;
  r[16] = (uint32_t)(s64 >> 7) ^ (uint32_t)((uint64_t)s64 >> 40);
  uint8_t buf[64];
  for (int i = 0; i < 64; i++) buf[i] = (uint8_t)(i * 7 + 1);
  r[17] = crc32_bytes(buf + 3, 50);
  int16_t sh = -1234;
  uint16_t us = 0xBEEF;
  r[18] = (uint32_t)(int32_t)sh * 3 + us;
  double acc_d = 0;
  for (int i = 1; i <= 10; i++) acc_d += 1.0 / (double)i;
  r[19] = (uint32_t)to_int(acc_d * 1e6);
  volatile uint32_t atom32 = 7;
  volatile uint64_t atom64 = 0xFEEDFACECAFEBEEFull;
  r[20] = atomics(&atom32, &atom64);
#if defined(FSR_NO_WIDE)
  r[21] = 0x32323232u; /* 64-bit division needs libgcc helpers in 32-bit mode */
  r[22] = 0x32323232u;
#else
  uint64_t dv = div64(0xFFFFFFFFFFFFFFF1ull, 0x123456789ull, -98765432109876ll, 12345ll);
  r[21] = (uint32_t)dv ^ (uint32_t)(dv >> 32);
  r[22] = wide_bits(0x00F0F0F0F0F0F0F0ull, -0x123456789ABCll);
#endif
  r[23] = (uint32_t)to_int(fp_misc(123.456789, -2.5f) * 1000.0);

  uint32_t h = 0;
  for (int i = 0; i < NRESULTS; i++) {
    results[i] = r[i];
    h = mix(h, r[i]);
  }
  return h;
}
