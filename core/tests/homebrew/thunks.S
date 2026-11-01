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

  .globl NtOpenFile
  .type NtOpenFile,@function
NtOpenFile:
  .long 0, 0, 0, 0
  .size NtOpenFile, 16

  .globl NtReadFile
  .type NtReadFile,@function
NtReadFile:
  .long 0, 0, 0, 0
  .size NtReadFile, 16

  .globl NtQueryInformationFile
  .type NtQueryInformationFile,@function
NtQueryInformationFile:
  .long 0, 0, 0, 0
  .size NtQueryInformationFile, 16

  .globl NtSetInformationFile
  .type NtSetInformationFile,@function
NtSetInformationFile:
  .long 0, 0, 0, 0
  .size NtSetInformationFile, 16

  .globl NtQueryFullAttributesFile
  .type NtQueryFullAttributesFile,@function
NtQueryFullAttributesFile:
  .long 0, 0, 0, 0
  .size NtQueryFullAttributesFile, 16

  .globl NtClose
  .type NtClose,@function
NtClose:
  .long 0, 0, 0, 0
  .size NtClose, 16
