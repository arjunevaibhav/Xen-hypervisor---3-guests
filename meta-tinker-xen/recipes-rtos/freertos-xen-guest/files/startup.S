/*
 * Start-up code for FreeRTOS running as a 32-bit ARM Xen guest (domU).
 *
 * Xen's toolstack loads an ARM32 "zImage" at guest RAM base + 0x8000
 * (0x40008000) and jumps to the address stored at offset 0x28 of the image.
 * The vCPU starts in SVC mode with the MMU and caches off; r2 holds the
 * address of the guest device tree (unused here).
 */
	.syntax unified
	.arm
	.fpu	neon-vfpv4

	.section .text.boot, "ax"
	.global	_start
_start:
	.rept	8
	mov	r0, r0
	.endr
	b	reset			@ 0x20
	.word	0x016f2818		@ 0x24: zImage magic
	.word	_start			@ 0x28: absolute load/run address (entry)
	.word	_end			@ 0x2c: end address
	.word	0x04030201		@ 0x30: little-endian marker

reset:
	cpsid	if

	/* Use our vector table (SCTLR.V = 0), ARM exceptions (SCTLR.TE = 0) */
	ldr	r0, =vectors
	mcr	p15, 0, r0, c12, c0, 0		@ VBAR
	mrc	p15, 0, r0, c1, c0, 0		@ SCTLR
	bic	r0, r0, #(1 << 13)
	bic	r0, r0, #(1 << 30)
	mcr	p15, 0, r0, c1, c0, 0
	isb

	/* One stack per exception mode, finish in SVC mode */
	cps	#0x12				@ IRQ
	ldr	sp, =__irq_stack_top
	cps	#0x17				@ ABT
	ldr	sp, =__abt_stack_top
	cps	#0x1b				@ UND
	ldr	sp, =__und_stack_top
	cps	#0x1f				@ SYS
	ldr	sp, =__sys_stack_top
	cps	#0x13				@ SVC
	ldr	sp, =__svc_stack_top

	/* Enable VFP/NEON: FreeRTOS saves the FPU context of every task */
	mrc	p15, 0, r0, c1, c0, 2		@ CPACR
	orr	r0, r0, #(0xf << 20)		@ cp10 + cp11 full access
	mcr	p15, 0, r0, c1, c0, 2
	isb
	mov	r0, #(1 << 30)			@ FPEXC.EN
	vmsr	fpexc, r0

	/* Clear .bss */
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b

	bl	main
2:	wfi
	b	2b

/* Exception vectors: SVC and IRQ go to the FreeRTOS port, faults are reported */
	.align	5
vectors:
	ldr	pc, =reset
	ldr	pc, =undef_entry
	ldr	pc, =FreeRTOS_SWI_Handler
	ldr	pc, =pabt_entry
	ldr	pc, =dabt_entry
	nop
	ldr	pc, =FreeRTOS_IRQ_Handler
	ldr	pc, =fiq_entry

undef_entry:
	mov	r0, #1
	sub	r1, lr, #4
	b	fault_report
pabt_entry:
	mov	r0, #3
	sub	r1, lr, #4
	b	fault_report
dabt_entry:
	mov	r0, #4
	sub	r1, lr, #8
	b	fault_report
fiq_entry:
	mov	r0, #7
	sub	r1, lr, #4
	b	fault_report

	.ltorg
