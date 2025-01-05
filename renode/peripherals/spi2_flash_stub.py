import os
import sys

sys.path.append(os.path.dirname(__file__))
import spi_flash_stub_common as common

common.handle_request(
    request,
    key="spi2_flash_state",
    default_out=0,
    spi_log_enabled=os.environ.get("BC280_SPI2_LOG", "0") == "1",
    load_spi_image=False,
    log_prefix="SPI2 ",
    log_spi_dr=False,
    log_spi_sr=False,
)
