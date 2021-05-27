@
@ shutt.s -- John Shutt's "Kernel" Language predicates
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
	.global b_pred
b_pred:		@ general predicate behavior
			@ (example_5: 0x04=predicate, 0x08=, 0x0c=, 0x10=)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [ip, #0x04]	@	get predicate
	ldr	r1, [fp, #0x0c]	@	get operands
	blx	apply_pred	@	apply predicate to operand list
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
	.global op_boolean_p
op_boolean_p:		@ applicative "$boolean?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	boolean_p	@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_boolean_p
ap_boolean_p:		@ applicative "boolean?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_boolean_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_symbol_p
op_symbol_p:		@ applicative "$symbol?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	symbol_p	@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_symbol_p
ap_symbol_p:		@ applicative "symbol?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_symbol_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_inert_p
op_inert_p:		@ applicative "$inert?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	inert_p		@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_inert_p
ap_inert_p:		@ applicative "inert?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_inert_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_pair_p
op_pair_p:		@ applicative "$pair?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	pair_p		@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_pair_p
ap_pair_p:		@ applicative "pair?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_pair_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_null_p
op_null_p:		@ applicative "$null?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	null_p		@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_null_p
ap_null_p:		@ applicative "null?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_null_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_ignore_p
op_ignore_p:		@ applicative "$ignore?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	ignore_p	@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_ignore_p
ap_ignore_p:		@ applicative "ignore?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_ignore_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_env_p
op_env_p:		@ applicative "$environment?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	environment_p	@ 0x04: predicate (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_pred		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_env_p
ap_env_p:		@ applicative "environment?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_env_p	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global b_rltn
b_rltn:		@ general binary relation behavior
			@ (example_5: 0x04=relation, 0x08=, 0x0c=, 0x10=)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [ip, #0x04]	@	get relation
	ldr	r1, [fp, #0x0c]	@	get operands
	blx	apply_rltn	@	apply relation to operand list
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
	.global op_eq_p
op_eq_p:		@ applicative "$eq?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	eq_p		@ 0x04: relation (in C)
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_rltn		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_eq_p
ap_eq_p:		@ applicative "eq?"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_eq_p		@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior
