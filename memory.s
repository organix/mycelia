@
@ memory.s -- Dynamic memory management and garbage collection
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

	.text
	.align 2		@ align to machine word
	.global reserve
reserve:		@ reserve a block (32 bytes) of memory
	ldr	r1, =block_next	@ address of free list pointer
	ldr	r0, [r1]	@ address of first free block
	mov	r3, #0		@ r3: null pointer

	cmp	r0, r3		@ check for null pointer
	beq	1f		@ if not null
	ldr	r2, [r0]	@	follow link to next free block
	str	r2, [r1]	@	update free list pointer
	str	r3, [r0]	@	set link to null

	ldr	r2, =block_free	@	address of free-block counter
	ldr	r1, [r2]	@	count of blocks in free-chain
	sub	r1, r1, #1	@	decrement free-block count
	str	r1, [r2]	@	update free-block counter

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

	.global release
release:		@ release the memory block pointed to by r0
	cmp	r0, sl		@ [FIXME] sanity check
	blt	panic		@ [FIXME] halt on bad address
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers

	ldr	r1, =block_next	@ address of free list pointer
	ldr	r2, [r1]	@ address of next free block
	str	r0, [r1]	@ update free list pointer

	ldr	r1, =block_junk	@ address of block-erase pattern
	ldmia	r1, {r3-r9}	@ read 7 words (32 - 4 bytes)
	stmia	r0, {r2-r9}	@ write 8 words (incl. next free block pointer)

	ldr	r2, =block_free	@ address of free-block counter
	ldr	r1, [r2]	@ count of blocks in free-chain
	add	r1, r1, #1	@ increment free-block count
	str	r1, [r2]	@ update free-block counter

	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global sync_gc
sync_gc:		@ synchronous garbage-collection
	@ r3=heap, r4=n_blocks, r5=mark, r6=scan_list, r7=scan_end, r8=i, r9=j
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers

	ldr	r2, =block_end	@ address of block end pointer
	ldr	r0, [r2]	@ get unused heap address
	ldr	r1, =0x3ff	@ 1k mask
	add	r0, r1		@ round up to 1k boundary
	bic	r0, r1		@ truncate to 1k boundary
	str	r0, [r2]	@ set unused heap address

	mov	r5, r0		@ r5: bit-string for marks
	ldr	r3, =heap_start	@ r3: base address of heap
	sub	r4, r0, r3	@ number of bytes in heap
	mov	r4, r4, ASR #5	@ r4: number of blocks in heap
	add	r6,r5,r4,ASR #3	@ r6: beginning of scan_list
	mov	r7, r6		@ r7: end of scan_list

	mov	r0, #0		@ clear all marks
	mov	r1, r5		@ base of mark bit-string
	movs	r2, r4, ASR #5	@ number of words in mark bit-string
1:				@ while (word count > 0) {
	strgt	r0, [r1], #4	@	clear 32 marks and increment pointer
	subgts	r2, r2, #1	@	decrement word count
	bgt	1b		@ }

	ldr	r8, =block_next	@ mark all free blocks, but don't scan them
	b	3f		@ for each free-list entry {
2:
	sub	r1, r8, r3	@	byte offset of block
	mov	r1, r1, ASR #5	@	r1: block number
	and	r2, r1, #0x1f	@	extract bit number
	mov	r1, r1, ASR #5	@	r1: mark word number
	mov	r0, #1		@	set bit
	mov	r2, r0, LSL r2	@	r2: mask for mark bit
	ldr	r0,[r5,r1,LSL #2]@	load mark word
	orr	r0, r0, r2	@	set mark bit
	str	r0,[r5,r1,LSL #2]@	store mark word
3:
	ldr	r8, [r8]	@	get next block in free-list
	cmp	r8, #0
	bne	2b		@ }

	mov	r0, r3		@ root_block is allocated at heap_start
	bl	scan_gc		@ mark and scan root_block

	ldr	r1, [sl, #1024]	@ event queue head/tail indicies
	uxtb	r8, r1, ROR #8	@ r8: head index
	uxtb	r9, r1, ROR #16	@ r9: tail index
	b	8f		@ for each event in queue {
7:
	ldr	r0,[sl,r8,LSL #2]@	next event block
	bl	scan_gc		@	mark and scan event
	add	r8, r8, #1	@	increment head
	and	r8, r8, #0xff	@	wrap index
8:
	cmp	r8, r9
	bne	7b		@ }

	b	6f		@ for each block in scan_list {
4:
	ldr	r8, [r6], #4	@	r8: next block from scan_list
	mov	r9, #0x1c	@	for each block field {
5:
	ldr	r0, [r8, r9]	@		get block field
	bl	scan_gc		@		mark and scan field
	subs	r9, r9, #4	@		decrement field offset
	bge	5b		@	}
6:
	cmp	r6, r7
	blt	4b		@ }

	mov	r6, r3		@ r6: current heap block (`release` uses r3)
9:
	tst	r6, #0x3e0	@ on each 1k boundary
	ldreq	r7, [r5], #4	@	load next mark word
	tst	r7, #1		@ if LSB of mark word is zero
	moveq	r0, r6		@	the current block is not reachable
	bleq	release		@	so, release it
	mov	r7, r7, LSR #1	@ shift to next bit in mark word
	add	r6, r6, #0x20	@ advance to next heap block
	subs	r4, #1		@ decrement block count
	bgt	9b		@ until there are no more blocks

	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
scan_gc:		@ mark block (r0) in-use and include in scan_list
	@ r3=heap, r4=n_blocks, r5=mark, r6=scan_list, r7=scan_end, r8=i, r9=j
	tst	r0, #0x1F	@ check block alignment
	bxne	lr		@ return, if not aligned

	str	r0, [r7]	@ save block in scan list
	subs	r1, r0, r3	@ byte offset of block
	bxlt	lr		@ return, if block < heap
	mov	r1, r1, ASR #5	@ r1: block number
	cmp	r1, r4		@ compare with n_blocks
	bxge	lr		@ return, if block number >= n_blocks

	and	r2, r1, #0x1F	@ extract bit number
	mov	r1, r1, ASR #5	@ r1: mark word number
	mov	r0, #1		@ set bit
	mov	r2, r0, LSL r2	@ r2: mask for mark bit
	ldr	r0,[r5,r1,LSL #2]@ load mark word
	tst	r0, r2		@ check for mark bit
	bxne	lr		@ return, if already marked

	orr	r0, r0, r2	@ set mark bit
	str	r0,[r5,r1,LSL #2]@ store mark word
	add	r7, r7, #4	@ increment scan_end (block already saved)
	bx	lr		@ return

	.text
	.align 2		@ align to machine word
	.global report_gc
report_gc:		@ report garbage-collection metrics
	ldr	r2, =block_end	@ address of block end pointer
	ldr	r0, [r2]	@ get unused heap address
	ldr	r1, =heap_start	@ base address of heap
	sub	r0, r0, r1	@ r0: number of bytes in heap
	ldr	r2, =block_free	@ address of free-block counter
	ldr	r1, [r2]	@ count of blocks in free-chain
	mov	r1, r1, LSL #5	@ r1: number of bytes in free-chain
	b	report_mem	@ tail call

	.section .rodata
	.align 5		@ align to cache-line
block_junk:
	.ascii "Who is licking my HONEYPOT?\0"
	.align 5		@ align to cache-line
	.global block_zero
block_zero:
	.int 0, 0, 0, 0, 0, 0, 0, 0

	.data
	.align 5		@ align to cache-line
block_next:
	.int 0			@ pointer to next free block, 0 if none
block_end:
	.int heap_start		@ pointer to end of block memory
block_free:
	.int 0			@ number of blocks in the free-chain

	.section .heap
	.align 10		@ align to 1k boundary
heap_start:

