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

@ CREATE Fail WITH \msg.[ THROW (#Undefined, SELF, msg) ]
	.text
	.align 5		@ align to cache-line
	.global a_fail
a_fail:			@ singleton failure actor
	bl	fail		@ signal an error
	b	complete	@ return to dispatcher
@
@ FIXME!
@	This failure handler allows multiple failures to be reported,
@	but does not ensure that customers, if any, receive a reply.
@	We should implement a top-level error-handler, initially a_exit,
@	that can be called (or sent a message) to signal an error.
@	During sexpr evaluation, it can be a one-shot customer
@	that will either return normally or signal exactly one error
@	and ignore all subsequent responses. This way, if fork/join
@	is still running, it will safely run to completion without
@	confusing a customer that has already received an error signal.
@	We need to think carefully about when a new error handler
@	can be safely installed or removed during asynchronous computation.
@
self_eval:		@ any self-evaluating actor
			@ message = (cust, #S_SELF)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req from message

	teq	r3, #S_SELF
	bne	1f		@ if req == "visit"
	bl	reserve		@	allocate event block
	ldmib	ip, {r2-r8}	@	copy actor state
	stmib	r0, {r2-r8}	@	write into event
	b	_a_reply	@	reply and return
1:
	teq	r3, #S_EVAL	@ else if req != "eval"
	bne	a_fail		@	signal error and return to dispatcher
	bl	reserve		@ allocate event block
	ldr	r1, [fp]	@ get SELF from message
	b	_a_answer	@ actor is self-evaluating

@ LET Self_EVAL = \(cust, req).[
@ 	CASE req OF
@ 	(#eval, _) : [ SEND SELF TO cust ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global a_inert
a_inert:		@ "#inert" singleton
	b	self_eval	@ self-evaluating

	.global a_no_bind
a_no_bind:		@ "#ignore" singleton
	b	self_eval	@ self-evaluating

	.global a_true
a_true:			@ "#t" singleton
	b	self_eval	@ self-evaluating

	.global a_false
a_false:		@ "#f" singleton
	b	self_eval	@ self-evaluating

@ LET Number(int32) = \(cust, req).[
@ 	CASE req OF
@ 	(#eval, env) : [ SEND SELF TO cust ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.global b_number
b_number:		@ integer constant
			@ (example_5: 0x04=int32, 0x08=, 0x0c=)
			@ message = (cust, #S_SELF)
			@         | (cust, #S_EVAL, env)
@	ldr	r3, [fp, #0x08] @ get req
@
@ although numbers are self-evaluating,
@ we need a distinct behavior
@ to serve as a "type" tag
@
	b	self_eval	@ self-evaluating

	.text
	.align 5		@ align to cache-line
	.global n_m1
n_m1:
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	-1		@ 0x04: int32
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_number	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global n_0
n_0:
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	0		@ 0x04: int32
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_number	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global n_1
n_1:
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	1		@ 0x04: int32
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_number	@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global n_2
n_2:
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	2		@ 0x04: int32
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_number	@ 0x1c: address of actor behavior

@ LET Binding(symbol, value, next) = \(cust, req).[
@ 	CASE req OF
@ 	(#lookup, $symbol) : [ SEND value TO cust ]
@ 	(#bind, $symbol, value') : [
@ 		BECOME Binding(symbol, value', next)
@ 		SEND Inert TO cust
@ 	]
@ 	_ : [ SEND (cust, req) TO next ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global b_binding
b_binding:		@ symbol binding
			@ (example_5: 0x04=symbol, 0x08=value, 0x0c=next)
			@ message = (cust, #S_GET, symbol')
			@         | (cust, #S_SET, symbol', value')
	ldmib	ip, {r4-r6}	@ r4=symbol, r5=value, r6=next
	ldr	r3, [fp, #0x08] @ get req

	teq	r3, #S_GET
	bne	1f		@ if req == "get"
	ldr	r3, [fp, #0x0c] @ 	get symbol'
	teq	r3, r4
	bne	1f		@	if symbol == symbol'

	ldr	r0, [fp, #0x04]	@		get cust
	mov	r1, r5		@		get value
	bl	send_1		@		send message
	b	complete	@		return to dispatcher
1:
	teq	r3, #S_SET
	bne	2f		@ else if req == "set"

	ldr	r0, [fp, #0x04]	@	get cust
	ldr	r1, =a_inert	@	message is "#inert"
	bl	send_1		@	send message

	ldr	r5, [fp, #0x10]	@	get value'
	str	r5, [ip, #0x04]	@	set value
	b	complete	@	return to dispatcher
2:
	teq	r3, #S_EVAL
	bne	3f		@ else if req == "eval"
	bl	reserve		@	allocate event block
	mov	r1, ip		@	get SELF
	b	_a_answer	@	was are self-evaluating
3:
	bl	reserve		@ allocate event block
	mov	r1, r6		@ target is next
	ldmia	fp, {r2-r9}	@ copy current event
	stmia	r0, {r2-r9}	@ write new event
	b	_a_send		@ set target, send, and return to dispatcher

	.text
	.align 5		@ align to cache-line
a_NIL_env:
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	s_NIL		@ 0x04: symbol (r4)
	.int	a_nil		@ 0x08: value (r5)
	.int	a_fail		@ 0x0c: next (r6)
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_binding	@ 0x1c: address of actor behavior

@ LET Scope(parent) = \(cust, req).[
@ 	CASE req OF
@ 	(#bind, symbol, value) : [
@ 		CREATE next WITH Scope(parent)
@ 		BECOME Binding(symbol, value, next)
@ 		SEND Inert TO cust
@ 	]
@ 	_ : [ SEND (cust, req) TO parent ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global b_scope
b_scope:		@ binding scope
			@ (example_5: 0x04=, 0x08=, 0x0c=parent)
			@ message = (cust, #S_SET, symbol, value)
	ldr	r4, [ip, #0x0c] @ get parent
	ldr	r3, [fp, #0x08] @ get req

	teq	r3, #S_SET
	bne	1f		@ if req == "set"

	ldr	r0, [fp, #0x04]	@	get cust
	ldr	r1, =a_inert	@	message is "#inert"
	bl	send_1		@	send message

	ldr	r0, =b_scope	@	scope behavior
	bl	create_5	@	create next actor
	str	r4, [r0, #0x0c] @	set parent

	ldr	r5, [fp, #0x0c]	@	get symbol
	ldr	r6, [fp, #0x10]	@	get value
	mov	r7, r0		@	get next
	stmib	ip, {r5-r7}	@
	ldr	r0, =b_binding	@	binding behavior
	ldr	r0, [ip, #0x1c]	@	BECOME

	b	complete	@	return to dispatcher
1:
	teq	r3, #S_EVAL
	bne	2f		@ else if req == "eval"
	bl	reserve		@	allocate event block
	mov	r1, ip		@	get SELF
	b	_a_answer	@	was are self-evaluating
2:
	bl	reserve		@ allocate event block
	mov	r1, r4		@ target is parent
	ldmia	fp, {r2-r9}	@ copy current event
	stmia	r0, {r2-r9}	@ write new event
	b	_a_send		@ set target, send, and return to dispatcher

@ CREATE Nil WITH \(cust, req).[
@ 	CASE req OF
@ 	(#eval, _) : [ SEND SELF TO cust ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global a_nil
a_nil:			@ "()" singleton
			@ message = (cust, #S_SELF)
			@         | (cust, #S_EVAL, env)
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	a_nil		@ 0x04: self reference
	.int	a_nil		@ 0x08: self reference
	.int	a_nil		@ 0x0c: self reference
	.int	a_nil		@ 0x10: self reference
	.int	a_nil		@ 0x14: self reference
	.int	a_nil		@ 0x18: self reference
	.int	self_eval	@ 0x1c: address of actor behavior

@ LET Symbol(name) = \(cust, req).[
@ 	CASE req OF
@ 	(#eval, env) : [ SEND (cust, #lookup, SELF) TO env ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global b_symbol
b_symbol:		@ symbolic name
			@ (example_5: [0x04..0x1b]=name)
			@ message = (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req

	teq	r3, #S_SELF
	bne	1f		@ if req == "visit"
	bl	reserve		@	allocate event block
	ldmib	ip, {r2-r8}	@	copy actor state
	stmib	r0, {r2-r8}	@	write into event
	b	_a_reply	@	reply and return
1:
	teq	r3, #S_EVAL
	bne	2f		@ else if req == "eval"
	bl	reserve		@	allocate event block
	ldr	r4, [fp, #0x0c] @	get env
	ldr	r5, [fp, #0x04] @	get cust
	mov	r6, #S_GET
	mov	r7, ip		@	get SELF
	stmia	r0, {r4-r7}	@	write new event
	b	_a_end		@	lookup name in environment
2:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

	.text
	.align 5		@ align to cache-line
s_NIL:
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.ascii	"NIL\0"		@ 0x04: state field 1
	.int	0		@ 0x08: state field 6
	.int	0		@ 0x0c: state field 2
	.int	0		@ 0x10: state field 3
	.int	0		@ 0x14: state field 4
	.int	0		@ 0x18: state field 5
	.int	b_symbol	@ 0x1c: address of actor behavior

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
b_pair:			@ pair combination
			@ (example_5: 0x04=left, 0x08=right, 0x0c=)
			@ message = (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req

	teq	r3, #S_SELF
	bne	1f		@ if req == "visit"
	bl	reserve		@	allocate event block
	ldmib	ip, {r2-r8}	@	copy actor state
	stmib	r0, {r2-r8}	@	write into event
	b	_a_reply	@	reply and return
1:
	teq	r3, #S_EVAL
	bne	2f		@ else if req == "eval"
	ldr	r0, =k_comb
	bl	create_5	@	create k_comb actor
	ldr	r6, [fp, #0x04] @	get cust
	mov	r7, #S_APPL
	ldr	r8, [ip, #0x08]	@	get right
	ldr	r9, [fp, #0x0c]	@	get env
	stmib	r0, {r6-r9}	@	write actor state

	ldr	r4, [ip, #0x04]	@	get left
	mov	r5, r0		@	k_comb
	mov	r6, #S_EVAL
	mov	r7, r9		@	env
	bl	send_3x		@	send message to left
	b	complete	@	return to dispatch loop
2:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

k_comb:			@ combiner continuation
			@ (example_5: 0x04=cust, 0x08=#S_APPL, 0x0c=right, 0x10=env)
			@ message = (combiner)
	mov	r0, ip		@ continuation actor becomes an event...
	b	_a_reply	@ delegate to combiner

@
@ built-in operatives and applicatives
@

	.text
	.align 5		@ align to cache-line
	.global op_list
op_list:		@ operative "$list"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	bl	reserve		@	allocate event block
	ldr	r1, [fp, #0x0c]	@	get operands
	b	_a_answer	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
k_list:			@ operative call continuation
			@ (example_5: 0x04=cust, 0x08=oper, 0x0c=env)
			@ message = (args)
	ldr	r4, [ip, #0x08]	@ target = oper
	ldr	r5, [ip, #0x04]	@ cust
	mov	r6, #S_APPL	@ "combine"
	ldr	r7, [fp, #0x04]	@ args
	ldr	r8, [ip, #0x0c]	@ env
	bl	reserve		@ allocate event block
	stmia	r0, {r4-r8}	@ write data to event
	b	_a_end		@ send and return

	.text
	.align 5		@ align to cache-line
	.global b_appl
b_appl:			@ applicative wrapper behavior
			@ (example_5: 0x04=oper, 0x08=, 0x0c=)
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_OPER)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r5, [fp, #0x04]	@	get cust
	ldr	r6, [ip, #0x04]	@	get operative
	ldr	r7, [fp, #0x10] @	get env

	ldr	r0, =k_list	@	k_cust behavior
	bl	create_5	@	create k_cust
	stmib	r0, {r5-r7}	@	store cust, oper, env
	mov	r5, r0		@	cust = k_cust

	bl	reserve		@	allocate event block
	ldr	r4, =a_map_eval	@	target
@	mov	r5, r5		@	cust (already in r5)
	ldr	r6, [fp, #0x0c]	@	list = opnds
	ldr	r7, [fp, #0x10] @	env
	stmia	r0, {r4-r7}	@	write data to event
	b	_a_end		@	send and return
1:
	teq	r3, #S_OPER	@ else if req != "unwrap" self-evaluate
	bne	self_eval	@ else
	bl	reserve		@	allocate event block
	ldr	r1, [ip, #0x04]	@	get operative  [FIXME: "unwrap" may get oper directly...]
	b	_a_answer	@	send to customer and return

	.text
	.align 5		@ align to cache-line
	.global ap_list
ap_list:		@ applicative "list"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_list		@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ap_list
a_map_eval:		@ sequential list evaluator
			@ message = (cust, list, env)
	ldr	r0, [fp, #0x04]	@ get cust
	ldr	r1, [fp, #0x08]	@ get list
	ldr	r2, =a_nil	@ get ()
	teq	r1, r2
	bne	1f		@ if list == ()
	bl	send_1		@	send () to cust
	b	complete	@	return
1:				@ else
	ldr	r7, [fp, #0x0c] @	save env
	mov	r8, r0		@	save cust
	mov	r9, r1		@	save list

	mov	r0, r9		@	get list
	bl	car
	movs	r4, r0		@	target = car(list)
	beq	a_kernel_err	@	if (target == NULL) signal error

	mov	r0, r9		@	get list
	bl	cdr
	movs	r5, r0		@	tail = cdr(list)
	beq	a_kernel_err	@	if (tail == NULL) signal error

	ldr	r0, =k_map_eval_1 @	k_cust behavior
	bl	create_5	@	create k_cust
	str	r8, [r0, #0x04] @	store cust
	str	r5, [r0, #0x08] @	store tail
	str	r7, [r0, #0x0c] @	store env

	mov	r5, r0		@	cust = k_cust
	bl	reserve		@	allocate event block
	mov	r6, #S_EVAL	@	"eval"
	stmia	r0, {r4-r7}	@	write data to event
	b	_a_end		@	send and return

k_map_eval_1:		@ eval arg continuation
			@ (example_5: 0x04=cust, 0x08=tail, 0x0c=env)
			@ message = (arg)
	bl	reserve		@ allocate event block
	ldr	r4, =a_map_eval	@ target
	mov	r5, ip		@ k_cust = SELF
	ldr	r6, [ip, #0x08]	@ list
	ldr	r7, [ip, #0x0c] @ env
	stmia	r0, {r4-r7}	@ write data to event
	bl	enqueue		@ send message

	ldr	r0, [fp, #0x04]	@ get arg
	str	r0, [ip, #0x08]	@ store arg
	ldr	r0, =k_map_eval_2 @ become...
	str	r0, [ip, #0x1c]	@ ...k_map_eval_2

	b	complete	@ return to dispatch loop

k_map_eval_2:		@ eval args continuation
			@ (example_5: 0x04=cust, 0x08=arg, 0x0c=)
			@ message = (args)
	bl	reserve		@ allocate event block
	ldr	r1, [ip, #0x04]	@ get cust

	ldr	r2, [ip, #0x08]	@ get arg
	ldr	r3, [fp, #0x04]	@ get args

	str	r2, [ip, #0x04]	@ store left
	str	r3, [ip, #0x08]	@ store right
	ldr	r3, =b_pair	@ become...
	str	r3, [ip, #0x1c]	@ ...b_pair

	str	ip, [r0, #0x04]	@ message is SELF
	b	_a_send		@ send to customer and return

	.text
	.align 5		@ align to cache-line
	.global op_wrap
op_wrap:	@ operative "$wrap"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =combiner_p	@	predicate
	bl	match_1_arg
	mov	r4, r0		@	save combiner

	ldr	r0, =b_appl	@	applicative behavior
	bl	create_5	@	create applicative
	str	r4, [r0, #0x04]	@	set combiner
	mov	r4, r0		@	save applicative

	bl	reserve		@	allocate event block
	str	r4, [r0, #0x04]	@	reply with applicative
	b	_a_reply	@	send reply and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_wrap
ap_wrap:		@ applicative "wrap"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_wrap		@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_vau
op_vau:			@ operative "$vau"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	2f		@ if req == "combine"
	ldr	r9, [fp, #0x0c] @	get opnds

	mov	r0, r9
	bl	car		@	formals
	movs	r4, r0		@	if NULL
	beq	a_kernel_err	@		signal error

	mov	r0, r9
	bl	cdr		@	next opnd
	movs	r9, r0		@	if NULL
	beq	a_kernel_err	@		signal error

@	mov	r0, r9
	bl	car		@	eformal
	movs	r5, r0		@	if NULL
	beq	a_kernel_err	@		signal error

	ldr	r1, =a_no_bind
	teq	r0, r1
	beq	1f		@	if (eformal != #ignore)
	bl	symbol_p
	teq	r0, #0		@	&& !symbol_p(eformal)
	beq	a_kernel_err	@		signal error
1:
	mov	r0, r9
	bl	cdr		@	body
	movs	r6, r0		@	if NULL
	beq	a_kernel_err	@		signal error

	ldr	r7, [fp, #0x10] @	get env

	ldr	r0, =b_oper
	bl	create_5	@	create operative
	stmib	r0, {r4-r7}	@	store formals, eformal, body, env
	mov	r8, r0

	bl	reserve		@	allocate event block
	str	r8, [r0, #0x04]	@	reply with operative
	b	_a_reply	@	send reply and return
2:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global b_oper
b_oper:			@ compound operative behavior
			@ (example_5: 0x04=formals, 0x08=eformal, 0x0c=body, 0x10=senv)
			@ message = (cust, #S_APPL, opnds, denv)
			@         | (cust, #S_OPER)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	2f		@ if req == "combine"
			@ (example_5: 0x04=, 0x08=, 0x0c=parent)
	ldr	r0, =b_scope	@	empty env behavior
	bl	create_5	@	create "local" env
	ldr	r1, [ip, #0x10]	@	get senv
	str	r1, [r0, #0x0c]	@	set parent

	mov	r4, r0		@	save local env
	ldr	r5, [ip, #0x04]	@	save formals
	ldr	r6, [fp, #0x0c]	@	save opnds

	ldr	r0, [ip, #0x08]	@	get eformal
	ldr	r1, =a_no_bind
	teq	r0, r1
	beq	1f		@	if (eformal != #ignore)

@	mov	r0, r0		@	get eformal (already in r0)
	mov	r1, r5		@	get formals
	bl	cons		@	cons(eformal, formals)
	movs	r5, r0		@	update formals
	beq	a_kernel_err	@	if NULL signal error

	ldr	r0, [fp, #0x10] @	get denv
	mov	r1, r6		@	get opnds
	bl	cons		@	cons(eformal, opnds)
	movs	r6, r0		@	update opnds
	beq	a_kernel_err	@	if NULL signal error
1:
	mov	r0, r5		@	def = formals
	mov	r1, r6		@	arg = opnds
	mov	r2, r4		@	env = local env
	bl	match_param_tree
	teq	r0, #0		@ if NULL
	beq	a_kernel_err	@	signal error

@	mov	r0, r0		@	get ext env (already in r0)
	mov	r1, r4		@	get local env
	bl	mutate_environment
	teq	r0, #0		@ if NULL
	beq	a_kernel_err	@	signal error

	ldr	r5, [fp, #0x04]	@	get cust
	ldr	r6, =a_inert	@	result = #inert
	mov	r7, r0		@	env = local env
	ldr	r0, =v_sequence	@	k_seq behavior
	bl	create_5	@	create k_seq
	stmib	r0, {r5-r7}	@	store cust, result, env
	mov	r5, r0		@	cust = k_seq

	bl	reserve		@	allocate event block
	ldr	r4, [ip, #0x0c]	@	target = body
@	mov	r5, r5		@	cust (already in r5)
	mov	r6, #S_SELF	@	"visit"
	stmia	r0, {r4-r6}	@	write data to event
	b	_a_end		@	send and return
2:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global v_sequence
v_sequence:		@ sequential evaluation visitor
			@ (example_5: 0x04=cust, 0x08=result, 0x0c=env, 0x10=next)
			@ message = (0x04..0x18=state, 0x1c=behavior)
	ldr	r0, [fp, #0x1c]	@ get behavior (type signature)

	ldr	r1, =self_eval
	teq	r0, r1
	bne	1f		@ if null_p(message)
			@ (example_5: 0x04=a_nil, 0x08=a_nil)
	ldr	r4, [ip, #0x04]	@	target = cust
	ldr	r5, [ip, #0x08]	@	answer = result
	mov	r0, ip		@	re-use k_seq as final message
	stmia	r0, {r4-r5}	@	write data to event
	b	_a_end		@	send and return
1:
	ldr	r1, =b_pair
	teq	r0, r1
	bne	2f		@ else if pair_p(message)
			@ (example_5: 0x04=left, 0x08=right)
	ldr	r0, [fp, #0x08]	@	cdr(message)
	str	r0, [ip, #0x10]	@	store next
	ldr	r0, =k_seq_eval	@	new behavior
	str	r0, [ip, #0x1c]	@	BECOME

	bl	reserve		@	allocate event block
	ldr	r4, [fp, #0x04]	@	target = car(message)
	mov	r5, ip		@	cust = SELF
	mov	r6, #S_EVAL	@	"eval"
	ldr	r7, [ip, #0x0c]	@	env
	stmia	r0, {r4-r7}	@	write data to event
	b	_a_end		@	send and return
2:
	b	a_kernel_err	@ else signal error

k_seq_eval:		@ sequential evaluation continuation
			@ (example_5: 0x04=cust, 0x08=result, 0x0c=env, 0x10=next)
			@ message = (result')
	ldr	r0, [fp, #0x04]	@ get result'
	str	r0, [ip, #0x08]	@ update result
	ldr	r0, =v_sequence	@ new behavior
	str	r0, [ip, #0x1c]	@ BECOME

	bl	reserve		@ allocate event block
	ldr	r4, [ip, #0x10]	@ target = next
	mov	r5, ip		@ cust = SELF
	mov	r6, #S_SELF	@ "visit"
	stmia	r0, {r4-r6}	@ write data to event
	b	_a_end		@ send and return

	.text
	.align 5		@ align to cache-line
	.global op_unwrap
op_unwrap:	@ operative "$unwrap"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =applicative_p @	predicate
	bl	match_1_arg
			@ (example_5: 0x04=oper, 0x08=, 0x0c=)
	ldr	r4, [r0, #0x04]	@	get combiner

	bl	reserve		@	allocate event block
	mov	r1, r4		@	result = combiner
	b	_a_answer	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_unwrap
ap_unwrap:		@ applicative "unwrap"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_unwrap	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_define
op_define:		@ operative "$define!"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =object_p	@	predicate 1
	ldr	r2, =object_p	@	predicate 2
	bl	match_2_args
	mov	r6, r0		@	definiend
	mov	r4, r1		@	expression

	ldr	r0, =k_bind
	bl	create_5	@	create k_bind actor
	ldr	r5, [fp, #0x04] @	get cust
@	mov	r6, r6		@	get definiend (already in r6)
	ldr	r7, [fp, #0x10]	@	get env
	stmib	r0, {r5-r7}	@	write actor state

@	mov	r4, r4		@	target = expression (already in r4)
	mov	r5, r0		@	cust = k_bind
	mov	r6, #S_EVAL
@	ldr	r7, [fp, #0x0c]	@	get env (already in r7)
	bl	reserve		@	allocate event block
	stmia	r0, {r4-r7}	@	write message
	b	_a_end		@	send message and return
1:
	b	self_eval	@ else we are self-evaluating

k_bind:			@ binding continuation
			@ (example_5: 0x04=cust, 0x08=definiend, 0x0c=env)
			@ message = (arg)
	ldr	r0, [ip, #0x08]	@ get def
	ldr	r1, [fp, #0x04]	@ get arg
	ldr	r2, [ip, #0x0c]	@ get env
	bl	match_param_tree
	teq	r0, #0		@ if NULL
	beq	a_kernel_err	@	signal error

	ldr	r1, [ip, #0x0c]	@ get env
	bl	mutate_environment
	teq	r0, #0		@ if NULL
	beq	a_kernel_err	@	signal error

	ldr	r1, [ip, #0x04]	@ get cust
	mov	r0, ip		@ continuation actor becomes an event...
	ldr	r2, =a_inert	@ message is #inert
	str	r2, [ip, #0x04]
	b	_a_send		@ send message and return

	.text
	.align 5		@ align to cache-line
	.global op_sequence
op_sequence:	@ operative "$sequence"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r5, [fp, #0x04]	@	get cust
	ldr	r6, =a_inert	@	result = #inert
	ldr	r7, [fp, #0x10]	@	get env
	ldr	r0, =v_sequence	@	k_seq behavior
	bl	create_5	@	create k_seq
	stmib	r0, {r5-r7}	@	store cust, result, env
	mov	r5, r0		@	cust = k_seq

	bl	reserve		@	allocate event block
	ldr	r4, [fp, #0x0c]	@	target = opnds
@	mov	r5, r5		@	cust (already in r5)
	mov	r6, #S_SELF	@	"visit"
	stmia	r0, {r4-r6}	@	write data to event
	b	_a_end		@	send and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global op_lambda
op_lambda:		@ operative "$lambda"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r9, [fp, #0x0c] @	get opnds

	mov	r0, r9
	bl	car		@	formals
	movs	r4, r0		@	if NULL
	beq	a_kernel_err	@		signal error

	ldr	r5, =a_no_bind	@	eformal (always #ignore)

	mov	r0, r9
	bl	cdr		@	body
	movs	r6, r0		@	if NULL
	beq	a_kernel_err	@		signal error

	ldr	r7, [fp, #0x10] @	get env

	ldr	r0, =b_oper
	bl	create_5	@	create operative
	stmib	r0, {r4-r7}	@	store formals, eformal, body, env
	mov	r8, r0

	ldr	r0, =b_appl	@	applicative behavior
	bl	create_5	@	applicative
	str	r8, [r0, #0x04]	@	combiner = operative
	mov	r9, r0

	bl	reserve		@	allocate event block
	str	r9, [r0, #0x04]	@	reply with applicative
	b	_a_reply	@	send reply and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
match_0_args:		@ match arg signature (op)
			@ or signal an error
			@ (r0=args)
	stmdb	sp!, {r4,lr}	@ preserve in-use registers

	ldr	r4, =a_nil
	teq	r0, r4
	bne	1f		@ if cdr(args) == ()

	ldmia	sp!, {r4,pc}	@	restore in-use registers and return
1:				@ else
	ldmia	sp!, {r4,lr}	@	restore in-use registers
	b	a_kernel_err	@	and signal error

	.text
match_1_arg:		@ match arg signature (op pred?)
			@ or signal an error
			@ (r0=args, r1=pred?)
	stmdb	sp!, {r4-r5,lr} @ preserve in-use registers
	mov	r4, r0		@ args

	bl	car
	movs	r5, r0
	beq	1f		@ if car(args) != NULL

@	blx	r1
@	teq	r0, #0
@	beq	1f		@ and pred(car(args))

	mov	r0, r4
	bl	cdr
	teq	r0, #0
	beq	1f		@ and cdr(args) != NULL

	ldr	r4, =a_nil
	teq	r0, r4
	bne	1f		@ and cdr(args) == ()

	mov	r0, r5		@	r0 = first arg
	ldmia	sp!, {r4-r5,pc}	@	restore in-use registers and return
1:				@ else
	ldmia	sp!, {r4-r5,lr}	@	restore in-use registers
	b	a_kernel_err	@	and signal error

	.text
match_2_args:		@ match arg signature (op pred_1? pred_2?)
			@ or signal an error
			@ (r0=args, r1=pred_1?, r2=pred_2?)
	stmdb	sp!, {r4-r6,lr}	@ preserve in-use registers
	mov	r4, r0		@ args

	bl	car
	movs	r5, r0
	beq	1f		@ if car(args) != NULL

@	blx	r1
@	teq	r0, #0
@	beq	1f		@ and pred_1(car(args))

	mov	r0, r4
	bl	cdr
	teq	r0, #0
	beq	1f		@ and cdr(args) != NULL

	mov	r4, r0
	bl	car
	movs	r6, r0
	beq	1f		@ if car(cdr(args)) != NULL

@	blx	r2
@	teq	r0, #0
@	beq	1f		@ and pred_2(car(cdr(args)))

	mov	r0, r4
	bl	cdr
	teq	r0, #0
	beq	1f		@ and cdr(cdr(args)) != NULL

	ldr	r4, =a_nil
	teq	r0, r4
	bne	1f		@ and cdr(cdr(args)) == ()

	mov	r0, r5		@	r0 = first arg
	mov	r1, r6		@	r1 = second arg
	ldmia	sp!, {r4-r6,pc}	@	restore in-use registers and return
1:				@ else
	ldmia	sp!, {r4-r6,lr}	@	restore in-use registers
	b	a_kernel_err	@	and signal error

	.text
	.align 5		@ align to cache-line
	.global op_boolean_p
op_boolean_p:	@ operative "$boolean?"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =boolean_p	@	get predicate
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
op_symbol_p:	@ operative "$symbol?"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =symbol_p	@	get predicate
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
op_inert_p:	@ operative "$inert?"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =inert_p	@	get predicate
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
op_pair_p:	@ operative "$pair?"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =pair_p	@	get predicate
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
op_null_p:	@ operative "$null?"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =null_p	@	get predicate
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
	.global op_eq_p
op_eq_p:	@ operative "$eq?"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, =eq_p	@	get relation
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

	.text
	.align 5		@ align to cache-line
	.global op_address_of
op_address_of:	@ operative "$address-of"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =object_p	@	predicate
	bl	match_1_arg
	mov	r4, r0		@	save object

	ldr	r0, =b_number	@	number behavior
	bl	create_5	@	create number
	str	r4, [r0, #0x04]	@	set value
	mov	r4, r0		@	save number

	bl	reserve		@	allocate event block
	str	r4, [r0, #0x04]	@	reply with number
	b	_a_reply	@	send reply and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_address_of
ap_address_of:		@ applicative "address-of"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_address_of	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_dump_bytes
op_dump_bytes:		@ operative "$dump-bytes"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate 1
	ldr	r2, =number_p	@	predicate 2
	bl	match_2_args
	ldr	r0, [r0, #0x04]	@	get numeric value of address
	ldr	r1, [r1, #0x04]	@	get numeric value of count
	bl	hexdump		@	call helper procedure
	bl	reserve		@	allocate event block
	ldr	r1, =a_inert	@	result is #inert
	b	_a_answer	@	send result to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_dump_bytes
ap_dump_bytes:		@ applicative "dump-bytes"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_dump_bytes	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_dump_words
op_dump_words:		@ operative "$dump-words"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate 1
	ldr	r2, =number_p	@	predicate 2
	bl	match_2_args
	ldr	r0, [r0, #0x04]	@	get numeric value of address
	ldr	r1, [r1, #0x04]	@	get numeric value of count
	bl	dump_words	@	call helper procedure
	bl	reserve		@	allocate event block
	ldr	r1, =a_inert	@	result is #inert
	b	_a_answer	@	send result to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_dump_words
ap_dump_words:		@ applicative "dump-words"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_dump_words	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_load_words
op_load_words:		@ operative "$load-words"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate 1
	ldr	r2, =number_p	@	predicate 2
	bl	match_2_args
	ldr	r0, [r0, #0x04]	@	get numeric value of address
	ldr	r1, [r1, #0x04]	@	get numeric value of count
	bl	load_words	@	load into list
	mov	r4, r0		@	save result
	bl	reserve		@	allocate event block
	str	r4, [r0, #0x04]	@	get result
	b	_a_reply	@	send to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_load_words
ap_load_words:		@ applicative "load-words"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_load_words	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_store_words
op_store_words:		@ operative "$store-words"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"
	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate 1
	ldr	r2, =list_p	@	predicate 2
	bl	match_2_args
	ldr	r0, [r0, #0x04]	@	get numeric value of address
	bl	store_words	@	store from list (in r1)
	bl	reserve		@	allocate event block
	ldr	r1, =a_inert	@	result is #inert
	b	_a_answer	@	send result to customer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_store_words
ap_store_words:		@ applicative "store-words"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_store_words	@ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_sponsor_reserve
op_sponsor_reserve:	@ operative "$sponsor-reserve"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r0, [fp, #0x0c] @	get opnds
	bl	match_0_args

	bl	reserve		@	allocate 32-byte block
	mov	r4, r0		@	save block address

	ldr	r0, =b_number	@	number behavior
	bl	create_5	@	create number
	str	r4, [r0, #0x04]	@	set block address
	mov	r4, r0		@	save number

	bl	reserve		@	allocate event block
	str	r4, [r0, #0x04]	@	reply with number
	b	_a_reply	@	send reply and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_sponsor_reserve
ap_sponsor_reserve:	@ applicative "sponsor-reserve"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_sponsor_reserve @ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global op_sponsor_release
op_sponsor_release:	@ operative "$sponsor-release"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate
	bl	match_1_arg
	ldr	r0, [r0, #0x04]	@	get numeric value of address

	bl	release		@	deallocate memory block

	bl	reserve		@	allocate event block
	ldr	r1, =a_inert	@	answer is #inert
	b	_a_answer	@	send answer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_sponsor_release
ap_sponsor_release:	@ applicative "sponsor-release"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_sponsor_release @ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior


	.text
	.align 5		@ align to cache-line
	.global op_sponsor_enqueue
op_sponsor_enqueue:	@ operative "$sponsor-enqueue"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r0, [fp, #0x0c] @	get opnds
	ldr	r1, =number_p	@	predicate
	bl	match_1_arg
	ldr	r0, [r0, #0x04]	@	get numeric value of address

	bl	enqueue		@	add event to queue

	bl	reserve		@	allocate event block
	ldr	r1, =a_inert	@	answer is #inert
	b	_a_answer	@	send answer and return
1:
	b	self_eval	@ else we are self-evaluating

	.text
	.align 5		@ align to cache-line
	.global ap_sponsor_enqueue
ap_sponsor_enqueue:	@ applicative "sponsor-enqueue"
	ldr	pc, [ip, #0x1c]	@ jump to actor behavior
	.int	op_sponsor_enqueue @ 0x04: operative
	.int	0		@ 0x08: -
	.int	0		@ 0x0c: --
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	b_appl		@ 0x1c: address of actor behavior

@ void
@ kernel_repl()
@ {
@     flush_char();
@     for (;;) {
@         puts("> ");
@         ACTOR* x = parse_sexpr();
@         if (x == NULL) break;
@         puts("= ");
@         print_sexpr(x);
@         putchar('\n');
@     }
@ }

	.text
	.align 5		@ align to cache-line
	.global a_kernel_repl
a_kernel_repl:		@ kernel read-eval-print loop
			@ message = ()
	bl	flush_char	@ flush pending/buffered input
	ldr	r0, =a_kernel_read @ target actor
	ldr	r1, =a_kernel_eval @ ok customer
	ldr	r2, =a_kernel_err @ fail customer
	bl	send_2		@ send message
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_kernel_read
a_kernel_read:		@ kernel read actor
			@ message = (ok, fail)
	bl	serial_eol	@ write end-of-line
	ldr	r0, =prompt_txt	@ load address of prompt
	bl	serial_puts	@ write text to console
	bl	parse_sexpr	@ parse s-expression from input
	movs	r1, r0		@ if expr == NULL
	ldreq	r0, [fp, #0x08]	@	send to fail
	ldrne	r0, [fp, #0x04]	@ else
	bl	send_1		@	send expr to ok
	b	complete	@ return to dispatch loop
prompt_txt:
	.ascii	"> \0"

	.text
	.align 5		@ align to cache-line
	.global a_kernel_eval
a_kernel_eval:		@ kernel eval actor
			@ message = (expr)
	bl	ground_env	@ get ground environment
	ldr	r4, [fp, #0x04]	@ get expr to eval
	ldr	r5, =a_kernel_print @ customer
	mov	r6, #S_EVAL	@ "eval" request
	mov	r7, r0		@ environment
	bl	send_3x		@ send message [FIXME: set watchdog timer...]
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_kernel_print
a_kernel_print:		@ kernel print actor
			@ message = (value)
	ldr	r0, [fp, #0x04]	@ get value from message
	bl	print_sexpr	@ print s-expression
	bl	serial_eol	@ write end-of-line
	ldr	r0, =a_kernel_read @ target actor
	ldr	r1, =a_kernel_eval @ ok customer
	ldr	r2, =a_kernel_err @ fail customer
	bl	send_2		@ send message
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_empty_env
a_empty_env:		@ empty environment
			@ message = (cust, #S_GET, symbol')
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_GET
	bne	a_kernel_err	@ if req == "get"

	ldr	r0, =undefined_txt @	load address of text
	bl	serial_puts	@	write text to console

	ldr	r0, [fp, #0x0c] @ 	get symbol'
	add	r0, r0, #0x04	@	get symbol name
	bl	serial_puts	@	write name to console

	mov	r0, #0x3e	@	'>' character
	bl	serial_write	@	write character to console
	bl	serial_eol	@	write end-of-line
	b	a_kernel_repl	@	re-enter REPL
undefined_txt:
	.ascii	"#<UNDEFINED \0"

	.text
	.align 5		@ align to cache-line
	.global a_kernel_err
a_kernel_err:		@ kernel error handler
			@ message = ()
	ldr	r0, =error_txt	@ load address of text
	bl	serial_puts	@ write text to console
	bl	serial_eol	@ write end-of-line
	b	a_kernel_repl	@ re-enter REPL
error_txt:
	.ascii	"#<ERROR>\0"

@
@ unit test actors and behaviors
@

	.text
	.align 5		@ align to cache-line
	.global b_kernel_t0
b_kernel_t0:		@ kernel unit test
			@ message = (ok, fail)
	ldr	r4, [fp, #0x04]	@ get ok customer
	ldr	r5, [fp, #0x08]	@ get fail customer
	ldr	r6, =a_inert	@ expected result value
	ldr	r7, =b_match_t	@ result value matching behavior
	bl	create_3x	@ create matching actor

	ldr	r4, =a_inert	@ target (expression to evaluate)
	mov	r5, r0		@ customer
	mov	r6, #S_EVAL	@ "eval" request
	ldr	r7, =a_fail	@ environment
	bl	send_3x		@ send message

	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_kernel_t1
b_kernel_t1:		@ kernel unit test
			@ message = (ok, fail)
	ldr	r4, [fp, #0x04]	@ get ok customer
	ldr	r5, [fp, #0x08]	@ get fail customer
	ldr	r6, =a_nil	@ expected result value
	ldr	r7, =b_match_t	@ result value matching behavior
	bl	create_3x	@ create matching actor

	ldr	r4, =a_nil	@ target (expression to evaluate)
	mov	r5, r0		@ customer
	mov	r6, #S_EVAL	@ "eval" request
	ldr	r7, =a_fail	@ environment
	bl	send_3x		@ send message

	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_kernel_t
b_kernel_t:		@ kernel unit test
			@ message = (ok, fail)
	ldr	r4, [fp, #0x04]	@ get ok customer
	ldr	r5, [fp, #0x08]	@ get fail customer
	ldr	r6, =a_nil	@ expected result value
	ldr	r7, =b_match_t	@ result value matching behavior
	bl	create_3x	@ create matching actor

	ldr	r4, =s_NIL	@ target (expression to evaluate)
	mov	r5, r0		@ customer
	mov	r6, #S_EVAL	@ "eval" request
	ldr	r7, =a_NIL_env	@ environment
	bl	send_3x		@ send message

	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global b_match_t
b_match_t:		@ result value matching (template_3: r4=ok, r5=fail, r6=expect)
			@ message = (actual)
	bl	reserve		@ allocate event block
	ldr	r3, [fp, #0x04]	@ get actual result
	teq	r3, r6		@ if (actual == expect)
	moveq	r1, r4		@	ok
	movne	r1, r5		@ else
	b	_a_send		@	fail
