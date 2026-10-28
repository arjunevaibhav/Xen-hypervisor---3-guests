/* FreeRTOS Xen guest: Xen loads ARM32 zImages at guest RAM base + 0x8000 */
ENTRY(_start)

MEMORY
{
	RAM (rwx) : ORIGIN = 0x40008000, LENGTH = 4M - 32K
}

SECTIONS
{
	.text : {
		KEEP(*(.text.boot))
		*(.text .text.*)
	} > RAM

	.rodata : ALIGN(4) { *(.rodata .rodata.*) } > RAM

	.ARM.exidx : { *(.ARM.exidx*) } > RAM

	.data : ALIGN(4) { *(.data .data.*) } > RAM

	.bss (NOLOAD) : ALIGN(8) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(8);
		__bss_end = .;
	} > RAM

	/* Exception mode stacks (the tasks have their own stacks on the heap) */
	.stacks (NOLOAD) : ALIGN(8) {
		. += 0x1000; __irq_stack_top = .;
		. += 0x400;  __abt_stack_top = .;
		. += 0x400;  __und_stack_top = .;
		. += 0x400;  __sys_stack_top = .;
		. += 0x1000; __svc_stack_top = .;
	} > RAM

	_end = .;

	/DISCARD/ : { *(.comment) *(.note*) }
}
