// SPDX-License-Identifier: MIT
#include "fsr/crypto.h"

#include <cstring>

namespace fsr {
namespace {

struct Tables {
  uint8_t sbox[256], inv[256];
  Tables() {
    // Generate the AES S-box from GF(2^8) inverses + affine transform.
    uint8_t p = 1, q = 1;
    do {
      p = uint8_t(p ^ (p << 1) ^ (p & 0x80 ? 0x1B : 0));
      q ^= q << 1; q ^= q << 2; q ^= q << 4; if (q & 0x80) q ^= 0x09;
      uint8_t x = uint8_t(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^
                          (q << 4 | q >> 4));
      sbox[p] = x ^ 0x63;
    } while (p != 1);
    sbox[0] = 0x63;
    for (int i = 0; i < 256; i++) inv[sbox[i]] = uint8_t(i);
  }
};
const Tables& T() { static Tables t; return t; }

inline uint8_t xt(uint8_t x) { return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
inline uint8_t mul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; }
  return r;
}

}  // namespace

Aes128::Aes128(const uint8_t key[16]) {
  const auto& t = T();
  std::memcpy(rk_, key, 16);
  uint8_t rcon = 1;
  for (int i = 16; i < 176; i += 4) {
    uint8_t tmp[4] = {rk_[i - 4], rk_[i - 3], rk_[i - 2], rk_[i - 1]};
    if (i % 16 == 0) {
      uint8_t a = tmp[0];
      tmp[0] = uint8_t(t.sbox[tmp[1]] ^ rcon);
      tmp[1] = t.sbox[tmp[2]];
      tmp[2] = t.sbox[tmp[3]];
      tmp[3] = t.sbox[a];
      rcon = xt(rcon);
    }
    for (int j = 0; j < 4; j++) rk_[i + j] = rk_[i - 16 + j] ^ tmp[j];
  }
}

void Aes128::encrypt_block(const uint8_t in[16], uint8_t out[16]) const {
  const auto& t = T();
  uint8_t s[16];
  for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk_[i];
  for (int r = 1; r <= 10; r++) {
    uint8_t u[16];
    for (int i = 0; i < 16; i++) u[i] = t.sbox[s[(i + 4 * (i % 4)) % 16]];  // SubBytes+ShiftRows
    if (r != 10) {
      for (int c = 0; c < 4; c++) {
        uint8_t* col = u + 4 * c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = uint8_t(xt(a0) ^ xt(a1) ^ a1 ^ a2 ^ a3);
        col[1] = uint8_t(a0 ^ xt(a1) ^ xt(a2) ^ a2 ^ a3);
        col[2] = uint8_t(a0 ^ a1 ^ xt(a2) ^ xt(a3) ^ a3);
        col[3] = uint8_t(xt(a0) ^ a0 ^ a1 ^ a2 ^ xt(a3));
      }
    }
    for (int i = 0; i < 16; i++) s[i] = u[i] ^ rk_[16 * r + i];
  }
  std::memcpy(out, s, 16);
}

void Aes128::decrypt_block(const uint8_t in[16], uint8_t out[16]) const {
  const auto& t = T();
  uint8_t s[16];
  for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk_[160 + i];
  for (int r = 9; r >= 0; r--) {
    uint8_t u[16];
    // InvShiftRows + InvSubBytes
    for (int i = 0; i < 16; i++) u[(i + 4 * (i % 4)) % 16] = t.inv[s[i]];
    for (int i = 0; i < 16; i++) u[i] ^= rk_[16 * r + i];
    if (r != 0) {
      for (int c = 0; c < 4; c++) {
        uint8_t* col = u + 4 * c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = uint8_t(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
        col[1] = uint8_t(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
        col[2] = uint8_t(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
        col[3] = uint8_t(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
      }
    }
    std::memcpy(s, u, 16);
  }
  std::memcpy(out, s, 16);
}

void Aes128::cbc_decrypt(const uint8_t* in, uint8_t* out, size_t n, uint8_t iv[16]) const {
  for (size_t off = 0; off + 16 <= n; off += 16) {
    uint8_t ct[16], pt[16];
    std::memcpy(ct, in + off, 16);
    decrypt_block(ct, pt);
    for (int i = 0; i < 16; i++) out[off + i] = pt[i] ^ iv[i];
    std::memcpy(iv, ct, 16);
  }
}

void Aes128::cbc_encrypt(const uint8_t* in, uint8_t* out, size_t n, uint8_t iv[16]) const {
  for (size_t off = 0; off + 16 <= n; off += 16) {
    uint8_t x[16];
    for (int i = 0; i < 16; i++) x[i] = in[off + i] ^ iv[i];
    encrypt_block(x, out + off);
    std::memcpy(iv, out + off, 16);
  }
}

// ---------------------------------------------------------------- SHA-1
Sha1::Sha1() : h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u} {}

static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void Sha1::block(const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
  for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    uint32_t tmp = rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol(b, 30); b = a; a = tmp;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void Sha1::update(const void* data, size_t n) {
  auto p = static_cast<const uint8_t*>(data);
  len += n;
  while (n) {
    size_t take = 64 - used < n ? 64 - used : n;
    std::memcpy(buf + used, p, take);
    used += take; p += take; n -= take;
    if (used == 64) { block(buf); used = 0; }
  }
}

void Sha1::final(uint8_t out[20]) {
  uint64_t bits = len * 8;
  uint8_t pad = 0x80;
  update(&pad, 1);
  uint8_t z = 0;
  while (used != 56) update(&z, 1);
  uint8_t l[8];
  for (int i = 0; i < 8; i++) l[i] = uint8_t(bits >> (56 - 8 * i));
  update(l, 8);
  for (int i = 0; i < 5; i++) {
    out[4 * i] = h[i] >> 24; out[4 * i + 1] = uint8_t(h[i] >> 16);
    out[4 * i + 2] = uint8_t(h[i] >> 8); out[4 * i + 3] = uint8_t(h[i]);
  }
}

void Sha1::hash(const void* data, size_t n, uint8_t out[20]) {
  Sha1 s;
  s.update(data, n);
  s.final(out);
}

}  // namespace fsr
