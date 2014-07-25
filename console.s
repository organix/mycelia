@
@ console.s -- Actors for serial console i/o
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
	.global a_poll
a_poll:			@ poll for serial i/o ()
	add	r1, ip, #0x14	@ event template address
	ldmia	r1, {r4-r6}	@ get (target, ok, fail)
	bl	reserve		@ allocate event block
	stmia	r0, {r4-r6}	@ set (target, ok, fail)
	b	_a_end		@ queue message
	.int	a_in_ready	@ 0x14: target actor
	.int	a_do_in		@ 0x18: ok customer
	.int	a_poll		@ 0x1c: fail customer

	.text
	.align 5		@ align to cache-line
	.global a_in_ready
a_in_ready:		@ check for serial input (ok, fail)
	bl	reserve		@ allocate event block
	ldr	r2, [ip, #0x1c]	@ UART0
	ldr	r3, [r2, #0x18]	@ UART0->FR
	tst	r3, #0x10	@ FR.RXFE
	ldreq	r1, [fp, #0x04]	@ if ready, notify ok customer
	ldrne	r1, [fp, #0x08]	@ otherwise, notify fail customer
	b	_a_send		@ send message
	.int	0x20201000	@ 0x1c: UART0 base address

	.text
	.align 5		@ align to cache-line
	.global a_do_in
a_do_in:		@ request input ()
	add	r1, ip, #0x18	@ event template address
	ldmia	r1, {r4,r5}	@ get (target, cust)
	bl	reserve		@ allocate event block
	stmia	r0, {r4,r5}	@ set (target, cust)
	b	_a_end		@ queue message
	.int	0		@ 0x14: --
	.int	a_char_in	@ 0x18: target actor
	.int	a_do_out	@ 0x1c: customer

	.text
	.align 5		@ align to cache-line
	.global a_char_in
a_char_in:		@ read serial input (cust)
	bl	reserve		@ allocate event block
	ldr	r2, [ip, #0x1c]	@ UART0
	ldr	r1, [r2]	@ UART0->DR
	b	_a_answer	@ answer and return
	.int	0		@ 0x10: --
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0x20201000	@ 0x1c: UART0 base address

	.text
	.align 5		@ align to cache-line
	.global a_do_out
a_do_out:		@ request output (char)
	ldr	r4, [ip, #0x18]	@ get target
	ldr	r5, [ip, #0x1c]	@ get customer
	ldr	r6, [fp, #0x04]	@ get character
	bl	reserve		@ allocate event block
	stmia	r0, {r4-r6}	@ set (target, cust, char)
	b	_a_end		@ queue message
	.int	a_char_out	@ 0x18: target actor
	.int	a_poll		@ 0x1c: customer

	.text
	.align 5		@ align to cache-line
	.global a_char_out
a_char_out:		@ write serial output (cust, char)
	ldr	r1, [fp, #0x08]	@ character
	ldr	r2, [ip, #0x1c]	@ UART0
	str	r1, [r2]	@ UART0->DR
	bl	reserve		@ allocate event block
	b	_a_reply	@ reply and return
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0x20201000	@ 0x1c: UART0 base address
