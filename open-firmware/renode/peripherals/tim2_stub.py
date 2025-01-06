import System

try:
    import Antmicro.Renode.Time as RenodeTime
except Exception:
    RenodeTime = None

import os


KEY = "tim2_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)

TIM2_IRQ = 28
TIM2_BASE = 0x40000000
NVIC_ISPR0 = 0xE000E200

# TIM2 is on APB1; with APB1 prescaler != 1 the timer clock is multiplied by 2.
# The BC280 firmware config uses a 72MHz system clock, and TIM2 is typically configured
# to generate a 1ms update. We approximate using a fixed 72MHz timer clock.
TIM2_INPUT_HZ = 72000000

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or "/tmp/bc280_renode"

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")
OFFSET_ATTRS = ("Offset", "offset")
IS_INIT_ATTRS = ("IsInit", "isInit", "is_init")


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
    try:
        budget = int(state.get("log_budget", 0))
    except Exception:
        budget = 0
    if budget <= 0:
        return
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "tim2_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] = budget - 1
    except Exception:
        pass


def _init_state():
    s = {
        # Minimal TIM2 register set needed by firmware:
        #   - CR1  (0x00): CEN bit0
        #   - DIER (0x0C): UIE bit0
        #   - SR   (0x10): UIF bit0 (write-0-to-clear)
        #   - EGR  (0x14): UG bit0 (forces update event)
        #   - PSC  (0x28), ARR (0x2C): used to derive tick period.
        "cr1": 0x00000000,
        "dier": 0x00000000,
        "sr": 0x00000000,
        "psc": 0x00000000,
        "arr": 0xFFFFFFFF,
        # Emulation helpers
        "tick_scheduled": False,
        "last_period_us": 0,
        "log_budget": 300,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get"):
    state = _init_state()


def _get_machine():
    try:
        if hasattr(self, "GetMachine"):
            return self.GetMachine()
    except Exception:
        pass
    try:
        if hasattr(self, "TryGetMachine"):
            ok, m = self.TryGetMachine()
            if ok:
                return m
    except Exception:
        pass
    return None


def _set_nvic_pending(machine, irq):
    try:
        irq = int(irq)
        if irq < 0:
            return
        reg = NVIC_ISPR0 + 4 * (irq // 32)
        bit = 1 << (irq % 32)
        machine["sysbus"].WriteDoubleWord(reg, bit)
    except Exception:
        # Best-effort only.
        pass


def _calc_update_period_us():
    try:
        psc = int(state.get("psc", 0)) & 0xFFFF
        arr = int(state.get("arr", 0xFFFFFFFF)) & 0xFFFFFFFF
    except Exception:
        psc = 0
        arr = 0xFFFFFFFF

    # Guard against uninitialized values causing absurd delays.
    if arr == 0xFFFFFFFF:
        return 1000

    ticks = (psc + 1) * (arr + 1)
    # Convert timer ticks to microseconds.
    try:
        us = int((ticks * 1000000) // TIM2_INPUT_HZ)
    except Exception:
        us = 1000
    if us <= 0:
        us = 1
    # Avoid pathological scheduling rates; 1us minimum still lets "fast-forward" if needed.
    if us < 1:
        us = 1
    if us > 500000:
        # Cap at 500ms so long delays don't stall boot forever if firmware misprograms ARR/PSC.
        us = 500000
    return us


def _tick_cb(_arg):
    try:
        machine = _get_machine()
        if machine is None:
            state["tick_scheduled"] = False
            return
        cr1 = int(state.get("cr1", 0)) & 0xFFFFFFFF
        dier = int(state.get("dier", 0)) & 0xFFFFFFFF
        cen = bool(cr1 & 1)
        uie = bool(dier & 1)
        if not cen:
            state["tick_scheduled"] = False
            return

        # Update event: set UIF in SR.
        state["sr"] = (int(state.get("sr", 0)) | 1) & 0xFFFFFFFF

        if uie:
            _set_nvic_pending(machine, TIM2_IRQ)

        # Reschedule next tick in *emulated* time.
        period_us = _calc_update_period_us()
        state["last_period_us"] = int(period_us)
        try:
            _log("TICK cen=1 uie=%d sr=0x%X period_us=%d" % (1 if uie else 0, int(state.get("sr", 0)) & 0xFFFFFFFF, int(period_us)))
        except Exception:
            pass
        if RenodeTime is None:
            # Fallback: if TimeInterval is not available, keep ISR driven by explicit EGR/UIF writes.
            state["tick_scheduled"] = False
            return

        machine.ScheduleAction(RenodeTime.TimeInterval.FromMicroseconds(int(period_us)), _tick_cb)
        state["tick_scheduled"] = True
    except Exception:
        state["tick_scheduled"] = False


def _maybe_start_ticks():
    try:
        machine = _get_machine()
        if machine is None:
            return
        cr1 = int(state.get("cr1", 0)) & 0xFFFFFFFF
        cen = bool(cr1 & 1)
        if not cen:
            return
        if state.get("tick_scheduled"):
            return
        if RenodeTime is None:
            return
        period_us = _calc_update_period_us()
        state["last_period_us"] = int(period_us)
        try:
            _log("SCHED_START cen=1 period_us=%d psc=0x%X arr=0x%X dier=0x%X cr1=0x%X" % (int(period_us), int(state.get("psc", 0)) & 0xFFFFFFFF, int(state.get("arr", 0)) & 0xFFFFFFFF, int(state.get("dier", 0)) & 0xFFFFFFFF, int(state.get("cr1", 0)) & 0xFFFFFFFF))
        except Exception:
            pass
        machine.ScheduleAction(RenodeTime.TimeInterval.FromMicroseconds(int(period_us)), _tick_cb)
        state["tick_scheduled"] = True
    except Exception:
        pass


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

if bool(_get_req(IS_INIT_ATTRS, False)):
    # Keep state but clear SR/UIF on reset-like init.
    try:
        state["sr"] = 0
        state["tick_scheduled"] = False
    except Exception:
        pass
    _maybe_start_ticks()
elif is_write:
    try:
        _log("W off=0x%X val=0x%X type=%s" % (off, val, str(getattr(request, "Type", ""))))
    except Exception:
        pass
    if off == 0x00:
        state["cr1"] = int(val) & 0xFFFFFFFF
        _maybe_start_ticks()
    elif off == 0x0C:
        state["dier"] = int(val) & 0xFFFFFFFF
    elif off == 0x10:
        # Firmware clears UIF by writing SR with bit0 cleared (write-to-clear style).
        state["sr"] = int(val) & 0xFFFFFFFF
    elif off == 0x14:
        # EGR: UG triggers an update event (UIF set) and, if enabled, an interrupt.
        state["sr"] = (int(state.get("sr", 0)) | 1) & 0xFFFFFFFF
        try:
            if int(state.get("dier", 0)) & 1:
                m = _get_machine()
                if m is not None:
                    _set_nvic_pending(m, TIM2_IRQ)
        except Exception:
            pass
    elif off == 0x28:
        state["psc"] = int(val) & 0xFFFFFFFF
    elif off == 0x2C:
        state["arr"] = int(val) & 0xFFFFFFFF
else:
    try:
        _log("R off=0x%X type=%s" % (off, str(getattr(request, "Type", ""))))
    except Exception:
        pass
    if off == 0x00:
        _set_req(REQUEST_ATTRS, int(state.get("cr1", 0)) & 0xFFFFFFFF)
    elif off == 0x0C:
        _set_req(REQUEST_ATTRS, int(state.get("dier", 0)) & 0xFFFFFFFF)
    elif off == 0x10:
        _set_req(REQUEST_ATTRS, int(state.get("sr", 0)) & 0xFFFFFFFF)
    elif off == 0x14:
        _set_req(REQUEST_ATTRS, 0)
    elif off == 0x28:
        _set_req(REQUEST_ATTRS, int(state.get("psc", 0)) & 0xFFFFFFFF)
    elif off == 0x2C:
        _set_req(REQUEST_ATTRS, int(state.get("arr", 0)) & 0xFFFFFFFF)
    else:
        _set_req(REQUEST_ATTRS, 0)
