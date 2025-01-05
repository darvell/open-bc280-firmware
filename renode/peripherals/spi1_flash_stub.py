import os
import sys

sys.path.append(os.path.dirname(__file__))
import spi_flash_stub_common as common

common.handle_request(
    request,
    key="spi1_flash_state",
    default_out=0xFF,
    spi_log_enabled=os.environ.get("BC280_SPI_LOG", "0") == "1",
    load_spi_image=True,
    log_prefix="",
    log_spi_dr=True,
    log_spi_sr=True,
)
