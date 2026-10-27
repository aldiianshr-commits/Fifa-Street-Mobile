/* FIFA STREET Recompiled - vector (AltiVec/VMX) self-test (original code, MIT).
 *
 * Plain C loops: the PowerPC build is compiled with -maltivec so clang
 * auto-vectorises them into VMX instructions; the native build compiles the
 * same loops for the host. Explicit AltiVec intrinsics are used only for
 * operations auto-vectorisation never emits (vperm/vsldoi/vsel/lvsl...),
 * each paired with a scalar reference written from the AltiVec spec.
 */
#include <stdint.h>

#define NOINLINE __attribute__((noinline))
#define N 64

static NOINLINE uint32_t hash_words(const void* p, int n) {
  const uint32_t* w = (const uint32_t*)p;
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ w[i]) * 16777619u;
  return h;
}

/* Element-wise hashes so host (little-endian) and guest (big-endian) agree. */
static NOINLINE uint32_t hash_u8(const uint8_t* p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static NOINLINE uint32_t hash_i16(const int16_t* p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ (uint16_t)p[i]) * 16777619u;
  return h;
}

static NOINLINE void f_ops(float* restrict o, const float* restrict a, const float* restrict b, int n) {
  for (int i = 0; i < n; i++) {
    float x = a[i] + b[i];
    float y = a[i] * b[i];
    float z = x - y;
    float m = a[i] > b[i] ? a[i] : b[i];
    float k = a[i] < b[i] ? a[i] : b[i];
    o[i] = z + m * 0.5f - k;
  }
}

static NOINLINE void i_ops(int32_t* restrict o, const int32_t* restrict a, const int32_t* restrict b, int n) {
  for (int i = 0; i < n; i++) {
    int32_t x = a[i] + b[i];
    int32_t y = (a[i] - b[i]) ^ (a[i] & 0x0F0F);
    int32_t s = (a[i] << 3) | ((uint32_t)b[i] >> 5);
    int32_t r = a[i] >> 2;
    int32_t mx = a[i] > b[i] ? a[i] : b[i];
    o[i] = x + y + s + r - mx;
  }
}

static NOINLINE void u8_ops(uint8_t* restrict o, const uint8_t* restrict a, const uint8_t* restrict b, int n) {
  for (int i = 0; i < n; i++) {
    unsigned s = (unsigned)a[i] + b[i];
    uint8_t sat = s > 255 ? 255 : (uint8_t)s;
    uint8_t avg = (uint8_t)((a[i] + b[i] + 1) >> 1);
    uint8_t mn = a[i] < b[i] ? a[i] : b[i];
    o[i] = (uint8_t)(sat ^ avg ^ mn);
  }
}

static NOINLINE void i16_ops(int16_t* restrict o, int32_t* restrict wide, const int16_t* restrict a, const int16_t* restrict b, int n) {
  for (int i = 0; i < n; i++) {
    o[i] = (int16_t)(a[i] * b[i] + (a[i] >> 1));
    wide[i] = (int32_t)a[i] * (int32_t)b[i];
  }
}

static NOINLINE void conv_ops(int32_t* restrict oi, float* restrict of, const float* restrict a, const int32_t* restrict b, int n) {
  for (int i = 0; i < n; i++) {
    oi[i] = (int32_t)(a[i] * 4.0f);
    of[i] = (float)b[i] * 0.25f;
  }
}

static NOINLINE void select_ops(float* restrict o, const float* restrict a, const float* restrict b, int n) {
  for (int i = 0; i < n; i++) o[i] = (a[i] >= b[i]) ? a[i] * 2.0f : b[i] - 1.0f;
}

static NOINLINE int32_t reduce_i(const int32_t* a, int n) {
  int32_t s = 0;
  for (int i = 0; i < n; i++) s += a[i];
  return s;
}

static NOINLINE void reverse_u32(uint32_t* restrict o, const uint32_t* restrict a, int n) {
  for (int i = 0; i < n; i++) o[i] = a[n - 1 - i];
}

#if defined(__ALTIVEC__)
#include <altivec.h>
typedef __vector unsigned char vu8;
typedef __vector unsigned int vu32;
typedef __vector float vf32;
static NOINLINE void intrin(uint32_t* out, const uint32_t* in) {
  vu32 a = vec_ld(0, in), b = vec_ld(16, in);
  vu8 perm = {3, 2, 1, 0, 23, 22, 21, 20, 8, 9, 10, 11, 31, 30, 29, 28};
  vec_st(vec_perm(a, b, perm), 0, out);
  vec_st(vec_sld(a, b, 5), 16, out);
  vu32 m = vec_splat_u32(7);
  vec_st(vec_sel(a, b, (vu32)vec_cmpgt(a, b)), 32, out);
  vec_st(vec_add(vec_splat(a, 2), m), 48, out);
  vec_st((vu32)vec_lvsl(5, (unsigned char*)0), 64, out);
  vec_st((vu32)vec_lvsr(7, (unsigned char*)0), 80, out);
  vec_st(vec_mergeh(a, b), 96, out);
  vec_st(vec_mergel(a, b), 112, out);
  vf32 fa = vec_ctf(a, 4);
  vec_st((vu32)vec_cts(vec_madd(fa, fa, (vf32)vec_splat_u32(0)), 2), 128, out);
  vec_st((vu32)vec_packs((__vector signed int)a, (__vector signed int)b), 144, out);
}

typedef __vector signed char vs8;
typedef __vector signed short vs16;
typedef __vector unsigned short vu16;
typedef __vector signed int vs32;
/* Second battery: saturating/averaging/multiply-sum/unpack/rotate/record forms. */
static NOINLINE uint32_t intrin2(uint32_t* out, const uint32_t* in) {
  vu8 a8 = (vu8)vec_ld(0, in), b8 = (vu8)vec_ld(16, in);
  vs16 a16 = (vs16)vec_ld(0, in), b16 = (vs16)vec_ld(16, in);
  vs32 a32 = (vs32)vec_ld(0, in), b32 = (vs32)vec_ld(16, in);
  vec_st((vu32)vec_adds(a8, b8), 0, out);                 /* vaddubs */
  vec_st((vu32)vec_subs(a16, b16), 16, out);              /* vsubshs */
  vec_st((vu32)vec_adds(a32, b32), 32, out);              /* vaddsws */
  vec_st((vu32)vec_avg(a8, b8), 48, out);                 /* vavgub */
  vec_st((vu32)vec_min((vs8)a8, (vs8)b8), 64, out);       /* vminsb */
  vec_st((vu32)vec_mule((vu16)a16, (vu16)b16), 80, out);  /* vmuleuh */
  vec_st((vu32)vec_mulo(a16, b16), 96, out);              /* vmulosh */
  vec_st((vu32)vec_msum(a16, b16, a32), 112, out);        /* vmsumshm */
  vec_st((vu32)vec_sum4s((vs8)a8, b32), 128, out);        /* vsum4sbs */
  vec_st((vu32)vec_sums(a32, b32), 144, out);             /* vsumsws */
  vec_st((vu32)vec_unpackh(a16), 160, out);               /* vupkhsh */
  vec_st((vu32)vec_unpackl((vs8)b8), 176, out);           /* vupklsb */
  vec_st((vu32)vec_rl((vu32)a32, (vu32)b32), 192, out);   /* vrlw */
  vec_st((vu32)vec_sl(a8, b8), 208, out);                 /* vslb */
  vec_st((vu32)vec_sra(a16, (vu16)b16), 224, out);        /* vsrah */
  vec_st((vu32)vec_slo(a8, vec_sl(vec_splat_u8(7), vec_splat_u8(3))), 240, out); /* vslo by 7 octets */
  vec_st((vu32)vec_packsu(a32, b32), 256, out);           /* vpkswus */
  vec_st((vu32)vec_mradds(a16, b16, a16), 272, out);      /* vmhraddshs */
  vec_st((vu32)vec_cmpeq(a8, b8), 288, out);              /* vcmpequb */
  vf32 fa = vec_ctf(a32, 8), fb = vec_ctf(b32, 12);
  vec_st((vu32)vec_nmsub(fa, fb, fa), 304, out);          /* vnmsubfp */
  vec_st((vu32)vec_floor(fa), 320, out);                  /* vrfim */
  vec_st((vu32)vec_ceil(fb), 336, out);                   /* vrfip */
  vec_st((vu32)vec_trunc(fa), 352, out);                  /* vrfiz */
  vec_st((vu32)vec_ctu(fb, 3), 368, out);                 /* vctuxs */
  uint32_t flags = 0;
  flags |= vec_all_eq(a32, a32) ? 1u : 0;                 /* vcmpequw. + CR6 */
  flags |= vec_any_eq(a32, b32) ? 2u : 0;
  flags |= vec_all_gt((vu8)a8, (vu8)b8) ? 4u : 0;
  flags |= vec_any_gt((vu8)a8, (vu8)b8) ? 8u : 0;
  flags |= vec_all_ge(fa, fb) ? 16u : 0;                  /* vcmpgefp. */
  flags |= vec_any_lt(fa, fb) ? 32u : 0;
  flags |= vec_all_in(fa, fb) ? 64u : 0;                  /* vcmpbfp. */
  return flags;
}
#else
/* Scalar reference implementations (AltiVec semantics, big-endian element order). */
static uint8_t byte_of(const uint32_t* v, int i) { return (uint8_t)(v[i >> 2] >> (24 - 8 * (i & 3))); }
static void put_byte(uint32_t* v, int i, uint8_t x) {
  int s = 24 - 8 * (i & 3);
  v[i >> 2] = (v[i >> 2] & ~(0xFFu << s)) | ((uint32_t)x << s);
}
static NOINLINE void intrin(uint32_t* out, const uint32_t* in) {
  const uint32_t* a = in;
  const uint32_t* b = in + 4;
  static const uint8_t perm[16] = {3, 2, 1, 0, 23, 22, 21, 20, 8, 9, 10, 11, 31, 30, 29, 28};
  for (int i = 0; i < 16; i++) {
    int k = perm[i] & 31;
    put_byte(out, i, k < 16 ? byte_of(a, k) : byte_of(b, k - 16));
  }
  for (int i = 0; i < 16; i++) {
    int k = i + 5;
    put_byte(out + 4, i, k < 16 ? byte_of(a, k) : byte_of(b, k - 16));
  }
  for (int i = 0; i < 4; i++) out[8 + i] = a[i] > b[i] ? b[i] : a[i];
  for (int i = 0; i < 4; i++) out[12 + i] = a[2] + 7;
  for (int i = 0; i < 16; i++) put_byte(out + 16, i, (uint8_t)(5 + i));
  for (int i = 0; i < 16; i++) put_byte(out + 20, i, (uint8_t)(16 - 7 + i));
  out[24] = a[0]; out[25] = b[0]; out[26] = a[1]; out[27] = b[1];
  out[28] = a[2]; out[29] = b[2]; out[30] = a[3]; out[31] = b[3];
  for (int i = 0; i < 4; i++) {
    float f = (float)a[i] / 16.0f;
    float p = f * f + 0.0f;
    double s = (double)p * 4.0;
    int32_t v = s >= 2147483647.0 ? 2147483647 : s <= -2147483648.0 ? (int32_t)0x80000000 : (int32_t)s;
    out[32 + i] = (uint32_t)v;
  }
  for (int i = 0; i < 8; i++) {
    int32_t v = (int32_t)(i < 4 ? a[i] : b[i - 4]);
    int16_t s = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
    uint32_t* w = out + 36 + i / 2;
    if (i & 1) *w = (*w & 0xFFFF0000u) | (uint16_t)s;
    else *w = (*w & 0x0000FFFFu) | ((uint32_t)(uint16_t)s << 16);
  }
}

/* ---- scalar references for intrin2 (AltiVec semantics) ---- */
static int8_t sb(const uint32_t* v, int i) { return (int8_t)byte_of(v, i); }
static uint16_t hw(const uint32_t* v, int i) { return (uint16_t)(v[i >> 1] >> ((i & 1) ? 0 : 16)); }
static void put_h(uint32_t* v, int i, uint16_t x) {
  int s = (i & 1) ? 0 : 16;
  v[i >> 1] = (v[i >> 1] & ~(0xFFFFu << s)) | ((uint32_t)x << s);
}
static float fl(uint32_t u) { float f; __builtin_memcpy(&f, &u, 4); return f; }
static uint32_t ufl(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
static int64_t clampi(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static NOINLINE uint32_t intrin2(uint32_t* out, const uint32_t* in) {
  const uint32_t* a = in;
  const uint32_t* b = in + 4;
  for (int i = 0; i < 16; i++) put_byte(out + 0, i, (uint8_t)clampi((int)byte_of(a, i) + byte_of(b, i), 0, 255));
  for (int i = 0; i < 8; i++) put_h(out + 4, i, (uint16_t)clampi((int)(int16_t)hw(a, i) - (int16_t)hw(b, i), -32768, 32767));
  for (int i = 0; i < 4; i++) out[8 + i] = (uint32_t)clampi((int64_t)(int32_t)a[i] + (int32_t)b[i], INT32_MIN, INT32_MAX);
  for (int i = 0; i < 16; i++) put_byte(out + 12, i, (uint8_t)((byte_of(a, i) + byte_of(b, i) + 1) >> 1));
  for (int i = 0; i < 16; i++) put_byte(out + 16, i, (uint8_t)(sb(a, i) < sb(b, i) ? sb(a, i) : sb(b, i)));
  for (int i = 0; i < 4; i++) out[20 + i] = (uint32_t)hw(a, 2 * i) * hw(b, 2 * i);
  for (int i = 0; i < 4; i++) out[24 + i] = (uint32_t)((int32_t)(int16_t)hw(a, 2 * i + 1) * (int16_t)hw(b, 2 * i + 1));
  for (int i = 0; i < 4; i++)
    out[28 + i] = a[i] + (uint32_t)((int32_t)(int16_t)hw(a, 2 * i) * (int16_t)hw(b, 2 * i)) +
                  (uint32_t)((int32_t)(int16_t)hw(a, 2 * i + 1) * (int16_t)hw(b, 2 * i + 1));
  for (int i = 0; i < 4; i++) {
    int64_t v = (int32_t)b[i];
    for (int j = 0; j < 4; j++) v += sb(a, 4 * i + j);
    out[32 + i] = (uint32_t)clampi(v, INT32_MIN, INT32_MAX);
  }
  {
    int64_t v = (int32_t)b[3];
    for (int i = 0; i < 4; i++) v += (int32_t)a[i];
    out[36] = out[37] = out[38] = 0;
    out[39] = (uint32_t)clampi(v, INT32_MIN, INT32_MAX);
  }
  for (int i = 0; i < 4; i++) out[40 + i] = (uint32_t)(int32_t)(int16_t)hw(a, i);
  for (int i = 0; i < 8; i++) put_h(out + 44, i, (uint16_t)(int16_t)sb(b, 8 + i));
  for (int i = 0; i < 4; i++) { uint32_t n = b[i] & 31; out[48 + i] = n ? (a[i] << n) | (a[i] >> (32 - n)) : a[i]; }
  for (int i = 0; i < 16; i++) put_byte(out + 52, i, (uint8_t)(byte_of(a, i) << (byte_of(b, i) & 7)));
  for (int i = 0; i < 8; i++) put_h(out + 56, i, (uint16_t)((int16_t)hw(a, i) >> (hw(b, i) & 15)));
  for (int i = 0; i < 16; i++) put_byte(out + 60, i, i + 7 < 16 ? byte_of(a, i + 7) : 0);
  for (int i = 0; i < 8; i++) {
    int32_t v = (int32_t)(i < 4 ? a[i] : b[i - 4]);
    put_h(out + 64, i, (uint16_t)clampi(v, 0, 65535));
  }
  for (int i = 0; i < 8; i++)
    put_h(out + 68, i, (uint16_t)clampi((((int32_t)(int16_t)hw(a, i) * (int16_t)hw(b, i) + 0x4000) >> 15) + (int16_t)hw(a, i), -32768, 32767));
  for (int i = 0; i < 16; i++) put_byte(out + 72, i, byte_of(a, i) == byte_of(b, i) ? 0xFF : 0);
  float fa[4], fb[4];
  for (int i = 0; i < 4; i++) { fa[i] = (float)(int32_t)a[i] / 256.0f; fb[i] = (float)(int32_t)b[i] / 4096.0f; }
  for (int i = 0; i < 4; i++) out[76 + i] = ufl(-__builtin_fmaf(fa[i], fb[i], -fa[i]));
  for (int i = 0; i < 4; i++) out[80 + i] = ufl(__builtin_floorf(fa[i]));
  for (int i = 0; i < 4; i++) out[84 + i] = ufl(__builtin_ceilf(fb[i]));
  for (int i = 0; i < 4; i++) out[88 + i] = ufl(__builtin_truncf(fa[i]));
  for (int i = 0; i < 4; i++) {
    double v = (double)fb[i] * 8.0;
    out[92 + i] = v <= 0 ? 0u : v >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)v;
  }
  uint32_t flags = 1;
  int any = 0, allgt = 1, anygt = 0, allge = 1, anylt = 0, allin = 1;
  for (int i = 0; i < 4; i++) any |= a[i] == b[i];
  for (int i = 0; i < 16; i++) { allgt &= byte_of(a, i) > byte_of(b, i); anygt |= byte_of(a, i) > byte_of(b, i); }
  for (int i = 0; i < 4; i++) {
    allge &= fa[i] >= fb[i];
    anylt |= fa[i] < fb[i];
    allin &= (fa[i] <= fb[i]) && (fa[i] >= -fb[i]);
  }
  flags |= any ? 2u : 0; flags |= allgt ? 4u : 0; flags |= anygt ? 8u : 0;
  flags |= allge ? 16u : 0; flags |= anylt ? 32u : 0; flags |= allin ? 64u : 0;
  (void)fl;
  return flags;
}
#endif

/* Diagnostic entry: writes the raw intrin()/intrin2() output vectors. */
uint32_t fsr_vectest_dump(uint32_t* out /* 40 + 96 words */) {
  uint32_t in[8] __attribute__((aligned(16))) = {0x01020304u, 0x80000001u, 0x7FFFFFFFu, 0x00012345u,
                                                 0xFFFFFFF0u, 0x00000002u, 0x10203040u, 0x00008000u};
  uint32_t in2[8] __attribute__((aligned(16))) = {0x7F80FF01u, 0x8001FFFEu, 0x12345678u, 0xFEDCBA98u,
                                                  0x01FF807Fu, 0x7FFF0002u, 0x80000000u, 0x00030005u};
  uint32_t o1[40] __attribute__((aligned(16)));
  uint32_t o2[96] __attribute__((aligned(16)));
  for (int i = 0; i < 40; i++) o1[i] = 0;
  for (int i = 0; i < 96; i++) o2[i] = 0;
  intrin(o1, in);
  uint32_t f = intrin2(o2, in2);
  for (int i = 0; i < 40; i++) out[i] = o1[i];
  for (int i = 0; i < 96; i++) out[40 + i] = o2[i];
  return f;
}

#define NVRESULTS 12
uint32_t fsr_vectest(uint32_t* results) {
  float fa[N] __attribute__((aligned(16))), fb[N] __attribute__((aligned(16))), fo[N] __attribute__((aligned(16)));
  int32_t ia[N] __attribute__((aligned(16))), ib[N] __attribute__((aligned(16))), io[N] __attribute__((aligned(16)));
  uint8_t ua[N] __attribute__((aligned(16))), ub[N] __attribute__((aligned(16))), uo[N] __attribute__((aligned(16)));
  int16_t sa[N] __attribute__((aligned(16))), sb[N] __attribute__((aligned(16))), so[N] __attribute__((aligned(16)));
  for (int i = 0; i < N; i++) {
    fa[i] = (float)(i * 3 - 50) * 0.37f;
    fb[i] = (float)(77 - i * 2) * 0.21f;
    ia[i] = i * 123457 - 3000000 + (i & 1 ? -i * 9999 : i);
    ib[i] = 1000003 - i * 77777;
    ua[i] = (uint8_t)(i * 37 + 11);
    ub[i] = (uint8_t)(250 - i * 13);
    sa[i] = (int16_t)(i * 517 - 9000);
    sb[i] = (int16_t)(31 - i * 3);
  }
  uint32_t r[NVRESULTS];
  f_ops(fo, fa, fb, N);
  r[0] = hash_words(fo, N);
  i_ops(io, ia, ib, N);
  r[1] = hash_words(io, N);
  u8_ops(uo, ua, ub, N);
  r[2] = hash_u8(uo, N);
  int32_t wide[N] __attribute__((aligned(16)));
  i16_ops(so, wide, sa, sb, N);
  r[3] = hash_i16(so, N) ^ hash_words(wide, N);
  conv_ops(io, fo, fa, ia, N);
  r[4] = hash_words(io, N) ^ hash_words(fo, N);
  select_ops(fo, fa, fb, N);
  r[5] = hash_words(fo, N);
  r[6] = (uint32_t)reduce_i(ia, N);
  uint32_t rv[N] __attribute__((aligned(16)));
  reverse_u32(rv, (const uint32_t*)ia, N);
  r[7] = hash_words(rv, N);
  uint32_t in[8] __attribute__((aligned(16))) = {0x01020304u, 0x80000001u, 0x7FFFFFFFu, 0x00012345u,
                                                 0xFFFFFFF0u, 0x00000002u, 0x10203040u, 0x00008000u};
  uint32_t out[40] __attribute__((aligned(16)));
  for (int i = 0; i < 40; i++) out[i] = 0;
  intrin(out, in);
  r[8] = hash_words(out, 40);
  r[9] = hash_words(out, 16);
  uint32_t in2[8] __attribute__((aligned(16))) = {0x7F80FF01u, 0x8001FFFEu, 0x12345678u, 0xFEDCBA98u,
                                                  0x01FF807Fu, 0x7FFF0002u, 0x80000000u, 0x00030005u};
  uint32_t out2[96] __attribute__((aligned(16)));
  for (int i = 0; i < 96; i++) out2[i] = 0;
  r[10] = intrin2(out2, in2);
  r[11] = hash_words(out2, 96);
  uint32_t h = 0;
  for (int i = 0; i < NVRESULTS; i++) {
    results[i] = r[i];
    h = (h ^ r[i]) * 0x01000193u;
  }
  return h;
}
