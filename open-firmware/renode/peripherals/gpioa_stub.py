import System
import os

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")
OFFSET_ATTRS = ("Offset", "offset")


def _get_req(names, default=0):
    for name in names:
        try:
            return getattr(request, name)
        except Exception:
            continue
    return default


def _set_req(names, value):
    for name in names:
        try:
            setattr(request, name, value)
            return True
        except Exception:
            continue
    return False


KEY = "spi1_flash_state"
def _get_spi_state():
    """
    GPIOA drives the SPI flash CS line (PA4).

    IMPORTANT: The SPI flash peripheral is also a PythonPeripheral using the same AppDomain key.
    Do not keep a stale reference across peripheral initializations; always fetch the latest dict.
    """
    st = None
    try:
        st = System.AppDomain.CurrentDomain.GetData(KEY)
    except Exception:
        st = None
    if st is None or not hasattr(st, "get"):
        st = {"cs_active": False, "cs_epoch": 0, "cs_epoch_seen": 0}
        System.AppDomain.CurrentDomain.SetData(KEY, st)
        return st
    # Ensure required keys exist.
    try:
        if "cs_active" not in st:
            st["cs_active"] = False
        if "cs_epoch" not in st:
            st["cs_epoch"] = 0
        if "cs_epoch_seen" not in st:
            st["cs_epoch_seen"] = 0
    except Exception:
        pass
    return st

LEVEL_KEY = "gpio_levels"
levels = System.AppDomain.CurrentDomain.GetData(LEVEL_KEY)
if levels is None or not hasattr(levels, "get"):
    levels = {"A": 0, "B": 0, "C": 0}
    System.AppDomain.CurrentDomain.SetData(LEVEL_KEY, levels)

# Shared GPIO input levels (IDR). Defaults to pulled-up/high.
INPUT_KEY = "gpio_input_levels"
inputs = System.AppDomain.CurrentDomain.GetData(INPUT_KEY)
if inputs is None or not hasattr(inputs, "get"):
    inputs = {"A": 0xFFFF, "B": 0xFFFF, "C": 0xFFFF}
    System.AppDomain.CurrentDomain.SetData(INPUT_KEY, inputs)


def update_levels(set_bits=0, rst_bits=0):
    try:
        cur = int(levels.get("A", 0))
    except Exception:
        cur = 0
    cur |= set_bits & 0xFFFF
    cur &= ~rst_bits & 0xFFFF
    levels["A"] = cur
    System.AppDomain.CurrentDomain.SetData(LEVEL_KEY, levels)


def log(msg):
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "gpioa_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
    except Exception:
        pass


# Minimal GPIOA model: only tracks BSRR/BRR writes for PA4 (SPI flash CS).
# STM32F1-style:
# - BSRR (offset 0x10) sets bits high
# - BSRR bits 16..31 reset bits low
# - BRR  (offset 0x14) resets bits low (alternate)
off = _get_req(OFFSET_ATTRS, 0)
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = _get_req(REQUEST_ATTRS, 0) if is_write else 0

if is_write:
    if off == 0x10:
        state = _get_spi_state()
        # Set PA4 high (inactive)
        if (val & 0x10) != 0:
            if state.get("cs_active"):
                state["cs_active"] = False
                state["cs_epoch"] = int(state.get("cs_epoch", 0)) + 1
                log("PA4 BSRR set -> CS inactive epoch=%d" % int(state.get("cs_epoch", 0)))
            update_levels(set_bits=val & 0xFFFF)
        # Reset PA4 low via upper-halfword (active)
        if (val & (0x10 << 16)) != 0:
            if not state.get("cs_active"):
                state["cs_active"] = True
                state["cs_epoch"] = int(state.get("cs_epoch", 0)) + 1
                log("PA4 BSRR reset -> CS active epoch=%d" % int(state.get("cs_epoch", 0)))
            update_levels(rst_bits=(val >> 16) & 0xFFFF)
        try:
            System.AppDomain.CurrentDomain.SetData(KEY, state)
        except Exception:
            pass
    elif off == 0x14:
        state = _get_spi_state()
        if (val & 0x10) != 0:
            if not state.get("cs_active"):
                state["cs_active"] = True
                state["cs_epoch"] = int(state.get("cs_epoch", 0)) + 1
                log("PA4 BRR -> CS active epoch=%d" % int(state.get("cs_epoch", 0)))
            update_levels(rst_bits=val & 0xFFFF)
        try:
            System.AppDomain.CurrentDomain.SetData(KEY, state)
        except Exception:
            pass
else:
    # Provide sane defaults for input reads. Many boards use pull-ups for buttons/straps.
    if off == 0x08:  # IDR
        try:
            _set_req(REQUEST_ATTRS, int(inputs.get("A", 0xFFFF)) & 0xFFFF)
        except Exception:
            _set_req(REQUEST_ATTRS, 0xFFFF)
    else:
        _set_req(REQUEST_ATTRS, 0)
