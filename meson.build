project('open-firmware', 'c',
  version: '1.0.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'optimization=2',
  ]
)

# No build-time feature toggles; firmware is single-path.
common_defs = []

# AT32F403A SDK configuration (cross-build only)
at32_defs = ['-DAT32F403ARGT7', '-DUSE_STDPERIPH_DRIVER']

# Host test configuration
host_test_defs = common_defs + ['-DHOST_TEST=1', '-DUI_TRACE_REGEN=1', '-DUI_PIXEL_SIM=1']

# Include directories
root_inc = include_directories('.')
src_inc = include_directories('src')
src_core_inc = include_directories('src/core')
src_motor_inc = include_directories('src/motor')
src_ble_inc = include_directories('src/ble')
src_power_inc = include_directories('src/power')
src_control_inc = include_directories('src/control')
src_input_inc = include_directories('src/input')
src_config_inc = include_directories('src/config')
src_telemetry_inc = include_directories('src/telemetry')
src_comm_inc = include_directories('src/comm')
src_bus_inc = include_directories('src/bus')
src_profiles_inc = include_directories('src/profiles')
src_kernel_inc = include_directories('src/kernel')
ui_inc = include_directories('ui')
gfx_inc = include_directories('gfx')
platform_inc = include_directories('platform')
drivers_inc = include_directories('drivers')
storage_inc = include_directories('storage')
util_inc = include_directories('util')
tests_inc = include_directories('tests/host')

all_inc = [root_inc, src_inc, src_core_inc, src_motor_inc, src_ble_inc, src_power_inc, src_control_inc, src_input_inc, src_config_inc, src_telemetry_inc, src_comm_inc, src_bus_inc, src_profiles_inc, src_kernel_inc, ui_inc, gfx_inc, platform_inc, drivers_inc, storage_inc, util_inc]

# Subdirectories for source modules
subdir('util')
subdir('platform')
subdir('drivers')
subdir('storage')
subdir('gfx')
subdir('ui')
subdir('src/core')
subdir('src/motor')
subdir('src/power')
subdir('src/control')
subdir('src/input')
subdir('src/config')
subdir('src/profiles')
subdir('src/telemetry')
subdir('src/comm')
subdir('src/bus')
subdir('src/kernel')
subdir('src/ble')
subdir('src')
subdir('sdk')
subdir('libc')
subdir('tests/host')

# Firmware target (cross-compile only)
if meson.is_cross_build()
  subdir('startup')

  firmware_srcs = [
    app_sources,
    core_sources,
    motor_sources,
    power_sources,
    control_sources,
    input_sources,
    config_sources,
    profiles_sources,
    telemetry_sources,
    comm_sources,
    bus_sources,
    kernel_sources,
    ble_sources,
    ui_sources,
    gfx_sources,
    platform_sources,
    driver_sources,
    storage_sources,
    util_sources,
    startup_sources,
    sdk_sources,
    libc_sources,
  ]

  firmware_c_args = common_defs + at32_defs + [
    '-ffreestanding',
    '-fno-builtin',
    '-nostdlib',
    '-fno-exceptions',
    '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables',
    '-fno-stack-protector',
    '-ffunction-sections',
    '-fdata-sections',
    '-Oz',
  ]

  firmware_elf = executable('open_firmware',
    firmware_srcs,
    c_args: firmware_c_args,
    link_args: ['-T', meson.current_source_dir() / 'startup/link_at32f403a.ld', '-Wl,--gc-sections', '-nostdlib'],
    link_depends: files('startup/link_at32f403a.ld'),
    include_directories: [libc_inc] + all_inc + [sdk_inc],
  )

  firmware_bin = custom_target('open_firmware.bin',
    input: firmware_elf,
    output: 'open_firmware.bin',
    command: [find_program('arm-none-eabi-objcopy'), '-O', 'binary', '@INPUT@', '@OUTPUT@'],
    build_by_default: true,
  )
endif
