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
	.byte	0xFF, 0x10, 0x84, 0xFF	@ 0x04: --, p_int_0, n_4, --
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
	.byte	0xFF, 0x04, 0x10, 0x84	@ 0x04: --, array, p_int_0, n_4
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
	.byte	0xFF, 0x05, 0x10, 0x84	@ 0x04: --, object, p_int_0, n_4
	.int	0			@ 0x08: size in octets (lsb first)
	.int	0xFFFFFFFF		@ 0x0c: --
	.int	0xFFFFFFFF		@ 0x10: --
	.int	0xFFFFFFFF		@ 0x14: --
	.int	0			@ 0x18: pointer to extended data
	.int	b_value			@ 0x1c: address of actor behavior

	.text
	.align 5		@ align to cache-line
	.global ext_value
ext_value:		@ extended-value template
	ldr	pc, [ip, #0x1c]		@ jump to actor behavior
	.byte	0xFF, 0x0E, 0x10, 0x84	@ 0x04: --, prefix, p_int_0, n_4
	.int	0			@ 0x08: size in octets (lsb first)
	.int	0xFFFFFFFF		@ 0x0c: --
	.int	0xFFFFFFFF		@ 0x10: --
	.int	0xFFFFFFFF		@ 0x14: --
	.int	0			@ 0x18: pointer to extended data
	.int	b_value			@ 0x1c: address of actor behavior

@
@ support procedures
@

	.text
	.global new_u32
new_u32:		@ allocate a new unsigned 32-bit value actor
			@ r0=value
	stmdb	sp!, {r4-r9,lr}	@ save in-use registers
	stmdb	sp!, {r0}	@ push value on stack
	bl	reserve		@ allocate actor block
	ldr	r1, =v_number_0
	ldmia	r1, {r2-r9}	@ copy number template
	ldmia	sp!, {r4}	@ pop value from stack
	stmia	r0, {r2-r9}	@ write actor contents
	ldmia	sp!, {r4-r9,pc}	@ restore in-use registers, and return

	.text
	.global new_int
new_int:		@ allocate a new signed integer value actor
			@ r0=value
	stmdb	sp!, {lr}	@ push return address on stack
	bl	new_u32		@ create positive value actor
	ldr	r1, [r0, #0x08]	@ get value
	teq	r1, #0		@ interpret as signed
	movmi	r2, #0x18	@ if value < 0
	strmib	r2, [r0, #0x05]	@	prefix = n_int_0
	ldmia	sp!, {pc}	@ pop address from stack, and return

	.text
	.global new_octets
new_octets:		@ allocate a new octet string value actor
			@ r0=octet pointer, r1=count
	stmdb	sp!, {r4-r9,lr}	@ save in-use registers
@ <debug>
@	bl	dump_regs
@ </debug>
	stmdb	sp!, {r0-r1}	@ push args on stack
	cmp	r1, #20
	bgt	1f		@ if count <= 20
	bl	reserve		@	allocate actor block
	ldr	r1, =v_string_0
	ldmia	r1, {r2-r9}	@	copy string template
	stmia	r0, {r2-r9}	@	write actor contents

	ldmia	sp!, {r1-r2}	@	pop args from stack
	add	r3, r2, #0x80	@	smol-encoded count
	strb	r3, [r0, #0x06]	@	write count
	add	r3, r0, #0x07	@	string dst pointer

	teq	r2, #0
	beq	2f		@	while (count != 0)
3:
	ldrb	r4, [r1], #1	@		read octet from src
	strb	r4, [r3], #1	@		write octet to dst
	subs	r2, r2, #1	@		decrement count
	bne	3b
2:
	ldmia	sp!, {r4-r9,pc}	@	restore in-use registers, and return
1:				@ else
	bl	reserve		@	allocate actor block
	ldr	r1, =ext_value
	ldmia	r1, {r2-r9}	@	copy extended-value template
	ldmia	sp!, {r1,r4}	@	pop src (r1) and count (r4) from stack
	stmia	r0, {r2-r9}	@	write actor contents
	mov	r2, #0x08	@	prefix = octets
	strb	r2, [r0, #0x05]	@	write prefix

	mov	r5, r1		@	r5 = src
	movs	r6, #12		@	r6 = dst size (12 octets)
	add	r7, r0, #0x0c	@	r7 = dst
	mov	r9, r0		@	r9 = actor
4:				@	loop
	ldrb	r8, [r5], #1	@		read octet from src
	strb	r8, [r7], #1	@		write octet to dst
	subs	r4, r4, #1	@		if (--count == 0)
	beq	5f		@			break;
	subs	r6, r6, #1
	bne	4b		@		if (--size == 0)

	bl	reserve		@			allocate extended block
	str	r0, [r7]	@			link current to new
	mov	r7, r0		@			dst = new block
	mov	r6, #28		@			dst size = 28 octets
	mov	r0, #0
	str	r0, [r7, #0x1c]	@			clear link/next pointer
	b	4b
5:
	mov	r0, r9		@	return actor
	ldmia	sp!, {r4-r9,pc}	@	restore in-use registers, and return

@
@ interactive environment
@

	.text
	.align 5		@ align to cache-line
	.global a_cal_test
a_cal_test:		@ CAL test suite
			@ message = ()
	bl	test_cal	@ run self-test
@	b	complete	@ return to dispatch loop
	b	exit		@ kernel exit

	.text
	.align 5		@ align to machine word
	.global cal_fail
cal_fail:		@ report failure, and call error hook
	bl	dump_regs	@ dump register contents
	ldr	r0, =cal_fail_txt @ load address of error text
	bl	serial_puts	@ write text to console
	bl	serial_eol	@ write end-of-line
	ldr	r0, =cal_err_hook @ address of error hook
	ldr	lr, [r0]	@ load error hook
	bx	lr		@ jump to error hook (no return)
	b	halt		@ halt if we reach this instruction...

	.section .rodata
cal_fail_txt:
	.ascii "*FAIL*\0"

	.data
	.global cal_err_hook
cal_err_hook:
	.int	=exit		@ address of error handler
