@
@ shutt.s -- John Shutt's "Kernel" Language numbers
@
@ Copyright 2021 Dale Schumacher
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

	.set S_SELF, 0x00	@ "visit"
	.set S_GET,  0x01	@ "lookup"
	.set S_SET,  0x02	@ "bind"
	.set S_EVAL, 0x03
	.set S_APPL, 0x04	@ "combine"
	.set S_OPER, 0x05	@ "unwrap"
	.set S_EXEC, 0x06
	.set S_PUSH, 0x07
	.set S_POP,  0x08
	.set S_PUT,  0x09
	.set S_PULL, 0x0A

	.text
	.align 5		@ align to cache-line
	.global b_num_cmp
b_num_cmp:	@ numeric comparison behavior
			@ (example_5: 0x04=code...)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =number_p	@	get predicate
	add	r1, ip, #0x04	@	relation addr (in operative block)
	ldr	r2, [fp, #0x0c]	@	get operands
	blx	apply_pred_rltn	@	apply predicate & relation to operands
	teq	r0, #0		@	if NULL
	beq	a_kernel_err	@		signal error
	mov	r4, r0		@	save result
	bl	reserve		@	allocate event block
	mov	r1, r4		@	restore result
	b	_a_answer	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global op_num_eq_p
op_num_eq_p:		@ operative "$=?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ relation(r0->number0, r1->number1) => 0/!0 (C boolean)
	ldr	r0, [r0, #0x04]	@ integer value
	ldr	r1, [r1, #0x04]	@ integer value
	subs	r0, r0, r1	@ subtract
	moveq	r0, #1		@ if n0 == n0 return 1
	movne	r0, #0		@ else return 0
	bx	lr		@ return
	.int	b_num_cmp	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_eq_p
ap_num_eq_p:		@ applicative "=?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_eq_p	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_num_lt_p
op_num_lt_p:		@ operative "$<?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ relation(r0->number0, r1->number1) => 0/!0 (C boolean)
	ldr	r0, [r0, #0x04]	@ integer value
	ldr	r1, [r1, #0x04]	@ integer value
	subs	r0, r0, r1	@ subtract
	movlt	r0, #1		@ if n0 < n0 return 1
	movge	r0, #0		@ else return 0
	bx	lr		@ return
	.int	b_num_cmp	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_lt_p
ap_num_lt_p:		@ applicative "<?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_lt_p	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_num_le_p
op_num_le_p:		@ operative "$<=?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ relation(r0->number0, r1->number1) => 0/!0 (C boolean)
	ldr	r0, [r0, #0x04]	@ integer value
	ldr	r1, [r1, #0x04]	@ integer value
	subs	r0, r0, r1	@ subtract
	movle	r0, #1		@ if n0 <= n0 return 1
	movgt	r0, #0		@ else return 0
	bx	lr		@ return
	.int	b_num_cmp	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_le_p
ap_num_le_p:		@ applicative "<=?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_le_p	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_num_ge_p
op_num_ge_p:		@ operative "$>=?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ relation(r0->number0, r1->number1) => 0/!0 (C boolean)
	ldr	r0, [r0, #0x04]	@ integer value
	ldr	r1, [r1, #0x04]	@ integer value
	subs	r0, r0, r1	@ subtract
	movge	r0, #1		@ if n0 >= n0 return 1
	movlt	r0, #0		@ else return 0
	bx	lr		@ return
	.int	b_num_cmp	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_ge_p
ap_num_ge_p:		@ applicative ">=?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_ge_p	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_num_gt_p
op_num_gt_p:		@ operative "$>?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ relation(r0->number0, r1->number1) => 0/!0 (C boolean)
	ldr	r0, [r0, #0x04]	@ integer value
	ldr	r1, [r1, #0x04]	@ integer value
	subs	r0, r0, r1	@ subtract
	movgt	r0, #1		@ if n0 > n0 return 1
	movle	r0, #0		@ else return 0
	bx	lr		@ return
	.int	b_num_cmp	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_gt_p
ap_num_gt_p:		@ applicative ">?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_gt_p	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global b_num_op
b_num_op:	@ numeric operator behavior
			@ (example_5: 0x04=code...)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [ip, #0x18]	@	get accumulator
	add	r1, ip, #0x04	@	operation addr (in operative block)
	ldr	r2, [fp, #0x0c]	@	get operands
	blx	num_reduce	@	apply operation to operands
	teq	r0, #0		@	if NULL
	beq	a_kernel_err	@		signal error
	mov	r4, r0		@	save result
	bl	reserve		@	allocate event block
	mov	r1, r4		@	restore result
	b	_a_answer	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global op_num_plus
op_num_plus:		@ operative "$+"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ operation(r0->int32, r1->int32) => int32
	add	r0, r1, r0	@ add r0 + r1
	bx	lr		@ return r0
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: initial accumulator value
	.int	b_num_op	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_plus
ap_num_plus:		@ applicative "+"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_plus	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_num_times
op_num_times:		@ operative "$*"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ operation(r0->int32, r1->int32) => int32
	mul	r0, r1, r0	@ multiply r0 * r1
	bx	lr		@ return r0
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	1		@ 0x18: initial accumulator value
	.int	b_num_op	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_times
ap_num_times:		@ applicative "*"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_times	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global b_num_minus
b_num_minus:	@ numeric subtraction behavior
			@ (example_5: 0x04=code...)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r9, [fp, #0x0c] @	get opnds

	mov	r0, r9
	bl	car		@	first arg
	movs	r4, r0		@	if NULL
	beq	a_kernel_err	@		signal error
	bl	number_p	@
	teq	r0, #0		@	if !number_p(first)
	beq	a_kernel_err	@		signal error

	mov	r0, r9
	bl	cdr		@	rest args
	movs	r5, r0		@	if NULL
	beq	a_kernel_err	@		signal error

	ldr	r0, [r4, #0x04]	@	initial accumulator
	add	r1, ip, #0x04	@	operation addr (in operative block)
	mov	r2, r5		@	get operands
	blx	num_reduce	@	apply operation to operands
	teq	r0, #0		@	if NULL
	beq	a_kernel_err	@		signal error

	mov	r4, r0		@	save result
	bl	reserve		@	allocate event block
	mov	r1, r4		@	restore result
	b	_a_answer	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global op_num_minus
op_num_minus:		@ operative "$-"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ operation(r0->int32, r1->int32) => int32
	sub	r0, r0, r1	@ subtract r0 - r1
	bx	lr		@ return r0
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_num_minus	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_num_minus
ap_num_minus:		@ applicative "-"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_num_minus	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

@
@ bit-vector operations
@

	.text
	.align 5		@ align to cache-line
	.global b_bit_not
b_bit_not:	@ bitwise negation behavior
			@ (example_5: 0x04=)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate
	bl	match_1_arg
	ldr	r0, [r0, #0x04]	@	integer value

	mvn	r0, r0		@	bit-invert number
	bl	number		@	construct a kernel number
	mov	r4, r0		@	save result

	bl	reserve		@	allocate event block
	mov	r1, r4		@	restore result
	b	_a_answer	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global op_bit_not
op_bit_not:		@ operative "$bit-not"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	0		@ 0x04: --
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_bit_not	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_bit_not
ap_bit_not:		@ applicative "bit-not"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_bit_not	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_bit_and
op_bit_and:		@ operative "$bit-and"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ operation(r0->int32, r1->int32) => int32
	and	r0, r1, r0	@ bitwise r0 & r1
	bx	lr		@ return r0
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	-1		@ 0x18: initial accumulator value
	.int	b_num_op	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_bit_and
ap_bit_and:		@ applicative "bit-and"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_bit_and	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_bit_or
op_bit_or:		@ operative "$bit-or"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ operation(r0->int32, r1->int32) => int32
	orr	r0, r1, r0	@ bitwise r0 | r1
	bx	lr		@ return r0
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: initial accumulator value
	.int	b_num_op	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_bit_or
ap_bit_or:		@ applicative "bit-or"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_bit_or	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_bit_xor
op_bit_xor:		@ operative "$bit-xor"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	@ operation(r0->int32, r1->int32) => int32
	eor	r0, r1, r0	@ bitwise r0 ^ r1
	bx	lr		@ return r0
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: initial accumulator value
	.int	b_num_op	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_bit_xor
ap_bit_xor:		@ applicative "bit-xor"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_bit_xor	@ 0x04: operative
	.int	0		@ 0x08: --
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior
