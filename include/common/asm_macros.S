/*
 * Copyright (c) 2013-2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch.h>


	.macro	func_prologue
	stp	x29, x30, [sp, #-0x10]!
	mov	x29,sp
	.endm

	.macro	func_epilogue
	ldp	x29, x30, [sp], #0x10
	.endm


	.macro	dcache_line_size  reg, tmp
	mrs	\tmp, ctr_el0
	ubfx	\tmp, \tmp, #16, #4
	mov	\reg, #4
	lsl	\reg, \reg, \tmp
	.endm


	.macro	icache_line_size  reg, tmp
	mrs	\tmp, ctr_el0
	and	\tmp, \tmp, #0xf
	mov	\reg, #4
	lsl	\reg, \reg, \tmp
	.endm


	.macro	smc_check  label
	bl	read_esr
	ubfx	x0, x0, #ESR_EC_SHIFT, #ESR_EC_LENGTH
	cmp	x0, #EC_AARCH64_SMC
	b.ne	$label
	.endm


	.macro	setup_dcsw_op_args  start_level, end_level, clidr, shift, fw, ls
	mrs	\clidr, clidr_el1
	mov	\start_level, xzr
	ubfx	\end_level, \clidr, \shift, \fw
	lsl	\end_level, \end_level, \ls
	.endm

	/*
	 * This macro verifies that the a given vector doesn't exceed the
	 * architectural limit of 32 instructions. This is meant to be placed
	 * immedately after the last instruction in the vector. It takes the
	 * vector entry as the parameter
	 */
	.macro check_vector_size since
	  .if (. - \since) > (32 * 4)
	    .error "Vector exceeds 32 instructions"
	  .endif
	.endm

	/*
	 * This macro is used to create a function label and place the
	 * code into a separate text section based on the function name
	 * to enable elimination of unused code during linking
	 */
	.macro func _name
	.section .text.\_name, "ax"
	.type \_name, %function
	\_name:
	.endm

	/*
	 * This macro declares an array of 1 or more stacks, properly
	 * aligned and in the requested section
	 */
#define STACK_ALIGN	6

	.macro declare_stack _name, _section, _size, _count
	.if ((\_size & ((1 << STACK_ALIGN) - 1)) <> 0)
	  .error "Stack size not correctly aligned"
	.endif
	.section    \_section, "aw", %nobits
	.align STACK_ALIGN
	\_name:
	.space ((\_count) * (\_size)), 0
	.endm

	/*
	 * This macro calculates the base address of an MP stack using the
	 * platform_get_core_pos() index, the name of the stack storage and
	 * the size of each stack
	 * In: X0 = MPIDR of CPU whose stack is wanted
	 * Out: X0 = physical address of stack base
	 * Clobber: X30, X1, X2
	 */
	.macro get_mp_stack _name, _size
	bl  platform_get_core_pos
	ldr x2, =(\_name + \_size)
	mov x1, #\_size
	madd x0, x0, x1, x2
	.endm

	/*
	 * This macro calculates the base address of a UP stack using the
	 * name of the stack storage and the size of the stack
	 * Out: X0 = physical address of stack base
	 */
	.macro get_up_stack _name, _size
	ldr x0, =(\_name + \_size)
	.endm
