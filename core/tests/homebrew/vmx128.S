/* VMX128 (Xbox 360 extended vector) encoding test, written as raw words
 * because no public assembler knows VMX128. Base opcodes and field layout
 * follow Xenia's decoder tables (ppc_opcode_table_gen.cc / ppc_decode_data.h).
 *
 *   uint32_t fsr_vmx128_test(const void* in  r3, 5 x 16 bytes, 16-aligned,
 *                             void* out       r4, 17 x 16 bytes, 16-aligned)
 * The harness recomputes every result with plain scalar code.
 */
  .macro VX128 base, vd, va, vb
  .long \base | ((\vd & 31) << 21) | (((\vd >> 5) & 3) << 2) | ((\va & 31) << 16) | (((\va >> 5) & 1) << 5) | (((\va >> 6) & 1) << 10) | ((\vb & 31) << 11) | ((\vb >> 5) & 3)
  .endm
  .macro VX128_1 base, vd, ra, rb   /* loads / stores */
  .long \base | ((\vd & 31) << 21) | (((\vd >> 5) & 3) << 2) | (\ra << 16) | (\rb << 11)
  .endm
  .macro VX128_2 base, vd, va, vb, vc
  VX128 (\base | (\vc << 6)), \vd, \va, \vb
  .endm
  .macro VX128_3 base, vd, vb, imm
  .long \base | ((\vd & 31) << 21) | (((\vd >> 5) & 3) << 2) | ((\imm & 31) << 16) | ((\vb & 31) << 11) | ((\vb >> 5) & 3)
  .endm
  .macro VX128_4 base, vd, vb, imm, z
  .long \base | ((\vd & 31) << 21) | (((\vd >> 5) & 3) << 2) | ((\imm & 31) << 16) | (\z << 6) | ((\vb & 31) << 11) | ((\vb >> 5) & 3)
  .endm
  .macro VX128_5 base, vd, va, vb, sh
  VX128 (\base | (\sh << 6)), \vd, \va, \vb
  .endm
  .macro VX128_P base, vd, vb, perm
  .long \base | ((\vd & 31) << 21) | (((\vd >> 5) & 3) << 2) | ((\perm & 31) << 16) | (((\perm >> 5) & 7) << 6) | ((\vb & 31) << 11) | ((\vb >> 5) & 3)
  .endm

  .macro LVX128 vd, ra, rb
  VX128_1 0x100000c3, \vd, \ra, \rb
  .endm
  .macro STV vd, slot   /* stvx128 vd -> out + 16*slot */
  addi 9, 4, (16 * \slot)
  VX128_1 0x100001c3, \vd, 0, 9
  .endm

  .text
  .balign 16
  .globl fsr_vmx128_test
  .type fsr_vmx128_test,@function
fsr_vmx128_test:
  li 5, 16
  li 6, 32
  li 7, 48
  li 8, 64
  LVX128 70, 0, 3          /* v70  = in[0] (floats) */
  LVX128 101, 3, 5         /* v101 = in[1] (floats) */
  LVX128 33, 3, 6          /* v33  = in[2] (floats) */
  LVX128 127, 3, 7         /* v127 = in[3] (ints)   */
  lvx 5, 3, 8              /* v5   = in[4] permute control (standard lvx) */

  VX128 0x14000010, 64, 70, 101      /* vaddfp128    v64 = v70 + v101 */
  STV 64, 0
  VX128 0x14000090, 65, 70, 33       /* vmulfp128    v65 = v70 * v33 */
  STV 65, 1
  VX128 0x140002d0, 66, 33, 33       /* vor128       v66 = v33 */
  VX128 0x140000d0, 66, 70, 101      /* vmaddfp128   v66 = v70*v101 + v66 */
  STV 66, 2
  VX128 0x14000190, 67, 70, 101      /* vmsum3fp128 */
  STV 67, 3
  VX128 0x140001d0, 68, 33, 101      /* vmsum4fp128 */
  STV 68, 4
  VX128_P 0x18000210, 69, 70, 0xE4   /* vpermwi128   reverse order? 0xE4 = 3,2,1,0 */
  STV 69, 5
  VX128 0x140002d0, 71, 33, 33       /* vor128       v71 = v33 */
  VX128_4 0x18000710, 71, 70, 10, 1  /* vrlimi128    imm=1010b (x,z), rotate 1 */
  STV 71, 6
  VX128_3 0x18000730, 72, 101, 2     /* vspltw128    v72 = splat(v101.z) */
  STV 72, 7
  VX128 0x18000140, 73, 70, 101      /* vcmpgtfp128. (Rc=1 -> bit 6) v73 = v70 > v101 */
  mfcr 10
  STV 73, 8
  VX128 0x140002d0, 74, 73, 73       /* vor128       v74 = mask */
  VX128 0x14000350, 74, 70, 101      /* vsel128      v74 = (v70 & ~v74) | (v101 & v74) */
  STV 74, 9
  VX128_2 0x14000000, 75, 70, 101, 5 /* vperm128     v75 = perm(v70, v101, v5) */
  STV 75, 10
  VX128_5 0x10000010, 76, 70, 101, 6 /* vsldoi128    shift 6 */
  STV 76, 11
  VX128_3 0x180002b0, 77, 127, 4     /* vcsxwfp128   v77 = float(int)/16 */
  STV 77, 12
  VX128_3 0x18000230, 78, 70, 2      /* vcfpsxws128  v78 = sat_int(v70 * 4) */
  STV 78, 13
  VX128 0x18000380, 79, 0, 127       /* vupkhsb128   (VA=0) */
  STV 79, 14
  VX128_3 0x18000770, 80, 0, 29      /* vspltisw128  imm=-3 */
  STV 80, 15
  stw 10, 256(4)                     /* CR after the record compare */
  mr 3, 10
  blr
  .size fsr_vmx128_test, .-fsr_vmx128_test
