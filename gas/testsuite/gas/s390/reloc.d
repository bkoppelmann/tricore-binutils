#name: s390 reloc
#objdump: -dr

.*: +file format .*

Disassembly of section .text:

.* <foo>:
   0:	d2 00 10 00 20 00 [ 	]*mvc	0\(1,%r1\),0\(%r2\)
[ 	]*1: R_390_8	test_R_390_8
   6:	58 01 20 00 [ 	]*l	%r0,0\(%r1,%r2\)
[ 	]*8: R_390_12	test_R_390_12
   a:	a7 08 00 00 [ 	]*lhi	%r0,0
[ 	]*c: R_390_16	test_R_390_16
[ 	]*...
[ 	]*e: R_390_32	test_R_390_32
[ 	]*12: R_390_PC32	test_R_390_PC32\+0x12
  16:	58 01 20 00 [ 	]*l	%r0,0\(%r1,%r2\)
[ 	]*18: R_390_GOT12	test_R_390_GOT12
[ 	]*...
[ 	]*1a: R_390_GOT32	test_R_390_GOT32
[ 	]*1e: R_390_PLT32	test_R_390_PLT32
  22:	a7 08 00 00 [ 	]*lhi	%r0,0
[ 	]*24: R_390_GOT16	test_R_390_GOT16
  26:	a7 08 00 00 [ 	]*lhi	%r0,0
[ 	]*28: R_390_16	test_R_390_PC16\+0x26
  2a:	a7 e5 00 00 [ 	]*bras	%r14,2a <foo\+0x2a>
[ 	]*2c: R_390_PC16DBL	test_R_390_PC16DBL\+0x2
  2e:	a7 e5 00 00 [ 	]*bras	%r14,2e <foo\+0x2e>
[ 	]*30: R_390_PC16DBL	test_R_390_PLT16DBL\+0x2
  32:	07 07 [ 	]*bcr	0,%r7
