@
@ mycelia.s -- A bare-metal actor kernel for Raspberry Pi
@
@ Copyright 2014 Dale Schumacher, Tristan Slominski
@
@ Licensed under the Apache License, Version 2.0 (the "License");
@ you may not use this file except in compliance with the License.
@ You may obtain a copy of the License at
@
@ http://www.apache.org/licenses/LICENSE-2.0
@
@ Unless required by applicable law or agreed to in writing, software
@ distributed under the License is distributed on an "AS IS" BASIS,
@ WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@ See the License for the specific language governing permissions and
@ limitations under the License.
@
@ View this file with hard tabs every 8 positions.
@	|	|	.	|	.	.	.	.  max width ->
@       |       |       .       |       .       .       .       .  max width ->
@ If your tabs are set correctly, the lines above should be aligned.
@

@ Special register usage:
@   fp (r11) the event being processed, including the message and target actor
@   sl (r10) the sponsor providing resources for this computation

@ Event structure:
@	+-------+-------+-------+-------+
@ 0x00	| address of target actor	|
@	+-------------------------------+
@ 0x04	| message contents		|
@	+	.	.	.	+
@ 0x08	|				|
@	+	.	.	.	+
@ 0x0c	|				|
@	+	.	.	.	+
@ 0x10	|				|
@	+	.	.	.	+
@ 0x14	|				|
@	+	.	.	.	+
@ 0x18	|				|
@	+	.	.	.	+
@ 0x1c	|				|
@	+-------+-------+-------+-------+

@ Actor structure(s):
@	+-------+-------+-------+-------+
@ 0x00	| machine instructions		|
@	+	.	.	.	+
@ 0x04	|				|
@	+	.	.	.	+
@ 0x08	|				|
@	+	.	.	.	+
@ 0x0c	|				|
@	+	.	.	.	+
@ 0x10	|				|
@	+	.	.	.	+
@ 0x14	|				|
@	+	.	.	.	+
@ 0x18	|				|
@	+-------------------------------+
@ 0x1c	|	b	commit		|
@	+-------+-------+-------+-------+
@
@	+-------+-------+-------+-------+
@ 0x00	|	ldr	pc, [pc, -#4]	|
@	+-------------------------------+
@ 0x04	| address of actor behavior	|
@	+-------------------------------+
@ 0x08	| actor state			|
@	+	.	.	.	+
@ 0x0c	|				|
@	+	.	.	.	+
@ 0x10	|				|
@	+	.	.	.	+
@ 0x14	|				|
@	+	.	.	.	+
@ 0x18	|				|
@	+	.	.	.	+
@ 0x1c	|				|
@	+-------+-------+-------+-------+
@
@	+-------+-------+-------+-------+
@ 0x00	|	ldr	lr, [pc]	|
@	+-------------------------------+
@ 0x04	|	blx	lr		|
@	+-------------------------------+
@ 0x08	| address of actor behavior	|
@	+-------------------------------+
@ 0x0c	| actor state			|
@	+	.	.	.	+
@ 0x10	|				|
@	+	.	.	.	+
@ 0x14	|				|
@	+	.	.	.	+
@ 0x18	|				|
@	+	.	.	.	+
@ 0x1c	|				|
@	+-------+-------+-------+-------+
@
@	+-------+-------+-------+-------+
@ 0x00	|	ldmia	pc,{r0-r3,ip,pc}|
@	+-------------------------------+
@ 0x04	| *UNUSED* (skipped)		|
@	+-------------------------------+
@ 0x08	| value for r0			|
@	+-------------------------------+
@ 0x0c	| value for r1			|
@	+-------------------------------+
@ 0x10	| value for r2			|
@	+-------------------------------+
@ 0x14	| value for r3			|
@	+-------------------------------+
@ 0x18	| value for ip			|
@	+-------------------------------+
@ 0x1c	| value for pc			|
@	+-------+-------+-------+-------+

	.text
	.align 2		@ alignment 2^n (2^2 = 4 byte machine word)
	.global mycelia
mycelia:		@ entry point for the actor kernel
	bl	monitor		@ monitor();
@ inject initial event
	bl	reserve		@ allocate event block
	ldr	r1, [pc]	@ get target actor
	b	_a_send		@ send message
	.int	a_poll		@ target actor

	.text
	.align 2		@ align to machine word
commit:			@ commit effects of actor behavior
	bl	dequeue		@ try to get next event
	cmp	r0, #0		@ check for null
	beq	commit		@ if no event, try again
	ldr	lr, [r0]	@ get target actor address
	bx	lr		@ jump to actor behavior

	.text
	.align 2		@ align to machine word
reserve:		@ reserve a block (32 bytes) of memory
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r0, [r1]	@ address of first free block
	cmp	r0, #0
	beq	1f		@ if not null
	ldr	r2, [r0]	@	follow link to next free block
	str	r2, [r1]	@	update free list pointer
	bx	lr		@	return
1:				@ else
	ldr	r1, =block_end	@	address of block end pointer
	ldr	r0, [r1]	@	address of new memory block
	bl	release		@	"free" new memory block
	b	reserve		@	try again

release:		@ release the memory block pointed to by r0
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r2, [r1]	@ address of next free block
	str	r0, [r1]	@ update free list pointer
	ldr	r1, =block_clr	@ address of block-erase pattern
	ldmia	r1, {r3-r9}	@ read 7 words (32 - 4 bytes)
	stmia	r0, {r2-r9}	@ write 8 words (incl. next free block pointer)
	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.section .rodata
	.align 5		@ align to cache-line
block_clr:
	.ascii "Who is licking my HONEYPOT?\0"

	.data
	.align 2		@ align to machine word
block_free:
	.int 0			@ pointer to next free block, 0 if none
block_end:
	.int heap_start		@ pointer to end of block memory

	.text
	.align 2		@ align to machine word
enqueue:		@ enqueue event pointed to by r0
	ldr	r1, [sl, #1024]	@ event queue head/tail indicies
	uxtb	r2, r1, ROR #8	@ get head index
	uxtb	r3, r1, ROR #16	@ get tail index
	str	r0, [sl,r3,LSL #2] @ store event pointer at tail
	add	r3, r3, #1	@ advance tail
	cmp	r2, r3		@ if queue full
	beq	halt		@	KERNEL PANIC!
	strb	r3, [sl, #1026]	@ update tail index
	bx	lr		@ return

dequeue:		@ dequeue next event from queue
	ldr	r1, [sl, #1024]	@ event queue head/tail indicies
	uxtb	r2, r1, ROR #8	@ get head index
	uxtb	r3, r1, ROR #16	@ get tail index
	cmp	r2, r3		@ if queue empty
	moveq	r0, #0		@	return null
	ldrne	r0, [sl,r2,LSL #2] @ else
	addne	r2, r2, #1	@	advance head
	strneb	r2, [sl, #1025]	@	update head index
	bx	lr		@	return event pointer

	.data
	.align 5		@ align to cache-line
sponsor_0:
	.space 256*4		@ event queue (offset 0)
	.int 0			@ queue head/tail (offset 1024)

	.text
	.align 5		@ align to cache-line
_a_reply:		@ reply to customer and return from actor (r0=event)
	ldr	r1, [fp, #4]	@ customer
_a_send:		@ send a message and return from actor (r0=event, r1=target)
	str	r1, [r0]	@ target actor
	bl	enqueue		@ add event to queue
	b	commit		@ return

	.text
	.align 5		@ align to cache-line
a_poll:			@ poll for serial i/o ()
	bl	reserve		@ allocate event block
	ldmia	pc, {r1-r3}	@ read (target, ok, fail)
	b	1f
	.int	a_in_ready	@ target actor
	.int	a_do_in		@ ok customer
	.int	a_poll		@ fail customer
	.align 5		@ align to cache-line
1:			@ a_poll continued...
	stmia	r0, {r1-r3}	@ write (target, ok, fail)
	bl	enqueue		@ add event to queue
	b	commit		@ return

	.text
	.align 5		@ align to cache-line
a_in_ready:		@ check for serial input (ok, fail)
	bl	reserve		@ allocate event block
@	ldr	r1, =0x20201000	@ UART0
	ldr	r1, [pc, #16]	@ UART0
	ldr	r2, [r1, #0x18]	@ UART0->FR
	tst	r2, #0x10	@ FR.RXFE
	ldrne	r1, [fp, #4]	@ if ready, notify ok customer
	ldreq	r1, [fp, #8]	@ otherwise, notify fail customer
	b	_a_send		@ send message
	.int	0x20201000	@ UART0 base address

	.text
	.align 5		@ align to cache-line
a_do_in:		@ request input ()
	bl	reserve		@ allocate event block
	ldr	r1, [pc, #8]	@ target actor
	ldr	r2, [pc, #8]	@ customer
	bl	enqueue		@ add event to queue
	b	commit		@ return
	.int	a_char_in	@ target actor
	.int	a_do_out	@ customer

	.text
	.align 5		@ align to cache-line
a_char_in:		@ read serial input (cust)
	bl	reserve		@ allocate event block
@	ldr	r1, =0x20201000	@ UART0
	ldr	r1, [pc, #8]	@ UART0
	ldr	r2, [r1]	@ UART0->DR
	str	r2, [r0, #4]	@ UART data
	b	_a_reply	@ reply and return
	.int	0x20201000	@ UART0 base address

	.text
	.align 5		@ align to cache-line
a_do_out:		@ request output (char)
	bl	reserve		@ allocate event block
	ldmia	pc, {r1,r2}	@ read (target, cust)
	b	1f
	.int	a_char_out	@ target actor
	.int	a_poll		@ customer
	.align 5		@ align to cache-line
1:			@ a_do_out continued...
	ldr	r3, [fp, #4]	@ character
	stmia	r0, {r1-r3}	@ write (target, cust, char)
	bl	enqueue		@ add event to queue
	b	commit		@ return

	.text
	.align 5		@ align to cache-line
a_char_out:		@ write serial output (cust, char)
	bl	reserve		@ allocate event block
	ldr	r2, [fp, #8]	@ character
@	ldr	r1, =0x20201000	@ UART0
	ldr	r1, [pc, #4]	@ UART0
	str	r2, [r1]	@ UART0->DR
	b	_a_reply	@ reply and return
	.int	0x20201000	@ UART0 base address

	.section .heap
	.align 5		@ align to cache-line
heap_start:
