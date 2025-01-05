import os
import System

# Allow overriding the log directory; default to repo-local renode/output.
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)

KEY = "rcc_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)


def _init_state():
    # Seed with "clocks already stable" to keep firmware progressing.
    # This is a pragmatic model (not cycle-accurate).
    s = {
        "CR": 0x03020083,   # HSION|HSIRDY|HSEON|HSERDY|PLLON|PLLRDY
        "CFGR": 0x001D040A, # includes SW=PLL and SWS=PLL (SWS is forced on read)
        "APB2ENR": 0xFFFFFFFF,
        "APB1ENR": 0xFFFFFFFF,
        # CSR reset flags default to POR on cold boot (bit27).
        "CSR": (1 << 27),
        "log_budget": 200,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get") or state.get("CR") is None:
    state = _init_state()

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


def _log(msg):
    if state.get("log_budget", 0) <= 0:
        return
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "rcc_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] -= 1
    except Exception:
        pass

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")
OFFSET_ATTRS = ("Offset", "offset")


def _cr_with_ready_bits(cr):
    # RCC_CR ready bits are read-only on real hardware; model them as always reflecting enable bits.
    HSION = 1 << 0
    HSIRDY = 1 << 1
    HSEON = 1 << 16
    HSERDY = 1 << 17
    PLLON = 1 << 24
    PLLRDY = 1 << 25

    v = int(cr) & 0xFFFFFFFF
    if v & HSION:
        v |= HSIRDY
    if v & HSEON:
        v |= HSERDY
    if v & PLLON:
        v |= PLLRDY
    return v


def _cfgr_with_status_bits(cfgr):
    # RCC_CFGR SWS bits [3:2] mirror SW bits [1:0].
    v = int(cfgr) & 0xFFFFFFFF
    sw = v & 0x3
    v &= ~0xC
    v |= (sw << 2) & 0xC
    return v


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

if is_write:
    if off == 0x00:
        # RCC_CR: allow writes to enable/config bits, but keep RDY bits read-only.
        # Writable bits we care about: HSION (0), HSEON (16), PLLON (24), plus pass-through for other bits.
        v = int(val) & 0xFFFFFFFF
        v = _cr_with_ready_bits(v)
        state["CR"] = v
    elif off == 0x04:
        # RCC_CFGR
        v = int(val) & 0xFFFFFFFF
        state["CFGR"] = _cfgr_with_status_bits(v)
    elif off == 0x18:
        # RCC_APB2ENR (common enable register used early)
        state["APB2ENR"] = int(val) & 0xFFFFFFFF
    elif off == 0x1C:
        # RCC_APB1ENR (TIM2/TIM3/etc live here on STM32F1-class parts)
        state["APB1ENR"] = int(val) & 0xFFFFFFFF
    elif off == 0x24:
        # RCC_CSR: RMVF clears reset flags.
        v = int(val) & 0xFFFFFFFF
        if v & (1 << 24):
            state["CSR"] = int(state.get("CSR", 0)) & ~0xFE000000
    else:
        # ignore other registers for now
        pass
else:
    if off == 0x00:
        _set_req(REQUEST_ATTRS, _cr_with_ready_bits(state.get("CR", 0)))
    elif off == 0x04:
        _set_req(REQUEST_ATTRS, _cfgr_with_status_bits(state.get("CFGR", 0)))
    elif off == 0x18:
        _set_req(REQUEST_ATTRS, int(state.get("APB2ENR", 0)) & 0xFFFFFFFF)
    elif off == 0x1C:
        _set_req(REQUEST_ATTRS, int(state.get("APB1ENR", 0)) & 0xFFFFFFFF)
    elif off == 0x24:
        _set_req(REQUEST_ATTRS, int(state.get("CSR", 0)) & 0xFFFFFFFF)
    else:
        _set_req(REQUEST_ATTRS, 0)
