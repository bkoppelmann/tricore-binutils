#objdump: -dr
#name: i386 non-pic tls

.*: +file format .*

Disassembly of section .text:

0+000 <fn>:
   0:	55 [ 	]*push   %ebp
   1:	89 e5 [ 	]*mov    %esp,%ebp
   3:	53 [ 	]*push   %ebx
   4:	50 [ 	]*push   %eax
   5:	e8 00 00 00 00 [ 	]*call   a <fn\+0xa>
   a:	5b [ 	]*pop    %ebx
   b:	81 c3 03 00 00 00 [ 	]*add    \$0x3,%ebx
[ 	]+d: R_386_GOTPC	_GLOBAL_OFFSET_TABLE_
  11:	65 a1 00 00 00 00 [ 	]*mov    %gs:0x0,%eax
  17:	8d 76 00 [ 	]*lea    0x0\(%esi\),%esi
  1a:	2b 83 00 00 00 00 [ 	]*sub    0x0\(%ebx\),%eax
[ 	]+1c: R_386_TLS_IE_32	foo
  20:	8b 5d fc [ 	]*mov    0xfffffffc\(%ebp\),%ebx
  23:	c9 [ 	]*leave[ 	]*
  24:	c3 [ 	]*ret[ 	]*
