/* Kernel import thunks for the homebrew XEX. mkxex rewrites the first word
 * of each thunk with the XEX import record (0x01000000 | ordinal); the
 * recompiler turns each thunk into a call to the runtime's HLE dispatcher. */
  .section .text.thunks,"ax",@progbits
  .balign 16

  .globl XamInputGetState
  .type XamInputGetState,@function
XamInputGetState:
  .long 0, 0, 0, 0
  .size XamInputGetState, 16

  .globl DbgPrint
  .type DbgPrint,@function
DbgPrint:
  .long 0, 0, 0, 0
  .size DbgPrint, 16
