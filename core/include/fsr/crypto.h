// FIFA STREET Recompiled - minimal AES-128 + SHA-1 used by the XEX loader.
// No keys are shipped with this project. Keys must be supplied by the user.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

namespace fsr {

class Aes128 {
 public:
  explicit Aes128(const uint8_t key[16]);
  void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;
  void decrypt_block(const uint8_t in[16], uint8_t out[16]) const;
  // CBC over whole 16-byte blocks; iv is updated in place so calls can chain.
  void cbc_decrypt(const uint8_t* in, uint8_t* out, size_t n, uint8_t iv[16]) const;
  void cbc_encrypt(const uint8_t* in, uint8_t* out, size_t n, uint8_t iv[16]) const;

 private:
  uint8_t rk_[176];
};

struct Sha1 {
  uint32_t h[5];
  uint64_t len = 0;
  uint8_t buf[64];
  size_t used = 0;
  Sha1();
  void update(const void* data, size_t n);
  void final(uint8_t out[20]);
  static void hash(const void* data, size_t n, uint8_t out[20]);

 private:
  void block(const uint8_t* p);
};

}  // namespace fsr
