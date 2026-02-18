# Host-side tests and simulation

# Include directories for tests
test_sim_inc = include_directories('sim')
pixel_inc = include_directories('pixel')

# Source file lists for external reference
sim_sources = files(
  'sim/sim_main.c',
  'sim/sim_mcu.c',
  'sim/sim_uart.c',
  'sim/sim_bike.c',
  'sim/sim_ble.c',
  'sim/sim_protocol.c',
  'sim/sim_shengyi.c',
  'sim/sim_shengyi_bus.c',
  'sim/sim_shengyi_frame.c',
  'sim/sim_shengyi_motor.c',
  '../../src/input/button_fsm.c',
)

pixel_sources = files(
  'pixel/ui_pixel_sink.c',
)

# Host tests (native build only)
if not meson.is_cross_build()

  # Host simulator executable
  host_sim = executable('host_sim',
    sim_sources,
    pixel_sources,
    ui_sources,
    core_sources,
    gfx_sources,
    util_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc, test_sim_inc, pixel_inc],
  )
  alias_target('host_sim', host_sim)

  # Unit test: core algorithms
  test_core_exe = executable('test_core',
    'unit/test_core.c',
    core_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('core', test_core_exe)

  # Unit test: battery SOC curve interpolation (OEM v2.5.1 tables)
  test_batt_soc_exe = executable('test_battery_soc',
    'unit/test_battery_soc.c',
    '../../src/power/battery_soc.c',
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('battery_soc', test_batt_soc_exe)

  # Unit test: UI engineer page
  test_ui_exe = executable('test_ui',
    'unit/test_ui_engineer.c',
    ui_sources,
    gfx_sources,
    core_sources,
    pixel_sources,
    util_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc, pixel_inc],
  )
  test('ui', test_ui_exe)

  # Unit test: Shengyi DWG22 bus protocol
  test_tsbus_exe = executable('test_shengyi_bus',
    'unit/test_shengyi_bus.c',
    'sim/sim_shengyi.c',
    'sim/sim_shengyi_bus.c',
    'sim/sim_shengyi_frame.c',
    'sim/sim_mcu.c',
    core_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc, test_sim_inc],
  )
  test('shengyi_bus', test_tsbus_exe)

  # Unit test: STX02/XOR cmd1 decoder (OEM v2.5.1 Protocol B)
  test_motor_stx02_exe = executable('test_motor_stx02',
    'unit/test_motor_stx02.c',
    '../../src/motor/motor_stx02.c',
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('motor_stx02', test_motor_stx02_exe)

  # Unit test: Event queue (kernel infrastructure)
  test_event_queue_exe = executable('test_event_queue',
    'unit/test_event_queue.c',
    kernel_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('event_queue', test_event_queue_exe)

  # Unit test: Button FSM and input layer
  button_fsm_test_sources = files(
    '../../src/input/gpio_sampler.c',
    '../../src/input/button_fsm.c',
  )
  test_button_fsm_exe = executable('test_button_fsm',
    'unit/test_button_fsm.c',
    button_fsm_test_sources,
    kernel_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('button_fsm', test_button_fsm_exe)

  # Unit test: System control helpers
  test_system_control_exe = executable('test_system_control',
    'unit/test_system_control.c',
    '../../src/system_control.c',
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('system_control', test_system_control_exe)

  # Unit test: A/B update metadata handling
  test_ab_update_exe = executable('test_ab_update',
    'unit/test_ab_update.c',
    '../../storage/ab_update.c',
    '../../util/crc32.c',
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('ab_update', test_ab_update_exe)

  # Unit test: Cooperative scheduler
  test_scheduler_exe = executable('test_scheduler',
    'unit/test_scheduler.c',
    kernel_sources,
    c_args: host_test_defs,
    include_directories: [all_inc, tests_inc],
  )
  test('scheduler', test_scheduler_exe)
endif
