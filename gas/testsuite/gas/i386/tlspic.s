	.section ".tdata", "awT", @progbits
	.globl foo
foo:	.long 25
	.text
	.globl	fn
	.type	fn,@function
fn:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	pushl	%eax
	call	1f
1:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-1b], %ebx

	/* foo can be anywhere in the startup TLS  */
	movl	%gs:0, %eax

	/* Arbitrary instructions in between  */
	leal	0(%esi, 1), %esi

	subl	foo@GOTTPOFF(%ebx), %eax
	/* %eax now contains &foo  */

	movl    -4(%ebp), %ebx
	leave
	ret
