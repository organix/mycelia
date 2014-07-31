@
@ idiom.s -- Actor Idioms
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
	.align 5		@ align to cache-line
	.global a_ignore
a_ignore:		@ ignore message
	b	complete	@ return to dispatcher
	.int	0		@ 0x04: --
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global a_forward
a_forward:		@ forward message (used by a_oneshot)
	bl	reserve		@ allocate event block
	ldmia	fp, {r2-r9}	@ copy request
	ldr	r2, [ip, #0x1c]	@ replace target with delegate
	stmia	r0, {r2-r9}	@ write new event
	b	_a_end		@ queue message and return
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	a_ignore	@ 0x1c: delegate actor address

	.text
	.align 5		@ align to cache-line
	.global a_oneshot
a_oneshot:		@ forward one message, then ignore
	ldr	pc, [ip, #0x18] @ jump to current behavior
	ldr	lr, [ip, #0x14]	@ load next behavior
	str	lr, [ip, #0x18]	@ update current behavior
	b	a_forward	@ forward, first time only
	.int	0		@ 0x10: --
	.int	complete	@ 0x14: next behavior
	.int	a_oneshot+4	@ 0x18: current behavior
	.int	a_ignore	@ 0x1c: delegate actor (used by a_forward)

	.text
	.align 5		@ align to cache-line
	.global a_fork
a_fork:			@ initiate two concurrent events
			@ message = (customer, event_0, event_1)
	@ r4=customer, r5=event_0, r6=event_1, r7=event_n, r8=tag, ip=join
	ldmia	fp, {r3-r6}	@ load current event

	ldr	r0, =b_join	@ get b_join address
	bl	create		@ create b_join actor
	mov	ip, r0		@ point ip to join actor
	str	r4, [ip, #0x08] @ remember customer

	mov	r7, r5		@ for event_0...
	bl	_fork_0		@ create tag actor
	str	r8, [ip, #0x0c] @ remember first tag actor

	mov	r7, r6		@ for event_1...
	bl	_fork_0		@ create tag actor
	str	r8, [ip, #0x10] @ remember second tag actor

	b	complete	@ return to dispatch loop

_fork_0:		@ create tag actor
	stmdb	sp!, {lr}	@ preserve in-use registers
	ldr	r0, =b_tag	@ get b_tag address
	mov	r1, ip		@ customer is join
	bl	create_1	@ create b_tag actor
	mov	r8, r0		@ remember tag actor
	str	r8, [r7, #0x04]	@ set/replace customer in event_n
	mov	r0, r7
	bl	enqueue		@ add event to queue
	ldmia	sp!, {pc}	@ restore in-use registers and return

@
@ indirect-coded actor behaviors (use with `create` procedures)
@

	.text
	.align 5		@ align to cache-line
	.global b_label
b_label:		@ add label to message (r4=delegate, r5=label)
	bl	reserve		@ allocate event block
	str	r4, [fp]	@ replace target with delegate
	mov	r3, r5		@ move label into position
	ldmia	fp, {r2,r4-r9}	@ copy request (drop last word)
	stmia	r0, {r2-r9}	@ write new event
	bl	enqueue		@ add event to queue
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_tag
b_tag:			@ label message with actor identity (r4=delegate)
	mov	r2, r4		@ move delegate into position
	ldmia	fp, {r3-r9}	@ copy request (drop last word, r3=label)
	stmia	fp, {r2-r9}	@ overwrite event
	mov	ip, r2		@ get target actor address
	bx	ip		@ jump to delegate behavior

	.text
	.align 5		@ align to cache-line
	.global b_join
b_join:			@ combine results of concurrent computation
			@ (r4=customer, r5=tag/result_0, r6=tag/result_1)
			@ message = (tag, result)
	ldr	r0, [fp, #0x04]	@ get tag
	cmp	r0, r5
	bne	1f		@ if tag == event_0
	ldr	r5, [fp, #0x08]	@	remember result_0
	ldr	r9, =_join_0	@	wait for result_1
	b	3f
1:
	cmp	r0, r6
	bne	complete	@ if tag == event_1
	ldr	r6, [fp, #0x08]	@	remember result_1
	ldr	r9, =_join_1	@	wait for result_0
	b	3f
	.align 5		@ align to cache-line
_join_0:		@ wait for result_1
	ldr	r0, [fp, #0x04]	@ get tag
	cmp	r0, r6		@ if tag == event_1
	ldreq	r6, [fp, #0x08]	@ 	get result_1
	beq	2f		@ else
	b	complete	@ 	ignore message
	.align 5		@ align to cache-line
_join_1:		@ wait for result_0
	ldr	r0, [fp, #0x04]	@ get tag
	cmp	r0, r5		@ if tag == event_0
	ldreq	r5, [fp, #0x08]	@	get result_0
	beq	2f		@ else
	b	complete	@ 	ignore message
2:
	bl	reserve		@ allocate event block
	stmia	r0, {r4-r6}	@ set (customer, result_0, result_1)
	bl	enqueue		@ add event to queue
	ldr	r9, =complete	@ ignore future messages
3:
	stmia	ip,{r4-r9}	@ copy state and behavior
	b	complete	@ return to dispatch loop

@
@ actor capabilities
@

	.text
	.align 5		@ align to cache-line
	.global b_literal
b_literal:		@ a literal evaluates to itself
			@ message = (ok, fail, environment)
	ldr	r0, [fp, #0x04]	@ get ok customer
	ldr	r1, [fp]	@ get target (self)
	bl	send_1		@ send self to ok
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_constant
b_constant:		@ a constant evaluates to a consistent value
			@ (0x08: r4=value)
			@ message = (ok, fail, environment)
	ldr	r0, [fp, #0x04]	@ get ok customer
	mov	r1, r4		@ get constant value
	bl	send_1		@ send value to ok
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_load
b_load:			@ retrieve the currently stored value
			@ (0x08: r4=value)
			@ message = (ok, fail, environment)
	ldr	r0, [fp, #0x04]	@ get ok customer
	mov	r1, r4		@ get constant value
	bl	send_1		@ send value to ok
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_store
b_store:		@ store a replacement value
			@ (0x08: r4=load_cap)
			@ message = (ok, fail, value)
	ldr	r0, [r4, #0x0c]	@ get load_cap behavior
	ldr	r1, =b_load	@ get b_load behavior
	cmp	r0, r1		@ if load_cap is not a b_load actor
	ldrne	ip, [fp, #0x08]	@	ip = fail customer
	bxne	ip		@	dispatch to fail
	ldr	r1, [fp, #0x0c]	@ get replacement value
	str	r1, [r4, #0x08]	@ store replacement value
	ldr	r0, [fp, #0x04]	@ get ok customer
	bl	send_1		@ send response
	b	complete	@ return to dispatch loop

@
@ unit test actors and behaviors
@

	.text
	.align 5		@ align to cache-line
	.global b_fork_t
b_fork_t:		@ fork unit test
			@ message = (ok, fail)
	ldr	r0, =b_match2_t	@ result value matching behavior
	bl	create		@ create matching actor
	ldr	r1, [fp, #0x04]	@ get ok customer
	str	r1, [r0, #0x08]	@ set ok customer
	ldr	r1, [fp, #0x08]	@ get fail customer
	str	r1, [r0, #0x0c]	@ set fail customer
	mov	r1, #123	@ n1 = 123
	str	r1, [r0, #0x10]	@ set fail customer
	mov	r1, #456	@ n2 = 456
	str	r1, [r0, #0x14]	@ set fail customer
	mov	r5, r0		@ r5 = match actor

	ldr	r9, =b_const_t	@ contant test "server" behavior

	mov	r0, r9
	mov	r1, #123	@ n1 = 123
	bl	create_1	@ create constant actor
	mov	r6, r0		@ r6 = 123 server
	bl	reserve		@ allocate event block
	str	r6, [r0]	@ target = 123 server
	mov	r6, r0		@ r6 = 123 server event

	mov	r0, r9
	mov	r1, #456	@ n2 = 456
	bl	create_1	@ create constant actor
	mov	r7, r0		@ r7 = 456 server
	bl	reserve		@ allocate event block
	str	r7, [r0]	@ target = 456 server
	mov	r7, r0		@ r7 = 456 server event

	ldr	r4, =a_fork	@ target is a_fork
	bl	send_3x		@ message = (r5=customer, r6=event_0, r7=event_1)
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_const_t
b_const_t:		@ test "server" that returns a constant value
			@ (0x08: r4=value)
			@ message = (customer)
	ldr	r0, [fp, #0x04]	@ get customer
	mov	r1, r4		@ get constant value
	bl	send_1		@ send value to customer
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_const_t
b_match2_t:		@ test comparison against 2-word result
			@ (0x08:r4=ok, 0x0c:r5=fail, 0x10:r6=n1, 0x14:r7=n2)
			@ message = (n1, n2)
	ldr	r0, [fp, #0x04]	@ get actual n1
	cmp	r0, r6		@ if n1 != expected
	bne	1f		@	fail
	ldr	r0, [fp, #0x08]	@ get actual n2
	cmp	r0, r7		@ if n2 != expected
	bne	1f		@	fail
	mov	r0, r4		@ get ok actor
	b	2f
1:
	mov	r0, r5		@ get fail actor
2:
	bl	send_0		@ send message
	b	complete	@ return to dispatch loop
