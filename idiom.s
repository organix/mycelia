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
	.global a_label
a_label:		@ add label to message
	bl	reserve		@ allocate event block
	ldmia	fp, {r2,r4-r9}	@ copy request (drop last word)
	ldr	r3, [ip, #0x1c]	@ insert label
	stmia	r0, {r2-r9}	@ write new event
	b	_a_end		@ queue message and return
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	-1		@ 0x1c: label value

	.text
	.align 5		@ align to cache-line
	.global a_tag
a_tag:			@ label with actor identity
	bl	reserve		@ allocate event block
	ldmia	fp, {r2,r4-r9}	@ copy request (drop last word)
	mov	r3, ip		@ insert label
	stmia	r0, {r2-r9}	@ write new event
	b	_a_end		@ queue message and return
	.int	0		@ 0x14: --
	.int	0		@ 0x18: --
	.int	0		@ 0x1c: --

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
