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
			@ (r0=sponsor, r1=boot, r2=trace)
	stmdb	sp!, {r4-ip,lr}	@ preserve registers
	mov	sl, r0		@ initialize sponsor link
	mov	ip, r1		@ bootstrap actor address

	ldr	r0, =exit_sp	@ location of exit stack pointer
	str	sp, [r0]	@ save exit stack pointer on entry

	ldr	r0, =trace_to	@ location of trace procedure
	str	r2, [r0]	@ set trace procedure

	bl	reserve		@ allocate initial event block
	mov	fp, r0		@ remember event pointer
	str	ip, [r0]	@ set target to bootstrap actor
	bl	enqueue		@ add event to queue

	mov	r0, fp		@ recall event pointer
	bl	serial_hex32	@ display event pointer
	bl	serial_eol	@ newline

	b	dispatch	@ start dispatch loop

	.text
	.align 2		@ align to machine word
	.global set_sponsor
set_sponsor:		@ set sponsor from outside mycelia (bootstrap)
			@ (r0=sponsor)
	mov	sl, r0		@ sponsor is kept in register sl (r10)
	bx	lr		@ return to caller

	.text
	.align 2		@ align to machine word
	.global fail
fail:			@ actor failure notification
	stmdb	sp!, {r0-r3,lr}	@ preserve registers
	ldr	r0, =fail_txt	@ load address of failure text
	bl	serial_puts	@ write text to console
	mov	r0, fp		@ get current message-event
	bl	dump_event	@ dump actor event structure
	ldmia	sp!, {r0-r3,pc}	@ restore registers and return
	.section .rodata
fail_txt:
	.ascii "\nFAIL!\n\0"

	.text
	.align 2		@ align to machine word
trace_event:		@ invoke event tracing hook, if present
			@ (r0=event)
	ldr	r3, =trace_to	@ location of trace procedure
	ldr	r1, [r3]	@ get trace procedure
	teq	r1, #0		@ if disabled
	bxeq	lr		@	return
	stmdb	sp!, {lr}	@ preserve in-use registers
	bx	r1		@ call trace procedure (r0=event)
	ldmia	sp!, {pc}	@ restore in-use registers and return
	.data
	.align 2		@ align to machine word
trace_to:
	.int 0			@ tracing procedure address, or 0 for none

	.text
	.align 2		@ align to machine word
watchdog_check:		@ check for timeout
	ldr	r2, =watchdog_a	@ watchdog actor address
	ldr	r0, [r2]	@ get watchdog actor
	teq	r0, #0		@ if disabled
	bxeq	lr		@	return
	stmdb	sp!, {lr}	@ preserve in-use registers
	bl	timer_usecs	@ get current timer value
	ldr	r3, =watchdog_t	@ watchdog time limit address
	ldr	r1, [r3]	@ get watchdog time limit
	subs	r0, r0, r1	@ (now - limit) = past if <0, future if >0
	blt	1f		@ if now or future
	ldr	r2, =watchdog_a	@ 	watchdog actor address
	ldr	r0, [r2]	@ 	get watchdog actor
	bl	send_0		@ 	send empty message to signal timeout
1:
	ldmia	sp!, {pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global watchdog_set
watchdog_set:		@ set watchdog timer
			@ (r0=customer, r1=timeout)
	stmdb	sp!, {r4,lr}	@ preserve in-use registers
	ldr	r2, =watchdog_a	@ watchdog actor address
	str	r0, [r2]	@ set watchdog actor
	mov	r4, r1		@ copy timeout
	bl	timer_usecs	@ get current timer value
	add	r0, r0, r4	@ calculate time limit
	ldr	r3, =watchdog_t	@ watchdog time limit address
	str	r0, [r3]	@ set watchdog time limit
	ldmia	sp!, {r4,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global watchdog_clear
watchdog_clear:		@ clear watchdog timer
	ldr	r2, =watchdog_a	@ watchdog actor address
	mov	r0, #0		@ 0 disables watchdog
	str	r0, [r2]	@ set watchdog actor
	bx	lr		@ return

	.data
	.align 2		@ align to machine word
watchdog_a:
	.int 0			@ actor to notify on timeout, or 0 for none
watchdog_t:
	.int 0			@ watchdog time limit

	.text
	.align 2		@ align to machine word
	.global exit
exit:			@ exit the actor kernel
	ldr	r0, =exit_sp	@ location of exit stack pointer
	ldr	sp, [r0]	@ get stack pointer saved on entry
	ldmia	sp!, {r4-ip,pc}	@ restore registers and return

	.data
	.align 2		@ align to machine word
exit_sp:
	.int 0			@ stack pointer to restore on exit

	.data
	.align 5		@ align to cache-line
event_q:
	.space 256*4		@ event queue (offset 0)
	.int 0			@ queue head/tail (offset 1024)

	.data
	.align 2		@ align to machine word
block_free:
	.int 0			@ pointer to next free block, 0 if none
block_end:
	.int heap_start		@ pointer to end of block memory

	.section .rodata
	.align 5		@ align to cache-line
block_clr:
	.ascii "Who is licking my HONEYPOT?\0"

@
@ Sponsor calls are delegated through register sl (r10)
@
	.text
	.align 5		@ align to cache-line

	.global dispatch
dispatch:		@ dispatch next event
	ldr	pc, [sl]	@ jump to sponsor dispatch handler

	.global complete
complete:		@ completion of event pointed to by fp
	ldr	pc, [sl,#0x04]	@ jump to sponsor complete handler

	.global reserve
reserve:		@ reserve a block (32 bytes) of memory
	ldr	pc, [sl,#0x08]	@ jump to sponsor reserve handler

	.global release
release:		@ release the memory block pointed to by r0
	ldr	pc, [sl,#0x0c]	@ jump to sponsor release handler

	.global enqueue
enqueue:		@ enqueue event pointed to by r0
	ldr	pc, [sl,#0x10]	@ jump to sponsor enqueue handler

	.global dequeue
dequeue:		@ dequeue next event from queue
	ldr	pc, [sl,#0x14]	@ jump to sponsor dequeue handler

@
@ The default sponsor supports optional tracing and watchdog features
@
	.data
	.align 5		@ align to cache-line
	.global sponsor_0
sponsor_0:
	.int	dispatch_0	@ 0x00: dispatch the next event (ip=actor, fp=event)
	.int	complete_0	@ 0x04: complete event dispatch (fp=event)
	.int	reserve_0	@ 0x08: reserve memory block (32 bytes)
	.int	release_0	@ 0x0c: release memory block (r0)
	.int	enqueue_0	@ 0x10: enqueue event (r0)
	.int	dequeue_0	@ 0x14: dequeue next event, or 0
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: current event

	.text
	.align 2		@ align to machine word
complete_0:		@ completion of event pointed to by fp
	mov	r0, fp		@ get completed event
	bl	release_0	@ free completed event
	mov	fp, #0		@ clear frame pointer
	str	fp, [sl, #0x1c]	@ clear current event

dispatch_0:		@ dispatch next event
	bl	watchdog_check	@ check for timeout, if enabled
	bl	dequeue_0	@ try to get next event
	cmp	r0, #0		@ check for null
	beq	dispatch_0	@ if no event, try again...

	mov	fp, r0		@ initialize frame pointer
	bl	trace_event	@ trace event, if enabled
	str	fp, [sl, #0x1c]	@ update current event
	ldr	ip, [fp]	@ get target actor address
	bx	ip		@ jump to actor behavior

reserve_0:		@ reserve a block (32 bytes) of memory
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r0, [r1]	@ address of first free block
	mov	r3, #0		@ null pointer
	cmp	r0, r3
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
	bl	release_0	@	"free" new memory block
	ldmia	sp!, {lr}	@	restore link register
	b	reserve_0	@	try again

release_0:		@ release the memory block pointed to by r0
			@ [FIXME] add sanity check(s), r0 must be in heap
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r2, [r1]	@ address of next free block
	str	r0, [r1]	@ update free list pointer
	ldr	r1, =block_clr	@ address of block-erase pattern
	ldmia	r1, {r3-r9}	@ read 7 words (32 - 4 bytes)
	stmia	r0, {r2-r9}	@ write 8 words (incl. next free block pointer)
	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

enqueue_0:		@ enqueue event pointed to by r0
			@ [FIXME] add sanity check(s), r0 must be in heap
	ldr	r1, =event_q	@ get event queue pointer
	ldr	r3, [r1, #1024]	@ event queue head/tail indicies
	uxtb	r2, r3, ROR #8	@ get head index
	uxtb	r3, r3, ROR #16	@ get tail index
	str	r0, [r1,r3,LSL #2] @ store event pointer at tail
	add	r3, r3, #1	@ advance tail
	cmp	r2, r3		@ if queue full
	beq	panic		@	kernel panic!
	strb	r3, [r1, #1026]	@ update tail index
	bx	lr		@ return

dequeue_0:		@ dequeue next event from queue
	ldr	r1, =event_q	@ get event queue pointer
	ldr	r3, [r1, #1024]	@ event queue head/tail indicies
	uxtb	r2, r3, ROR #8	@ get head index
	uxtb	r3, r3, ROR #16	@ get tail index
	cmp	r2, r3		@ if queue empty
	moveq	r0, #0		@	return null
	ldrne	r0, [r1,r2,LSL #2] @ else
	addne	r2, r2, #1	@	advance head
	strneb	r2, [r1, #1025]	@	update head index
	bx	lr		@	return event pointer

@
@ The fast sponsor supports does not support tracing or watchdog features
@
	.data
	.align 5		@ align to cache-line
	.global sponsor_1
sponsor_1:
	.int	dispatch_1	@ 0x00: dispatch the next event (ip=actor, fp=event)
	.int	complete_1	@ 0x04: complete event dispatch (fp=event)
	.int	reserve_1	@ 0x08: reserve memory block (32 bytes)
	.int	release_1	@ 0x0c: release memory block (r0)
	.int	enqueue_1	@ 0x10: enqueue event (r0)
	.int	dequeue_1	@ 0x14: dequeue next event, or 0
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

	.text
	.align 2		@ align to machine word
complete_1:		@ completion of event pointed to by fp
	mov	r0, fp		@ get completed event
	bl	release_1	@ free completed event
	mov	fp, #0		@ clear frame pointer

dispatch_1:		@ dispatch next event
	bl	dequeue_1	@ try to get next event
	teq	r0, #0		@ check for null
	beq	dispatch_1	@ if no event, try again...

	mov	fp, r0		@ initialize frame pointer
	ldr	ip, [fp]	@ get target actor address
	bx	ip		@ jump to actor behavior

reserve_1:		@ reserve a block (32 bytes) of memory
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r0, [r1]	@ address of first free block
	mov	r3, #0		@ null pointer
	teq	r0, r3
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
	bl	release_1	@	"free" new memory block
	ldmia	sp!, {lr}	@	restore link register
	b	reserve_1	@	try again

release_1:		@ release the memory block pointed to by r0
	ldr	r1, =block_free	@ address of free list pointer
	ldr	r2, [r1]	@ address of next free block
	str	r2, [r0]	@ link block into free list
	str	r0, [r1]	@ update free list pointer
	bx	lr		@ return

enqueue_1:		@ enqueue event pointed to by r0
	ldr	r1, =event_q	@ get event queue pointer
	ldr	r3, [r1, #1024]	@ event queue head/tail indicies
	uxtb	r2, r3, ROR #8	@ get head index
	uxtb	r3, r3, ROR #16	@ get tail index
	str	r0, [r1,r3,LSL #2] @ store event pointer at tail
	add	r3, r3, #1	@ advance tail
	teq	r2, r3		@ if queue full
	beq	panic		@	kernel panic!
	strb	r3, [r1, #1026]	@ update tail index
	bx	lr		@ return

dequeue_1:		@ dequeue next event from queue
	ldr	r1, =event_q	@ get event queue pointer
	ldr	r3, [r1, #1024]	@ event queue head/tail indicies
	uxtb	r2, r3, ROR #8	@ get head index
	uxtb	r3, r3, ROR #16	@ get tail index
	teq	r2, r3		@ if queue empty
	moveq	r0, #0		@	return null
	ldrne	r0, [r1,r2,LSL #2] @ else
	addne	r2, r2, #1	@	advance head
	strneb	r2, [r1, #1025]	@	update head index
	bx	lr		@	return event pointer

@
@ Common actor examples and templates
@

	.text
	.align 5		@ align to cache-line
example_0:
	bl	reserve		@ allocate event block
	ldr	r1, [ip, #0x1c] @ get answer
	.global _a_answer
_a_answer:		@ send answer to customer and return
			@ (r0=event, r1=answer)
	str	r1, [r0, #0x04]	@ set answer
	.global _a_reply
_a_reply:		@ reply to customer and return from actor
			@ (r0=event)
	ldr	r1, [fp, #0x04]	@ get customer
	.global _a_send
_a_send:		@ send a message and return from actor
			@ (r0=event, r1=target)
	str	r1, [r0]	@ set target actor
	.global _a_end
_a_end:			@ queue message and return from actor
			@ (r0=event)
	bl	enqueue		@ add event to queue
	b	complete	@ return to dispatch loop
	.int	0x42424242	@ answer data

	.text
	.align 5		@ align to cache-line
example_1:
	ldr	pc, [ip, #4]	@ jump to actor behavior
	.int	complete	@ 0x04: address of actor behavior
	.int	0x11111111	@ 0x08: state field 1
	.int	0x22222222	@ 0x0c: state field 2
	.int	0x33333333	@ 0x10: state field 3
	.int	0x44444444	@ 0x14: state field 4
	.int	0x55555555	@ 0x18: state field 5
	.int	0x66666666	@ 0x1c: state field 6

	.text
	.align 5		@ align to cache-line
example_2:
	ldr	lr, [pc]	@ get actor behavior address
	blx	lr		@ jump to behavior, lr points to state
	.int	complete	@ 0x08: address of actor behavior
	.int	0x11111111	@ 0x0c: state field 1
	.int	0x22222222	@ 0x10: state field 2
	.int	0x33333333	@ 0x14: state field 3
	.int	0x44444444	@ 0x18: state field 4
	.int	0x55555555	@ 0x1c: state field 5

	.text
	.align 5		@ align to cache-line
template_1:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4,pc}	@ copy state and jump to behavior
	.int	0		@ 0x08: value for r4
	.int	complete	@ 0x0c: address of actor behavior

	.text
	.align 5		@ align to cache-line
template_2:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4-r5,pc}	@ copy state and jump to behavior
	.int	0		@ 0x08: value for r4
	.int	0		@ 0x0c: value for r5
	.int	complete	@ 0x10: address of actor behavior

	.text
	.align 5		@ align to cache-line
template_3:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4-r6,pc}	@ copy state and jump to behavior
	.int	0		@ 0x08: value for r4
	.int	0		@ 0x0c: value for r5
	.int	0		@ 0x10: value for r6
	.int	complete	@ 0x14: address of actor behavior

	.text
	.align 5		@ align to cache-line
example_3:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip,{r4-r8,pc}	@ copy state and jump to behavior
	.int	0		@ 0x08: value for r4
	.int	0		@ 0x0c: value for r5
	.int	0		@ 0x10: value for r6
	.int	0		@ 0x14: value for r7
	.int	0		@ 0x18: value for r8
	.int	complete	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
example_4:
	ldmib	ip,{r4-r9,pc}	@ copy state and jump to behavior
	.int	0		@ 0x04: value for r4
	.int	0		@ 0x08: value for r5
	.int	0		@ 0x0c: value for r6
	.int	0		@ 0x10: value for r7
	.int	0		@ 0x14: value for r8
	.int	0		@ 0x18: value for r9
	.int	complete	@ 0x1c: address of actor behavior

	.text
	.align 2		@ align to machine word
	.global create
create:			@ create an actor from example_3
			@ (r0=behavior)
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers
	mov	r9, r0		@ move behavior pointer into place
	bl	reserve		@ allocate actor block
	ldr	r1, =example_3	@ load template address
	ldmia	r1, {r2-r8}	@ read template (minus behavior)
	stmia	r0, {r2-r9}	@ write actor
	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_0
create_0:		@ create an actor from example_1
			@ (r0=behavior)
	stmdb	sp!, {r4,lr}	@ preserve in-use registers
	mov	r4, r0		@ move behavior pointer into place
	bl	reserve		@ allocate actor block
	ldr	r1, =example_1	@ load template address
	ldmia	r1, {r3}	@ read template (minus behavior)
	stmia	r0, {r3-r4}	@ write actor
	ldmia	sp!, {r4,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_1
create_1:		@ create 1 parameter actor
			@ (r0=behavior, r1=r4)
	stmdb	sp!, {r4-r5,lr}	@ preserve in-use registers
	mov	r4, r1		@ move state parameter into place
	mov	r5, r0		@ move behavior pointer into place
	bl	reserve		@ allocate actor block
	ldr	r1, =template_1	@ load template address
	ldmia	r1, {r2-r3}	@ read template (code only)
	stmia	r0, {r2-r5}	@ write actor
	ldmia	sp!, {r4-r5,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_2
create_2:		@ create 2 parameter actor
			@ (r0=behavior, r1=r4, r2=r5)
	stmdb	sp!, {r4-r6,lr}	@ preserve in-use registers
	mov	r4, r1		@ move 1st state parameter into place
	mov	r5, r2		@ move 2nd state parameter into place
	mov	r6, r0		@ move behavior pointer into place
	bl	reserve		@ allocate actor block
	ldr	r1, =template_2	@ load template address
	ldmia	r1, {r2-r3}	@ read template (code only)
	stmia	r0, {r2-r6}	@ write actor
	ldmia	sp!, {r4-r6,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_3
create_3:		@ create 3 parameter actor
			@ (r0=behavior, r1=r4, r2=r5, r3=r6)
	stmdb	sp!, {r4-r7,lr}	@ preserve in-use registers
	mov	r4, r1		@ move 1st state parameter into place
	mov	r5, r2		@ move 2nd state parameter into place
	mov	r6, r3		@ move 3rd state parameter into place
	mov	r7, r0		@ move behavior pointer into place
	bl	reserve		@ allocate actor block
	ldr	r1, =template_3	@ load template address
	ldmia	r1, {r2-r3}	@ read template (code only)
	stmia	r0, {r2-r7}	@ write actor
	ldmia	sp!, {r4-r7,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_3x
create_3x:		@ create 3 parameter actor
			@ (r4-r6=state, r7=behavior)
	stmdb	sp!, {lr}	@ preserve in-use registers
	bl	reserve		@ allocate actor block
	ldr	r1, =template_3	@ load template address
	ldmia	r1, {r2-r3}	@ read template (code only)
	stmia	r0, {r2-r7}	@ write actor
	ldmia	sp!, {pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_4x
create_4x:		@ create an actor from example_4
			@ (r0=behavior)
	stmdb	sp!, {r4-r9,lr}	@ preserve in-use registers
	mov	r9, r0		@ move behavior pointer into place
	bl	reserve		@ allocate actor block
	ldr	r1, =example_4	@ load template address
	ldmia	r1, {r2-r8}	@ read template (minus behavior)
	stmia	r0, {r2-r9}	@ write actor
	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global create_4_9
create_4_9:		@ create an actor from example_4
			@ (r4-r9=state, r0=behavior)
	stmdb	sp!, {r0,lr}	@ preserve in-use registers
	bl	reserve		@ allocate actor block
	ldr	r3, =example_4	@ load template address
	ldr	r3, [r3]	@ load compiled code
	stmia	r0, {r3-r9}	@ write actor state
	ldmia	sp!, {r1}	@ restore behavior
	str	r1, [r0, #0x1c]	@ write actor behavior
	ldmia	sp!, {pc}	@ return

	.text
	.align 2		@ align to machine word
	.global send
send:			@ send full-block message
			@ (r0=target, r1-r7=message)
	stmdb	sp!, {r4-r8,lr}	@ preserve in-use registers
	stmdb	sp!, {r0-r7}	@ preserve event data
	bl	reserve		@ allocate event block
	ldmia	sp!, {r1-r8}	@ restore event data
	stmia	r0, {r1-r8}	@ write data to event
	bl	enqueue		@ add event to queue
	ldmia	sp!, {r4-r8,pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global send_0
send_0:			@ send 0 parameter message
			@ (r0=target)
	stmdb	sp!, {lr}	@ preserve in-use registers
	stmdb	sp!, {r0}	@ preserve event data
	bl	reserve		@ allocate event block
	ldmia	sp!, {r1}	@ restore event data
	stmia	r0, {r1}	@ write data to event
	bl	enqueue		@ add event to queue
	ldmia	sp!, {pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global send_1
send_1:			@ send 1 parameter message
			@ (r0=target, r1=message)
	stmdb	sp!, {lr}	@ preserve in-use registers
	stmdb	sp!, {r0-r1}	@ preserve event data
	bl	reserve		@ allocate event block
	ldmia	sp!, {r1-r2}	@ restore event data
	stmia	r0, {r1-r2}	@ write data to event
	bl	enqueue		@ add event to queue
	ldmia	sp!, {pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global send_2
send_2:			@ send 2 parameter message
			@ (r0=target, r1-r2=message)
	stmdb	sp!, {lr}	@ preserve in-use registers
	stmdb	sp!, {r0-r2}	@ preserve event data
	bl	reserve		@ allocate event block
	ldmia	sp!, {r1-r3}	@ restore event data
	stmia	r0, {r1-r3}	@ write data to event
	bl	enqueue		@ add event to queue
	ldmia	sp!, {pc}	@ restore in-use registers and return

	.text
	.align 2		@ align to machine word
	.global send_3x
send_3x:		@ send 3 parameter message
			@ (r4=target, r5-r7=message)
	stmdb	sp!, {lr}	@ preserve in-use registers
	bl	reserve		@ allocate event block
	stmia	r0, {r4-r7}	@ write data to event
	bl	enqueue		@ add event to queue
	ldmia	sp!, {pc}	@ restore in-use registers and return

@
@ Unit test fixtures
@

	.text
	.align 5		@ align to cache-line
	.global a_test
a_test:			@ test suite execution actor
	mov	r0, #1000	@ 1 millisecond
	mul	r1, r0, r0	@ 1 second
	mov	r0, #3		@ 3x
	mul	r1, r0, r1	@ 3 seconds
	ldr	r0, =a_failed	@ fail after 1 second
	bl	watchdog_set	@ set watchdog timer

	@ unit test launcher
@	ldr	r0, =b_fork_t
	ldr	r0, =b_kernel_t
	bl	create_0	@ create unit test actor
	ldr	r1, =a_test_ok	@ ok customer
	ldr	r2, =a_failed	@ fail customer
	bl	send_2
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_test_ok
a_test_ok:		@ succesful completion of test suite
	bl	watchdog_clear	@ clear watchdog timer
	ldr	r0, =a_passed	@ get success actor
	bl	send_0		@ send message to report success
	b	complete	@ return to dispatch loop
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global a_passed
a_passed:		@ report tests passed, and exit
	add	r0, ip, #0x18	@ address of output text
	bl	serial_puts	@ write output text
	bl	serial_eol	@ write end-of-line
	b	exit		@ kernel exit!
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.ascii	"Passed.\0"	@ 0x18..0x1f: output text

	.text
	.align 5		@ align to cache-line
	.global a_failed
a_failed:		@ report tests failed, and exit
	add	r0, ip, #0x18	@ address of output text
	bl	serial_puts	@ write output text
	bl	serial_eol	@ write end-of-line
	b	exit		@ kernel exit!
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.ascii	"FAILED!\0"	@ 0x18..0x1f: output text

	.text
	.align 5		@ align to cache-line
	.global a_exitq
a_exitq:		@ exit quietly from runtime
	b	exit		@ kernel exit!

	.text
	.align 5		@ align to cache-line
	.global a_bench
a_bench:		@ benchmark bootstrap actor
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip,{r4-r8,pc}	@ copy state and jump to behavior
	.int	b_countdown	@ 0x00: r4 = child behavior
	.int	0		@ 0x04: r5 = child state
	.int	a_exitq		@ 0x08: r6 = child message[0] (cust)
	.int	1000000		@ 0x0c: r7 = child message[1] (count)
	.int	0		@ 0x10: r8 = -- time 2491923us
	.int	b_bench		@ 0x14: behavior

	.text
	.align 5		@ align to cache-line
	.global a_bench
a_bench2:		@ benchmark bootstrap actor
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip,{r4-r8,pc}	@ copy state and jump to behavior
	.int	b_fbomb		@ 0x00: r4 = child behavior
	.int	0		@ 0x04: r5 = child state
	.int	a_exitq		@ 0x08: r6 = child message[0] (cust)
	.int	7		@ 0x0c: r7 = child message[1] (count)
	.int	0		@ 0x10: r8 = --
	.int	b_bench		@ 0x14: behavior

	.text
	.align 5		@ align to cache-line
b_bench:		@ benchmark bootstrap behavior
	mov	r0, r4		@ get child behavior
	mov	r1, r5		@ get child state
	bl	create_1	@ create child actor
	mov	r1, r6		@ get child message[0] (cust)
	mov	r2, r7		@ get child message[1] (count)
	bl	send_2		@ send message to child
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
b_null_srv:		@ nothing server behavior
			@ message = (cust, ...)
	ldr	r0, [fp, #0x04]	@ get cust
	bl	send_0		@ 	send empty message to cust
	b	complete	@ 	return to dispatch loop

	.text
	.align 5		@ align to cache-line
b_countdown:		@ countdown server behavior
			@ message = (cust, count)
	ldr	r0, [fp, #0x04]	@ get cust
	ldr	r2, [fp, #0x08]	@ get count
	subs	r2, r2, #0x1	@ decrement count
	bpl	1f		@ if count < 0
	bl	send_0		@ 	send empty message to cust
	b	complete	@ 	return to dispatch loop
1:
	ldr	r0, [fp]	@ get SELF
	ldr	r1, [fp, #0x04]	@ get cust
	bl	send_2		@ send (cust, count') to SELF
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
b_fbomb:		@ fork-bomb behavior
			@ message = (cust, count)
	ldr	r1, [fp, #0x08]	@ get count
	subs	r8, r1, #0x1	@ decrement count
	bpl	1f		@ if count < 0
	ldr	r0, [fp, #0x04]	@       get cust
	bl	send_1		@ 	send original count to cust
	b	complete	@ 	return to dispatch loop
1:
	bl	reserve		@ allocate left event block
	mov	r6, r0		@ save for later reference
	ldr	r0, =b_fbomb	@ get b_fbomb address
	bl	create_0	@ create left child
	str	r0, [r6]	@ target left child
	str	r8, [r6, #0x08]	@ send decremented count

	bl	reserve		@ allocate right event block
	mov	r7, r0		@ save for later reference
	ldr	r0, =b_fbomb	@ get b_fbomb address
	bl	create_0	@ create right child
	str	r0, [r7]	@ target right child
	str	r8, [r7, #0x08]	@ send decremented count

	ldr	r4, =a_fork	@ get a_fork address
	ldr	r5, [fp, #0x04]	@ get cust
	bl	reserve		@ allocate fork event block
	stmia	r0, {r4-r7}	@ write new event
	bl	enqueue		@ add event to queue
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_exit
a_exit:			@ print message, and exit
	add	r0, ip, #0x18	@ address of output text
	bl	serial_puts	@ write output text
	bl	serial_eol	@ write end-of-line
	b	exit		@ kernel exit!
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.ascii	"EXIT...\0"	@ 0x18..0x1f: output text

	.text
	.align 2		@ align to machine word
	.global panic
panic:			@ kernel panic!
	stmdb	sp!, {r0-r3,lr}	@ preserve registers
	ldr	r0, =panic_txt	@ load address of panic text
	bl	serial_puts	@ write text to console
	ldmia	sp!, {r0-r3,lr}	@ restore registers
	b	halt
	.section .rodata
panic_txt:
	.ascii "\nPANIC!\0"

	.section .heap
@	.align 8		@ align to 256-byte boundary
	.align 12		@ align to 4kB boundary
heap_start:
