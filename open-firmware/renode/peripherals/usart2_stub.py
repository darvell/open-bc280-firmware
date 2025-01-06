import os
import threading
import time
import System

from renode_utils import b2i as _b2i, tohex as _tohex

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)
# UART2 = motor controller bus (Shengyi DWG22).

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


KEY = "usart2_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)


def _init_state():
    s = {
        # Minimal STM32F1-ish USART register set (offsets from base 0x40004400).
        "sr": 0x000000C0,  # TXE|TC set initially (bits 7,6)
        "dr": 0x00000000,
        "brr": 0x00000000,
        "cr1": 0x00000000,
        "cr2": 0x00000000,
        "cr3": 0x00000000,
        "sr_read": False,
        # Queues
        "rx_fifo": [],  # list of ints (0..255)
        "tx_fifo": [],  # list of ints (0..255)
        "tx_log": bytearray(),
        # Logging
        "log_budget": 400,
        "seen": False,
        "rx_reads": 0,
        "tx_writes": 0,
        # Optional RX file bridge (set BC280_UART2_RX to a file path).
        "rx_file_failed": False,
        "rx_file_path": None,
        "rx_file_thread": None,
        "rx_log_left": 20,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get"):
    state = _init_state()


def _log(msg):
    if state.get("log_budget", 0) <= 0:
        return
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "usart2_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] -= 1
    except Exception:
        pass


def _log_rx(msg):
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "usart2_rx_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
    except Exception:
        pass


def _emit_tx_byte(b):
    # Tap TX bytes into a host-visible log so headless Renode runs can assert UART output.
    try:
        b = int(b) & 0xFF
        state.setdefault("tx_log", bytearray()).append(b)
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "uart2_tx.log"), "ab") as f:
            f.write(bytes([b]))
    except Exception:
        pass


def _start_thread(target, args=()):
    t = threading.Thread(target=target, args=args)
    try:
        t.setDaemon(True)
    except Exception:
        pass
    t.start()
    return t


def _get_sysbus():
    # Similar to renode/usart1_stub.py: tolerate different Python host surfaces.
    for attr in ("SystemBus", "sysbus", "Sysbus"):
        try:
            if hasattr(self, attr):
                return getattr(self, attr)
        except Exception:
            pass
    try:
        if hasattr(self, "GetMachine"):
            m = self.GetMachine()
            if m is not None and hasattr(m, "SystemBus"):
                return m.SystemBus
    except Exception:
        pass
    try:
        if hasattr(self, "TryGetMachine"):
            ok, m = self.TryGetMachine()
            if ok and m is not None and hasattr(m, "SystemBus"):
                return m.SystemBus
    except Exception:
        pass
    try:
        if hasattr(self, "Machine"):
            return self.Machine["sysbus"]
    except Exception:
        pass
    try:
        return self["sysbus"]
    except Exception:
        pass
    return None


def _write_u32(bus, addr, value):
    try:
        bus.WriteDoubleWord(int(addr) & 0xFFFFFFFF, int(value) & 0xFFFFFFFF)
    except Exception:
        v = int(value) & 0xFFFFFFFF
        bus.WriteBytes(
            bytearray([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF]),
            int(addr) & 0xFFFFFFFF,
        )


def _set_nvic_pending(irq):
    bus = _get_sysbus()
    if bus is None:
        return
    try:
        irq = int(irq)
        if irq < 0:
            return
        reg = 0xE000E200 + 4 * (irq // 32)  # NVIC_ISPRx
        bit = 1 << (irq % 32)
        _write_u32(bus, reg, bit)
    except Exception:
        return


def _update_irq_lines():
    # Implement minimal behavior needed by the shared USART2 IRQ handler (IRQ38):
    # - RXNE interrupt when CR1.RXNEIE and SR.RXNE are set
    # - TC interrupt when CR1.TCIE and SR.TC are set
    # - TXE interrupt when CR1.TXEIE and SR.TXE are set (some builds may use it)
    try:
        sr = int(state.get("sr", 0)) & 0xFFFFFFFF
        cr1 = int(state.get("cr1", 0)) & 0xFFFFFFFF
        # CR1 bits: UE(13), RE(2), TE(3), RXNEIE(5), TCIE(6), TXEIE(7)
        ue = bool(cr1 & (1 << 13))
        rxneie = bool(cr1 & (1 << 5))
        tcie = bool(cr1 & (1 << 6))
        txeie = bool(cr1 & (1 << 7))
        rxne = bool(sr & (1 << 5))
        tc = bool(sr & (1 << 6))
        txe = bool(sr & (1 << 7))
        if ue and rxneie and rxne:
            _set_nvic_pending(38)
        if ue and tcie and tc:
            _set_nvic_pending(38)
        if ue and txeie and txe:
            _set_nvic_pending(38)
    except Exception:
        pass


def _recompute_sr():
    # RXNE depends on rx_fifo contents.
    sr = int(state.get("sr", 0)) & 0xFFFFFFFF
    if state.get("rx_fifo"):
        sr |= (1 << 5)  # RXNE
    else:
        sr &= ~(1 << 5)
    # If we're not explicitly simulating a busy transmitter, keep TXE/TC asserted.
    if not state.get("tx_busy"):
        sr |= (1 << 7)  # TXE
        sr |= (1 << 6)  # TC
    state["sr"] = sr & 0xFFFFFFFF
    return sr


def _consume_rx_file():
    if state.get("rx_file_failed"):
        return
    path = os.environ.get("BC280_UART2_RX")
    if not path:
        return
    state["rx_file_path"] = path
    try:
        if not os.path.isfile(path):
            return
        with open(path, "rb") as f:
            data = f.read()
        if not data:
            return
        # Truncate file after consumption.
        try:
            with open(path, "wb") as f:
                f.write(b"")
        except Exception:
            pass
        if state.get("rx_log_left", 0) > 0:
            _log_rx("RX file len=%d bytes=%s" % (len(data), _tohex(data[:16])))
            state["rx_log_left"] = state.get("rx_log_left", 1) - 1
        state.setdefault("rx_fifo", []).extend([_b2i(b) & 0xFF for b in data])
        _recompute_sr()
        _update_irq_lines()
    except Exception as e:
        state["rx_file_failed"] = True
        _log("USART2 RX file read failed: %s" % str(e))


def _rx_file_worker():
    while True:
        try:
            _consume_rx_file()
        except Exception:
            pass
        time.sleep(0.05)


def _ensure_rx_file_thread():
    if state.get("rx_file_thread") is not None:
        return
    if not os.environ.get("BC280_UART2_RX"):
        return
    state["rx_file_thread"] = _start_thread(_rx_file_worker)


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

_ensure_rx_file_thread()
_consume_rx_file()

if is_write:
    if not state.get("seen"):
        _log("USART2 first access (write)")
        state["seen"] = True
    if state.get("log_budget", 0) > 0:
        _log("W off=0x%X val=0x%X" % (off, val))

    if off == 0x04:
        # DR write (TX). Support 9-bit writes; store low 8 for wire.
        dr = int(val) & 0x1FF
        state["dr"] = dr
        state["tx_fifo"].append(dr & 0xFF)
        _emit_tx_byte(dr & 0xFF)
        try:
            state["tx_writes"] = int(state.get("tx_writes", 0)) + 1
        except Exception:
            state["tx_writes"] = 1
        # Writing DR clears TC/TXE briefly on real hardware; emulate "busy then complete".
        sr = int(state.get("sr", 0)) & 0xFFFFFFFF
        sr &= ~(1 << 7)  # TXE = 0
        sr &= ~(1 << 6)  # TC = 0
        state["sr"] = sr
        state["tx_busy"] = True
        state["tx_busy"] = False
        _recompute_sr()
        _update_irq_lines()
    elif off == 0x00:
        # SR is mostly read-only; ignore writes.
        pass
    elif off == 0x08:
        state["brr"] = int(val) & 0xFFFFFFFF
    elif off == 0x0C:
        state["cr1"] = int(val) & 0xFFFFFFFF
        _recompute_sr()
        _update_irq_lines()
    elif off == 0x10:
        state["cr2"] = int(val) & 0xFFFFFFFF
    elif off == 0x14:
        state["cr3"] = int(val) & 0xFFFFFFFF
    else:
        pass
else:
    if not state.get("seen"):
        _log("USART2 first access (read)")
        state["seen"] = True

    if off == 0x00:
        state["sr_read"] = True
        sr = int(_recompute_sr()) & 0xFFFFFFFF
        if state.get("log_budget", 0) > 0:
            _log("R off=0x00 sr=0x%X" % sr)
        _set_req(REQUEST_ATTRS, sr)
    elif off == 0x04:
        # DR read (RX). Pop from RX FIFO, clear RXNE if empty.
        b = 0
        try:
            if state.get("rx_fifo"):
                b = int(state["rx_fifo"].pop(0)) & 0xFF
        except Exception:
            b = 0
        state["dr"] = b
        try:
            state["rx_reads"] = int(state.get("rx_reads", 0)) + 1
        except Exception:
            state["rx_reads"] = 1
        # Clear ORE if firmware did the standard SR->DR read sequence.
        try:
            if state.get("sr_read"):
                state["sr"] = int(state.get("sr", 0)) & ~(1 << 3)
        except Exception:
            pass
        state["sr_read"] = False
        _recompute_sr()
        if state.get("log_budget", 0) > 0:
            _log("R off=0x04 dr=0x%02X rx_reads=%d tx_writes=%d rx_fifo_len=%d" % (int(b) & 0xFF, int(state.get("rx_reads", 0)), int(state.get("tx_writes", 0)), len(state.get("rx_fifo") or [])))
        _set_req(REQUEST_ATTRS, int(b) & 0xFFFFFFFF)
        _update_irq_lines()
    elif off == 0x08:
        _set_req(REQUEST_ATTRS, int(state.get("brr", 0)) & 0xFFFFFFFF)
    elif off == 0x0C:
        _set_req(REQUEST_ATTRS, int(state.get("cr1", 0)) & 0xFFFFFFFF)
    elif off == 0x10:
        _set_req(REQUEST_ATTRS, int(state.get("cr2", 0)) & 0xFFFFFFFF)
    elif off == 0x14:
        _set_req(REQUEST_ATTRS, int(state.get("cr3", 0)) & 0xFFFFFFFF)
    else:
        _set_req(REQUEST_ATTRS, 0)
