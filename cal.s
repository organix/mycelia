@
@ cal.s -- Core Actor Language
@
@ Copyright 2020-2021 Dale Schumacher
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
	.set S_FIND, 0x0B
	.set S_CMP,  0x0C
	.set S_PTRN, 0x0D
	.set S_TYPE, 0x0E

	.text
	.align 5		@ align to cache-line
	.global b_value
b_value:		@ behavior for literal-value actors
	b	self_eval	@ delegate to self-eval behavior

	.text
	.align 5		@ align to cache-line
	.global v_null
v_null:			@ literal value "null"
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0xFF, 0xFF, 0xFF	@ 0x04: prefix/value
	.byte	0xFF, 0xFF, 0xFF, 0xFF	@ 0x08: --
	.byte	0xFF, 0xFF, 0xFF, 0xFF	@ 0x0c: --
	.byte	0xFF, 0xFF, 0xFF, 0xFF	@ 0x10: --
	.byte	0xFF, 0xFF, 0xFF, 0xFF	@ 0x14: --
	.byte	0xFF, 0xFF, 0xFF, 0xFF	@ 0x18: --
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global v_false
v_false:		@ literal value "false"
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0x00, 0x00, 0x00, 0x00	@ 0x04: prefix/value
	.byte	0x00, 0x00, 0x00, 0x00	@ 0x08: --
	.byte	0x00, 0x00, 0x00, 0x00	@ 0x0c: --
	.byte	0x00, 0x00, 0x00, 0x00	@ 0x10: --
	.byte	0x00, 0x00, 0x00, 0x00	@ 0x14: --
	.byte	0x00, 0x00, 0x00, 0x00	@ 0x18: --
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global v_true
v_true:			@ literal value "true"
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0x01, 0x01, 0x01, 0x01	@ 0x04: prefix/value
	.byte	0x01, 0x01, 0x01, 0x01	@ 0x08: --
	.byte	0x01, 0x01, 0x01, 0x01	@ 0x0c: --
	.byte	0x01, 0x01, 0x01, 0x01	@ 0x10: --
	.byte	0x01, 0x01, 0x01, 0x01	@ 0x14: --
	.byte	0x01, 0x01, 0x01, 0x01	@ 0x18: --
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global v_number_0
v_number_0:		@ literal number zero
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0x10, 0x86, 0xFF	@ 0x04: --, +int_0, n_6, --
	.int	0			@ 0x08: 32-bit integer
	.int	0			@ 0x0c: 32-bit exponent
	.int	10			@ 0x10: 32-bit base
	.int	0xFFFFFFFF		@ 0x14: --
	.int	0xFFFFFFFF		@ 0x18: --
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global v_string_0
v_string_0:		@ literal empty string
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0x08, 0x80, 0x00	@ 0x04: --, octets, n_0, '\0'
	.int	0			@ 0x08: 4x'\0'
	.int	0			@ 0x0c: 4x'\0'
	.int	0			@ 0x10: 4x'\0'
	.int	0			@ 0x14: 4x'\0'
	.int	0			@ 0x18: 4x'\0'
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global v_array_0
v_array_0:		@ literal empty array
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0x04, 0x10, 0x84	@ 0x04: --, array, +int_0, n_4
	.int	0			@ 0x08: size in octets (lsb first)
	.int	0xFFFFFFFF		@ 0x0c: --
	.int	0xFFFFFFFF		@ 0x10: --
	.int	0xFFFFFFFF		@ 0x14: --
	.int	0			@ 0x18: pointer to extended data
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global v_object_0
v_object_0:		@ literal empty object
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0x05, 0x10, 0x84	@ 0x04: --, object, +int_0, n_4
	.int	0			@ 0x08: size in octets (lsb first)
	.int	0xFFFFFFFF		@ 0x0c: --
	.int	0xFFFFFFFF		@ 0x10: --
	.int	0xFFFFFFFF		@ 0x14: --
	.int	0			@ 0x18: pointer to extended data
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global x_value
x_value:			@ extended-value template
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0x0E, 0x10, 0x84	@ 0x04: --, prefix, +int_0, n_4
	.int	0			@ 0x08: size in octets (lsb first)
	.int	0xFFFFFFFF		@ 0x0c: --
	.int	0xFFFFFFFF		@ 0x10: --
	.int	0xFFFFFFFF		@ 0x14: --
	.int	0			@ 0x18: pointer to extended data
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global a_bose_test
a_bose_test:		@ BOSE self-test
			@ message = ()
	bl	test_bose	@ run test suite
	ldr	r0, =a_exit	@ target actor
	bl	send_0		@ send message
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_bose_read
a_bose_read:		@ BOSE read actor
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
	.global a_bose_eval
a_bose_eval:		@ BOSE eval actor
			@ message = (expr)
	bl	ground_env	@ get ground environment
	ldr	r4, [fp, #0x04]	@ get expr to eval
	ldr	r5, =a_bose_print @ customer
	mov	r6, #S_EVAL	@ "eval" request
	mov	r7, r0		@ environment
	bl	send_3x		@ send message [FIXME: set watchdog timer...]
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_bose_print
a_bose_print:		@ BOSE print actor
			@ message = (value)
	ldr	r0, [fp, #0x04]	@ get value from message
	bl	print_sexpr	@ print s-expression
	bl	serial_eol	@ write end-of-line
	ldr	r0, =a_bose_read @ target actor
	ldr	r1, =a_bose_eval @ ok customer
	ldr	r2, =a_bose_err @ fail customer
	bl	send_2		@ send message
	b	complete	@ return to dispatch loop

	.text
	.align 5		@ align to cache-line
	.global a_bose_err
a_bose_err:		@ BOSE error handler
			@ message = ()
	ldr	r0, =error_txt	@ load address of text
	bl	serial_puts	@ write text to console
	bl	serial_eol	@ write end-of-line
	b	a_bose_test	@ re-enter REPL

	.section .rodata
error_txt:
	.ascii	"#<ERROR>\0"
