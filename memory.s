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
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r0, [r1]	@ address of first free block
	cmp	r0, #0		@ check for null pointer
	beq	1f		@ if not null
	ldr	r2, [r0]	@	follow link to next free block
	str	r2, [r1]	@	update free list pointer
	str	r3, [r0]	@	set link to null
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
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r2, [r1]	@ address of next free block
	str	r0, [r1]	@ update free list pointer
	ldr	r1, =block_zero	@ address of block-erase pattern
	ldmia	r1, {r3-r9}	@ read 7 words (32 - 4 bytes)
	stmia	r0, {r2-r9}	@ write 8 words (incl. next free block pointer)
	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global sync_gc
sync_gc:		@ synchronous garbage-collection
	@ r4=n_blocks, r5=mark, r6=scan_list, r7=scan_end, r8=i, r9=j
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers

	ldr	r2, =block_end	@ address of block end pointer
	ldr	r0, [r2]	@ get unused heap address
	ldr	r1, =0x3ff	@ 1k mask
	add	r0, r1		@ round up to 1k boundary
	bic	r0, r1		@ truncate to 1k boundary
	str	r0, [r2]	@ set unused heap address

	ldr	r1, =heap_start	@ base address of heap
	sub	r4, r0, r1	@ number of bytes in heap
	mov	r4, r4, ASR #5	@ r4: number of blocks in heap
	mov	r5, r0		@ r5: bit-string for marks
	add	r6,r5,r4,ASR #5	@ r6: beginning of scan_list
	mov	r7, r6		@ r7: end of scan_list

	mov	r0, #0		@ clear all marks
	mov	r1, r5		@ base of mark bit-string
	movs	r2, r4, ASR #5	@ number of words in mark bit-string
1:				@ while (word count > 0) {
	strgt	r0, [r1], #4	@	clear 32 marks and increment pointer
	subgts	r2, r2, #-1	@	decrement word count
	bgt	1b		@ }

	ldr	r0, =block_free	@ address of free list pointer
	ldr	lr, [r0]	@ address of first free block
	ldr	r3, =heap_start	@ cache base address of heap
2:
	cmp	lr, #0		@ while block not null
	beq	3f		@ {
	sub	r1, lr, r3	@	byte offset of block
	mov	r1, r1, ASR #5	@	block number
	and	r2, r1, #0x1F	@	extract bit number
	mov	r0, #1		@	set bit
	mov	r2, r0, LSL r2	@	move bit into place
	ldr	r0,[r5,r1,ASR #5]@	load mark word
	orr	r0, r0, r2	@	set mark bit
	str	r0,[r5,r1,ASR #5]@	store mark word
	ldr	lr, [lr]	@	block = block->next
	b	2b		@ }
3:
	str	r3, [r7], #4	@ add root block to scan list
	mov	r2, #1		@ bit number zero
	ldr	r0,[r5]		@ load mark word
	orr	r0, r0, r2	@ set mark bit
	str	r0,[r5]		@ store mark word

	cmp	r6, r7		@ while scan list not empty
	bge	7f		@ {
4:
	ldr	r9, [r6], #4	@	b = *scan_list++
	mov	r8, #0x18	@	n = 6
5:
	ldr	r0, [r9, r8]	@	r = b[n]

	subs	r1, r0, r3	@	byte offset of block
	blt	6f
	mov	r1, r1, ASR #5	@	block number
	cmp	r1, r4
	bge	6f

	and	r2, r1, #0x1F	@	extract bit number
	mov	r0, #1		@	set bit
	mov	r2, r0, LSL r2	@	move bit into place
	ldr	r0,[r5,r1,ASR #5]@	load mark word

	tst	r0, r2		@	if mark bit not set

	orrne	r0, r0, r2	@		set mark bit
	strne	r0,[r5,r1,ASR #5]@		store mark word
	ldrne	r0, [r9, r8]	@		r = b[n]
	strne	r0, [r7], #4	@		add to scan list
	
	subs	r8, r8, #4	@	--n
	bge	5b
6:
	b	4b		@ }
7:

	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.section .rodata
	.align 5		@ align to cache-line
block_zero:
	.ascii "Who is licking my HONEYPOT?\0"

	.data
	.align 5		@ align to cache-line
block_free:
	.int 0			@ pointer to next free block, 0 if none
block_end:
	.int heap_start		@ pointer to end of block memory

	.section .heap
	.align 10		@ align to 1k boundary
heap_start:

