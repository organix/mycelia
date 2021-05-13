@
@ kernel.s -- John Shutt's "Kernel" Language core
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

	.set S_SELF, 0x00
	.set S_GET,  0x01
	.set S_SET,  0x02
	.set S_EVAL, 0x03
	.set S_APPL, 0x04
	.set S_OPER, 0x05
	.set S_EXEC, 0x06
	.set S_PUSH, 0x07
	.set S_POP,  0x08
	.set S_PUT,  0x09
	.set S_PULL, 0x0A

@ CREATE Nil WITH \(cust, req).[
@ 	CASE req OF
@ 	(#eval, _) : [ SEND SELF TO cust ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global a_nil
a_nil:			@ () singleton
			@ message = (cust, #S_EVAL, _)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_EVAL
	bne	1f		@ if req == "eval"
	bl	reserve		@	allocate event block
	mov	r1, ip		@	get SELF
	b	_a_answer	@	() is self-evaluating
1:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

@ LET Symbol(name) = \(cust, req).[
@ 	CASE req OF
@ 	(#eval, env) : [ SEND (cust, #lookup, SELF) TO env ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global b_symbol
b_symbol:		@ symbolic name (example_1: [0x08..0x1f]=name)
			@ message = (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_EVAL
	bne	1f		@ if req == "eval"
	bl	reserve		@	allocate event block
	ldr	r4, [fp, #0x0c] @	get env
	ldr	r5, [fp, #0x04] @	get cust
	ldr	r6, #S_GET
	mov	r7, ip		@	get SELF
	stmia	r0, {r4-r7}	@	write new event
	b	_a_end		@	lookup name in environment
1:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

@ LET Pair(left, right) = \(cust, req).[
@ 	CASE req OF
@ 	(#eval, env) : [
@ 		SEND (k_comb, #eval, env) TO left
@ 		CREATE k_comb WITH \comb.[
@ 			SEND (cust, #comb, right, env) TO comb
@ 		]
@ 	]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global b_pair
b_pair:			@ pair combination (template_2: r4=left, r5=right)
			@ message = (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_EVAL
	bne	1f		@ if req == "eval"

	ldr	r0, =k_oper
	bl	create		@	create k_oper actor
	ldr	r6, [fp, #0x04] @	get cust
	ldr	r7, #S_OPER
	mov	r8, r5		@	get right
	ldr	r9, [fp, #0x0c]	@	get env
	add	r1, r0, #0x08
	stmia	r1, {r6-r9}	@	write actor state

	mov	r5, r0		@	k_oper
	ldr	r6, #S_EVAL
	mov	r7, r9		@	env
	bl	send_3x		@	send message to left (r4)
	b	complete	@	return to dispatch loop
1:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

k_oper:			@ operative continuation (example_3: r4=cust, r5=#S_OPER, r6=right, r7=env)
			@ message = (oper)
	bl	reserve		@ allocate event block
	ldr	r3, [fp, #0x04] @ get oper
	stmia	r0, {r3-r7}	@ write new event
	b	_a_end		@ delegate to operative
