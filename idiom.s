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

@
@ indirect-coded actor behaviors (use with `create` procedures)
@

	.text
	.align 5		@ align to cache-line
	.global b_label
b_label:		@ add label to message (r4=customer, r5=label)
	bl	reserve		@ allocate event block
	str	r4, [fp]	@ replace target with customer
	mov	r3, r5		@ move label into position
	ldmia	fp, {r2,r4-r9}	@ copy request (drop last word)
	stmia	r0, {r2-r9}	@ write new event
	bl	enqueue		@ add event to queue
	b	complete	@ return to dispatch loop
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global b_tag
b_tag:			@ label one message with actor identity (r4=customer)
	mov	r2, r4		@ move customer into position
	ldmia	fp, {r0,r4-r9}	@ copy request (drop last word)
	mov	r3, r0		@ move label into position
	stmia	fp, {r2-r9}	@ overwrite event
	mov	ip, r2		@ get target actor address
	bl	release		@ destroy current actor (r0=tag_actor)
	bx	ip		@ jump to customer behavior
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global b_fork
b_fork:			@ initiate two concurrent events
			@ message = (customer, event_0, event_1)
	ldr	r0, =b_join	@ get b_join address
	bl	create		@ create b_join actor
	mov	ip, r0		@ point ip to join actor
	ldr	r0, [fp, #0x04]	@ get customer
	str	r0, [ip, #0x08] @ remember customer

	ldr	r0, =b_tag	@ get b_tag address
	mov	r1, ip		@ customer is join
	bl	create_1	@ create b_tag actor
	str	r0, [ip, #0x0c] @ remember first tag actor
	ldr	r1, [fp, #0x08]	@ get event_0
	str	r0, [r1, #0x04]	@ set/replace customer in event_0
	mov	r0, r1
	bl	enqueue		@ add event to queue

	ldr	r0, =b_tag	@ get b_tag address
	mov	r1, ip		@ customer is join
	bl	create_1	@ create b_tag actor
	str	r0, [ip, #0x10] @ remember second tag actor
	ldr	r1, [fp, #0x0c]	@ get event_1
	str	r0, [r1, #0x04]	@ set/replace customer in event_1
	mov	r0, r1
	bl	enqueue		@ add event to queue

	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_join
b_join:			@ combine results of concurrent computation
			@ (r4=customer, r5=event/answer_0, r6=event/answer_1)
			@ message = (tag, answer)
	ldr	r0, [fp, #0x04]	@ get tag
	cmp	r0, r5
	bne	1f		@ if tag == event_0
	ldr	r5, [fp, #0x08]	@	remember answer_0
	ldr	r9, =_join_0	@	wait for answer_1
	b	3f
1:
	cmp	r0, r6
	bne	complete	@ if tag == event_1
	ldr	r6, [fp, #0x08]	@	remember answer_1
	ldr	r9, =_join_1	@	wait for answer_0
	b	3f
	.align 5		@ align to cache-line
_join_0:		@ wait for answer_1
	ldr	r0, [fp, #0x04]	@ get tag
	cmp	r0, r6		@ if tag == event_1
	ldreq	r6, [fp, #0x08]	@ 	get answer_1
	beq	2f		@ else
	b	complete	@ 	ignore message
	.align 5		@ align to cache-line
_join_1:		@ wait for answer_0
	ldr	r0, [fp, #0x04]	@ get tag
	cmp	r0, r5		@ if tag == event_0
	ldreq	r5, [fp, #0x08]	@	get answer_0
	beq	2f		@ else
	b	complete	@ 	ignore message
2:
	bl	reserve		@ allocate event block
	ldmia	r0, {r4-r6}	@ set (customer, answer_0, answer_1)
	bl	enqueue		@ add event to queue
	ldr	r9, =complete	@ ignore future messages
3:
	stmia	ip,{r4-r9}	@ copy state and behavior
	b	complete	@ return to dispatch loop
