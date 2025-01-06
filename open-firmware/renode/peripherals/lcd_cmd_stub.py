import System
import os

from renode_utils import dump_ppm_frame

# Prefer a stable temp location so external viewers can watch frames live without
# depending on the repo path.
OUTDIR = os.environ.get("BC280_LCD_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)
FAST = os.environ.get("BC280_LCD_FAST") == "1"

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")


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

KEY = "lcd_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)
# spi1_flash_stub.py may pre-seed lcd_state for its own purposes; make sure the
# keys this stub relies on exist even when the dict already exists.
_defaults = {
    "cmd": None,
    "caset": (0, 239),
    "paset": (0, 319),
    "pending_caset": [],
    "pending_paset": [],
    "collect_pixels": False,
    "pixel_bytes": bytearray(),
    "frame_counter": 0,
    "log_budget": 500,
    "fb_width": 240,
    "fb_height": 320,
    "framebuffer": bytearray(240 * 320 * 2),
    "cur_x": 0,
    "cur_y": 0,
    "expect_high": True,
}

if state is None or not hasattr(state, "get"):
    state = dict(_defaults)
else:
    try:
        for k in _defaults:
            if k not in state:
                state[k] = _defaults[k]
    except Exception:
        pass
System.AppDomain.CurrentDomain.SetData(KEY, state)
lcd_state = state



def log(msg):
    outdir = OUTDIR
    if not os.path.isdir(outdir):
        try:
            os.makedirs(outdir)
        except Exception:
            pass
    if lcd_state.get("log_budget", 0) > 0:
        with open(os.path.join(outdir, "lcd_debug.txt"), "a") as f:
            f.write("[cmd] %s\n" % msg)
        lcd_state["log_budget"] -= 1


if lcd_state.get("logged_init") is None:
    log("init id=%d" % id(lcd_state))
    lcd_state["logged_init"] = True


def dump_frame():
    s = lcd_state
    s["frame_counter"] = dump_ppm_frame(
        OUTDIR,
        s["frame_counter"],
        s["fb_width"],
        s["fb_height"],
        s["framebuffer"],
    )


def flush_partial():
    s = lcd_state
    dump_frame()


is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

if is_write:
    new_cmd = val & 0xFF
    # If we were in the middle of a RAMWR sequence, dump the current framebuffer snapshot
    # when a new command arrives. This catches partial-region updates (logo/UI blits).
    if not FAST:
        try:
            if lcd_state.get("collect_pixels") and lcd_state.get("pix_written", 0) > 0:
                dump_frame()
        except Exception:
            pass

    lcd_state["cmd"] = new_cmd
    if not FAST:
        log("cmd=0x%02X id=%d" % (lcd_state["cmd"], id(lcd_state)))
    if lcd_state["cmd"] == 0x2C:  # RAMWR
        lcd_state["collect_pixels"] = not FAST
        if not FAST:
            lcd_state["pixel_bytes"] = bytearray()
            lcd_state["next_len_log"] = 1000
        lcd_state["cur_x"] = lcd_state["caset"][0]
        lcd_state["cur_y"] = lcd_state["paset"][0]
        lcd_state["expect_high"] = True
        lcd_state["pix_written"] = 0
    else:
        lcd_state["collect_pixels"] = False
        lcd_state["expect_high"] = True
else:
    _set_req(REQUEST_ATTRS, 0)
