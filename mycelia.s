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
@   sl (r10) the sponsor providing resources for this computation
@   fp (r11) the event being processed, including the message and target actor
@   ip (r12) the base address of the target actor


	.text
	.align 2		@ alignment 2^n (2^2 = 4 byte machine word)
	.global mycelia
mycelia:		@ entry point for the actor kernel
@	bl	monitor		@ monitor();

@ inject initial event
	ldr	sl, =sponsor_0	@ initialize sponsor link
	bl	reserve		@ allocate event block
	ldr	r1, =a_poll	@ get target actor
	str	r1, [r0]	@ set target actor
	bl	enqueue		@ add event to queue
	b	dispatch	@ start dispatch loop

	.text
	.align 2		@ align to machine word
panic:			@ kernel panic!
	ldr	r0, =panic_txt	@ load address of panic text
	bl	serial_puts	@ write text to console
	b	halt
	.section .rodata
panic_txt:
	.ascii "\nPANIC!\0"

	.text
	.align 2		@ align to machine word
complete:		@ completion of event pointed to by fp
	mov	r0, fp		@ get completed event
	bl	release		@ free completed event
	mov	fp, #0		@ clear frame pointer
	str	fp, [sl, #1028]	@ clear current event
dispatch:		@ dispatch next event
	bl	dequeue		@ try to get next event
	cmp	r0, #0		@ check for null
	beq	dispatch	@ if no event, try again...
	mov	fp, r0		@ initialize frame pointer
	str	fp, [sl, #1028]	@ update current event
	ldr	ip, [fp]	@ get target actor address
	bx	ip		@ jump to actor behavior

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
	stmdb	sp!, {lr}	@	preserve link register
	ldr	r1, =block_end	@	address of block end pointer
	ldr	r0, [r1]	@	address of new memory block
	add	r2, r0, #32	@	calculate next block address
	str	r2, [r1]	@	update block end pointer
	bl	release		@	"free" new memory block
	ldmia	sp!, {lr}	@	restore link register
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
	beq	panic		@	kernel panic!
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
	.int 0			@ current event (offset 1028)

	.text
	.align 5		@ align to cache-line
_a_answer:		@ send answer to customer and return (r0=event, r1=answer)
	str	r1, [r0, #0x04]	@ answer
_a_reply:		@ reply to customer and return from actor (r0=event)
	ldr	r1, [fp, #0x04]	@ customer
_a_send:		@ send a message and return from actor (r0=event, r1=target)
	str	r1, [r0]	@ target actor
_a_end:			@ queue message and return from actor (r0=event)
	bl	enqueue		@ add event to queue
	b	complete	@ return to dispatcher

	.text
	.align 5		@ align to cache-line
a_poll:			@ poll for serial i/o ()
	bl	reserve		@ allocate event block
	add	r1, pc, #8	@ event template address
	ldmia	r1, {r1-r3}	@ read (target, ok, fail)
	stmia	r0, {r1-r3}	@ write (target, ok, fail)
	b	_a_end		@ queue message
	.int	a_in_ready	@ target actor
	.int	a_do_in		@ ok customer
	.int	a_poll		@ fail customer

	.text
	.align 5		@ align to cache-line
a_in_ready:		@ check for serial input (ok, fail)
	bl	reserve		@ allocate event block
@	ldr	r2, =0x20201000	@ UART0
	ldr	r2, [pc, #16]	@ UART0
	ldr	r1, [r2, #0x18]	@ UART0->FR
	tst	r1, #0x10	@ FR.RXFE
	ldreq	r1, [fp, #0x04]	@ if ready, notify ok customer
	ldrne	r1, [fp, #0x08]	@ otherwise, notify fail customer
	b	_a_send		@ send message
	.int	0x20201000	@ UART0 base address

	.text
	.align 5		@ align to cache-line
a_do_in:		@ request input ()
	bl	reserve		@ allocate event block
	ldr	r1, [pc, #8]	@ target actor
	ldr	r2, [pc, #8]	@ customer
	stmia	r0, {r1,r2}	@ write (target, cust)
	b	_a_end		@ queue message
	.int	a_char_in	@ target actor
	.int	a_do_out	@ customer

	.text
	.align 5		@ align to cache-line
a_char_in:		@ read serial input (cust)
	bl	reserve		@ allocate event block
@	ldr	r2, =0x20201000	@ UART0
	ldr	r2, [pc, #4]	@ UART0
	ldr	r1, [r2]	@ UART0->DR
	b	_a_answer	@ answer and return
	.int	0x20201000	@ UART0 base address

	.text
	.align 5		@ align to cache-line
a_do_out:		@ request output (char)
	bl	reserve		@ allocate event block
	ldr	r1, [pc, #12]	@ get target
	ldr	r2, [pc, #12]	@ get customer
	ldr	r3, [fp, #0x04]	@ get character
	stmia	r0, {r1-r3}	@ write (target, cust, char)
	b	_a_end		@ queue message
	.int	a_char_out	@ target actor
	.int	a_poll		@ customer

	.text
	.align 5		@ align to cache-line
a_char_out:		@ write serial output (cust, char)
	bl	reserve		@ allocate event block
	ldr	r1, [fp, #0x08]	@ character
@	ldr	r2, =0x20201000	@ UART0
	ldr	r2, [pc, #4]	@ UART0
	str	r1, [r2]	@ UART0->DR
	b	_a_reply	@ reply and return
	.int	0x20201000	@ UART0 base address

	.text
	.align 5		@ align to cache-line
example_0:
	bl	reserve		@ allocate event block
	ldr	r1, [ip, #0x1c] @ get answer
	str	r1, [r0, #0x04]	@ set answer
	ldr	r1, [fp, #0x04]	@ get customer
	str	r1, [r0]	@ set target actor
	bl	enqueue		@ add event to queue
	b	complete	@ return to dispatcher
	.int	0x42424242	@ answer data

	.text
	.align 5		@ align to cache-line
example_1:
	ldr	pc, [pc, -#4]	@ jump to actor behavior
	.int	complete	@ address of actor behavior
	.int	0x00000000	@ state field 0
	.int	0x11111111	@ state field 1
	.int	0x22222222	@ state field 2
	.int	0x33333333	@ state field 3
	.int	0x44444444	@ state field 4
	.int	0x55555555	@ state field 5

	.text
	.align 5		@ align to cache-line
example_2:
	ldr	lr, [pc]	@ get actor behavior address
	blx	lr		@ jump to behavior, lr points to state
	.int	complete	@ address of actor behavior
	.int	0x00000000	@ state field 0
	.int	0x11111111	@ state field 1
	.int	0x22222222	@ state field 2
	.int	0x33333333	@ state field 3
	.int	0x44444444	@ state field 4

	.text
	.align 5		@ align to cache-line
example_3:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip,{r4-r7,lr,pc} @ copy state and jump to behavior
	.int	0x04040404	@ value for r4
	.int	0x05050505	@ value for r5
	.int	0x06060606	@ value for r6
	.int	0x07070707	@ value for r7
	.int	0x14141414	@ value for r14 (lr)
	.int	complete	@ address of actor behavior

	.section .heap
	.align 5		@ align to cache-line
heap_start:
