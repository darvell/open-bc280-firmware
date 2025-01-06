import os
import System

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "output")
)

KEY = "flash_if_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)

FLASH_BASE = 0x40022000
FLASH_ACR = 0x00
FLASH_KEYR = 0x04
FLASH_OPTKEYR = 0x08
FLASH_SR = 0x0C
FLASH_CR = 0x10
FLASH_AR = 0x14

FLASH_UNLOCK_KEY1 = 0x45670123
FLASH_UNLOCK_KEY2 = 0xCDEF89AB

# STM32F1-style bits (AT32F403 similar for basic erase/program).
FLASH_CR_PG = 1 << 0
FLASH_CR_PER = 1 << 1
FLASH_CR_MER = 1 << 2
FLASH_CR_STRT = 1 << 6
FLASH_CR_LOCK = 1 << 7

FLASH_SR_BSY = 1 << 0
FLASH_SR_EOP = 1 << 5

APP_BASE = 0x08010000
APP_SIZE = 0x23000
APP_PAGE = 0x800


def _init_state():
    s = {
        "acr": 0,
        "keyr": 0,
        "optkeyr": 0,
        "sr": 0,
        "cr": FLASH_CR_LOCK,
        "ar": 0,
        "unlock_step": 0,
        "log_budget": 200,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "flash_if_debug.txt"), "a") as f:
            f.write("FLASH_IF init\n")
    except Exception:
        pass
    return s


if state is None or not hasattr(state, "get"):
    state = _init_state()


def _log(msg):
    if state.get("log_budget", 0) <= 0:
        return
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "flash_if_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] -= 1
    except Exception:
        pass


def _get_sysbus():
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


def _write_bytes(bus, addr, data):
    try:
        bus.WriteBytes(int(addr) & 0xFFFFFFFF, data)
        return
    except Exception:
        pass
    for i, b in enumerate(bytearray(data)):
        try:
            bus.WriteByte((int(addr) + i) & 0xFFFFFFFF, int(b) & 0xFF)
        except Exception:
            return


def _erase_range(bus, start, size):
    if bus is None or size <= 0:
        return
    size = int(size)
    chunk = bytes([0xFF]) * 256
    addr = int(start)
    end = addr + size
    while addr < end:
        remain = end - addr
        block = chunk if remain >= len(chunk) else bytes([0xFF]) * remain
        _write_bytes(bus, addr, block)
        addr += len(block)


def _flash_alias_addr(addr):
    addr = int(addr) & 0xFFFFFFFF
    if 0x08000000 <= addr < 0x08080000:
        return addr - 0x08000000
    return None


def _erase_page(bus, addr):
    base = int(addr) & ~(APP_PAGE - 1)
    _erase_range(bus, base, APP_PAGE)
    alias = _flash_alias_addr(base)
    if alias is not None:
        _erase_range(bus, alias, APP_PAGE)


def _erase_app_region(bus):
    _erase_range(bus, APP_BASE, APP_SIZE)
    alias = _flash_alias_addr(APP_BASE)
    if alias is not None:
        _erase_range(bus, alias, APP_SIZE)


def _handle_key(value):
    if state.get("unlock_step", 0) == 0 and value == FLASH_UNLOCK_KEY1:
        state["unlock_step"] = 1
        return
    if state.get("unlock_step", 0) == 1 and value == FLASH_UNLOCK_KEY2:
        state["unlock_step"] = 0
        state["cr"] &= ~FLASH_CR_LOCK
        _log("FLASH unlocked")
        return
    state["unlock_step"] = 0


def _set_sr_done():
    state["sr"] &= ~FLASH_SR_BSY
    state["sr"] |= FLASH_SR_EOP


def _handle_cr_write(value):
    value = int(value) & 0xFFFFFFFF
    state["cr"] = value
    if value & FLASH_CR_STRT:
        bus = _get_sysbus()
        if value & FLASH_CR_MER:
            _log("FLASH MER (mass erase)")
            _erase_app_region(bus)
            _set_sr_done()
        elif value & FLASH_CR_PER:
            _log("FLASH PER addr=0x%08X" % (state.get("ar") or 0))
            _erase_page(bus, state.get("ar") or 0)
            _set_sr_done()
        else:
            _set_sr_done()


def _read(offset):
    if offset == FLASH_ACR:
        return state.get("acr", 0)
    if offset == FLASH_KEYR:
        return state.get("keyr", 0)
    if offset == FLASH_OPTKEYR:
        return state.get("optkeyr", 0)
    if offset == FLASH_SR:
        return state.get("sr", 0)
    if offset == FLASH_CR:
        return state.get("cr", 0)
    if offset == FLASH_AR:
        return state.get("ar", 0)
    return 0


def _write(offset, value):
    if offset == FLASH_ACR:
        state["acr"] = int(value) & 0xFFFFFFFF
    elif offset == FLASH_KEYR:
        state["keyr"] = int(value) & 0xFFFFFFFF
        _log("FLASH_KEYR=0x%08X" % (state["keyr"]))
        _handle_key(state["keyr"])
    elif offset == FLASH_OPTKEYR:
        state["optkeyr"] = int(value) & 0xFFFFFFFF
    elif offset == FLASH_SR:
        # Write 1 to clear bits (EOP, errors); ignore BSY.
        _log("FLASH_SR clr=0x%08X" % (int(value) & 0xFFFFFFFF))
        state["sr"] &= ~int(value)
    elif offset == FLASH_CR:
        _log("FLASH_CR=0x%08X" % (int(value) & 0xFFFFFFFF))
        _handle_cr_write(value)
    elif offset == FLASH_AR:
        state["ar"] = int(value) & 0xFFFFFFFF
        _log("FLASH_AR=0x%08X" % (state["ar"]))
    else:
        pass


if request is not None:
    try:
        offset = int(getattr(request, "Offset", 0)) & 0xFFFFFFFF
    except Exception:
        offset = 0
    try:
        is_write = bool(getattr(request, "IsWrite", False))
    except Exception:
        is_write = False
    if is_write:
        try:
            value = int(getattr(request, "Value", 0)) & 0xFFFFFFFF
        except Exception:
            value = 0
        _write(offset, value)
    else:
        value = _read(offset)
        try:
            setattr(request, "Value", int(value) & 0xFFFFFFFF)
        except Exception:
            pass
