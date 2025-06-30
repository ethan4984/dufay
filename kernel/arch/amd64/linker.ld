OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(amd64_entry)


PHDRS {
	null PT_NULL FLAGS(0);
	text PT_LOAD FLAGS((1 << 0) | (1 << 2));
	rodata  PT_LOAD	FLAGS((1 << 2));
	data PT_LOAD FLAGS((1 << 1) | (1 << 2));
	dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2));
	bss PT_LOAD	FLAGS((1 << 1) | (1 << 2));
}

SECTIONS {
	. = 0xffffffff80200000;

	.text : ALIGN(0x1000) {
		*(.text)
		*(.init)
		*(.fini)
	} :text

	. = ALIGN(0x1000);
	.data : ALIGN(0x1000) {
		*(.data)
	} :data

	. = ALIGN(0x1000);
	.dynamic : {
		*(.dynamic)
	} :data :dynamic

	. = ALIGN(0x1000);
	.rodata : ALIGN(0x1000) {
		*(.rodata)
	} :rodata

	. = ALIGN(0x1000);
	.bss : ALIGN(0x1000) {
		*(COMMON)
		*(.bss)
	} :bss
}
