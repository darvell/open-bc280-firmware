import System
import os

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)
PORT = "B"
PERIPH_NAME = "GPIO" + PORT

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

# Logging in Renode PythonPeripherals is *very* expensive if we hit the filesystem
# on every GPIO toggle. Default to disabled; enable explicitly by setting
# `System.AppDomain.CurrentDomain.SetData('gpio_log_ctl', {'enabled': True, 'budget': N})`.
LOG_CTL_KEY = "gpio_log_ctl"

# Shared level map across GPIO peripherals.
LEVEL_KEY = "gpio_levels"
levels = System.AppDomain.CurrentDomain.GetData(LEVEL_KEY)
if levels is None or not hasattr(levels, "get"):
    levels = {"A": 0, "B": 0, "C": 0}
    System.AppDomain.CurrentDomain.SetData(LEVEL_KEY, levels)

# Shared input level map across GPIO peripherals (IDR).
INPUT_KEY = "gpio_input_levels"
inputs = System.AppDomain.CurrentDomain.GetData(INPUT_KEY)
if inputs is None or not hasattr(inputs, "get"):
    inputs = {"A": 0xFFFF, "B": 0xFFFF, "C": 0xFFFF}
    System.AppDomain.CurrentDomain.SetData(INPUT_KEY, inputs)

KEY = "gpio_log_state_" + PERIPH_NAME
state = System.AppDomain.CurrentDomain.GetData(KEY)
if state is None or not hasattr(state, "get"):
    # Determine default logging budget from the optional control key.
    log_budget = 0
    try:
        ctl = System.AppDomain.CurrentDomain.GetData(LOG_CTL_KEY)
        if ctl is None:
            log_budget = 0
        elif hasattr(ctl, "get"):
            if bool(ctl.get("enabled", False)):
                log_budget = int(ctl.get("budget", 2000))
        else:
            # If user sets `gpio_log_ctl` to an int, treat it as a budget.
            log_budget = int(ctl)
    except Exception:
        log_budget = 0
    state = {
        "odr": 0,
        "bsrr": 0,
        "brr": 0,
        "log_budget": log_budget,
        "seq": 0,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, state)


def log(msg):
    if state.get("log_budget", 0) <= 0:
        return
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "gpio_log.txt"), "a") as f:
            f.write("%s %s\n" % (PERIPH_NAME, msg))
        state["log_budget"] -= 1
    except Exception:
        pass


def update_levels(set_bits=0, rst_bits=0):
    try:
        cur = int(levels.get(PORT, 0))
    except Exception:
        cur = 0
    cur |= set_bits & 0xFFFF
    cur &= ~rst_bits & 0xFFFF
    levels[PORT] = cur
    System.AppDomain.CurrentDomain.SetData(LEVEL_KEY, levels)


SPI1_KEY = "spi1_flash_state_gpiob"
def _get_spi1_state():
    st = None
    try:
        st = System.AppDomain.CurrentDomain.GetData(SPI1_KEY)
    except Exception:
        st = None
    if st is None or not hasattr(st, "get"):
        st = {"cs_active": False, "cs_epoch": 0, "cs_epoch_seen": 0}
        System.AppDomain.CurrentDomain.SetData(SPI1_KEY, st)
        return st
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


def _log_cs(msg):
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "gpiob_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
    except Exception:
        pass


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

if is_write:
    if off == 0x0C:  # ODR
        state["odr"] = val & 0xFFFF
        update_levels(set_bits=val & 0xFFFF)
        state["seq"] += 1
        log("seq=%d ODR=0x%04X lvl=0x%04X" % (state["seq"], state["odr"], levels.get(PORT, 0)))
    elif off == 0x10:  # BSRR
        state["bsrr"] = val & 0xFFFFFFFF
        set_bits = val & 0xFFFF
        rst_bits = (val >> 16) & 0xFFFF
        # PB1 drives SPI1 flash CS (active low).
        if (set_bits & 0x0002) or (rst_bits & 0x0002):
            spi1 = _get_spi1_state()
            if set_bits & 0x0002:
                if spi1.get("cs_active"):
                    spi1["cs_active"] = False
                    spi1["cs_epoch"] = int(spi1.get("cs_epoch", 0)) + 1
                    _log_cs("PB1 BSRR set -> SPI1 CS inactive epoch=%d" % int(spi1.get("cs_epoch", 0)))
            if rst_bits & 0x0002:
                if not spi1.get("cs_active"):
                    spi1["cs_active"] = True
                    spi1["cs_epoch"] = int(spi1.get("cs_epoch", 0)) + 1
                    _log_cs("PB1 BSRR reset -> SPI1 CS active epoch=%d" % int(spi1.get("cs_epoch", 0)))
            System.AppDomain.CurrentDomain.SetData(SPI1_KEY, spi1)
        update_levels(set_bits=set_bits, rst_bits=rst_bits)
        state["seq"] += 1
        log("seq=%d BSRR=0x%08X lvl=0x%04X" % (state["seq"], state["bsrr"], levels.get(PORT, 0)))
    elif off == 0x14:  # BRR
        state["brr"] = val & 0xFFFF
        if val & 0x0002:
            spi1 = _get_spi1_state()
            if not spi1.get("cs_active"):
                spi1["cs_active"] = True
                spi1["cs_epoch"] = int(spi1.get("cs_epoch", 0)) + 1
                _log_cs("PB1 BRR -> SPI1 CS active epoch=%d" % int(spi1.get("cs_epoch", 0)))
            System.AppDomain.CurrentDomain.SetData(SPI1_KEY, spi1)
        update_levels(rst_bits=val & 0xFFFF)
        state["seq"] += 1
        log("seq=%d BRR=0x%04X lvl=0x%04X" % (state["seq"], state["brr"], levels.get(PORT, 0)))
else:
    if off == 0x0C:  # ODR
        _set_req(REQUEST_ATTRS, state.get("odr", 0))
    elif off == 0x08:  # IDR (inputs)
        try:
            _set_req(REQUEST_ATTRS, int(inputs.get(PORT, 0xFFFF)) & 0xFFFF)
        except Exception:
            _set_req(REQUEST_ATTRS, 0xFFFF)
    elif off == 0x00:  # CRL
        _set_req(REQUEST_ATTRS, 0x44444444)
    elif off == 0x04:  # CRH
        _set_req(REQUEST_ATTRS, 0x44444444)
    else:
        _set_req(REQUEST_ATTRS, 0)
