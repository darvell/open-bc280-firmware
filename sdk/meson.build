# Artery AT32F403A/407 SDK integration
# Only compiled for cross-build (target firmware), not host tests

sdk_inc = include_directories(
  'at32f403a_407',
  'at32f403a_407/cmsis',
  'at32f403a_407/drivers/inc',
)

if meson.is_cross_build()
  sdk_sources = files(
    # CMSIS system initialization
    'at32f403a_407/cmsis/system_at32f403a_407.c',

    # Peripheral drivers (only include what we need)
    'at32f403a_407/drivers/src/at32f403a_407_crm.c',
    'at32f403a_407/drivers/src/at32f403a_407_gpio.c',
    'at32f403a_407/drivers/src/at32f403a_407_usart.c',
    'at32f403a_407/drivers/src/at32f403a_407_tmr.c',
    'at32f403a_407/drivers/src/at32f403a_407_xmc.c',
    'at32f403a_407/drivers/src/at32f403a_407_flash.c',
    'at32f403a_407/drivers/src/at32f403a_407_adc.c',
    'at32f403a_407/drivers/src/at32f403a_407_misc.c',
    'at32f403a_407/drivers/src/at32f403a_407_spi.c',
    'at32f403a_407/drivers/src/at32f403a_407_dma.c',
    'at32f403a_407/drivers/src/at32f403a_407_wdt.c',
    'at32f403a_407/drivers/src/at32f403a_407_exint.c',
  )
else
  # For host builds, SDK is not needed (simulation stubs handle peripherals)
  sdk_sources = []
endif
