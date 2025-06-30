src += files('apic.c', 
'cpu.c', 
'debug.c', 
'entry.c',
'gdt.c',
'idt.c',
'paging.c', 
'smp.c',
'hpet.c',
'isr.asm', 
'syscall.asm',
'thread.asm')


c_args += '-mcmodel=kernel'

add_languages('nasm')

linker_script = join_paths(meson.current_source_dir(), 'linker.ld')
