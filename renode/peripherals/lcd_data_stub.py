import System
import os
import sys

sys.path.append(os.path.dirname(__file__))
from renode_utils import dump_ppm_frame

# Prefer a stable temp location so external viewers can watch frames live without
# depending on the repo path.
OUTDIR = os.environ.get("BC280_LCD_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)
FAST = os.environ.get("BC280_LCD_FAST") == "1"

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")
OFFSET_ATTRS = ("Offset", "offset")
LENGTH_ATTRS = ("Length", "length")


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
# spi1_flash_stub.py may pre-seed lcd_state; ensure required keys exist.
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
    "vram": bytearray(0x20000),  # backing store for DMA reads
    # Scan/debug helpers
    "nonzero_seen": False,
    "nonzero_word_count": 0,
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
            f.write("[data] %s\n" % msg)
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


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0
req_len = _get_req(LENGTH_ATTRS, None)
try:
    if req_len is not None:
        req_len = int(req_len)
except Exception:
    req_len = None

if is_write:
    if FAST:
        cmd = lcd_state.get("cmd")
        if cmd == 0x2A:
            lcd_state["pending_caset"].append(val & 0xFF)
            if len(lcd_state["pending_caset"]) >= 4:
                s = lcd_state["pending_caset"][:4]
                lcd_state["pending_caset"] = lcd_state["pending_caset"][4:]
                lcd_state["caset"] = ((s[0] << 8) | s[1], (s[2] << 8) | s[3])
        elif cmd == 0x2B:
            lcd_state["pending_paset"].append(val & 0xFF)
            if len(lcd_state["pending_paset"]) >= 4:
                s = lcd_state["pending_paset"][:4]
                lcd_state["pending_paset"] = lcd_state["pending_paset"][4:]
                lcd_state["paset"] = ((s[0] << 8) | s[1], (s[2] << 8) | s[3])
        elif cmd == 0x2C:
            # Fast path: skip per-pixel work, just count writes.
            if "pix_written" not in lcd_state:
                lcd_state["pix_written"] = 0
            if req_len is None:
                lcd_state["pix_written"] += 1
            elif req_len >= 4:
                lcd_state["pix_written"] += 2
            elif req_len >= 2:
                lcd_state["pix_written"] += 1
            else:
                lcd_state["pix_written"] += 1
        else:
            pass
        _set_req(REQUEST_ATTRS, val)
    else:
        # Persist to VRAM so DMA reads can fetch pixel data.
        try:
            vram = lcd_state.get("vram")
            if isinstance(vram, bytearray):
                if req_len is None or req_len <= 1:
                    if off < len(vram):
                        vram[off] = val & 0xFF
                elif req_len >= 4:
                    if off + 3 < len(vram):
                        vram[off] = val & 0xFF
                        vram[off + 1] = (val >> 8) & 0xFF
                        vram[off + 2] = (val >> 16) & 0xFF
                        vram[off + 3] = (val >> 24) & 0xFF
                elif req_len >= 2:
                    if off + 1 < len(vram):
                        vram[off] = val & 0xFF
                        vram[off + 1] = (val >> 8) & 0xFF
        except Exception:
            pass

        b = val & 0xFF
        cmd = lcd_state.get("cmd")
        if cmd == 0x2A:
            lcd_state["pending_caset"].append(b)
            if len(lcd_state["pending_caset"]) >= 4:
                s = lcd_state["pending_caset"][:4]
                lcd_state["pending_caset"] = lcd_state["pending_caset"][4:]
                lcd_state["caset"] = ((s[0] << 8) | s[1], (s[2] << 8) | s[3])
                if lcd_state.get("log_budget", 0) > 0:
                    log("CASET %d..%d" % lcd_state["caset"])
        elif cmd == 0x2B:
            lcd_state["pending_paset"].append(b)
            if len(lcd_state["pending_paset"]) >= 4:
                s = lcd_state["pending_paset"][:4]
                lcd_state["pending_paset"] = lcd_state["pending_paset"][4:]
                lcd_state["paset"] = ((s[0] << 8) | s[1], (s[2] << 8) | s[3])
                if lcd_state.get("log_budget", 0) > 0:
                    log("PASET %d..%d" % lcd_state["paset"])
        elif cmd == 0x2C:
            # Pixel writes can be either 16-bit words (common for FSMC 16-bit) or byte-streams.
            if req_len is not None and req_len >= 2:
                # Renode may deliver DMA/CPU writes as 32-bit values even though the LCD bus is 16-bit.
                # Treat a 32-bit write as two consecutive RGB565 pixels (low 16 bits, then high 16 bits).
                pix_list = []
                if req_len >= 4:
                    pix_list = [val & 0xFFFF, (val >> 16) & 0xFFFF]
                else:
                    pix_list = [val & 0xFFFF]

                for pix in pix_list:
                    if "pix_written" not in lcd_state:
                        lcd_state["pix_written"] = 0
                    lcd_state["pix_written"] += 1
                    if "pix_log_left" not in lcd_state:
                        lcd_state["pix_log_left"] = 50
                    if lcd_state["pix_log_left"] > 0:
                        log("PIX16 0x%04X at n=%d (len=%s)" % (pix, lcd_state["pix_written"], str(req_len)))
                        lcd_state["pix_log_left"] -= 1

                    x = lcd_state.get("cur_x", 0)
                    y = lcd_state.get("cur_y", 0)
                    fbw = lcd_state["fb_width"]
                    fbh = lcd_state["fb_height"]
                    if 0 <= x < fbw and 0 <= y < fbh:
                        idx = (y * fbw + x) * 2
                        fb = lcd_state["framebuffer"]
                        if isinstance(fb, bytearray):
                            fb[idx] = (pix >> 8) & 0xFF
                            fb[idx + 1] = pix & 0xFF
                    if pix:
                        lcd_state["nonzero_seen"] = True
                        try:
                            lcd_state["nonzero_word_count"] = int(lcd_state.get("nonzero_word_count", 0)) + 1
                        except Exception:
                            lcd_state["nonzero_word_count"] = 1
                    x += 1
                    if x > lcd_state["caset"][1]:
                        x = lcd_state["caset"][0]
                        y += 1
                        if y > lcd_state["paset"][1]:
                            dump_frame()
                            y = lcd_state["paset"][0]
                            lcd_state["collect_pixels"] = False
                    lcd_state["cur_x"] = x
                    lcd_state["cur_y"] = y
                    lcd_state["expect_high"] = True
            else:
                if lcd_state.get("expect_high", True):
                    lcd_state["hi_byte"] = b
                    lcd_state["expect_high"] = False
                else:
                    hi = lcd_state.get("hi_byte", 0)
                    val = (hi << 8) | b
                    # write into framebuffer
                    x = lcd_state.get("cur_x", 0)
                    y = lcd_state.get("cur_y", 0)
                    fbw = lcd_state["fb_width"]
                    fbh = lcd_state["fb_height"]
                    if 0 <= x < fbw and 0 <= y < fbh:
                        idx = (y * fbw + x) * 2
                        fb = lcd_state["framebuffer"]
                        if isinstance(fb, bytearray):
                            fb[idx] = (val >> 8) & 0xFF
                            fb[idx + 1] = val & 0xFF
                    if val:
                        lcd_state["nonzero_seen"] = True
                        try:
                            lcd_state["nonzero_word_count"] = int(lcd_state.get("nonzero_word_count", 0)) + 1
                        except Exception:
                            lcd_state["nonzero_word_count"] = 1
                    x += 1
                    if x > lcd_state["caset"][1]:
                        x = lcd_state["caset"][0]
                        y += 1
                        if y > lcd_state["paset"][1]:
                            dump_frame()
                            y = lcd_state["paset"][0]
                            lcd_state["collect_pixels"] = False
                            lcd_state["expect_high"] = True
                    lcd_state["cur_x"] = x
                    lcd_state["cur_y"] = y
                    lcd_state["expect_high"] = True
                    if "next_len_log" not in lcd_state:
                        lcd_state["next_len_log"] = 1000
                    pos = y * fbw + x
                    if pos >= lcd_state["next_len_log"]:
                        log("fb_pos=%d" % pos)
                        lcd_state["next_len_log"] += 1000
else:
    # Reads from VRAM (for DMA source).
    v = 0
    try:
        vram = lcd_state.get("vram")
        if isinstance(vram, bytearray):
            if req_len is not None and req_len >= 4:
                if off + 3 < len(vram):
                    v = (
                        (vram[off] & 0xFF)
                        | ((vram[off + 1] & 0xFF) << 8)
                        | ((vram[off + 2] & 0xFF) << 16)
                        | ((vram[off + 3] & 0xFF) << 24)
                    )
            elif req_len is not None and req_len >= 2:
                if off + 1 < len(vram):
                    v = vram[off] | (vram[off + 1] << 8)
            else:
                if off < len(vram):
                    v = vram[off]
    except Exception:
        v = 0
    _set_req(REQUEST_ATTRS, v)
