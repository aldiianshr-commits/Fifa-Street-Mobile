// Unit tests for the FIFA STREET Recompiled core (no external framework).
//   unit_tests <homebrew-out-dir> <tools-dir>
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "fsr/bytes.h"
#include "fsr/crypto.h"
#include "fsr/iso.h"
#include "fsr/ppc.h"
#include "fsr/recomp.h"
#include "fsr/report.h"
#include "fsr/runtime.h"
#include "fsr/xex.h"

using namespace fsr;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) g_pass++;                                                      \
    else { g_fail++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> v;
  for (size_t i = 0; h[i] && h[i + 1]; i += 2) v.push_back(uint8_t(std::stoul(std::string(h + i, 2), nullptr, 16)));
  return v;
}

static void test_aes() {
  // FIPS-197 Appendix C.1
  auto key = unhex("000102030405060708090a0b0c0d0e0f");
  auto pt = unhex("00112233445566778899aabbccddeeff");
  auto ct = unhex("69c4e0d86a7b0430d8cdb78070b4c55a");
  Aes128 aes(key.data());
  uint8_t out[16], back[16];
  aes.encrypt_block(pt.data(), out);
  CHECK(std::memcmp(out, ct.data(), 16) == 0);
  aes.decrypt_block(ct.data(), back);
  CHECK(std::memcmp(back, pt.data(), 16) == 0);
  // CBC round trip over several blocks with chained IV.
  uint8_t buf[64], enc[64], dec[64], iv1[16] = {}, iv2[16] = {};
  for (int i = 0; i < 64; i++) buf[i] = uint8_t(i * 13);
  aes.cbc_encrypt(buf, enc, 64, iv1);
  aes.cbc_decrypt(enc, dec, 32, iv2);
  aes.cbc_decrypt(enc + 32, dec + 32, 32, iv2);
  CHECK(std::memcmp(buf, dec, 64) == 0);
}

static void test_sha1() {
  uint8_t h[20];
  Sha1::hash("abc", 3, h);
  CHECK(hexbytes(h, 20) == "A9993E364706816ABA3E25717850C26C9CD0D89D");
  std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  Sha1::hash(m.data(), m.size(), h);
  CHECK(hexbytes(h, 20) == "84983E441C3BD26EBAAE4AA1F95129E5E54670F1");
}

static void test_ppc_decode() {
  using namespace ppc;
  CHECK(decode(0x7C0802A6) == Op::mfspr);  // mflr r0
  CHECK(f_spr(0x7C0802A6) == 8);
  CHECK(decode(0x4E800020) == Op::bclr);   // blr
  CHECK(decode(0x4E800420) == Op::bcctr);  // bctr
  CHECK(decode(0x7C641A14) == Op::add);    // add r3,r4,r3
  CHECK(decode(0x7C6419D6) == Op::mullw);
  CHECK(decode(0x38630003) == Op::addi);
  CHECK(decode(0x7C6307B4) == Op::extsw);
  CHECK(decode(0x5463103A) == Op::rlwinm);  // slwi r3,r3,2
  CHECK(decode(0x78630020) == Op::rldicl);  // clrldi r3,r3,32
  CHECK(decode(0x7C6300D0) == Op::neg);
  CHECK(decode(0x7C2007EC) == Op::dcbz);    // dcbz128 r0,r0
  CHECK(decode(0x10000000) == Op::vmx);
  CHECK(decode(0x7C6A1E70) == Op::srawi);
  CHECK(decode(0x7C641E74) == Op::sradi);
  CHECK(decode(0xFC20082A) == Op::fadd);
  CHECK(decode(0xEC21007A) == Op::fmadds);
  CHECK(decode(0xE8610010) == Op::ld);
  CHECK(decode(0xF8210001) == Op::stdu);
  CHECK(branch_target(0x4BFFFFFC, 0x82001004) == 0x82001000);  // b -4
  CHECK(branch_target(0x41820010, 0x82001000) == 0x82001010);  // beq +16
  CHECK(mask64(32, 63) == 0x00000000FFFFFFFFull);
  CHECK(mask64(0, 0) == 0x8000000000000000ull);
  CHECK(mask64(63, 0) == 0x8000000000000001ull);  // wrapping mask
  CHECK(mask64(48, 59) == 0x000000000000FFF0ull);
  // rldicl r3,r3,0,32 field decode: sh=0, mb=32
  CHECK(f_mb64(0x78630020) == 32);
  CHECK(f_sh64(0x7C641E74) == 3 + 0);
}

static void test_runtime_helpers() {
  PPCContext c{};
  CHECK(ppc_sraw(c, 0xFFFFFFF1u, 2) == uint64_t(int64_t(-4)) && c.xer_ca == 1);
  CHECK(ppc_sraw(c, 0x10u, 2) == 4 && c.xer_ca == 0);
  CHECK(add_carry(c, ~0ull, 1, 0) == 0 && c.xer_ca == 1);
  CHECK(ppc_slw(1, 32) == 0 && ppc_slw(1, 31) == 0x80000000u);
  CHECK(cntlz32(1) == 31 && cntlz32(0) == 32 && cntlz64(0) == 64);
  CHECK(divw_s(uint64_t(int64_t(-7)), 2) == uint32_t(-3));
  CHECK(divw_s(0x80000000u, uint64_t(int64_t(-1))) == 0);  // overflow defined as 0
  CHECK((fctiw_bits(-2.7, true) & 0xFFFFFFFF) == uint32_t(-2));
  CHECK((fctiw_bits(3e10, true) & 0xFFFFFFFF) == 0x7FFFFFFFu);
  set_cr_field_bits(c, 0x12345678u, 0xFF);
  CHECK(get_cr(c) == 0x12345678u);
}

static void test_xex(const std::string& dir) {
  std::vector<uint8_t> f;
  std::string err;
  // Plain XEX.
  CHECK(read_file(dir + "/hb64.xex", f, &err));
  XexModule x;
  CHECK(x.parse(f, err));
  CHECK(x.info().title_id == 0x46535231u);
  CHECK(x.info().load_address == 0x82000000u);
  CHECK(x.load_image(nullptr, err) == LoadStatus::Ok);
  CHECK(x.info().imports.size() == 2);
  bool saw_input = false;
  for (auto& lib : x.info().imports)
    for (auto& r : lib.records) saw_input |= r.name == "XamInputGetState";
  CHECK(saw_input);
  CHECK(!x.pdata().empty());
  std::vector<uint8_t> plain_image = x.image();

  // Encrypted + basic-compressed XEX built with the generated TEST key.
  std::vector<uint8_t> fe;
  CHECK(read_file(dir + "/hb64_enc.xex", fe, &err));
  XexModule xe;
  CHECK(xe.parse(fe, err));
  CHECK(xe.info().encryption == XexEncryption::Normal);
  CHECK(xe.info().compression == XexCompression::Basic);
  CHECK(xe.load_image(nullptr, err) == LoadStatus::NeedsKey);
  uint8_t wrong[16] = {1};
  XexModule xw;
  xw.parse(fe, err);
  CHECK(xw.load_image(wrong, err) == LoadStatus::KeyMismatch);
  uint8_t key[16];
  CHECK(load_key_file(dir + "/test.key", key, &err));
  CHECK(xe.load_image(key, err) == LoadStatus::Ok);
  CHECK(xe.image() == plain_image);

  // Corrupt inputs must fail cleanly.
  XexModule bad;
  std::vector<uint8_t> junk(f.begin(), f.begin() + 64);
  CHECK(!bad.parse(junk, err) || bad.load_image(nullptr, err) != LoadStatus::Ok);
  std::vector<uint8_t> notxex = {'M', 'Z', 0, 0, 0, 0};
  CHECK(!bad.parse(notxex, err));

  auto rep = analyze_xex(dir + "/hb64.xex", nullptr, true);
  CHECK(rep.parsed && rep.image_loaded && rep.deep);
  CHECK(rep.stats.instructions > 1000);
  CHECK(rep.imports_implemented == 2);
  CHECK(to_json(rep).find("\"title_id\":\"46535231\"") != std::string::npos);
}

static void test_iso(const std::string& dir, const std::string& tools) {
  std::string iso = dir + "/test.iso";
  std::string cmd = tools + "/mkiso " + iso + " default.xex=" + dir + "/hb64.xex media/readme.txt=" + dir + "/test.key >/dev/null";
  CHECK(std::system(cmd.c_str()) == 0);
  CHECK(sniff_container(iso) == ContainerKind::Xdvdfs);
  CHECK(sniff_container(dir + "/hb64.xex") == ContainerKind::Xex);
  XisoReader r;
  std::string err;
  CHECK(r.open(iso, err));
  const IsoEntry* e = r.find("DEFAULT.XEX");
  CHECK(e != nullptr);
  CHECK(r.find("media/readme.txt") != nullptr);
  std::vector<uint8_t> a, b;
  if (e) CHECK(r.read(*e, a));
  read_file(dir + "/hb64.xex", b);
  CHECK(a == b);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: unit_tests <homebrew-dir> <tools-dir>\n"); return 2; }
  test_aes();
  test_sha1();
  test_ppc_decode();
  test_runtime_helpers();
  test_xex(argv[1]);
  test_iso(argv[1], argv[2]);
  std::printf("unit_tests: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
