OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(main)

SECTIONS
{
	. = 0x0000000000401000;

	.text : ALIGN(0x1000) {
		*(.text)
		*(.init)
		*(.fini)
	}

	. = ALIGN(0x1000);
	.data : ALIGN(0x1000) {
		*(.data)
	}

	. = ALIGN(0x1000);
	.bss : ALIGN(0x1000) {
		*(.bss)
		*(COMMON)
	}

	. = ALIGN(0x1000);
	.ctors : ALIGN(0x1000) {
		*(.ctors)
	}

	. = ALIGN(0x1000);
	.dtors : ALIGN(0x1000) {
		*(.dtors)
	}

	. = ALIGN(0x1000);
	.rodata : ALIGN(0x1000) {
		*(.rodata)
	}

	/DISCARD/ : {
		*(.note.*)
	}
}

