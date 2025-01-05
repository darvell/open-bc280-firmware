import System
import os

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or "/tmp/bc280_renode"

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")
OFFSET_ATTRS = ("Offset", "offset")

KEY = "fsmc_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)


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
        with open(os.path.join(OUTDIR, "fsmc_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] = budget - 1
    except Exception:
        pass


def _init_state():
    s = {
        "regs": {},
        "log_budget": 200,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get"):
    state = _init_state()


off = _get_req(OFFSET_ATTRS, 0)
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = _get_req(REQUEST_ATTRS, 0) if is_write else 0

if is_write:
    state["regs"][int(off) & 0xFFFFFFFF] = int(val) & 0xFFFFFFFF
    _log("WR off=0x%X val=0x%08X" % (int(off) & 0xFFFFFFFF, int(val) & 0xFFFFFFFF))
else:
    r = int(state["regs"].get(int(off) & 0xFFFFFFFF, 0)) & 0xFFFFFFFF
    _set_req(REQUEST_ATTRS, r)
