import os
import System

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)

KEY = "dma1_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)


def _init_state():
    # Default to no filesystem logging (Renode PythonPeripherals are extremely sensitive to I/O).
    try:
        log_budget = int(os.environ.get("BC280_DMA_LOG_BUDGET", "0"))
    except Exception:
        log_budget = 0
    s = {
        "isr": 0,
        "ch": [
            {
                "ccr": 0,
                "cndtr": 0,
                "cpar": 0,
                "cmar": 0,
                # Sticky "initial" config for circular modes and for peripherals that
                # advance CNDTR/CMAR outside of the DMA script (e.g., bridge-driven USART RX).
                "cndtr_init": 0,
                "cmar_init": 0,
                "cpar_init": 0,
            }
            for _ in range(7)
        ],
        "log_budget": log_budget,
        "seen": False,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    return s


if state is None or not hasattr(state, "get") or state.get("ch") is None:
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


def _get_value(default=0):
    return _get_req(REQUEST_ATTRS, default)


def _set_value(val):
    _set_req(REQUEST_ATTRS, val)


def _log(msg):
    if state.get("log_budget", 0) <= 0:
        return
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "dma1_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] -= 1
    except Exception:
        pass


def _dump_env_once():
    # Removed: noisy env dumping (kept as a stub for compatibility with older versions).
    return


def _get_sysbus():
    # Try several likely surfaces depending on Renode python host.
    for attr in ("SystemBus", "sysbus", "Sysbus"):
        try:
            if hasattr(self, attr):
                return getattr(self, attr)
        except Exception:
            pass
    # PythonPeripheral exposes GetMachine/TryGetMachine.
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


def _get_cpu():
    try:
        bus = _get_sysbus()
        if bus is None:
            return None
        cpus = list(bus.GetCPUs())
        return cpus[0] if cpus else None
    except Exception:
        return None


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


def _signal_dma_irq_if_enabled(ch_idx, ccr):
    # STM32F1: DMA1 Channel1..7 IRQ numbers are 11..17
    try:
        if not (int(ccr) & (1 << 1)):  # TCIE
            return
        irq = 11 + int(ch_idx)
        _set_nvic_pending(irq)
        _log("DMA ch%d set NVIC pending (irq=%d)" % (ch_idx + 1, irq))
    except Exception:
        pass


def _read_u16(bus, addr):
    try:
        return int(bus.ReadWord(addr)) & 0xFFFF
    except Exception:
        b = bus.ReadBytes(addr, 2)
        return (int(b[0]) & 0xFF) | ((int(b[1]) & 0xFF) << 8)


def _read_u32(bus, addr):
    try:
        return int(bus.ReadDoubleWord(addr)) & 0xFFFFFFFF
    except Exception:
        b = bus.ReadBytes(addr, 4)
        return (
            (int(b[0]) & 0xFF)
            | ((int(b[1]) & 0xFF) << 8)
            | ((int(b[2]) & 0xFF) << 16)
            | ((int(b[3]) & 0xFF) << 24)
        )


def _write_u16(bus, addr, value):
    try:
        bus.WriteWord(addr, int(value) & 0xFFFF)
    except Exception:
        v = int(value) & 0xFFFF
        bus.WriteBytes(bytearray([v & 0xFF, (v >> 8) & 0xFF]), addr)


def _write_u32(bus, addr, value):
    try:
        bus.WriteDoubleWord(addr, int(value) & 0xFFFFFFFF)
    except Exception:
        v = int(value) & 0xFFFFFFFF
        bus.WriteBytes(
            bytearray(
                [
                    v & 0xFF,
                    (v >> 8) & 0xFF,
                    (v >> 16) & 0xFF,
                    (v >> 24) & 0xFF,
                ]
            ),
            addr,
        )


def _unit_size_from_ccr(ccr, is_mem):
    # STM32F1-style: PSIZE bits [9:8], MSIZE bits [11:10]
    bits = (ccr >> (10 if is_mem else 8)) & 0x3
    if bits == 0:
        return 1
    if bits == 1:
        return 2
    return 4


def _do_transfer(ch_idx):
    bus = _get_sysbus()
    ch = state["ch"][ch_idx]
    ccr = int(ch.get("ccr", 0)) & 0xFFFFFFFF
    count = int(ch.get("cndtr", 0)) & 0xFFFF
    paddr = int(ch.get("cpar", 0)) & 0xFFFFFFFF
    maddr = int(ch.get("cmar", 0)) & 0xFFFFFFFF

    if count == 0:
        return

    # CCR bits (STM32F1):
    # EN(0), TCIE(1), HTIE(2), TEIE(3), DIR(4), CIRC(5), PINC(6), MINC(7),
    # PSIZE(8-9), MSIZE(10-11)
    direction_mem_to_periph = bool(ccr & (1 << 4))

    pinc = bool(ccr & (1 << 6))
    minc = bool(ccr & (1 << 7))

    psize = _unit_size_from_ccr(ccr, is_mem=False)
    msize = _unit_size_from_ccr(ccr, is_mem=True)
    unit = min(psize, msize)
    if unit not in (1, 2, 4):
        unit = 1

    _log("DMA ch%d start count=%d unit=%d m=0x%08X p=0x%08X ccr=0x%08X" % (ch_idx + 1, count, unit, maddr, paddr, ccr))

    # Important correctness: for "peripheral -> memory" channels that are driven by
    # peripheral request lines (e.g., USART RX, SPI RX), do NOT complete the entire
    # transfer immediately on EN=1. Those channels only progress when the peripheral
    # indicates data is ready.
    #
    # For this project we currently drive:
    #   - SPI1 RX via paired SPI1 TX channel (see special-case below)
    #   - USART1 RX via BLE passthrough (TTM:CONNECTED + 0x55 frames)
    # Defer USART RX DMA channels: they are driven by peripheral request lines (RXNE)
    # and should only progress when the peripheral produces bytes.
    if not direction_mem_to_periph and paddr in (0x40013804, 0x40004404):
        _log("DMA ch%d periph->mem deferred for paddr=0x%08X" % (ch_idx + 1, paddr))
        return

    if bus is None:
        # Can't access system bus from this python host; fake completion so firmware can progress.
        tcif_bit = 1 << (1 + 4 * ch_idx)
        state["isr"] |= tcif_bit
        ch["cndtr"] = 0
        _log("DMA ch%d no-sysbus: faking completion" % (ch_idx + 1))
        _signal_dma_irq_if_enabled(ch_idx, ccr)
        return

    # Special-case: SPI full-duplex via paired DMA channels (one TX mem->periph, one RX periph->mem).
    # We drive both from the TX channel to populate the RX buffer with data produced by the SPI peripheral model.
    if direction_mem_to_periph and paddr in (0x4001300C, 0x4000380C):
        rx_ch = None
        rx_idx = None
        for j, other in enumerate(state["ch"]):
            if j == ch_idx:
                continue
            occr = int(other.get("ccr", 0)) & 0xFFFFFFFF
            if not (occr & 1):
                continue
            if bool(occr & (1 << 4)):
                continue  # also TX
            if int(other.get("cpar", 0)) & 0xFFFFFFFF != paddr:
                continue
            rx_ch = other
            rx_idx = j
            break
        if rx_ch is not None:
            rx_count = int(rx_ch.get("cndtr", 0)) & 0xFFFF
            rx_maddr = int(rx_ch.get("cmar", 0)) & 0xFFFFFFFF
            rx_unit = min(_unit_size_from_ccr(int(rx_ch.get("ccr", 0)), is_mem=True), unit)
            n = min(count, rx_count) if rx_count else count
            _log("DMA ch%d paired with RX ch%d n=%d rx_m=0x%08X" % (ch_idx + 1, rx_idx + 1, n, rx_maddr))

            for _ in range(n):
                # TX word -> SPI DR
                if unit == 2:
                    w = _read_u16(bus, maddr)
                    _write_u16(bus, paddr, w)
                elif unit == 1:
                    b = int(bus.ReadBytes(maddr, 1)[0]) & 0xFF
                    bus.WriteBytes(bytearray([b]), paddr)
                else:
                    w = _read_u32(bus, maddr)
                    _write_u32(bus, paddr, w)

                # RX word <- SPI DR
                if rx_unit == 2:
                    rw = _read_u16(bus, paddr)
                    _write_u16(bus, rx_maddr, rw)
                elif rx_unit == 1:
                    rb = int(bus.ReadBytes(paddr, 1)[0]) & 0xFF
                    bus.WriteBytes(bytearray([rb]), rx_maddr)
                else:
                    rw = _read_u32(bus, paddr)
                    _write_u32(bus, rx_maddr, rw)

                if minc:
                    maddr = (maddr + unit) & 0xFFFFFFFF
                if rx_ch.get("ccr", 0) & (1 << 7):
                    rx_maddr = (rx_maddr + rx_unit) & 0xFFFFFFFF

            # mark both channels complete
            tcif_tx = 1 << (1 + 4 * ch_idx)
            tcif_rx = 1 << (1 + 4 * rx_idx)
            state["isr"] |= (tcif_tx | tcif_rx)
            ch["cndtr"] = 0
            rx_ch["cndtr"] = 0
            _log("DMA paired complete tx=0x%08X rx=0x%08X" % (tcif_tx, tcif_rx))
            _signal_dma_irq_if_enabled(ch_idx, ccr)
            _signal_dma_irq_if_enabled(rx_idx, int(rx_ch.get("ccr", 0)))
            return

    if not direction_mem_to_periph:
        # Generic periph->mem: copy current peripheral register value into memory.
        for _ in range(count):
            if unit == 2:
                w = _read_u16(bus, paddr)
                _write_u16(bus, maddr, w)
            elif unit == 1:
                b = int(bus.ReadBytes(paddr, 1)[0]) & 0xFF
                bus.WriteBytes(bytearray([b]), maddr)
            else:
                w = _read_u32(bus, paddr)
                _write_u32(bus, maddr, w)
            if minc:
                maddr = (maddr + unit) & 0xFFFFFFFF
            if pinc:
                paddr = (paddr + unit) & 0xFFFFFFFF
        tcif_bit = 1 << (1 + 4 * ch_idx)
        state["isr"] |= tcif_bit
        ch["cndtr"] = 0
        _log("DMA ch%d done TCIF bit=0x%08X" % (ch_idx + 1, tcif_bit))
        _signal_dma_irq_if_enabled(ch_idx, ccr)
        return

    for _ in range(count):
        if unit == 1:
            try:
                data = int(bus.ReadByte(maddr)) & 0xFF
            except Exception:
                data = int(bus.ReadBytes(maddr, 1)[0]) & 0xFF
            try:
                bus.WriteByte(paddr, data)
            except Exception:
                bus.WriteBytes(bytearray([data & 0xFF]), paddr)
        elif unit == 2:
            w = _read_u16(bus, maddr)
            _write_u16(bus, paddr, w)
        else:
            w = _read_u32(bus, maddr)
            _write_u32(bus, paddr, w)

        if minc:
            maddr = (maddr + unit) & 0xFFFFFFFF
        if pinc:
            paddr = (paddr + unit) & 0xFFFFFFFF

    # Mark transfer complete: TCIFx bit in DMA_ISR (STM32F1 layout: 4 bits per channel).
    tcif_bit = 1 << (1 + 4 * ch_idx)
    state["isr"] |= tcif_bit
    ch["cndtr"] = 0
    _log("DMA ch%d done TCIF bit=0x%08X" % (ch_idx + 1, tcif_bit))
    _signal_dma_irq_if_enabled(ch_idx, ccr)


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_value(0)) if is_write else 0

if is_write:
    if not state.get("seen"):
        _log("DMA1 first access (write)")
        state["seen"] = True
    if state.get("log_budget", 0) > 0:
        _log("W off=0x%X val=0x%X" % (off, val))
    # Global ISR/IFCR
    if off == 0x04:
        # IFCR: write-1-to-clear flags
        state["isr"] &= ~int(val) & 0xFFFFFFFF
    else:
        # Channel registers
        if off >= 0x08:
            ch_idx = (off - 0x08) // 0x14
            reg_off = (off - 0x08) % 0x14
            if 0 <= ch_idx < 7:
                ch = state["ch"][ch_idx]
                if reg_off == 0x00:
                    prev = int(ch.get("ccr", 0))
                    ch["ccr"] = int(val) & 0xFFFFFFFF
                    prev_en = bool(prev & 1)
                    now_en = bool(ch["ccr"] & 1)
                    if (not prev_en) and now_en:
                        _do_transfer(ch_idx)
                elif reg_off == 0x04:
                    ch["cndtr"] = int(val) & 0xFFFF
                    # Remember for circular mode reloads.
                    if not (int(ch.get("ccr", 0)) & 1):
                        ch["cndtr_init"] = int(val) & 0xFFFF
                    # Some firmwares enable DMA before programming CNDTR/CPAR/CMAR.
                    # If EN is already set and we now have a non-zero count, kick the transfer.
                    if int(ch.get("ccr", 0)) & 1 and int(ch.get("cndtr", 0)) > 0:
                        _do_transfer(ch_idx)
                elif reg_off == 0x08:
                    ch["cpar"] = int(val) & 0xFFFFFFFF
                    if not (int(ch.get("ccr", 0)) & 1):
                        ch["cpar_init"] = int(val) & 0xFFFFFFFF
                    if int(ch.get("ccr", 0)) & 1 and int(ch.get("cndtr", 0)) > 0:
                        _do_transfer(ch_idx)
                elif reg_off == 0x0C:
                    ch["cmar"] = int(val) & 0xFFFFFFFF
                    if not (int(ch.get("ccr", 0)) & 1):
                        ch["cmar_init"] = int(val) & 0xFFFFFFFF
                    if int(ch.get("ccr", 0)) & 1 and int(ch.get("cndtr", 0)) > 0:
                        _do_transfer(ch_idx)
else:
    if not state.get("seen"):
        _log("DMA1 first access (read)")
        state["seen"] = True
    if off == 0x00:
        _set_value(int(state.get("isr", 0)) & 0xFFFFFFFF)
    elif off == 0x04:
        _set_value(0)
    elif off >= 0x08:
        ch_idx = (off - 0x08) // 0x14
        reg_off = (off - 0x08) % 0x14
        if 0 <= ch_idx < 7:
            ch = state["ch"][ch_idx]
            if reg_off == 0x00:
                _set_value(int(ch.get("ccr", 0)) & 0xFFFFFFFF)
            elif reg_off == 0x04:
                _set_value(int(ch.get("cndtr", 0)) & 0xFFFF)
            elif reg_off == 0x08:
                _set_value(int(ch.get("cpar", 0)) & 0xFFFFFFFF)
            elif reg_off == 0x0C:
                _set_value(int(ch.get("cmar", 0)) & 0xFFFFFFFF)
            else:
                _set_value(0)
    else:
        _set_value(0)
