import System
import os
import sys

sys.path.append(os.path.dirname(__file__))
from renode_utils import tohex

REQUEST_ATTRS = ("Value", "value")
IS_WRITE_ATTRS = ("IsWrite", "isWrite", "iswrite")
OFFSET_ATTRS = ("Offset", "offset")

def _get_req(request, names, default=0):
    for name in names:
        try:
            return getattr(request, name)
        except Exception:
            continue
    return default


def _set_req(request, names, value):
    for name in names:
        try:
            setattr(request, name, value)
            return True
        except Exception:
            continue
    return False

def _init_state(key, outdir, spi_log_enabled, load_spi_image):
    size = 0x400000  # 4MiB (W25Q32-class)
    flash = bytearray([0xFF]) * size
    # Optional OEM SPI flash image (raw dump) to emulate vendor storage accurately.
    if load_spi_image:
        spi_img = os.environ.get("BC280_SPI_FLASH_BIN")
        if spi_img:
            try:
                with open(spi_img, "rb") as f:
                    data = f.read()
                if data:
                    flash[: min(len(data), size)] = data[: min(len(data), size)]
            except Exception:
                pass
    # Default boot flags / config to avoid constant self-repair writes.
    # BOOTLOADER_MODE is read from 0x3FF080 and compared against 0xAA.
    #
    # IMPORTANT: the firmware compares only the *first byte* (LDRB), not a 32-bit word.
    # - If flash[0x3FF080] == 0xAA -> it stays in "Please BLE Update" mode.
    # - Otherwise -> it schedules validate_firmware_header() and jumps to the main app.
    #
    # Keep the default as "boot app" (0xFF also allows programming 0xAA without erase).
    flash[0x3FF080:0x3FF084] = b"\xFF\xFF\xFF\xFF"
    # Bootloader version tag at 0x3FF040: len + string
    bl_tag = b"B_JH_FW_BL_DT_BC280_V3.3.6"
    flash[0x3FF040] = len(bl_tag)
    flash[0x3FF041:0x3FF041 + len(bl_tag)] = bl_tag

    if spi_log_enabled:
        try:
            if not os.path.isdir(outdir):
                os.makedirs(outdir)
            with open(os.path.join(outdir, "spi_flash_debug.txt"), "a") as f:
                f.write("INIT BOOTLOADER_MODE bytes=%s\n" % tohex(bytes(flash[0x3FF080:0x3FF090])))
        except Exception:
            pass

    s = {
        "flash": flash,
        "size": size,
        "cs_active": False,
        "cs_epoch": 0,
        "cs_epoch_seen": 0,
        "wel": False,
        # For commands like Page Program (0x02) the write-enable latch (WEL) is sampled
        # when the command is accepted. WEL is then consumed/cleared, but the program
        # operation remains allowed for the remainder of that transaction (until CS high).
        "pp_allowed": False,
        "wip_reads_left": 0,
        "mode": "IDLE",
        "cmd": None,
        "addr": 0,
        "addr_bytes": 0,
        "dummy_bytes": 0,
        # Incoming bytes shifted from MISO. For DFF=0 firmware typically reads 8-bit values;
        # for DFF=1 it reads 16-bit frames. Model this as a byte FIFO and pack on reads.
        "rx_fifo": [],
        "rx": 0,  # legacy single-byte latch (kept for older code paths)
        "jedec": [0xEF, 0x40, 0x16],  # Winbond W25Q32
        "jedec_i": 0,
        # minimal SPI regs
        "cr1": 0,
        "cr2": 0,
        # SR bits (STM32F1):
        #   RXNE bit0, TXE bit1, BSY bit7
        # Keep TXE set; set RXNE when rx_fifo non-empty; BSY stays 0 in this simplified model.
        "sr": 0x02,
        "bootflag_dumped": False,
        "bootflag_trace_left": 64,
        "log_reads_left": 5000,
        "status_log_left": 50,
        "cmd_log_left": 200,
        "current_read_start": None,
        "current_read_count": 0,
    }
    System.AppDomain.CurrentDomain.SetData(key, s)
    return s


def _get_state(key, outdir, spi_log_enabled, load_spi_image):
    state = System.AppDomain.CurrentDomain.GetData(key)
    if state is None or not hasattr(state, "get") or state.get("flash") is None:
        state = _init_state(key, outdir, spi_log_enabled, load_spi_image)
    return state


def _log(ctx, msg, use_prefix=True):
    if not ctx["spi_log_enabled"]:
        return
    if use_prefix and ctx["log_prefix"]:
        msg = "%s%s" % (ctx["log_prefix"], msg)
    try:
        if not os.path.isdir(ctx["outdir"]):
            os.makedirs(ctx["outdir"])
        with open(os.path.join(ctx["outdir"], "spi_flash_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
    except Exception:
        pass

def _status_byte(ctx):
    # WIP = bit0, WEL = bit1 (common SPI NOR semantics)
    v = 0
    if ctx["state"].get("wip_reads_left", 0) > 0:
        if not ctx["spi_wip_fast"]:
            v |= 0x01
    if ctx["state"].get("wel"):
        v |= 0x02
    return v


def _reset_transaction(ctx):
    # If a read transaction was in progress, log how many bytes were served.
    try:
        if ctx["state"].get("current_read_start") is not None and ctx["state"].get("current_read_count", 0) > 0:
            _log(
                ctx,
                "READ txn addr=0x%06X len=%d"
                % (int(ctx["state"].get("current_read_start")) & 0xFFFFFF, int(ctx["state"].get("current_read_count"))),
            )
    except Exception:
        pass
    ctx["state"]["current_read_start"] = None
    ctx["state"]["current_read_count"] = 0
    ctx["state"]["mode"] = "IDLE"
    ctx["state"]["cmd"] = None
    ctx["state"]["addr"] = 0
    ctx["state"]["addr_bytes"] = 0
    ctx["state"]["dummy_bytes"] = 0
    ctx["state"]["jedec_i"] = 0
    # Clear per-command authorization state. Do NOT clear WEL here: on typical SPI NOR,
    # WEL persists across CS boundaries until it is consumed by a write/erase command
    # or cleared explicitly by WRDI.
    ctx["state"]["pp_allowed"] = False


def _begin_command(ctx, cmd):
    ctx["state"]["cmd"] = cmd
    try:
        if ctx["state"].get("cmd_log_left", 0) > 0 and cmd in (0x03, 0x0B, 0x05, 0x06, 0x04, 0x02, 0x20, 0xD8, 0x9F):
            _log(
                ctx,
                "CMD 0x%02X cs=%s epoch=%d" % (cmd & 0xFF, bool(ctx["state"].get("cs_active")), int(ctx["state"].get("cs_epoch", 0))),
            )
            ctx["state"]["cmd_log_left"] = int(ctx["state"].get("cmd_log_left", 0)) - 1
    except Exception:
        pass
    if cmd == 0x03:
        ctx["state"]["mode"] = "READ_ADDR"
        ctx["state"]["addr"] = 0
        ctx["state"]["addr_bytes"] = 3
        ctx["state"]["current_read_start"] = None
        ctx["state"]["current_read_count"] = 0
    elif cmd == 0x0B:
        ctx["state"]["mode"] = "FASTREAD_ADDR"
        ctx["state"]["addr"] = 0
        ctx["state"]["addr_bytes"] = 3
        ctx["state"]["dummy_bytes"] = 1
        ctx["state"]["current_read_start"] = None
        ctx["state"]["current_read_count"] = 0
    elif cmd == 0x05:
        ctx["state"]["mode"] = "RDSR"
        try:
            if ctx["state"].get("status_log_left", 0) > 0:
                _log(ctx, "RDSR cmd")
        except Exception:
            pass
    elif cmd == 0x06:
        ctx["state"]["wel"] = True
        ctx["state"]["mode"] = "IDLE"
    elif cmd == 0x04:
        ctx["state"]["wel"] = False
        ctx["state"]["mode"] = "IDLE"
    elif cmd == 0x02:
        ctx["state"]["mode"] = "PP_ADDR"
        ctx["state"]["addr"] = 0
        ctx["state"]["addr_bytes"] = 3
    elif cmd in (0x20, 0xD8):
        ctx["state"]["mode"] = "ERASE_ADDR"
        ctx["state"]["addr"] = 0
        ctx["state"]["addr_bytes"] = 3
    elif cmd == 0x9F:
        ctx["state"]["mode"] = "RDID"
        ctx["state"]["jedec_i"] = 0
    elif cmd in (0xC7, 0x60):
        # Chip erase
        if ctx["state"].get("wel"):
            ctx["state"]["flash"][:] = bytearray([0xFF]) * int(ctx["state"]["size"])
            ctx["state"]["wip_reads_left"] = 2
        ctx["state"]["wel"] = False
        ctx["state"]["mode"] = "IDLE"
    else:
        # Unknown command; stay in IDLE
        ctx["state"]["mode"] = "IDLE"


def _handle_mosi_byte(ctx, b):
    default_out = ctx["default_out"]
    cs_epoch = int(ctx["state"].get("cs_epoch", 0))
    if cs_epoch != int(ctx["state"].get("cs_epoch_seen", -1)):
        _reset_transaction(ctx)
        ctx["state"]["cs_epoch_seen"] = cs_epoch
    cs = bool(ctx["state"].get("cs_active"))
    if not cs:
        return default_out

    mode = ctx["state"].get("mode")

    # If we're in a data-collection mode but receive a new command byte, treat as implicit CS boundary.
    # (This avoids getting stuck if the firmware toggles CS but we miss it.)
    if mode in ("PP_DATA", "READ_DATA") and b in (0x03, 0x0B, 0x05, 0x06, 0x04, 0x02, 0x20, 0xD8, 0x9F):
        _reset_transaction(ctx)
        _begin_command(ctx, b)
        return default_out

    if mode == "IDLE":
        _begin_command(ctx, b)
        return default_out

    if mode in ("READ_ADDR", "FASTREAD_ADDR", "PP_ADDR", "ERASE_ADDR"):
        ctx["state"]["addr"] = ((ctx["state"]["addr"] << 8) | b) & 0xFFFFFF
        ctx["state"]["addr_bytes"] -= 1
        if ctx["state"]["addr_bytes"] <= 0:
            if mode == "READ_ADDR":
                ctx["state"]["mode"] = "READ_DATA"
                if ctx["state"].get("log_reads_left", 0) > 0:
                    _log(ctx, "READ 0x03 addr=0x%06X" % (ctx["state"].get("addr") & 0xFFFFFF))
                    ctx["state"]["log_reads_left"] -= 1
            elif mode == "FASTREAD_ADDR":
                ctx["state"]["mode"] = "FASTREAD_DUMMY"
            elif mode == "PP_ADDR":
                # Sample WEL once the address is fully received.
                ctx["state"]["pp_allowed"] = bool(ctx["state"].get("wel"))
                # Consumed by PP command (typical semantics).
                ctx["state"]["wel"] = False
                ctx["state"]["mode"] = "PP_DATA"
            elif mode == "ERASE_ADDR":
                if ctx["state"].get("wel"):
                    addr = ctx["state"]["addr"] % ctx["state"]["size"]
                    # 4K sector erase (0x20) or 64K block erase (0xD8)
                    if ctx["state"]["cmd"] == 0x20:
                        base = addr & ~0xFFF
                        end = min(base + 0x1000, ctx["state"]["size"])
                    else:
                        base = addr & ~0xFFFF
                        end = min(base + 0x10000, ctx["state"]["size"])
                    ctx["state"]["flash"][base:end] = bytearray([0xFF]) * int(end - base)
                    ctx["state"]["wip_reads_left"] = 2
                # Consumed by erase command regardless of whether it was honored.
                ctx["state"]["wel"] = False
                ctx["state"]["mode"] = "IDLE"
        return default_out

    if mode == "FASTREAD_DUMMY":
        ctx["state"]["dummy_bytes"] -= 1
        if ctx["state"]["dummy_bytes"] <= 0:
            ctx["state"]["mode"] = "READ_DATA"
        return default_out

    if mode == "READ_DATA":
        addr = ctx["state"]["addr"] % ctx["state"]["size"]
        out = ctx["state"]["flash"][addr]
        if ctx["state"].get("current_read_start") is None:
            ctx["state"]["current_read_start"] = ctx["state"].get("addr", 0) & 0xFFFFFF
            ctx["state"]["current_read_count"] = 0
        ctx["state"]["current_read_count"] = int(ctx["state"].get("current_read_count", 0)) + 1
        if not ctx["state"].get("bootflag_dumped"):
            a = ctx["state"].get("addr") & 0xFFFFFF
            if a in (0x3FF080, 0x3FF081):
                try:
                    start = 0x3FF080
                    span = 16
                    data = bytes(ctx["state"]["flash"][start:start + span])
                    _log(
                        ctx,
                        "BOOTLOADER_MODE window @0x%06X now=0x%06X out=0x%02X bytes=%s" % (start, a, out, tohex(data)),
                    )
                except Exception:
                    pass
                ctx["state"]["bootflag_dumped"] = True
        # Extra trace for BOOTLOADER_MODE reads (helps debug CS/DFF issues without touching firmware code).
        try:
            if ctx["state"].get("bootflag_trace_left", 0) > 0:
                a = ctx["state"].get("addr") & 0xFFFFFF
                if 0x3FF07E <= a <= 0x3FF090:
                    ctx["state"]["bootflag_trace_left"] = int(ctx["state"].get("bootflag_trace_left", 0)) - 1
                    _log(
                        ctx,
                        "TRACE READ_DATA a=0x%06X out=0x%02X cs=%s epoch=%d mode=%s"
                        % (a, out & 0xFF, bool(ctx["state"].get("cs_active")), int(ctx["state"].get("cs_epoch", 0)), str(ctx["state"].get("mode"))),
                    )
        except Exception:
            pass
        ctx["state"]["addr"] = (ctx["state"]["addr"] + 1) & 0xFFFFFF
        return out

    if mode == "RDSR":
        if ctx["state"].get("wip_reads_left", 0) > 0:
            ctx["state"]["wip_reads_left"] -= 1
        out = _status_byte(ctx)
        try:
            if ctx["state"].get("status_log_left", 0) > 0:
                _log(
                    ctx,
                    "RDSR out=0x%02X wip_left=%d wel=%s"
                    % (out & 0xFF, int(ctx["state"].get("wip_reads_left", 0)), str(bool(ctx["state"].get("wel")))),
                )
                ctx["state"]["status_log_left"] = int(ctx["state"].get("status_log_left", 0)) - 1
        except Exception:
            pass
        return out

    if mode == "RDID":
        i = ctx["state"].get("jedec_i", 0)
        out = ctx["state"]["jedec"][i] if i < len(ctx["state"]["jedec"]) else 0
        ctx["state"]["jedec_i"] = i + 1
        return out

    if mode == "PP_DATA":
        if ctx["state"].get("pp_allowed"):
            addr = ctx["state"]["addr"] % ctx["state"]["size"]
            if addr in (0x3FF080, 0x3FF081, 0x3FF082, 0x3FF083) and not ctx["state"].get("bootflag_prog_logged"):
                try:
                    _log(ctx, "PP_DATA writing BOOTLOADER_MODE addr=0x%06X byte=0x%02X" % (addr, b))
                except Exception:
                    pass
                ctx["state"]["bootflag_prog_logged"] = True
            # NOR flash programming: only 1->0 transitions
            ctx["state"]["flash"][addr] = ctx["state"]["flash"][addr] & b
            ctx["state"]["addr"] = (ctx["state"]["addr"] + 1) & 0xFFFFFF
            ctx["state"]["wip_reads_left"] = 2
        return default_out

    return default_out


def _update_sr(ctx):
    try:
        sr = int(ctx["state"].get("sr", 0)) & 0xFFFF
    except Exception:
        sr = 0
    # TXE always set in this simplified model.
    sr |= 0x02
    # RXNE set if we have unread bytes.
    try:
        if ctx["state"].get("rx_fifo"):
            sr |= 0x01
        else:
            sr &= ~0x01
    except Exception:
        sr &= ~0x01
    # BSY cleared.
    sr &= ~(1 << 7)
    ctx["state"]["sr"] = sr & 0xFFFF


def handle_request(
    request,
    key,
    default_out=0,
    spi_log_enabled=False,
    load_spi_image=False,
    log_prefix="",
    log_spi_dr=False,
    log_spi_sr=False,
):
    outdir = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
    )
    spi_wip_fast = os.environ.get("BC280_SPI_WIP_FAST", "1") == "1"
    state = _get_state(key, outdir, spi_log_enabled, load_spi_image)

    ctx = {
        "outdir": outdir,
        "spi_wip_fast": spi_wip_fast,
        "spi_log_enabled": spi_log_enabled,
        "default_out": int(default_out) & 0xFF,
        "log_prefix": log_prefix,
        "log_spi_dr": log_spi_dr,
        "log_spi_sr": log_spi_sr,
        "state": state,
    }

    off = _get_req(request, OFFSET_ATTRS, 0)
    is_write = bool(_get_req(request, IS_WRITE_ATTRS, False))
    val = _get_req(request, REQUEST_ATTRS, 0) if is_write else 0

    if is_write:
        if off == 0x0:
            state["cr1"] = val & 0xFFFF
        elif off == 0x4:
            state["cr2"] = val & 0xFFFF
        elif off == 0xC:
            # STM32F1: CR1.DFF (bit 11) selects 8/16-bit data frame format.
            dff16 = bool(int(state.get("cr1", 0)) & (1 << 11))
            if dff16:
                bytes_out = [((val >> 8) & 0xFF), (val & 0xFF)]
            else:
                bytes_out = [val & 0xFF]
            # Push all received bytes into an RX FIFO; pack on reads based on DFF.
            rx_fifo = state.get("rx_fifo")
            if rx_fifo is None:
                state["rx_fifo"] = []
                rx_fifo = state["rx_fifo"]
            last = 0
            for bb in bytes_out:
                last = _handle_mosi_byte(ctx, bb & 0xFF)
                if last is None:
                    last = 0
                try:
                    rx_fifo.append(int(last) & 0xFF)
                except Exception:
                    pass
            if log_spi_dr and state.get("cmd_log_left", 0) > 0:
                try:
                    _log(ctx, "SPI_DR write val=0x%04X rx_fifo=%d" % (val & 0xFFFF, len(rx_fifo)))
                    state["cmd_log_left"] = int(state.get("cmd_log_left", 0)) - 1
                except Exception:
                    pass
            state["rx"] = int(last) & 0xFF
            _update_sr(ctx)
    else:
        if off == 0x0:
            _set_req(request, REQUEST_ATTRS, state.get("cr1", 0))
        elif off == 0x4:
            _set_req(request, REQUEST_ATTRS, state.get("cr2", 0))
        elif off == 0x8:
            # Keep bits 1 and 2 set so spi_check_status_flag(base, 1/2) passes.
            _update_sr(ctx)
            if log_spi_sr and state.get("status_log_left", 0) > 0:
                try:
                    _log(
                        ctx,
                        "SPI_SR read sr=0x%02X rx_fifo=%d mode=%s"
                        % (int(state.get("sr", 0)) & 0xFF, len(state.get("rx_fifo") or []), str(state.get("mode"))),
                    )
                    state["status_log_left"] = int(state.get("status_log_left", 0)) - 1
                except Exception:
                    pass
            _set_req(request, REQUEST_ATTRS, state.get("sr", 0x02))
        elif off == 0xC:
            # Read DR: return 8-bit or 16-bit depending on DFF; pop from RX FIFO.
            cr1 = int(state.get("cr1", 0)) & 0xFFFF
            dff16 = bool(cr1 & (1 << 11))
            rxonly = bool(cr1 & (1 << 10))
            rx_fifo = state.get("rx_fifo")
            if rx_fifo is None:
                state["rx_fifo"] = []
                rx_fifo = state["rx_fifo"]

            # RX-only mode: emulate internal dummy clocks by producing MISO bytes on DR reads.
            if rxonly and not rx_fifo and bool(state.get("cs_active")) and str(state.get("mode")) == "READ_DATA":
                want = 2 if dff16 else 1
                for _ in range(want):
                    outb = _handle_mosi_byte(ctx, 0x00)
                    try:
                        rx_fifo.append(int(outb) & 0xFF)
                    except Exception:
                        pass
            if dff16:
                hi = int(rx_fifo.pop(0)) & 0xFF if rx_fifo else 0
                lo = int(rx_fifo.pop(0)) & 0xFF if rx_fifo else 0
                word = ((hi << 8) | lo) & 0xFFFF
                state["rx"] = lo & 0xFF
                _set_req(request, REQUEST_ATTRS, word)
            else:
                b = int(rx_fifo.pop(0)) & 0xFF if rx_fifo else int(state.get("rx", 0)) & 0xFF
                state["rx"] = b & 0xFF
                _set_req(request, REQUEST_ATTRS, b)
            _update_sr(ctx)
        else:
            _set_req(request, REQUEST_ATTRS, 0)
