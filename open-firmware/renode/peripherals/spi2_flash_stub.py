import spi_flash_stub_common as common

common.handle_request(
    request,
    key="spi2_flash_state",
    default_out=0,
    spi_log_enabled=True,
    lcd_log_enabled=True,
    load_spi_image=False,
    log_prefix="SPI2 ",
    log_spi_dr=False,
    log_spi_sr=False,
)
