@
@ tools.s -- John Shutt's "Kernel" Language tools
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
	.global match_0_args
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
	.global match_1_arg
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
	.global match_2_args
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
	bl	ground_env	@ pre-cache ground environment
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

	.section .rodata
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

	.section .rodata
error_txt:
	.ascii	"#<ERROR>\0"

@
@ unit test actors and behaviors
@

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
