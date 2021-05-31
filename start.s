@@
@@ start.s -- Bootstrap entry point for Mycelia
@@
@@ Copyright 2014 Dale Schumacher, Tristan Slominski
@@
@@ Licensed under the Apache License, Version 2.0 (the "License");
@@ you may not use this file except in compliance with the License.
@@ You may obtain a copy of the License at
@@
@@ http://www.apache.org/licenses/LICENSE-2.0
@@
@@ Unless required by applicable law or agreed to in writing, software
@@ distributed under the License is distributed on an "AS IS" BASIS,
@@ WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@@ See the License for the specific language governing permissions and
@@ limitations under the License.
@@
@@ This code is loaded at 0x00008000 on the Raspberry Pi ARM processor
@@ and is the first code that runs to boot the O/S kernel.
@@
@@ View this file with hard tabs every 8 positions.
@@	|	|	.	|	.	.	.	.  max width ->
@@      |       |       .       |       .       .       .       .  max width ->
@@ If your tabs are set correctly, the lines above should be aligned.
@@

@ _start is the bootstrap entry point
	.text
	.align 2
	.global _start
_start:
	sub	r1, pc, #8	@ Where are we?
	mov	sp, r1		@ Bootstrap stack immediately before _start
	ldr	lr, =halt	@ Halt on "return"
	ldr	r0, =0x8000	@ Absolute address of kernel memory
	cmp	r0, r1		@ Are we loaded where we expect to be?
	beq	k_start		@ Then, jump to kernel entry-point
	mov	lr, r0		@ Otherwise, relocate ourselves
	ldr	r2, =0xFF00	@ Copy (64k - 256) bytes
1:	ldmia	r1!, {r3-r10}	@ Read 8 words
	stmia	r0!, {r3-r10}	@ Write 8 words
	subs	r2, #32		@ Decrement len
	bgt	1b		@ More to copy?
	bx	lr		@ Jump to bootstrap entry-point
	.global halt
halt:
	b	halt		@ Full stop

@@
@@ Provide a few assembly-language helpers used by C code, e.g.: raspberry.c
@@
	.text
	.align 2

	.globl NO_OP
NO_OP:			@ void NO_OP();
	bx	lr

	.globl PUT_32
PUT_32:			@ void PUT_32(u32 addr, u32 data);
	str	r1, [r0]
	bx	lr

	.globl GET_32
GET_32:			@ u32 GET_32(u32 addr);
	ldr	r0, [r0]
	bx	lr

	.globl BRANCH_TO
BRANCH_TO:		@ void BRANCH_TO(u32 addr);
	bx	r0

	.globl SPIN
SPIN:			@ void SPIN(u32 count);
	subs	r0, #1		@ decrement count
	bge	SPIN		@ until negative
	bx	lr
