project('fuga', 'c', default_options: ['c_std=gnu23', 'warning_level=2', 'werror=false', 'buildtype=debugoptimized'])

c_args = [
    '-D__DUFAY__',
    '-fno-stack-protector',
    '-fno-pie',
    '-fno-pic',
    '-nostdlib',
    '-ffreestanding',
    '-fsanitize=undefined',
]


add_global_arguments(c_args, language : 'c')

inc = [include_directories('.', 'external')]

src = files('main.c', 'sync_test.c', 'fireworks.c')

dep = dependency('aria', fallback: ['aria', 'aria_dep'])

keyval = import('keyval')
kconfig = configuration_data(keyval.load('.config'))

if (kconfig.has('CONFIG_SCHED_RR'))
    c_args += [
        '-DCONFIG_SCHED_RR',
    ]
elif (kconfig.has('CONFIG_SCHED_ULE'))
    c_args += [
        '-DCONFIG_SCHED_ULE',
    ]
endif


subdir('core')
subdir('arch')
subdir('acpi')
subdir('mm')


executable('fuga', src, include_directories: inc, c_args: c_args, dependencies: dep, link_args: '-Wl,-T@0@'.format(linker_script))
