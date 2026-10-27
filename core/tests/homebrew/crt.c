/* Minimal freestanding runtime for the PowerPC homebrew build (MIT).
 * Compilers may lower loops/struct copies into memcpy/memset calls and, on
 * 32-bit PowerPC, 64-bit arithmetic into libgcc-style helpers. */
#include <stddef.h>
#include <stdint.h>

void* memcpy(void* d, const void* s, size_t n) {
  uint8_t* dd = (uint8_t*)d;
  const uint8_t* ss = (const uint8_t*)s;
  while (n--) *dd++ = *ss++;
  return d;
}
void* memset(void* d, int c, size_t n) {
  uint8_t* dd = (uint8_t*)d;
  while (n--) *dd++ = (uint8_t)c;
  return d;
}
void* memmove(void* d, const void* s, size_t n) {
  uint8_t* dd = (uint8_t*)d;
  const uint8_t* ss = (const uint8_t*)s;
  if (dd < ss) while (n--) *dd++ = *ss++;
  else { dd += n; ss += n; while (n--) *--dd = *--ss; }
  return d;
}

#if !defined(__powerpc64__)
typedef unsigned long long du_int;
typedef long long di_int;
static du_int udivmod(du_int n, du_int d, du_int* rem) {
  du_int q = 0, r = 0;
  for (int i = 63; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1);
    if (r >= d) { r -= d; q |= 1ull << i; }
  }
  if (rem) *rem = r;
  return q;
}
du_int __udivdi3(du_int a, du_int b) { return udivmod(a, b, 0); }
du_int __umoddi3(du_int a, du_int b) { du_int r; udivmod(a, b, &r); return r; }
di_int __divdi3(di_int a, di_int b) {
  int neg = (a < 0) ^ (b < 0);
  du_int q = udivmod(a < 0 ? -(du_int)a : (du_int)a, b < 0 ? -(du_int)b : (du_int)b, 0);
  return neg ? -(di_int)q : (di_int)q;
}
di_int __moddi3(di_int a, di_int b) {
  du_int r;
  udivmod(a < 0 ? -(du_int)a : (du_int)a, b < 0 ? -(du_int)b : (du_int)b, &r);
  return a < 0 ? -(di_int)r : (di_int)r;
}
di_int __ashldi3(di_int a, int b) { return (di_int)((du_int)a << b); }
di_int __ashrdi3(di_int a, int b) { return a >> b; }
du_int __lshrdi3(du_int a, int b) { return a >> b; }
#endif
