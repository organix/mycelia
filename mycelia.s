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

@ mycelia is the entry point for the actor kernel
	.text
	.align 2		@ alignment 2^n (2^2 = 4 byte machine word)
	.global mycelia
mycelia:
	bl	monitor		@ monitor();
	b	mycelia

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

	.section .heap
	.align 5		@ align to cache-line
heap_start:
