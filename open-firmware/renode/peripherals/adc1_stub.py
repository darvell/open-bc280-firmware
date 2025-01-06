import System
import os


KEY = "adc1_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)


def _init_state():
    # STM32F1-ish ADC register subset (ADC1 base 0x40012400).
    s = {
        # Registers
        "sr": 0x00000000,     # offset 0x00 (EOC bit1)
        "cr1": 0x00000000,    # offset 0x04
        "cr2": 0x00000000,    # offset 0x08
        "smpr1": 0x00000000,  # offset 0x0C
        "smpr2": 0x00000000,  # offset 0x10
        "sqr1": 0x00000000,   # offset 0x2C
        "sqr2": 0x00000000,   # offset 0x30
        "sqr3": 0x00000000,   # offset 0x34
        "dr": 0x00000000,     # offset 0x4C (12-bit on real HW)
        # Emulation helpers
        "log_budget": 1200,
        # Plausible 12-bit samples by ADC channel (0..17). Unknown channels fall back to default.
        # Keep values non-zero to avoid "dead battery" / "invalid sensor" paths.
        "channel_values": {
            0: 3800,  # battery divider-ish (guess)
            1: 2000,
            2: 2500,
            3: 1800,
            4: 1600,
            5: 1700,
            6: 1900,
            7: 2100,
            8: 2300,
            9: 2400,
            10: 2200,
            11: 2100,
            12: 2000,
            13: 1900,
            14: 1800,
            15: 1700,
            16: 1600,
            17: 1500,
        },
        "default_value": 3000,
        "last_channel": None,
        "conv_count": 0,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get"):
    state = _init_state()

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

OUTDIR = "/tmp/bc280_renode"


def _log(msg):
    try:
        bud = int(state.get("log_budget", 0))
    except Exception:
        bud = 0
    if bud <= 0:
        return
    try:
        if os is not None:
            try:
                if not os.path.isdir(OUTDIR):
                    os.makedirs(OUTDIR)
            except Exception:
                pass
            with open(os.path.join(OUTDIR, "adc1_debug.txt"), "a") as f:
                f.write("%s\n" % msg)
        state["log_budget"] = bud - 1
    except Exception:
        pass


def _get_selected_channel():
    # Firmware programs the first regular conversion channel in SQR3 bits [4:0].
    try:
        return int(state.get("sqr3", 0)) & 0x1F
    except Exception:
        return 0


def _start_conversion(kind):
    # kind: "regular" or "injected" (we only model regular right now)
    ch = _get_selected_channel()
    try:
        vals = state.get("channel_values") or {}
        v = int(vals.get(int(ch), int(state.get("default_value", 0)))) & 0xFFF
    except Exception:
        v = 0x800
    state["dr"] = int(v) & 0xFFFFFFFF
    # SR.EOC bit1 set.
    state["sr"] = (int(state.get("sr", 0)) | (1 << 1)) & 0xFFFFFFFF
    state["last_channel"] = int(ch)
    try:
        state["conv_count"] = int(state.get("conv_count", 0)) + 1
    except Exception:
        state["conv_count"] = 1
    _log("CONV %s ch=%d dr=0x%03X sr=0x%X cnt=%d" % (kind, int(ch), int(v) & 0xFFF, int(state.get("sr", 0)) & 0xFFFFFFFF, int(state.get("conv_count", 0))))


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

# STM32F1-ish ADC bits we care about:
ADC_SR_EOC = 1 << 1       # 0x2
ADC_CR2_ADON = 1 << 0     # 0x1
ADC_CR2_CAL = 1 << 2      # 0x4
ADC_CR2_RSTCAL = 1 << 3   # 0x8
ADC_CR2_JSWSTART = 1 << 21  # 0x200000
ADC_CR2_SWSTART = 1 << 22   # 0x400000

if is_write:
    if off == 0x00:
        # SR: write-0-to-clear flags; firmware uses this to clear EOC.
        state["sr"] = int(val) & 0xFFFFFFFF
    elif off == 0x04:
        state["cr1"] = int(val) & 0xFFFFFFFF
    elif off == 0x08:
        v = int(val) & 0xFFFFFFFF
        # When firmware sets RSTCAL/CAL it busy-waits for the bit to clear.
        # Model these bits as self-clearing immediately.
        v &= ~(ADC_CR2_RSTCAL | ADC_CR2_CAL)

        # Start conversion when SWSTART/JSWSTART is written. Also clear the start bits
        # so code that re-writes them doesn't accumulate.
        if v & ADC_CR2_SWSTART:
            _start_conversion("regular")
            v &= ~ADC_CR2_SWSTART
        if v & ADC_CR2_JSWSTART:
            # We don't model injected channels separately; still return a valid sample.
            _start_conversion("injected")
            v &= ~ADC_CR2_JSWSTART

        # If ADON was set and the firmware expects an immediate conversion-ready state,
        # we still require SWSTART/JSWSTART for EOC. Keep ADON as-is.
        state["cr2"] = v & 0xFFFFFFFF
    elif off == 0x0C:
        state["smpr1"] = int(val) & 0xFFFFFFFF
    elif off == 0x10:
        state["smpr2"] = int(val) & 0xFFFFFFFF
    elif off == 0x2C:
        state["sqr1"] = int(val) & 0xFFFFFFFF
    elif off == 0x30:
        state["sqr2"] = int(val) & 0xFFFFFFFF
    elif off == 0x34:
        state["sqr3"] = int(val) & 0xFFFFFFFF
    else:
        # ignore other registers
        pass
else:
    if off == 0x00:
        _set_req(REQUEST_ATTRS, int(state.get("sr", 0)) & 0xFFFFFFFF)
    elif off == 0x04:
        _set_req(REQUEST_ATTRS, int(state.get("cr1", 0)) & 0xFFFFFFFF)
    elif off == 0x08:
        _set_req(REQUEST_ATTRS, int(state.get("cr2", 0)) & 0xFFFFFFFF)
    elif off == 0x0C:
        _set_req(REQUEST_ATTRS, int(state.get("smpr1", 0)) & 0xFFFFFFFF)
    elif off == 0x10:
        _set_req(REQUEST_ATTRS, int(state.get("smpr2", 0)) & 0xFFFFFFFF)
    elif off == 0x2C:
        _set_req(REQUEST_ATTRS, int(state.get("sqr1", 0)) & 0xFFFFFFFF)
    elif off == 0x30:
        _set_req(REQUEST_ATTRS, int(state.get("sqr2", 0)) & 0xFFFFFFFF)
    elif off == 0x34:
        _set_req(REQUEST_ATTRS, int(state.get("sqr3", 0)) & 0xFFFFFFFF)
    elif off == 0x4C:
        # DR read clears EOC on STM32F1.
        v = int(state.get("dr", 0)) & 0xFFFF
        state["sr"] = int(state.get("sr", 0)) & ~ADC_SR_EOC
        _set_req(REQUEST_ATTRS, v)
    else:
        _set_req(REQUEST_ATTRS, 0)
# Log directory (overridable) defaulting to repo-local renode/output.
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)
