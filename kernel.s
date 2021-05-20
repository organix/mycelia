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
			@ message = (cust, #S_EVAL, _)
	ldr	r3, [fp, #0x08] @ get req from message
	teq	r3, #S_EVAL	@ if req != "eval"
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
			@ (template_3: r4=symbol, r5=value, r6=next)
			@ message = (cust, req, ...)
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
	bne	2f		@ if req == "set"

	ldr	r0, [fp, #0x04]	@	get cust
	ldr	r1, =a_inert	@	message is "#inert"
	bl	send_1		@	send message

	ldr	r5, [fp, #0x10]	@	get value'
	str	r5, [ip, #0x04]	@	set value
	b	complete	@	return to dispatcher
2:
	bl	reserve		@ allocate event block
	mov	r1, r6		@ target is next
	ldmia	fp, {r2-r9}	@ copy current event
	stmia	r0, {r2-r9}	@ write new event
	b	_a_send		@ set target, send, and return to dispatcher

	.text
	.align 5		@ align to cache-line
a_NIL_env:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4-r6,pc}	@ copy state and jump to behavior
	.int	s_NIL		@ 0x08: r4=symbol
	.int	a_nil		@ 0x0c: r5=value
	.int	a_fail		@ 0x10: r6=next
	.int	b_binding	@ 0x14: address of actor behavior

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
			@ (template_3: r4=parent, r5=n/a, r6=n/a)
			@ message = (cust, req, ...)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_SET
	bne	1f		@ if req == "set"

	ldr	r0, [fp, #0x04]	@	get cust
	ldr	r1, =a_inert	@	message is "#inert"
	bl	send_1		@	send message

	ldr	r7, =b_scope	@	scope behavior
	bl	create_3x	@	create next actor (r4=parent, ...)

	ldr	r4, [fp, #0x0c]	@	get symbol
	ldr	r5, [fp, #0x10]	@	get value
	mov	r6, r0		@	get next
	ldr	r7, =b_binding	@	binding behavior
	stmia	ip, {r4-r7}	@	BECOME
	b	complete	@	return to dispatcher
1:
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
			@ message = (cust, #S_EVAL, _)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_EVAL
	bne	1f		@ if req == "eval"
	bl	reserve		@	allocate event block
	mov	r1, ip		@	get SELF
	b	_a_answer	@	"()" is self-evaluating
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
b_symbol:		@ symbolic name
			@ (example_1: [0x08..0x1f]=name)
			@ message = (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_EVAL
	bne	1f		@ if req == "eval"
	bl	reserve		@	allocate event block
	ldr	r4, [fp, #0x0c] @	get env
	ldr	r5, [fp, #0x04] @	get cust
	mov	r6, #S_GET
	mov	r7, ip		@	get SELF
	stmia	r0, {r4-r7}	@	write new event
	b	_a_end		@	lookup name in environment
1:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

	.text
	.align 5		@ align to cache-line
s_NIL:
	ldr	pc, [ip, #4]	@ jump to actor behavior
	.int	b_symbol	@ 0x04: address of actor behavior
	.ascii	"NIL\0"		@ 0x08: state field 1
	.int	0		@ 0x0c: state field 2
	.int	0		@ 0x10: state field 3
	.int	0		@ 0x14: state field 4
	.int	0		@ 0x18: state field 5
	.int	0		@ 0x1c: state field 6

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
	teq	r3, #S_EVAL
	bne	1f		@ if req == "eval"

	ldr	r0, =k_comb
	bl	create		@	create k_comb actor
	ldr	r6, [fp, #0x04] @	get cust
	mov	r7, #S_APPL
	ldr	r8, [ip, #0x08]	@	get right
	ldr	r9, [fp, #0x0c]	@	get env
	add	r1, r0, #0x08
	stmia	r1, {r6-r9}	@	write actor state

	ldr	r4, [ip, #0x04]	@	get left
	mov	r5, r0		@	k_comb
	mov	r6, #S_EVAL
	mov	r7, r9		@	env
	bl	send_3x		@	send message to left
	b	complete	@	return to dispatch loop
1:
	bl	fail		@ else
	b	complete	@	signal error and return to dispatcher

k_comb:			@ combiner continuation
			@ (example_3: r4=cust, r5=#S_APPL, r6=right, r7=env)
			@ message = (combiner)
	bl	reserve		@ allocate event block
	ldr	r3, [fp, #0x04] @ get combiner
	stmia	r0, {r3-r7}	@ write new event
	b	_a_end		@ delegate to operative

@ LET Number(int32) = \(cust, req).[
@ 	CASE req OF
@ 	(#eval, env) : [ SEND SELF TO cust ]
@ 	_ : [ THROW (#Not-Understood, SELF, req) ]
@ 	END
@ ]
	.text
	.align 5		@ align to cache-line
	.global b_number
b_number:		@ integer constant
			@ (template_1: r4=int32)
			@ message = (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_SELF
	bne	1f		@ if req == "self"

	ldr	r0, [fp, #0x04]	@	get cust
	mov	r0, r4		@	get int32
	bl	send_1		@	send message
	b	complete	@	return to dispatcher
1:
	b	self_eval	@ self-evaluating

	.text
	.align 5		@ align to cache-line
	.global n_m1
n_m1:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4,pc}	@ copy state and jump to behavior
	.int	-1		@ 0x08: r4=int32
	.int	b_number	@ 0x0c: address of actor behavior
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global n_0
n_0:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4,pc}	@ copy state and jump to behavior
	.int	0		@ 0x08: r4=int32
	.int	b_number	@ 0x0c: address of actor behavior
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global n_1
n_1:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4,pc}	@ copy state and jump to behavior
	.int	1		@ 0x08: r4=int32
	.int	b_number	@ 0x0c: address of actor behavior
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

	.text
	.align 5		@ align to cache-line
	.global n_2
n_2:
	mov	ip, pc		@ point ip to data fields (state)
	ldmia	ip, {r4,pc}	@ copy state and jump to behavior
	.int	2		@ 0x08: r4=int32
	.int	b_number	@ 0x0c: address of actor behavior
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

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

k_list:			@ operative call continuation
			@ (example_5: 0x04=cust, 0x08=env, 0x0c=)
			@ message = (args)
	ldr	r4, =op_list	@ target
	ldr	r5, [ip, #0x04]	@ cust
	mov	r6, #S_APPL	@ "combine"
	ldr	r7, [fp, #0x04]	@ args
	ldr	r8, [ip, #0x08]	@ env
	bl	reserve		@ allocate event block
	stmia	r0, {r4-r8}	@ write data to event
	b	_a_end		@ send and return

	.text
	.align 5		@ align to cache-line
	.global ap_list
ap_list:		@ applicative "list"
			@ message = (cust, #S_APPL, opnds, env)
			@         | (cust, #S_OPER)
			@         | (cust, #S_EVAL, env)
	ldr	r3, [fp, #0x08] @ get req
	teq	r3, #S_APPL
	bne	1f		@ if req == "combine"

	ldr	r5, [fp, #0x04]	@	get cust
	ldr	r7, [fp, #0x10] @	get env

	ldr	r0, =k_list	@	k_cust behavior
	bl	create_5	@	create k_cust
	str	r5, [r0, #0x04] @	store cust
	str	r7, [r0, #0x08] @	store env

	ldr	r4, =a_map_eval	@	target
	mov	r5, r0		@	cust = k_cust
	ldr	r6, [fp, #0x0c]	@	list = opnds
	ldr	r7, [fp, #0x10] @	env
	bl	send_3x		@	send message

	b	complete	@	return
1:
	teq	r3, #S_OPER	@ else if req != "unwrap" self-evaluate
	bne	self_eval	@ else
	bl	reserve		@	allocate event block
	ldr	r1, =op_list	@	get operative
	b	_a_answer	@	send to customer and return

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

@	mov	r4, r4		@	target (already in r4)
	mov	r5, r0		@	k_cust
	mov	r6, #S_EVAL	@	"eval"
@	mov	r7, r7		@	env (already in r7)
	bl	send_3x		@	send message

	b	complete	@ return to dispatch loop

k_map_eval_1:		@ eval arg continuation
			@ (example_5: 0x04=cust, 0x08=tail, 0x0c=env)
			@ message = (arg)
	ldr	r4, =a_map_eval	@ target
	mov	r5, ip		@ k_cust = SELF
	ldr	r6, [ip, #0x08]	@ list
	ldr	r7, [ip, #0x0c] @ env
	bl	send_3x		@ send message

	ldr	r0, [fp, #0x04]	@ get arg
	str	r0, [ip, #0x08]	@ store arg
	ldr	r0, =k_map_eval_2 @ become...
	str	r0, [ip, #0x1c]	@ ...k_map_eval_2

	b	complete	@ return to dispatch loop

k_map_eval_2:		@ eval args continuation
			@ (example_5: 0x04=cust, 0x08=arg, 0x0c=)
			@ message = (args)
	ldr	r0, [ip, #0x08]	@ get arg
	ldr	r1, [fp, #0x04]	@ get args
	bl	cons
@ FIXME: when Pair is converted to example_5, we can "become" the Pair...
	mov	r1, r0		@ move message
	ldr	r0, [ip, #0x04]	@ get cust
	bl	send_1		@ cons(arg, args) to cust

	b	complete	@ return to dispatch loop

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
