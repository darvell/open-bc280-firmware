import System

try:
    import Antmicro.Renode.Time as RenodeTime
except Exception:
    RenodeTime = None

import os

KEY = "iwdg_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)

IWDG_BASE = 0x40003000
LSI_HZ = 40000
PRESCALER_TABLE = {
    0: 4,
    1: 8,
    2: 16,
    3: 32,
    4: 64,
    5: 128,
    6: 256,
}

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)

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


def _init_state():
    try:
        log_budget = int(os.environ.get("BC280_IWDG_LOG_BUDGET", "0"))
    except Exception:
        log_budget = 0
    s = {
        "pr": 0,
        "rlr": 0x0FFF,
        "started": False,
        "unlocked": False,
        "feed_epoch": 0,
        "log_budget": log_budget,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get"):
    state = _init_state()


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
        with open(os.path.join(OUTDIR, "iwdg_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] = budget - 1
    except Exception:
        pass


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
    try:
        if hasattr(self, "Machine"):
            return self.Machine
    except Exception:
        pass
    return None


def _set_iwdg_reset_flag():
    try:
        rcc_state = System.AppDomain.CurrentDomain.GetData("rcc_state")
        if rcc_state is None or not hasattr(rcc_state, "get"):
            return
        csr = int(rcc_state.get("CSR", 0)) & 0xFFFFFFFF
        csr |= (1 << 29)  # IWDGRSTF
        rcc_state["CSR"] = csr
        System.AppDomain.CurrentDomain.SetData("rcc_state", rcc_state)
    except Exception:
        pass


def _reset_machine():
    m = _get_machine()
    if m is None:
        return False
    for name in ("Reset", "RequestReset", "ResetMachine"):
        try:
            if hasattr(m, name):
                getattr(m, name)()
                return True
        except Exception:
            pass
    try:
        if hasattr(m, "SystemBus") and hasattr(m.SystemBus, "Reset"):
            m.SystemBus.Reset()
            return True
    except Exception:
        pass
    try:
        bus = m["sysbus"]
        if hasattr(bus, "Reset"):
            bus.Reset()
            return True
    except Exception:
        pass
    return False


def _calc_timeout_us():
    try:
        pr = int(state.get("pr", 0)) & 0x7
        rlr = int(state.get("rlr", 0)) & 0x0FFF
    except Exception:
        pr = 0
        rlr = 0x0FFF
    presc = PRESCALER_TABLE.get(pr, 4)
    ticks = (rlr + 1) * presc
    try:
        us = int((ticks * 1000000) // LSI_HZ)
    except Exception:
        us = 1000
    if us <= 0:
        us = 1
    return us


def _schedule_timeout():
    if RenodeTime is None:
        return
    if not state.get("started"):
        return
    m = _get_machine()
    if m is None:
        return
    timeout_us = _calc_timeout_us()
    epoch = int(state.get("feed_epoch", 0))
    try:
        _log("SCHED timeout_us=%d pr=%d rlr=%d" % (int(timeout_us), int(state.get("pr", 0)), int(state.get("rlr", 0))))
    except Exception:
        pass
    try:
        def _cb(_arg, e=epoch):
            _timeout_cb(_arg, e)
        m.ScheduleAction(RenodeTime.TimeInterval.FromMicroseconds(int(timeout_us)), _cb)
    except Exception:
        pass


def _timeout_cb(_arg, epoch):
    try:
        if not state.get("started"):
            return
        if int(epoch) != int(state.get("feed_epoch", 0)):
            return
        _log("TIMEOUT")
        _set_iwdg_reset_flag()
        _reset_machine()
    except Exception:
        pass


def _kick_watchdog():
    state["feed_epoch"] = int(state.get("feed_epoch", 0)) + 1
    _schedule_timeout()


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

if bool(_get_req(IS_INIT_ATTRS, False)):
    # Reset-like init: clear state.
    state["started"] = False
    state["unlocked"] = False
    state["feed_epoch"] = 0
elif is_write:
    if off == 0x00:
        # KR
        if val == 0x5555:
            state["unlocked"] = True
        elif val == 0xCCCC:
            state["started"] = True
            _kick_watchdog()
        elif val == 0xAAAA:
            _kick_watchdog()
    elif off == 0x04:
        # PR
        if state.get("unlocked"):
            state["pr"] = int(val) & 0x7
    elif off == 0x08:
        # RLR
        if state.get("unlocked"):
            state["rlr"] = int(val) & 0x0FFF
else:
    if off == 0x00:
        _set_req(REQUEST_ATTRS, 0)
    elif off == 0x04:
        _set_req(REQUEST_ATTRS, int(state.get("pr", 0)) & 0x7)
    elif off == 0x08:
        _set_req(REQUEST_ATTRS, int(state.get("rlr", 0)) & 0x0FFF)
    elif off == 0x0C:
        _set_req(REQUEST_ATTRS, 0)
    else:
        _set_req(REQUEST_ATTRS, 0)
