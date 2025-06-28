project('dufay', ['c', 'nasm'], default_options: ['c_std=gnu23', 'warning_level=2', 'buildtype=debugoptimized'])

c_args = [
    '-D__DUFAY__',
    '-fno-stack-protector',
    '-fno-pie',
    '-fno-pic',
    '-nostdlib',
    '-ffreestanding',
    '-fsanitize=undefined',
    '-mcmodel=kernel',
]

inc = [include_directories('.', 'external')]

src = files('main.c', 'sync_test.c')

keyval = import('keyval')
kconfig = configuration_data(keyval.load('.config'))

if (kconfig.has('CONFIG_SCHED_RR'))
    c_args += [
        '-DCONFIG_SCHED_RR',
    ]
elif (kconfig.has('CONFIG_SCHED_CFS'))
    c_args += [
        '-DCONFIG_SCHED_CFS',
    ]
endif


subdir('core')
subdir('arch')
subdir('fayt')
subdir('acpi')
subdir('mm')

executable('dufay', src, include_directories: inc, c_args: c_args)