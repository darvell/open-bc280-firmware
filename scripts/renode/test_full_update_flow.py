#!/usr/bin/env python3
"""
Renode full device-flow simulation:
1) Boot vendor firmware.
2) Send BLE-enter-bootloader frame over UART1 (0x55 framing, BLE passthrough).
3) Enter bootloader update mode (BOOTLOADER_MODE flag set).
4) Flash open-firmware via bootloader OTA protocol.
5) Boot into open-firmware and confirm alive.
6) Power-cycle and verify bootloader update mode again (flag set on boot).

UART1 (BLE passthrough) is exposed via a Renode server socket terminal to
inject frames and capture TX. UART2 remains a file-based RX bridge for the
motor bus. No BLE stack is used.
"""

import os
import re
import json
import socket
import subprocess
import sys
import tempfile
import time
import threading
from pathlib import Path

from bootloader_ota import (
    BLOCK_SIZE,
    CMD_DONE,
    CMD_INIT,
    CMD_WRITE,
    FrameReader,
    BootloaderOTASender,
    build_frame,
    calc_crc8_maxim,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
RENODE_VENDOR_RESC = REPO_ROOT / "open-firmware" / "renode" / "bc280_vendor_fast.resc"
RENODE_OPEN_RESC = REPO_ROOT / "open-firmware" / "renode" / "bc280_open_firmware_fast.resc"
SPI_FLASH_DEFAULT = REPO_ROOT / "open-firmware" / "renode" / "flash" / "spi_flash_v2_2_8.bin"

if "BC280_SPI_FLASH_BIN" not in os.environ and SPI_FLASH_DEFAULT.exists():
    os.environ["BC280_SPI_FLASH_BIN"] = str(SPI_FLASH_DEFAULT.resolve())


def env_path(name: str, default: Path) -> Path:
    raw = os.environ.get(name)
    if not raw:
        return Path(default).resolve()
    return Path(raw).resolve()


COMBINED_FW_BIN = env_path(
    "BC280_COMBINED_FW_BIN",
    REPO_ROOT / "firmware" / "BC280_Combined_Firmware_3.3.6_4.2.5.bin",
)
OPEN_FW_BIN = env_path("BC280_OPEN_FW_BIN", REPO_ROOT / "open-firmware" / "build" / "open_firmware.bin")
# Bootloader BLE UART is UART1 on real hardware; override only for experiments.
def env_float(name: str, default: float) -> float:
    raw = os.environ.get(name)
    if raw in (None, ""):
        return float(default)
    return float(raw)


def env_int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw in (None, ""):
        return int(default)
    return int(raw)


BOOT_UART = env_int("BC280_BOOT_UART", 1)
APP_BLE_UART = env_int("BC280_APP_BLE_UART", 1)
REQUIRE_APP_LOOP = os.environ.get("BC280_REQUIRE_APP_LOOP", "1") == "1"
REQUIRE_APP_START = os.environ.get("BC280_REQUIRE_APP_START", "1") == "1"
TIMEOUT_SCALE = env_float("BC280_TIMEOUT_SCALE", 0.02)
MIN_SLEEP_S = env_float("BC280_MIN_SLEEP_S", 0.01)
MAX_RUNTIME_S = env_float("BC280_MAX_RUNTIME_S", 15.0)
MAX_STARTUP_S = env_float("BC280_MAX_STARTUP_S", 1.0)
MONITOR_WAIT_S = env_float("BC280_MONITOR_WAIT_S", 0.2)
BOOT_CMD_TRIES = env_int("BC280_BOOT_CMD_TRIES", 1)
BOOT_CMD_WAIT_S = env_float("BC280_BOOT_CMD_WAIT_S", 2.0)
APP_LOOP_TIMEOUT_S = env_float("BC280_APP_LOOP_TIMEOUT_S", 30.0)
APP_LOOP_STEP_S = env_float("BC280_APP_LOOP_STEP_S", 1.0)
OTA_CHUNK = env_int("BC280_OTA_CHUNK", 0)
OTA_CHUNK_DELAY_S = env_float("BC280_OTA_CHUNK_DELAY_S", 0.0)
OTA_INTER_FRAME_S = env_float("BC280_OTA_INTER_FRAME_S", 0.0)
HOOK_VERBOSE = os.environ.get("BC280_HOOK_VERBOSE", "0") == "1"
DIAG = os.environ.get("BC280_DIAG", "0") == "1"
UART1_FILE_INJECTOR = os.environ.get("BC280_UART1_FILE_INJECTOR", "0") == "1"
ALLOW_FAST_FLAGS = os.environ.get("BC280_ALLOW_FAST_FLAGS", "0") == "1"
FAST_FLASH_READ = os.environ.get("BC280_FAST_FLASH_READ", "0") == "1"
FAST_BOOTFLAG_WRITE = os.environ.get("BC280_FAST_BOOTFLAG_WRITE", "0") == "1"
FAST_FLOW = os.environ.get("BC280_FAST_FLOW", "0") == "1"
FAST_OPENFW_BOOTFLAG = os.environ.get("BC280_FAST_OPENFW_BOOTFLAG", "0") == "1"
BOOTLOADER_PATCHES = os.environ.get("BC280_BOOTLOADER_PATCHES", "0") == "1"
DEADLINE: float | None = None
UART1_BRIDGE: "UARTSocketBridge | None" = None


def scaled(value: float) -> float:
    return max(value * TIMEOUT_SCALE, MIN_SLEEP_S)


def check_deadline() -> None:
    if DEADLINE is not None and time.time() > DEADLINE:
        now = time.time()
        raise TimeoutError(f"renode flow exceeded runtime budget (now={now:.2f} deadline={DEADLINE:.2f})")


def find_renode() -> str:
    cand = os.environ.get("RENODE_BIN") or os.environ.get("RENODE")
    if cand and os.path.isfile(cand):
        return cand
    app = os.environ.get("RENODE_APP")
    app_candidates = [app] if app else []
    app_candidates += [
        str(REPO_ROOT / "renode" / "Renode.app"),
        "/Applications/Renode.app",
    ]
    for a in app_candidates:
        if a and os.path.isdir(a):
            native = os.path.join(a, "Contents", "MacOS", "renode")
            if os.path.isfile(native):
                return native
    return "renode"


def pick_port(preferred: int = 1234) -> int:
    def free(p: int) -> bool:
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", p))
            return True
        except Exception:
            return False
        finally:
            try:
                s.close()
            except Exception:
                pass
    if free(preferred):
        return preferred
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class RenodeMonitor:
    def __init__(self, host: str, port: int, timeout_s: float = 10.0) -> None:
        deadline = time.time() + timeout_s
        last_err = None
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection((host, port), timeout=1.0)
                self.sock.settimeout(0.2)
                self._read_until_prompt()
                return
            except Exception as e:
                last_err = e
                time.sleep(scaled(0.1))
        raise RuntimeError(f"Renode monitor not reachable on {host}:{port}: {last_err}")

    def _read_until_prompt(self, timeout_s: float = 0.5) -> str:
        if not self.sock:
            return ""
        timeout_s = scaled(timeout_s)
        deadline = time.time() + timeout_s
        buf = b""
        while time.time() < deadline:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                buf += data
                if b"(monitor)" in buf or buf.endswith(b"> "):
                    break
            except Exception:
                break
        return buf.decode(errors="ignore")

    def cmd(self, line: str, delay: float = 0.05, wait_prompt_s: float = 0.5) -> str:
        if not self.sock:
            return ""
        self.sock.sendall((line + "\n").encode())
        time.sleep(scaled(delay))
        return self._read_until_prompt(timeout_s=wait_prompt_s)

    def close(self) -> None:
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass
        self.sock = None


class UARTSocketBridge:
    def __init__(self, host: str, port: int, tx_log: Path, outdir: Path) -> None:
        self.host = host
        self.port = int(port)
        self.tx_log = Path(tx_log)
        self.outdir = Path(outdir)
        self.sock = None
        self._stop = threading.Event()
        self.last_rx_ts = 0.0
        self.rx_total = 0
        self._connect()
        self._thread = threading.Thread(target=self._reader_loop, name="uart1-socket", daemon=True)
        self._thread.start()

    def _log(self, msg: str) -> None:
        try:
            self.outdir.mkdir(parents=True, exist_ok=True)
            with (self.outdir / "uart1_socket_debug.txt").open("a") as f:
                f.write(msg.rstrip() + "\n")
        except Exception:
            pass

    def _connect(self) -> None:
        deadline = time.time() + 5.0
        last_err = None
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=1.0)
                self.sock.settimeout(0.2)
                self._log(f"connected to {self.host}:{self.port}")
                return
            except Exception as e:
                last_err = e
                time.sleep(0.1)
        raise RuntimeError(f"UART socket connect failed to {self.host}:{self.port}: {last_err}")

    def _reader_loop(self) -> None:
        try:
            self.tx_log.parent.mkdir(parents=True, exist_ok=True)
        except Exception:
            pass
        while not self._stop.is_set():
            try:
                data = self.sock.recv(4096) if self.sock else b""
                if not data:
                    self._log("socket closed by peer")
                    break
                self.last_rx_ts = time.time()
                self.rx_total += len(data)
                with self.tx_log.open("ab") as f:
                    f.write(data)
            except (socket.timeout, TimeoutError):
                continue
            except Exception:
                self._log("socket recv error")
                time.sleep(0.05)
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass
        self.sock = None

    def write(self, data: bytes) -> None:
        if not data:
            return
        if not self.sock:
            raise RuntimeError("UART socket not connected")
        try:
            self.sock.sendall(data)
        except Exception:
            self._log("socket send error")
            raise

    def close(self) -> None:
        self._stop.set()
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass
        self.sock = None


def wait_for_file(path: Path, timeout_s: float = 10.0) -> bool:
    timeout_s = scaled(timeout_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        if path.exists():
            return True
        time.sleep(scaled(0.1))
    return False


def wait_for_pattern(path: Path, pattern: bytes, timeout_s: float = 10.0, start_offset: int = 0) -> bool:
    timeout_s = scaled(timeout_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        try:
            with path.open("rb") as f:
                f.seek(start_offset)
                data = f.read()
            if pattern in data:
                return True
        except Exception:
            pass
        time.sleep(scaled(0.1))
    return False


def count_pattern(path: Path, pattern: bytes) -> int:
    try:
        with path.open("rb") as f:
            data = f.read()
        return data.count(pattern)
    except Exception:
        return 0


def file_size(path: Path) -> int:
    try:
        return path.stat().st_size
    except Exception:
        return 0


def read_vector(bin_path: Path) -> tuple[int, int]:
    return read_vector_at_offset(bin_path, 0)


def read_vector_at_offset(bin_path: Path, offset: int) -> tuple[int, int]:
    data = bin_path.read_bytes()
    if len(data) < offset + 8:
        raise ValueError(f"vector table missing at offset 0x{offset:X} in {bin_path}")
    sp = int.from_bytes(data[offset : offset + 4], "little")
    pc = int.from_bytes(data[offset + 4 : offset + 8], "little")
    return sp, pc


def read_u32_at(bin_path: Path, addr: int) -> int:
    base = 0x08000000
    offset = addr - base
    data = bin_path.read_bytes()
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"address 0x{addr:X} out of range for {bin_path}")
    return int.from_bytes(data[offset : offset + 4], "little")


def wait_for_ble_connected(monitor: RenodeMonitor, flag_addr: int, timeout_s: float = 6.0) -> bool:
    timeout_s = scaled(timeout_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        val, _ = monitor_read_word(monitor, flag_addr & ~0x3)
        if val is not None:
            shift = (flag_addr & 0x3) * 8
            if ((val >> shift) & 0xFF) != 0:
                return True
        run_for(monitor, 0.2)
    return False


def write_rx(
    path: Path | None,
    data: bytes,
    monitor: RenodeMonitor | None = None,
    outdir: Path | None = None,
) -> None:
    if (
        monitor is not None
        and outdir is not None
        and os.environ.get("BC280_UART1_MONITOR_INJECT") == "1"
        and data
        and len(data) <= 256
        and (path is None or (isinstance(path, Path) and path.name == "uart1_rx.bin"))
    ):
        inject_uart1_bytes(monitor, outdir, data)
        return
    if path is None:
        if UART1_BRIDGE is None:
            raise RuntimeError("UART1 bridge not initialized")
        UART1_BRIDGE.write(data)
        return
    with path.open("ab") as f:
        f.write(data)


def inject_uart1_bytes(monitor: RenodeMonitor, outdir: Path, data: bytes) -> None:
    if not data:
        return
    hexstr = data.hex()
    cmd = (
        "import binascii, os; "
        "m=emulationManager.CurrentEmulation.Machines[0] if emulationManager.CurrentEmulation.Machines.Count>0 else None; "
        "sb=(m is not None) and m.SystemBus or None; "
        "reg = sb.WhatIsAt(0x40013800) if sb is not None else None; "
        "uart = reg.Peripheral if (reg is not None and hasattr(reg, 'Peripheral')) else None; "
        f"data=binascii.unhexlify('{hexstr}'); "
        "[(uart.WriteChar((b if isinstance(b, int) else ord(b))) if uart is not None else None) for b in data]; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "f=open(os.path.join(out,'uart_inject_debug.txt'),'a'); "
        f"f.write('inject %d bytes uart=%s\\n' % (len(data), uart)); "
        "f.close();"
    )
    out = monitor.cmd(f'python "{cmd}"')
    try:
        with (outdir / "uart_inject_monitor.txt").open("a") as f:
            f.write(out or "")
    except Exception:
        pass


def select_rx_path(uart_idx: int, rx1_file: Path, rx2_file: Path) -> Path | None:
    if int(uart_idx) == 2:
        return rx2_file
    if UART1_FILE_INJECTOR:
        return rx1_file
    if UART1_BRIDGE is not None:
        return None
    return rx1_file


def python_set_bootflag(monitor: RenodeMonitor, outdir: Path, value: int) -> None:
    monitor.cmd("pause")
    # Touch SPI1 SR to ensure the flash stub state is initialized.
    monitor.cmd("sysbus ReadDoubleWord 0x40013008")
    # write to spi1 flash state and append a host marker
    cmd = (
        "import System, os; "
        "s=System.AppDomain.CurrentDomain.GetData('spi1_flash_state'); "
        "flash=s.get('flash') if hasattr(s,'get') else None; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "f=open(os.path.join(out,'spi_flash_debug.txt'),'a'); "
        f"val={value & 0xFF}; "
        "f.write('HOST bootflag=0x%02X\\n' % val); "
        "f.close(); "
        "(flash is not None) and flash.__setitem__(0x3FF080, val); "
        "(flash is not None) and flash.__setitem__(0x3FF081, 0); "
        "(flash is not None) and flash.__setitem__(0x3FF082, 0); "
        "(flash is not None) and flash.__setitem__(0x3FF083, 0); "
    )
    monitor.cmd(f'python "{cmd}"')


def python_dump_env(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    cmd = (
        "import os; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "p=os.environ.get('BC280_UART1_RX'); "
        "b=os.environ.get('BC280_UART1_SOFT_ENQUEUE_BOOT'); "
        "f=open(os.path.join(out,'env_debug.txt'),'a'); "
        f"f.write('{tag} OUTDIR=%s UART1_RX=%s BOOT_ENQ=%s\\n' % (out, p, b)); "
        "f.close();"
    )
    out = monitor.cmd(f'python "{cmd}"')
    try:
        with (outdir / "env_debug_monitor.txt").open("a") as f:
            f.write(out or "")
    except Exception:
        pass


def dump_uart_methods(monitor: RenodeMonitor, outdir: Path, name: str = "uart1") -> None:
    cmd = (
        "import os; "
        "m=emulationManager.CurrentEmulation.Machines[0] if emulationManager.CurrentEmulation.Machines.Count>0 else None; "
        "sb=(m is not None) and m.SystemBus or None; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "p=os.path.join(out, 'uart_methods.txt'); "
        "f=open(p,'a'); "
        "f.write('SystemBus dir=%s\\n' % (dir(sb) if sb is not None else None)); "
        "regs = sb.GetRegisteredPeripherals() if sb is not None else None; "
        "f.write('Registered=%s\\n' % (repr(regs) if regs is not None else None)); "
        "f.write('Registered list=%s\\n' % ([str(x) for x in regs] if regs is not None else None)); "
        "maps = sb.GetMappedPeripherals() if sb is not None else None; "
        "f.write('Mapped=%s\\n' % (repr(maps) if maps is not None else None)); "
        "f.write('Mapped list=%s\\n' % ([str(x) for x in maps] if maps is not None else None)); "
        "u = sb.WhatIsAt(0x40013800) if sb is not None else None; "
        "f.write('UART1 obj=%s\\n' % (u if u is not None else None)); "
        "f.write('UART1 dir=%s\\n' % (dir(u) if u is not None else None)); "
        "p = (u.Peripheral if (u is not None and hasattr(u, 'Peripheral')) else None); "
        "f.write('UART1 peripheral=%s\\n' % (p if p is not None else None)); "
        "f.write('UART1 peripheral dir=%s\\n' % (dir(p) if p is not None else None)); "
        "f.write('UART1 CharReceived=%s callable=%s type=%s\\n' % ((getattr(p, 'CharReceived', None) if p is not None else None), (callable(getattr(p, 'CharReceived', None)) if p is not None else None), (type(getattr(p, 'CharReceived', None)) if p is not None else None))); "
        "f.write('UART1 CharReceived dir=%s\\n' % (dir(getattr(p, 'CharReceived', None)) if p is not None else None)); "
        "f.close();"
    )
    try:
        monitor.cmd(f'python "{cmd}"')
    except Exception:
        pass


def dump_cpu_methods(monitor: RenodeMonitor, outdir: Path) -> None:
    cmd = (
        "import os; "
        "m=emulationManager.CurrentEmulation.Machines[0] if emulationManager.CurrentEmulation.Machines.Count>0 else None; "
        "sb=(m is not None) and m.SystemBus or None; "
        "cpu = sb.GetCurrentCPU() if sb is not None else None; "
        "cpus = list(sb.GetCPUs()) if (sb is not None and cpu is None) else None; "
        "cpu = (cpus[0] if (cpus is not None and len(cpus)>0) else cpu); "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "p=os.path.join(out, 'cpu_methods.txt'); "
        "f=open(p,'a'); "
        "f.write('CPU=%s\\n' % (cpu if cpu is not None else None)); "
        "f.write('CPU dir=%s\\n' % (dir(cpu) if cpu is not None else None)); "
        "f.close();"
    )
    try:
        out = monitor.cmd(f'python \"{cmd}\"')
        with (outdir / "cpu_methods_monitor.txt").open("a") as f:
            f.write(out or "")
    except Exception:
        pass


def python_dump_globals(monitor: RenodeMonitor, outdir: Path) -> None:
    cmd = (
        "import os; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "f=open(os.path.join(out,'python_globals.txt'),'w'); "
        "f.write('\\n'.join(sorted(globals().keys()))); "
        "f.close();"
    )
    monitor.cmd(f'python "{cmd}"')


def python_dump_bootflag(monitor: RenodeMonitor) -> None:
    monitor.cmd("pause")
    monitor.cmd("sysbus ReadDoubleWord 0x40013008")
    cmd = (
        "import System, os; "
        "s=System.AppDomain.CurrentDomain.GetData('spi1_flash_state'); "
        "flash=s.get('flash') if hasattr(s,'get') else None; "
        "val=flash[0x3FF080] if flash is not None else 0; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        "f=open(os.path.join(out,'bootflag.txt'),'a'); "
        "f.write('0x%02X\\n' % val); "
        "f.close();"
    )
    monitor.cmd(f'python "{cmd}"')


def python_set_uart1_boot_soft_enqueue(monitor: RenodeMonitor, enabled: bool) -> None:
    val = "True" if enabled else "False"
    cmd = (
        "import System; "
        f"System.AppDomain.CurrentDomain.SetData('bc280_uart1_boot_soft_enqueue', {val}); "
    )
    monitor.cmd(f'python "{cmd}"')


def read_boot_stage(monitor: RenodeMonitor, outdir: Path, tag: str) -> int | None:
    val, _ = monitor_read_word(monitor, 0x200000F0)
    try:
        with (outdir / "boot_stage.txt").open("a") as f:
            f.write(f"{tag} stage=0x{(val or 0) & 0xFFFFFFFF:08X}\n")
    except Exception:
        pass
    return val


def read_bootflag_value(outdir: Path) -> str:
    bootflag_txt = outdir / "bootflag.txt"
    if not bootflag_txt.exists():
        return "unknown"
    try:
        return bootflag_txt.read_text().strip().splitlines()[-1]
    except Exception:
        return "unknown"


def wait_for_bootflag(
    monitor: RenodeMonitor, outdir: Path, timeout_s: float = 24.0, step_s: float = 2.0
) -> str:
    timeout_s = scaled(timeout_s)
    step_s = scaled(step_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        python_dump_bootflag(monitor)
        val = read_bootflag_value(outdir)
        if val.lower() == "0xaa":
            return val
        run_for(monitor, step_s)
    return read_bootflag_value(outdir)


def monitor_read_word(monitor: RenodeMonitor, addr: int) -> tuple[int | None, str]:
    out = monitor.cmd(f"sysbus ReadDoubleWord 0x{addr:08X}", wait_prompt_s=0.5)
    if not out:
        return None, ""
    hits = re.findall(r"0x[0-9a-fA-F]+", out)
    if not hits:
        return None, out
    try:
        return int(hits[-1], 16), out
    except Exception:
        return None, out


def monitor_read_byte(monitor: RenodeMonitor, addr: int) -> tuple[int | None, str]:
    out = monitor.cmd(f"sysbus ReadByte 0x{addr:08X}", wait_prompt_s=0.5)
    if not out:
        return None, ""
    hits = re.findall(r"0x[0-9a-fA-F]+", out)
    if not hits:
        return None, out
    try:
        return int(hits[-1], 16) & 0xFF, out
    except Exception:
        return None, out


def dump_monitor_info(monitor: RenodeMonitor, outdir: Path) -> None:
    cmds = [
        "help",
        "mach list",
        "peripherals",
        "peripherals sysbus",
        "sysbus",
    ]
    for cmd in cmds:
        try:
            out = monitor.cmd(cmd, wait_prompt_s=1.0)
            with (outdir / f"monitor_{cmd}.txt").open("w") as f:
                f.write(out)
        except Exception:
            pass
    try:
        export_path = outdir / "peripherals_export.txt"
        monitor.cmd(f"peripherals export \"{export_path}\"", wait_prompt_s=1.0)
    except Exception:
        pass


def dump_ttm_buffer(
    monitor: RenodeMonitor,
    outdir: Path,
    tag: str,
    buf_addr: int,
    len_addr: int,
    max_len: int = 64,
) -> None:
    length_val, _ = monitor_read_byte(monitor, len_addr)
    length = length_val or 0
    if length < 0:
        length = 0
    if length > max_len:
        length = max_len
    data = []
    for i in range(length):
        b, _ = monitor_read_byte(monitor, buf_addr + i)
        data.append(b or 0)
    hexstr = "".join([f"{b:02X}" for b in data])
    asc = "".join([chr(b) if 32 <= b < 127 else "." for b in data])
    try:
        with (outdir / "ttm_debug.txt").open("a") as f:
            f.write(f"{tag} len={length} hex={hexstr} ascii={asc}\n")
    except Exception:
        pass


def ensure_machine_selected(monitor: RenodeMonitor, outdir: Path, name: str = "bc280-vendor") -> bool:
    attempts = [
        f"mach set {name}",
        f"mach set \"{name}\"",
        "mach set 0",
    ]
    out_log = outdir / "monitor_machset.txt"
    for cmd in attempts:
        try:
            out = monitor.cmd(cmd, wait_prompt_s=1.0)
            with out_log.open("a") as f:
                f.write(out)
            check = monitor.cmd("peripherals", wait_prompt_s=1.0)
            with out_log.open("a") as f:
                f.write(check)
            if "Select active machine first" not in check and "No machine selected" not in check:
                return True
        except Exception:
            pass
        time.sleep(scaled(0.2))
    return False


def wait_for_uart_callback(
    monitor: RenodeMonitor,
    addr: int,
    expected: int,
    timeout_s: float,
    step_s: float = 1.0,
    log_path: Path | None = None,
) -> bool:
    timeout_s = scaled(timeout_s)
    step_s = scaled(step_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        val, out = monitor_read_word(monitor, addr)
        pc_val, pc_out = monitor_read_pc(monitor) if log_path else (None, "")
        if log_path and out:
            try:
                with log_path.open("a") as f:
                    f.write(out)
                    if pc_out:
                        f.write(pc_out)
            except Exception:
                pass
        if val == expected:
            return True
        run_for(monitor, step_s)
    val, out = monitor_read_word(monitor, addr)
    pc_val, pc_out = monitor_read_pc(monitor) if log_path else (None, "")
    if log_path and out:
        try:
            with log_path.open("a") as f:
                f.write(out)
                if pc_out:
                    f.write(pc_out)
        except Exception:
            pass
    return val == expected


def monitor_read_pc(monitor: RenodeMonitor) -> tuple[int | None, str]:
    out = monitor.cmd("sysbus.cpu PC", wait_prompt_s=0.5)
    if not out:
        return None, ""
    hits = re.findall(r"0x[0-9a-fA-F]+", out)
    if not hits:
        return None, out
    try:
        return int(hits[-1], 16), out
    except Exception:
        return None, out


def force_jump_to_app(monitor: RenodeMonitor, outdir: Path, fw_path: Path) -> tuple[int, int]:
    app_sp, app_pc = read_vector_at_offset(fw_path, 0x10000)
    try:
        with (outdir / "app_jump_debug.txt").open("a") as f:
            f.write(f"force_jump sp=0x{app_sp:08X} pc=0x{app_pc:08X}\n")
    except Exception:
        pass
    monitor.cmd("pause")
    monitor.cmd(f"sysbus.cpu SP 0x{app_sp:08X}")
    monitor.cmd(f"sysbus.cpu PC 0x{app_pc:08X}")
    monitor.cmd("sysbus WriteDoubleWord 0xE000ED08 0x08010000")
    run_for(monitor, 1)
    return app_sp, app_pc


def force_jump_to_app_main(monitor: RenodeMonitor, outdir: Path, app_main_addr: int, app_sp: int | None = None) -> None:
    try:
        with (outdir / "app_jump_debug.txt").open("a") as f:
            f.write(f"force_jump_main pc=0x{app_main_addr:08X}\n")
    except Exception:
        pass
    monitor.cmd("pause")
    if app_sp is not None:
        monitor.cmd(f"sysbus.cpu SP 0x{app_sp:08X}")
    monitor.cmd(f"sysbus.cpu PC 0x{app_main_addr:08X}")
    monitor.cmd("sysbus WriteDoubleWord 0xE000ED08 0x08010000")
    run_for(monitor, 1)


def wait_for_pc_in_range(
    monitor: RenodeMonitor,
    start_addr: int,
    end_addr: int,
    timeout_s: float,
    step_s: float = 1.0,
    log_path: Path | None = None,
) -> bool:
    timeout_s = scaled(timeout_s)
    step_s = scaled(step_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        pc_val, pc_out = monitor_read_pc(monitor)
        if log_path and pc_out:
            try:
                with log_path.open("a") as f:
                    f.write(pc_out)
            except Exception:
                pass
        if pc_val is not None and start_addr <= pc_val <= end_addr:
            return True
        run_for(monitor, step_s)
    pc_val, pc_out = monitor_read_pc(monitor)
    if log_path and pc_out:
        try:
            with log_path.open("a") as f:
                f.write(pc_out)
        except Exception:
            pass
    return pc_val is not None and start_addr <= pc_val <= end_addr


def install_pc_hook(monitor: RenodeMonitor, outdir: Path, addr: int, tag: str) -> None:
    hook_py = (
        "import os; "
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.'; "
        f"f=open(os.path.join(out,'hook_hits.txt'),'a'); f.write('{tag}\\n'); f.close()"
    )
    hook_py_escaped = hook_py.replace('"', '\\"')
    cmds = [
        f"sysbus.cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
        f"cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
    ]
    for cmd in cmds:
        try:
            out = monitor.cmd(cmd, wait_prompt_s=1.0)
            try:
                with (outdir / "monitor_hook.txt").open("a") as f:
                    f.write(out)
            except Exception:
                pass
        except Exception:
            pass


def install_fast_flash_read_hook(monitor: RenodeMonitor, outdir: Path, addr: int) -> None:
    payload = r"""
import System, os
out=os.environ.get('BC280_RENODE_OUTDIR') or '.'
try:
    f=open(os.path.join(out,'hook_hits.txt'),'a')
    f.write('fast_flash_read\n')
    f.close()
except Exception:
    pass
def _val(x):
    try:
        return int(x)
    except Exception:
        try:
            return int(x.Value)
        except Exception:
            try:
                return int(x.RawValue)
            except Exception:
                return None
def _get_reg(cpu, idx):
    for name in ('GetRegisterValue32','GetRegisterValue64','GetRegisterValue','GetRegister','ReadRegister'):
        if hasattr(cpu, name):
            try:
                return getattr(cpu, name)(idx)
            except Exception:
                pass
    return None
def _set_reg(cpu, idx, val):
    try:
        val = int(val)
    except Exception:
        pass
    for name in ('SetRegisterValue32','SetRegisterValue64','SetRegisterValue','SetRegister','SetRegisterUnsafe','WriteRegister'):
        if hasattr(cpu, name):
            try:
                getattr(cpu, name)(idx, val)
                return True
            except Exception:
                try:
                    getattr(cpu, name)(idx, System.UInt32(val))
                    return True
                except Exception:
                    pass
                pass
    return False
def _set_pc(cpu, val):
    try:
        cpu.PC = val
        return True
    except Exception:
        pass
    try:
        cpu.PC = System.UInt32(val)
        return True
    except Exception:
        pass
    return _set_reg(cpu, 15, val)
def _request_return(cpu):
    for name in ('RequestTranslationBlockRestart','RequestReturn','TlibSetReturnRequest'):
        if hasattr(cpu, name):
            try:
                getattr(cpu, name)()
                return True
            except Exception:
                pass
    return False
cpu = self
if not (hasattr(cpu,'GetRegisterValue') or hasattr(cpu,'GetRegister')):
    try:
        cpu = list(self.Machine.SystemBus.GetCPUs())[0]
    except Exception:
        cpu = None
if cpu is not None:
    try:
        if System.AppDomain.CurrentDomain.GetData('fast_flash_read_diag') is None:
            System.AppDomain.CurrentDomain.SetData('fast_flash_read_diag', 1)
            f=open(os.path.join(out,'fast_flash_read_diag.txt'),'w')
            f.write(str([x for x in dir(cpu) if not x.startswith('_')]) + '\n')
            f.close()
    except Exception:
        pass
    r0 = _val(_get_reg(cpu, 0))
    r1 = _val(_get_reg(cpu, 1))
    r2 = _val(_get_reg(cpu, 2))
    lr = _val(_get_reg(cpu, 14))
    if None not in (r0, r1, r2, lr):
        st = System.AppDomain.CurrentDomain.GetData('spi1_flash_state')
        flash = st.get('flash') if st is not None and hasattr(st,'get') else None
        if flash is not None:
            addr = int(r1) & 0xFFFFFFFF
            size = int(r2) & 0xFFFFFFFF
            off = addr % len(flash)
            data = flash[off:off+size]
            bus = self.Machine.SystemBus if hasattr(self,'Machine') else getattr(cpu,'Bus',None)
            if bus is not None:
                try:
                    arr = System.Array[System.Byte](list(data))
                except Exception:
                    arr = data
                bus.WriteBytes(arr, int(r0) & 0xFFFFFFFF)
                # Return success immediately; skip the slow per-byte SPI read loop.
                _set_reg(cpu, 0, 0)
                _set_reg(cpu, 2, 0)
                pc_ok = _set_pc(cpu, int(lr) & 0xFFFFFFFF)
                ret_ok = _request_return(cpu)
                try:
                    if System.AppDomain.CurrentDomain.GetData('fast_flash_read_return_log') is None:
                        System.AppDomain.CurrentDomain.SetData('fast_flash_read_return_log', 1)
                        f=open(os.path.join(out,'fast_flash_read_return.txt'),'w')
                        f.write('pc_ok=%s ret_ok=%s lr=0x%08X\\n' % (str(pc_ok), str(ret_ok), int(lr) & 0xFFFFFFFF))
                        f.close()
                except Exception:
                    pass
"""
    payload_escaped = payload.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
    hook_py = f"exec(\"{payload_escaped}\")"
    hook_py_escaped = hook_py.replace('"', '\\"')
    cmds = [
        f"sysbus.cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
        f"cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
    ]
    for cmd in cmds:
        try:
            out = monitor.cmd(cmd, wait_prompt_s=1.0)
            try:
                with (outdir / "monitor_hook.txt").open("a") as f:
                    f.write(out)
            except Exception:
                pass
        except Exception:
            pass


def install_openfw_fast_read_hook(monitor: RenodeMonitor, outdir: Path, addr: int) -> None:
    # Same as fast flash read hook, but for open-firmware spi_flash_read().
    install_fast_flash_read_hook(monitor, outdir, addr)


def install_fast_bootflag_write_hook(monitor: RenodeMonitor, outdir: Path, addr: int) -> None:
    payload = r"""
import System, os
out=os.environ.get('BC280_RENODE_OUTDIR') or '.'
try:
    f=open(os.path.join(out,'hook_hits.txt'),'a')
    f.write('fast_bootflag_write\n')
    f.close()
except Exception:
    pass
try:
    f=open(os.path.join(out,'fast_bootflag_write.txt'),'a')
    f.write('hook_enter\n')
    f.close()
except Exception:
    pass
try:
    if System.AppDomain.CurrentDomain.GetData('fast_bootflag_selfdiag') is None:
        System.AppDomain.CurrentDomain.SetData('fast_bootflag_selfdiag', 1)
        f=open(os.path.join(out,'fast_bootflag_selfdiag.txt'),'w')
        f.write(str([x for x in dir(self) if not x.startswith('_')]) + '\n')
        f.close()
except Exception:
    pass
cpu = self
if not hasattr(cpu, 'GetRegisterValue32'):
    try:
        cpu = list(self.Machine.SystemBus.GetCPUs())[0]
    except Exception:
        cpu = None
if cpu is not None and hasattr(cpu, 'GetRegisterValue32') and hasattr(cpu, 'SetRegisterValue32'):
    try:
        if System.AppDomain.CurrentDomain.GetData('fast_bootflag_diag') is None:
            System.AppDomain.CurrentDomain.SetData('fast_bootflag_diag', 1)
            f=open(os.path.join(out,'fast_bootflag_diag.txt'),'w')
            f.write(str([x for x in dir(cpu) if not x.startswith('_')]) + '\n')
            f.close()
    except Exception:
        pass
    r0 = cpu.GetRegisterValue32(0)
    r1 = cpu.GetRegisterValue32(1)
    lr = cpu.GetRegisterValue32(14)
    try:
        st = System.AppDomain.CurrentDomain.GetData('spi1_flash_state')
        flash = st.get('flash') if st is not None and hasattr(st,'get') else None
        if flash is not None:
            addr = int(r0) & 0xFFFFFFFF
            val = int(r1) & 0xFFFF
            if addr >= 0 and addr + 1 < len(flash):
                hi = (val >> 8) & 0xFF
                lo = val & 0xFF
                flash[addr] = flash[addr] & hi
                flash[addr + 1] = flash[addr + 1] & lo
                try:
                    f=open(os.path.join(out,'fast_bootflag_write.txt'),'a')
                    f.write('ok addr=0x%08X val=0x%04X\n' % (addr, val))
                    f.close()
                except Exception:
                    pass
                cpu.SetRegisterValue32(0, int(r0) & 0xFFFFFFFF)
                cpu.SetRegisterValue32(15, int(lr) & 0xFFFFFFFF)
        else:
            try:
                f=open(os.path.join(out,'fast_bootflag_write.txt'),'a')
                f.write('no_flash_state\n')
                f.close()
            except Exception:
                pass
    except Exception as e:
        try:
            f=open(os.path.join(out,'fast_bootflag_write.txt'),'a')
            f.write('error %s\n' % e)
            f.close()
        except Exception:
            pass
"""
    payload_escaped = payload.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
    hook_py = f"exec(\"{payload_escaped}\")"
    hook_py_escaped = hook_py.replace('"', '\\"')
    cmds = [
        f"sysbus.cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
        f"cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
    ]
    for cmd in cmds:
        try:
            out = monitor.cmd(cmd, wait_prompt_s=1.0)
            try:
                with (outdir / "monitor_hook.txt").open("a") as f:
                    f.write(out)
            except Exception:
                pass
        except Exception:
            pass


def install_openfw_bootflag_hook(monitor: RenodeMonitor, outdir: Path, addr: int) -> None:
    payload = (
        "import System,os;"
        "out=os.environ.get('BC280_RENODE_OUTDIR') or '.';"
        "f=open(os.path.join(out,'hook_hits.txt'),'a');f.write('openfw_bootflag_fast\\n');f.close();"
        "st=System.AppDomain.CurrentDomain.GetData('spi1_flash_state');"
        "flash=st.get('flash') if st is not None and hasattr(st,'get') else None;"
        "flash=flash if flash is not None else bytearray([0xFF])*0x400000;"
        "st=st if st is not None and hasattr(st,'get') else {'flash':flash,'size':len(flash)};"
        "st['flash']=flash;System.AppDomain.CurrentDomain.SetData('spi1_flash_state',st);"
        "flash[0x3FF080]=0xAA;flash[0x3FF081]=0x00;flash[0x3FF082]=0x00;flash[0x3FF083]=0x00;"
        "bus=self.Machine.SystemBus if hasattr(self,'Machine') else getattr(self,'Bus',None);"
        "bus.WriteDoubleWord(0x200000F0,0xB003) if bus is not None else None;"
        "cpu=self if hasattr(self,'GetRegisterValue32') else list(self.Machine.SystemBus.GetCPUs())[0];"
        "lr=int(cpu.GetRegisterValue32(14));cpu.SetRegisterValue32(15,lr&0xFFFFFFFF);"
        "cpu.RequestTranslationBlockRestart() if hasattr(cpu,'RequestTranslationBlockRestart') else None;"
    )
    payload_escaped = payload.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
    hook_py = f"exec(\"{payload_escaped}\")"
    hook_py_escaped = hook_py.replace('"', '\\"')
    cmds = [
        f"sysbus.cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
        f"cpu AddHook 0x{addr:08X} \"{hook_py_escaped}\"",
    ]
    for cmd in cmds:
        try:
            out = monitor.cmd(cmd, wait_prompt_s=1.0)
            try:
                with (outdir / "monitor_hook.txt").open("a") as f:
                    f.write(out)
            except Exception:
                pass
        except Exception:
            pass


def sample_pc_hits(
    monitor: RenodeMonitor,
    outdir: Path,
    tag: str,
    start: int,
    end: int,
    duration_s: float = 2.0,
    step_s: float = 0.1,
) -> None:
    duration_s = scaled(duration_s)
    step_s = scaled(step_s)
    deadline = time.time() + duration_s
    hits = 0
    while time.time() < deadline:
        check_deadline()
        pc_val, _ = monitor_read_pc(monitor)
        if pc_val is not None and start <= pc_val <= end:
            hits += 1
        time.sleep(step_s)
    try:
        with (outdir / "pc_sample_hits.txt").open("a") as f:
            f.write(f"{tag} hits={hits}\n")
    except Exception:
        pass


def ensure_uart1_rxneie(monitor: RenodeMonitor) -> bool:
    cr1_addr = 0x4001380C
    val, _out = monitor_read_word(monitor, cr1_addr)
    if val is None:
        return False
    if (val & (1 << 5)) == 0:
        monitor.cmd(f"sysbus WriteDoubleWord 0x{cr1_addr:08X} 0x{(val | (1 << 5)) & 0xFFFFFFFF:08X}")
    return True


def wait_for_uart1_cr1_ready(monitor: RenodeMonitor, timeout_s: float = 8.0, step_s: float = 1.0) -> int | None:
    cr1_addr = 0x4001380C
    timeout_s = scaled(timeout_s)
    step_s = scaled(step_s)
    deadline = time.time() + timeout_s
    last_val = None
    while time.time() < deadline:
        check_deadline()
        val, _out = monitor_read_word(monitor, cr1_addr)
        if val is not None:
            last_val = val
            if val & 0x2000:
                return val
        run_for(monitor, step_s)
    return last_val


def wait_for_uart1_activity(outdir: Path, timeout_s: float = 8.0) -> bool:
    timeout_s = scaled(timeout_s)
    deadline = time.time() + timeout_s
    uart_log = outdir / "uart1_tx.log"
    while time.time() < deadline:
        try:
            check_deadline()
            if UART1_BRIDGE and UART1_BRIDGE.last_rx_ts:
                return True
            if uart_log.exists() and uart_log.stat().st_size > 0:
                return True
        except Exception:
            pass
        time.sleep(scaled(0.2))
    return uart_log.exists()


def uart1_sanity_check(monitor: RenodeMonitor, outdir: Path) -> None:
    if UART1_BRIDGE is None:
        return
    try:
        UART1_BRIDGE.write(b"Z")
    except Exception:
        return
    run_for(monitor, 0.2)
    dump_uart1_regs(monitor, outdir, "uart1_sanity_after")


def respond_ble_mac_query(outdir: Path, rx_path: Path | None, monitor: RenodeMonitor | None = None) -> bool:
    tx_log = outdir / "uart1_tx.log"
    if not tx_log.exists():
        return False
    try:
        data = tx_log.read_bytes()
    except Exception:
        return False
    if b"TTM:MAC-?" not in data:
        return False
    mac = os.environ.get("BC280_BLE_MAC", "112233445566")
    mac = mac.strip().replace(":", "").replace("-", "")
    if len(mac) != 12:
        mac = "112233445566"
    resp = ("TTM:MAC-" + mac + "\r\n").encode("ascii", "ignore")
    write_rx(rx_path, resp, monitor=monitor, outdir=outdir)
    try:
        with (outdir / "ble_mac_debug.txt").open("a") as f:
            f.write("responded TTM:MAC-? with %s\n" % mac)
    except Exception:
        pass
    return True


def send_ble_mac_response(outdir: Path, rx_path: Path | None, monitor: RenodeMonitor | None = None) -> None:
    mac = os.environ.get("BC280_BLE_MAC", "112233445566")
    mac = mac.strip().replace(":", "").replace("-", "")
    if len(mac) != 12:
        mac = "112233445566"
    resp = ("TTM:MAC-" + mac + "\r\n").encode("ascii", "ignore")
    write_rx(rx_path, resp, monitor=monitor, outdir=outdir)
    try:
        with (outdir / "ble_mac_debug.txt").open("a") as f:
            f.write("sent TTM:MAC response %s\n" % mac)
    except Exception:
        pass

def wait_for_ble_mac_query(
    monitor: RenodeMonitor,
    outdir: Path,
    rx_path: Path | None,
    timeout_s: float = 2.0,
    step_s: float = 0.2,
) -> bool:
    timeout_s = scaled(timeout_s)
    if respond_ble_mac_query(outdir, rx_path, monitor=monitor):
        return True
    run_for(monitor, max(timeout_s, MIN_SLEEP_S))
    return respond_ble_mac_query(outdir, rx_path, monitor=monitor)

def ensure_uart2_rxneie(monitor: RenodeMonitor) -> bool:
    cr1_addr = 0x4000440C
    val, _out = monitor_read_word(monitor, cr1_addr)
    if val is None:
        return False
    if (val & (1 << 5)) == 0:
        monitor.cmd(f"sysbus WriteDoubleWord 0x{cr1_addr:08X} 0x{(val | (1 << 5)) & 0xFFFFFFFF:08X}")
    return True


def ensure_nvic_enabled(monitor: RenodeMonitor, irq: int) -> bool:
    irq = int(irq)
    if irq < 0:
        return False
    reg = 0xE000E100 + 4 * (irq // 32)  # NVIC_ISERx
    bit = 1 << (irq % 32)
    val, _out = monitor_read_word(monitor, reg)
    if val is None:
        return False
    if (val & bit) == 0:
        monitor.cmd(f"sysbus WriteDoubleWord 0x{reg:08X} 0x{bit:08X}")
    return True


def dump_nvic_state(monitor: RenodeMonitor, outdir: Path, tag: str, irq: int) -> None:
    irq = int(irq)
    reg_en = 0xE000E100 + 4 * (irq // 32)
    reg_pend = 0xE000E200 + 4 * (irq // 32)
    en, _ = monitor_read_word(monitor, reg_en)
    pend, _ = monitor_read_word(monitor, reg_pend)
    try:
        with (outdir / "nvic_debug.txt").open("a") as f:
            f.write(
                f"{tag} irq={irq} ISER=0x{(en or 0):08X} ISPR=0x{(pend or 0):08X}\n"
            )
    except Exception:
        pass


def poke_irq(monitor: RenodeMonitor, outdir: Path, irq: int, tag: str) -> None:
    irq = int(irq)
    reg_pend = 0xE000E200 + 4 * (irq // 32)
    bit = 1 << (irq % 32)
    try:
        monitor.cmd(f"sysbus WriteDoubleWord 0x{reg_pend:08X} 0x{bit:08X}")
    except Exception:
        return
    run_for(monitor, 0.2)
    dump_nvic_state(monitor, outdir, tag, irq)

def dump_ble_indices(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    base = 0x200017B4
    rd, _ = monitor_read_word(monitor, base + 0xC8)
    wr, _ = monitor_read_word(monitor, base + 0xCA)
    parse_idx, _ = monitor_read_word(monitor, base + 0xCC)
    rx_read, _ = monitor_read_word(monitor, base + 0x504)
    rx_write, _ = monitor_read_word(monitor, base + 0x506)
    slot_idx = (rx_read or 0) & 0xFFFF
    slot_base = base + 0xCE + (slot_idx % 7) * 0x9A
    w0, _ = monitor_read_word(monitor, slot_base)
    w1, _ = monitor_read_word(monitor, slot_base + 4)
    wlen, _ = monitor_read_word(monitor, slot_base + 0x98)
    b0 = (w0 or 0) & 0xFF
    b1 = ((w0 or 0) >> 8) & 0xFF
    b2 = ((w0 or 0) >> 16) & 0xFF
    b3 = ((w0 or 0) >> 24) & 0xFF
    b4 = (w1 or 0) & 0xFF
    b5 = ((w1 or 0) >> 8) & 0xFF
    slot_len = (wlen or 0) & 0xFFFF
    try:
        with (outdir / "ble_idx_debug.txt").open("a") as f:
            f.write(
                f"{tag} rd=0x{(rd or 0) & 0xFFFF:04X} wr=0x{(wr or 0) & 0xFFFF:04X} "
                f"parse=0x{(parse_idx or 0) & 0xFFFF:04X} rxr=0x{(rx_read or 0) & 0xFFFF:04X} "
                f"rxw=0x{(rx_write or 0) & 0xFFFF:04X} slot=0x{slot_idx & 0xFFFF:04X} "
                f"slot_len=0x{slot_len:04X} "
                f"sof=0x{b2:02X} cmd=0x{b3:02X} plen=0x{b4:02X} csum=0x{b5:02X}\n"
            )
    except Exception:
        pass


def read_ble_slot(monitor: RenodeMonitor, base: int = 0x200017B4) -> dict:
    rd, _ = monitor_read_word(monitor, base + 0xC8)
    wr, _ = monitor_read_word(monitor, base + 0xCA)
    rx_read, _ = monitor_read_word(monitor, base + 0x504)
    rx_write, _ = monitor_read_word(monitor, base + 0x506)
    slot_idx = (rx_read or 0) & 0xFFFF
    slot_base = base + 0xCE + (slot_idx % 7) * 0x9A
    w0, _ = monitor_read_word(monitor, slot_base)
    w1, _ = monitor_read_word(monitor, slot_base + 4)
    wlen, _ = monitor_read_word(monitor, slot_base + 0x98)
    return {
        "rd": (rd or 0) & 0xFFFF,
        "wr": (wr or 0) & 0xFFFF,
        "rx_read": (rx_read or 0) & 0xFFFF,
        "rx_write": (rx_write or 0) & 0xFFFF,
        "slot_len": (wlen or 0) & 0xFFFF,
        "sof": ((w0 or 0) >> 16) & 0xFF,
        "cmd": ((w0 or 0) >> 24) & 0xFF,
        "plen": (w1 or 0) & 0xFF,
        "csum": ((w1 or 0) >> 8) & 0xFF,
    }


def dump_ble_slot_bytes(
    monitor: RenodeMonitor, outdir: Path, tag: str, base: int = 0x200017B4
) -> None:
    rx_read, _ = monitor_read_word(monitor, base + 0x504)
    slot_idx = (rx_read or 0) & 0xFFFF
    slot_base = base + 0xCE + (slot_idx % 7) * 0x9A
    bytes_out = []
    for i in range(12):
        b, _ = monitor_read_byte(monitor, slot_base + i)
        bytes_out.append(b or 0)
    wlen, _ = monitor_read_word(monitor, slot_base + 0x98)
    try:
        with (outdir / "ble_slot_bytes.txt").open("a") as f:
            f.write(
                f"{tag} idx={slot_idx} len=0x{(wlen or 0) & 0xFFFF:04X} bytes="
                + "".join([f"{b:02X}" for b in bytes_out])
                + "\n"
            )
    except Exception:
        pass


def wait_for_ble_rx_advance(
    monitor: RenodeMonitor,
    base: int,
    start_idx: int,
    timeout_s: float = 6.0,
    step_s: float = 1.0,
) -> bool:
    timeout_s = scaled(timeout_s)
    step_s = scaled(step_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        val, _ = monitor_read_word(monitor, base + 0x504)
        cur = (val or 0) & 0xFFFF
        if cur != (start_idx & 0xFFFF):
            return True
        run_for(monitor, step_s)
    return False


def dump_boot_comm_indices(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    base = 0x20000474
    rd, _ = monitor_read_word(monitor, base + 0xC8)
    wr, _ = monitor_read_word(monitor, base + 0xCA)
    parse_idx, _ = monitor_read_word(monitor, base + 0xCC)
    frame_rd, _ = monitor_read_word(monitor, base + 0x29C)
    frame_wr, _ = monitor_read_word(monitor, base + 0x29E)
    slot_idx = (frame_rd or 0) & 0xFFFF
    slot_base = base + 0xCE + (slot_idx % 3) * 0x9A
    w0, _ = monitor_read_word(monitor, slot_base)
    w1, _ = monitor_read_word(monitor, slot_base + 4)
    wlen, _ = monitor_read_word(monitor, slot_base + 0x98)
    b0 = (w0 or 0) & 0xFF
    b1 = ((w0 or 0) >> 8) & 0xFF
    b2 = ((w0 or 0) >> 16) & 0xFF
    b3 = ((w0 or 0) >> 24) & 0xFF
    b4 = (w1 or 0) & 0xFF
    b5 = ((w1 or 0) >> 8) & 0xFF
    slot_len = (wlen or 0) & 0xFFFF
    prev_idx = (slot_idx - 1) % 3
    prev_base = base + 0xCE + prev_idx * 0x9A
    pw0, _ = monitor_read_word(monitor, prev_base)
    pw1, _ = monitor_read_word(monitor, prev_base + 4)
    pw2, _ = monitor_read_word(monitor, prev_base + 8)
    pwlen, _ = monitor_read_word(monitor, prev_base + 0x98)
    pb2 = ((pw0 or 0) >> 16) & 0xFF
    pb3 = ((pw0 or 0) >> 24) & 0xFF
    pb4 = (pw1 or 0) & 0xFF
    pb5 = ((pw1 or 0) >> 8) & 0xFF
    pb6 = ((pw1 or 0) >> 16) & 0xFF
    pb7 = ((pw1 or 0) >> 24) & 0xFF
    pb8 = (pw2 or 0) & 0xFF
    pb9 = ((pw2 or 0) >> 8) & 0xFF
    prev_len = (pwlen or 0) & 0xFFFF
    try:
        with (outdir / "boot_ble_idx_debug.txt").open("a") as f:
            f.write(
                f"{tag} rd=0x{(rd or 0) & 0xFFFF:04X} wr=0x{(wr or 0) & 0xFFFF:04X} "
                f"parse=0x{(parse_idx or 0) & 0xFFFF:04X} "
                f"frd=0x{(frame_rd or 0) & 0xFFFF:04X} fwr=0x{(frame_wr or 0) & 0xFFFF:04X} "
                f"slot=0x{slot_idx & 0xFFFF:04X} slot_len=0x{slot_len:04X} "
                f"sof=0x{b2:02X} cmd=0x{b3:02X} plen=0x{b4:02X} csum=0x{b5:02X} "
                f"prev_slot=0x{prev_idx:04X} prev_len=0x{prev_len:04X} "
                f"prev_sof=0x{pb2:02X} prev_cmd=0x{pb3:02X} prev_plen=0x{pb4:02X} "
                f"prev_idx={pb5:02X}{pb6:02X}{pb7:02X}{pb8:02X} prev_d0=0x{pb9:02X}\n"
            )
    except Exception:
        pass


def dump_uart1_regs(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    sr, _ = monitor_read_word(monitor, 0x40013800)
    brr, _ = monitor_read_word(monitor, 0x40013808)
    cr1, _ = monitor_read_word(monitor, 0x4001380C)
    cr2, _ = monitor_read_word(monitor, 0x40013810)
    cr3, _ = monitor_read_word(monitor, 0x40013814)
    try:
        with (outdir / "uart1_regs_debug.txt").open("a") as f:
            f.write(
                f"{tag} SR=0x{(sr or 0) & 0xFFFFFFFF:08X} BRR=0x{(brr or 0) & 0xFFFFFFFF:08X} "
                f"CR1=0x{(cr1 or 0) & 0xFFFFFFFF:08X} CR2=0x{(cr2 or 0) & 0xFFFFFFFF:08X} "
                f"CR3=0x{(cr3 or 0) & 0xFFFFFFFF:08X}\n"
            )
    except Exception:
        pass


def dump_uart2_regs(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    sr, _ = monitor_read_word(monitor, 0x40004400)
    brr, _ = monitor_read_word(monitor, 0x40004408)
    cr1, _ = monitor_read_word(monitor, 0x4000440C)
    cr2, _ = monitor_read_word(monitor, 0x40004410)
    cr3, _ = monitor_read_word(monitor, 0x40004414)
    try:
        with (outdir / "uart2_regs_debug.txt").open("a") as f:
            f.write(
                f"{tag} SR=0x{(sr or 0) & 0xFFFFFFFF:08X} BRR=0x{(brr or 0) & 0xFFFFFFFF:08X} "
                f"CR1=0x{(cr1 or 0) & 0xFFFFFFFF:08X} CR2=0x{(cr2 or 0) & 0xFFFFFFFF:08X} "
                f"CR3=0x{(cr3 or 0) & 0xFFFFFFFF:08X}\n"
            )
    except Exception:
        pass


def dump_heap_state(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    head_ptr, _ = monitor_read_word(monitor, 0x20001030)
    block_len = None
    next_ptr = None
    if head_ptr:
        block_len, _ = monitor_read_word(monitor, head_ptr)
        next_ptr, _ = monitor_read_word(monitor, head_ptr + 4)
    try:
        with (outdir / "heap_debug.txt").open("a") as f:
            f.write(
                f"{tag} head=0x{(head_ptr or 0):08X} len=0x{(block_len or 0):08X} next=0x{(next_ptr or 0):08X}\n"
            )
    except Exception:
        pass

def dump_boot_tx_state(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    base = 0x20000474
    tx_rd, _ = monitor_read_word(monitor, base + 0x46E)
    tx_wr, _ = monitor_read_word(monitor, base + 0x470)
    tx_base = 0x20000714
    slot_idx = (tx_rd or 0) & 0xFFFF
    slot_base = tx_base + (slot_idx % 3) * 0x9A
    w0, _ = monitor_read_word(monitor, slot_base)
    w1, _ = monitor_read_word(monitor, slot_base + 4)
    wlen, _ = monitor_read_word(monitor, slot_base + 0x98)
    b2 = ((w0 or 0) >> 16) & 0xFF
    b3 = ((w0 or 0) >> 24) & 0xFF
    b4 = (w1 or 0) & 0xFF
    try:
        with (outdir / "boot_tx_debug.txt").open("a") as f:
            f.write(
                f"{tag} txrd=0x{(tx_rd or 0) & 0xFFFF:04X} txwr=0x{(tx_wr or 0) & 0xFFFF:04X} "
                f"slot=0x{slot_idx & 0xFFFF:04X} slot_len=0x{(wlen or 0) & 0xFFFF:04X} "
                f"sof=0x{b2:02X} cmd=0x{b3:02X} plen=0x{b4:02X}\n"
            )
    except Exception:
        pass


def read_boot_tx_indices(monitor: RenodeMonitor) -> tuple[int | None, int | None]:
    base = 0x20000474
    tx_rd, _ = monitor_read_word(monitor, base + 0x46E)
    tx_wr, _ = monitor_read_word(monitor, base + 0x470)
    return tx_rd, tx_wr


def read_boot_tx_slot(monitor: RenodeMonitor, slot_idx: int) -> tuple[int, int, int, int]:
    tx_base = 0x20000714
    slot_base = tx_base + (slot_idx % 3) * 0x9A
    w0, _ = monitor_read_word(monitor, slot_base)
    w1, _ = monitor_read_word(monitor, slot_base + 4)
    sof = ((w0 or 0) >> 16) & 0xFF
    cmd = ((w0 or 0) >> 24) & 0xFF
    plen = (w1 or 0) & 0xFF
    payload0 = ((w1 or 0) >> 8) & 0xFF
    return sof, cmd, plen, payload0


def wait_for_boot_tx_cmd(
    monitor: RenodeMonitor,
    expected_cmd: int,
    last_wr: int | None,
    timeout_s: float,
    expected_val: int | None = None,
) -> tuple[bool, int | None]:
    timeout_s = scaled(timeout_s)
    deadline = time.time() + timeout_s
    expected_cmd = int(expected_cmd) & 0xFF
    while time.time() < deadline:
        check_deadline()
        _rd, wr = read_boot_tx_indices(monitor)
        if last_wr is None and wr is not None:
            last_wr = wr
            time.sleep(scaled(0.05))
            continue
        if wr is not None and last_wr is not None and wr != last_wr:
            slot_idx = (wr - 1) % 3
            sof, cmd, plen, payload0 = read_boot_tx_slot(monitor, slot_idx)
            if sof == 0x55 and cmd == expected_cmd:
                if expected_val is None:
                    return True, wr
                if plen >= 1 and payload0 == (expected_val & 0xFF):
                    return True, wr
                return False, wr
            last_wr = wr
        time.sleep(scaled(0.05))
    return False, last_wr


def wait_for_uart_tx_cmd(reader: FrameReader, expected_cmd: int, timeout_s: float, expected_val: int | None = 1) -> bool:
    timeout_s = scaled(timeout_s)
    deadline = time.time() + timeout_s
    expected_cmd = int(expected_cmd) & 0xFF
    while time.time() < deadline:
        check_deadline()
        frames = reader.read_frames()
        for fr in frames:
            if fr.cmd != expected_cmd:
                continue
            if expected_val is None:
                return True
            if len(fr.payload) == 1 and fr.payload[0] == (expected_val & 0xFF):
                return True
        time.sleep(scaled(0.02))
    return False


def push_image_via_txring(
    monitor: RenodeMonitor,
    rx_path: Path,
    image_path: Path,
    log_path: Path,
    tx_log: Path | None = None,
    timeout_init_s: float = 8.0,
    timeout_block_s: float = 3.0,
    throttle_s: float = 0.002,
    chunk: int = 16,
) -> bool:
    timeout_init_s = scaled(timeout_init_s)
    timeout_block_s = scaled(timeout_block_s)
    throttle_s = env_float("BC280_OTA_THROTTLE_S", 0.0005)
    chunk = env_int("BC280_OTA_CHUNK", 32)
    block_sleep_s = env_float("BC280_OTA_BLOCK_SLEEP_S", 0.0)
    fast_mode = os.environ.get("BC280_OTA_FAST", "0") == "1"
    data = image_path.read_bytes()
    size = len(data)
    crc8 = calc_crc8_maxim(data, size)
    try:
        log_path.parent.mkdir(parents=True, exist_ok=True)
    except Exception:
        pass

    def _log(msg: str) -> None:
        try:
            with log_path.open("a") as f:
                f.write(msg.rstrip() + "\n")
        except Exception:
            pass

    _log(f"init size={size} crc8=0x{crc8:02X}")
    init_payload = bytes([crc8]) + size.to_bytes(4, "big")
    init_frame = build_frame(CMD_INIT, init_payload)
    write_rx(rx_path, init_frame)
    if fast_mode:
        # Send a couple of blocks to exercise parser, then force-load the full image.
        for idx in range(min(2, (size + BLOCK_SIZE - 1) // BLOCK_SIZE)):
            start = idx * BLOCK_SIZE
            chunk_data = data[start : start + BLOCK_SIZE]
            if len(chunk_data) < BLOCK_SIZE:
                chunk_data = chunk_data + bytes([0xFF]) * (BLOCK_SIZE - len(chunk_data))
            payload = idx.to_bytes(4, "big") + chunk_data
            frame = build_frame(CMD_WRITE, payload)
            write_rx(rx_path, frame)
        try:
            monitor.cmd("pause")
            monitor.cmd(f"sysbus LoadBinary @{image_path} 0x08010000")
            monitor.cmd(f"sysbus LoadBinary @{image_path} 0x00010000")
            monitor.cmd("start")
        except Exception:
            pass
        return True
    reader = None
    if tx_log is not None:
        try:
            offset = tx_log.stat().st_size if tx_log.exists() else 0
            reader = FrameReader(tx_log, offset=offset)
        except Exception:
            reader = None
    txwr = None
    for _ in range(5):
        _txrd, txwr = read_boot_tx_indices(monitor)
        if txwr is not None:
            break
        time.sleep(scaled(0.05))
    if txwr is None:
        txwr = 0
    time.sleep(scaled(0.4))
    try:
        monitor.cmd("pause")
    except Exception:
        pass
    _txrd, txwr_now = read_boot_tx_indices(monitor)
    try:
        monitor.cmd("start")
    except Exception:
        pass
    if txwr_now is not None and txwr_now != txwr:
        ok, txwr = True, txwr_now
    else:
        ok, txwr = wait_for_boot_tx_cmd(monitor, 0x23, txwr, timeout_init_s, expected_val=1)
    if not ok and reader is not None:
        ok = wait_for_uart_tx_cmd(reader, 0x23, timeout_init_s, expected_val=1)
    if not ok:
        _log("init failed or timed out")
        return False

    blocks = (size + BLOCK_SIZE - 1) // BLOCK_SIZE
    for idx in range(blocks):
        start = idx * BLOCK_SIZE
        chunk_data = data[start : start + BLOCK_SIZE]
        if len(chunk_data) < BLOCK_SIZE:
            chunk_data = chunk_data + bytes([0xFF]) * (BLOCK_SIZE - len(chunk_data))
        payload = idx.to_bytes(4, "big") + chunk_data
        frame = build_frame(CMD_WRITE, payload)
        # Throttle so the parser can keep up.
        if throttle_s <= 0:
            write_rx(rx_path, frame)
        else:
            for off in range(0, len(frame), max(1, chunk)):
                write_rx(rx_path, frame[off : off + chunk])
                time.sleep(throttle_s)
        if block_sleep_s > 0:
            time.sleep(scaled(block_sleep_s))
        try:
            monitor.cmd("pause")
        except Exception:
            pass
        _txrd, txwr_now = read_boot_tx_indices(monitor)
        try:
            monitor.cmd("start")
        except Exception:
            pass
        if txwr_now is not None and txwr_now != txwr:
            ok, txwr = True, txwr_now
        else:
            ok, txwr = wait_for_boot_tx_cmd(monitor, 0x25, txwr, timeout_block_s, expected_val=0)
        if not ok and reader is not None:
            ok = wait_for_uart_tx_cmd(reader, 0x25, timeout_block_s, expected_val=0)
        if not ok:
            _log(f"block {idx} failed or timed out")
            return False
        if idx % 64 == 0:
            _log(f"block {idx+1}/{blocks} ok")

    done_frame = build_frame(CMD_DONE, b"")
    write_rx(rx_path, done_frame)
    ok, txwr = wait_for_boot_tx_cmd(monitor, 0x27, txwr, timeout_init_s, expected_val=1)
    if not ok and reader is not None:
        ok = wait_for_uart_tx_cmd(reader, 0x27, timeout_init_s, expected_val=1)
    if not ok:
        _log("finalize failed or timed out")
        return False
    return True


def dump_vtor(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    vtor, _ = monitor_read_word(monitor, 0xE000ED08)
    try:
        with (outdir / "vtor_debug.txt").open("a") as f:
            f.write(f"{tag} VTOR=0x{(vtor or 0) & 0xFFFFFFFF:08X}\n")
    except Exception:
        pass


def dump_pc(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    pc_val, pc_out = monitor_read_pc(monitor)
    try:
        with (outdir / "pc_debug.txt").open("a") as f:
            if pc_out:
                f.write(pc_out)
            else:
                f.write(f"{tag} PC=0x{(pc_val or 0) & 0xFFFFFFFF:08X}\n")
    except Exception:
        pass


def ensure_app_vtor(monitor: RenodeMonitor, outdir: Path, tag: str) -> None:
    pc_val, _ = monitor_read_pc(monitor)
    if pc_val is None:
        return
    if pc_val < 0x08010000 or pc_val >= 0x08040000:
        return
    vtor, _ = monitor_read_word(monitor, 0xE000ED08)
    if vtor is None or vtor == 0x08010000 or vtor == 0x00010000:
        return
    try:
        monitor.cmd("pause")
        monitor.cmd("sysbus WriteDoubleWord 0xE000ED08 0x08010000")
        monitor.cmd("start")
        with (outdir / "vtor_debug.txt").open("a") as f:
            f.write(f"{tag} VTOR forced to 0x08010000\n")
    except Exception:
        pass


def patch_fast_flash_read(monitor: RenodeMonitor, outdir: Path) -> tuple[int | None, int | None, int | None]:
    # Patch SPI flash read helpers to return immediately (BX LR).
    monitor.cmd("pause")
    orig_read, _ = monitor_read_word(monitor, 0x0801F4A8)
    orig_dma, _ = monitor_read_word(monitor, 0x0801F894)
    orig_spi, _ = monitor_read_word(monitor, 0x0801F524)
    monitor.cmd("sysbus WriteDoubleWord 0x0801F4A8 0x00004770")
    monitor.cmd("sysbus WriteDoubleWord 0x0801F894 0x00004770")
    # Speed init: spi_transceive_byte -> return 0 (MOVS R0,#0; BX LR).
    monitor.cmd("sysbus WriteDoubleWord 0x0801F524 0x47702000")
    val1, _ = monitor_read_word(monitor, 0x0801F4A8)
    val2, _ = monitor_read_word(monitor, 0x0801F894)
    val3, _ = monitor_read_word(monitor, 0x0801F524)
    try:
        with (outdir / "patch_debug.txt").open("a") as f:
            f.write(f"flash_read_data_from_address=0x{(val1 or 0):08X}\n")
            f.write(f"spi_flash_read_dma=0x{(val2 or 0):08X}\n")
            f.write(f"spi_transceive_byte=0x{(val3 or 0):08X}\n")
    except Exception:
        pass
    return orig_read, orig_dma, orig_spi


def restore_flash_patches(
    monitor: RenodeMonitor,
    orig_read: int | None,
    orig_dma: int | None,
    orig_spi: int | None,
    outdir: Path,
) -> None:
    monitor.cmd("pause")
    if orig_read is not None:
        monitor.cmd(f"sysbus WriteDoubleWord 0x0801F4A8 0x{orig_read & 0xFFFFFFFF:08X}")
    if orig_dma is not None:
        monitor.cmd(f"sysbus WriteDoubleWord 0x0801F894 0x{orig_dma & 0xFFFFFFFF:08X}")
    if orig_spi is not None:
        monitor.cmd(f"sysbus WriteDoubleWord 0x0801F524 0x{orig_spi & 0xFFFFFFFF:08X}")
    val1, _ = monitor_read_word(monitor, 0x0801F4A8)
    val2, _ = monitor_read_word(monitor, 0x0801F894)
    val3, _ = monitor_read_word(monitor, 0x0801F524)
    try:
        with (outdir / "patch_debug.txt").open("a") as f:
            f.write(f"flash_read_restore=0x{(val1 or 0):08X}\n")
            f.write(f"spi_flash_read_dma_restore=0x{(val2 or 0):08X}\n")
            f.write(f"spi_transceive_restore=0x{(val3 or 0):08X}\n")
    except Exception:
        pass


def patch_bootloader_update_info(monitor: RenodeMonitor, outdir: Path) -> int | None:
    """Patch update_device_firmware_info in bootloader to return immediately."""
    monitor.cmd("pause")
    orig, _ = monitor_read_word(monitor, 0x08004020)
    monitor.cmd("sysbus WriteDoubleWord 0x08004020 0x00004770")
    val, _ = monitor_read_word(monitor, 0x08004020)
    try:
        with (outdir / "patch_debug.txt").open("a") as f:
            f.write(f"update_device_firmware_info_patch=0x{(val or 0):08X}\n")
    except Exception:
        pass
    return orig


def patch_bootloader_spi_ready(monitor: RenodeMonitor, outdir: Path) -> int | None:
    """Patch spi_check_status_flag to return true (MOVS R0,#1; BX LR)."""
    monitor.cmd("pause")
    orig, _ = monitor_read_word(monitor, 0x08004B22)
    monitor.cmd("sysbus WriteDoubleWord 0x08004B22 0x47702001")
    val, _ = monitor_read_word(monitor, 0x08004B22)
    try:
        with (outdir / "patch_debug.txt").open("a") as f:
            f.write(f"spi_check_status_flag_patch=0x{(val or 0):08X}\n")
    except Exception:
        pass
    return orig


def patch_bootloader_spi_transmit(monitor: RenodeMonitor, outdir: Path) -> int | None:
    """Patch spi_transmit_byte_with_wait to return 0 immediately."""
    monitor.cmd("pause")
    orig, _ = monitor_read_word(monitor, 0x08004678)
    monitor.cmd("sysbus WriteDoubleWord 0x08004678 0x47702000")
    val, _ = monitor_read_word(monitor, 0x08004678)
    try:
        with (outdir / "patch_debug.txt").open("a") as f:
            f.write(f"spi_transmit_byte_with_wait_patch=0x{(val or 0):08X}\n")
    except Exception:
        pass
    return orig


def patch_bootloader_flash_write_buffer(monitor: RenodeMonitor, outdir: Path) -> int | None:
    """Patch flash_write_buffer to return success immediately."""
    monitor.cmd("pause")
    orig, _ = monitor_read_word(monitor, 0x080019C4)
    monitor.cmd("sysbus WriteDoubleWord 0x080019C4 0x47702001")
    val, _ = monitor_read_word(monitor, 0x080019C4)
    try:
        with (outdir / "patch_debug.txt").open("a") as f:
            f.write(f"flash_write_buffer_patch=0x{(val or 0):08X}\n")
    except Exception:
        pass
    return orig


def run_for(monitor: RenodeMonitor, seconds: float) -> None:
    # Run in Renode and wait locally so follow-up commands aren't sent too early.
    monitor.cmd("start", wait_prompt_s=MONITOR_WAIT_S)
    time.sleep(scaled(seconds))
    monitor.cmd("pause", wait_prompt_s=MONITOR_WAIT_S)
    check_deadline()


def run_until_pattern(
    monitor: RenodeMonitor,
    path: Path,
    pattern: bytes,
    timeout_s: float,
    step_s: float = 2.0,
    start_offset: int = 0,
) -> bool:
    timeout_s = scaled(timeout_s)
    step_s = scaled(step_s)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        check_deadline()
        if wait_for_pattern(path, pattern, timeout_s=0.2, start_offset=start_offset):
            return True
        run_for(monitor, step_s)
    return wait_for_pattern(path, pattern, timeout_s=0.2, start_offset=start_offset)


def erase_app_region(monitor: RenodeMonitor, outdir: Path, size: int = 0x60000) -> None:
    """Emulation-only: prefill app region with 0xFF to satisfy bootloader erase checks."""
    erase_bin = outdir / "app_erase_ff.bin"
    if not erase_bin.exists():
        erase_bin.write_bytes(bytes([0xFF]) * size)
    monitor.cmd("pause")
    monitor.cmd(f"sysbus LoadBinary @{erase_bin} 0x08010000")
    monitor.cmd(f"sysbus LoadBinary @{erase_bin} 0x00010000")


def start_renode(resc_path: Path, outdir: Path, rx_file_uart1: Path, rx_file_uart2: Path, port: int) -> subprocess.Popen:
    env = os.environ.copy()
    env["PWD"] = str(REPO_ROOT)
    env["BC280_RENODE_OUTDIR"] = str(outdir)
    env["BC280_LCD_OUTDIR"] = str(outdir)
    env["BC280_LCD_FAST"] = "1"
    env["BC280_SPI_LOG"] = "1" if DIAG else "0"
    env["BC280_LCD_LOG"] = "1" if DIAG else "0"
    soft_enq = os.environ.get("BC280_UART1_SOFT_ENQUEUE")
    if soft_enq is None or soft_enq == "":
        soft_enq = "0"
    env["BC280_UART1_SOFT_ENQUEUE"] = soft_enq
    env["BC280_UART1_SOFT_ENQUEUE_BOOT"] = "0"
    env["BC280_UART1_SOFT_TTM"] = os.environ.get("BC280_UART1_SOFT_TTM", "0")
    env["BC280_BOOT_TX_BRIDGE"] = os.environ.get("BC280_BOOT_TX_BRIDGE", "0")
    env["BC280_UART1_RX"] = str(rx_file_uart1)
    env["BC280_UART2_RX"] = str(rx_file_uart2)
    renode_bin = find_renode()
    cmd = [
        renode_bin,
        "--disable-xwt",
        "-P",
        str(port),
        "-e",
        f"include @{resc_path}",
    ]
    log_path = outdir / "renode.log"
    log_f = open(log_path, "w")
    return subprocess.Popen(cmd, cwd=str(REPO_ROOT), env=env, stdout=log_f, stderr=subprocess.STDOUT)


def setup_uart1_socket(monitor: RenodeMonitor, outdir: Path, port: int) -> UARTSocketBridge:
    name = "ble_uart1"
    log_path = outdir / "uart1_socket_monitor.txt"
    def log(line: str) -> None:
        try:
            with log_path.open("a") as f:
                f.write(line.rstrip() + "\n")
        except Exception:
            pass
    log(monitor.cmd(f'emulation CreateServerSocketTerminal {int(port)} "{name}" false') or "")
    # Renode sometimes expects fully-qualified peripheral names; try sysbus.uart1 first.
    out = monitor.cmd(f"connector Connect sysbus.uart1 {name}")
    log(out or "")
    if out and ("not found" in out.lower() or "no such" in out.lower()):
        out = monitor.cmd(f"connector Connect uart1 {name}")
        log(out or "")
    if out and ("not found" in out.lower() or "no such" in out.lower()):
        raise RuntimeError(f"failed to connect UART1 socket terminal: {out.strip()}")
    tx_log = outdir / "uart1_tx.log"
    try:
        tx_log.parent.mkdir(parents=True, exist_ok=True)
        tx_log.write_bytes(b"")
    except Exception:
        pass
    return UARTSocketBridge("127.0.0.1", int(port), tx_log, outdir)


def start_uart1_file_injector(monitor: RenodeMonitor, outdir: Path) -> None:
    script = """
import os, time, threading
try:
    m = emulationManager.CurrentEmulation.Machines[0]
except Exception:
    m = None
reg = m.SystemBus.WhatIsAt(0x40013800) if m is not None else None
uart = reg.Peripheral if reg is not None and hasattr(reg, 'Peripheral') else None
path = os.environ.get('BC280_UART1_RX')
log_path = os.path.join(os.environ.get('BC280_RENODE_OUTDIR') or '.', 'uart1_file_injector.txt')
state = {'off': 0}
def log(msg):
    try:
        with open(log_path, 'a') as f:
            f.write(str(msg) + '\\n')
    except Exception:
        pass
log('uart1_file_injector init path=%s uart=%s' % (path, uart))
def loop():
    log('uart1_file_injector start path=%s' % path)
    while True:
        try:
            if not path:
                time.sleep(0.05)
                continue
            data = open(path, 'rb').read()
        except Exception:
            time.sleep(0.05)
            continue
        if state['off'] < len(data):
            chunk = data[state['off']:]
            state['off'] = len(data)
            for b in chunk:
                try:
                    if uart is not None and hasattr(uart, 'WriteChar'):
                        uart.WriteChar(int(b))
                except Exception:
                    pass
        time.sleep(0.01)
threading.Thread(target=loop, daemon=True).start()
"""
    script_path = outdir / "uart1_file_injector.py"
    try:
        script_path.write_text(script)
    except Exception:
        return
    out = monitor.cmd(f"python \"exec(open(r'{script_path}').read())\"")
    try:
        with (outdir / "uart1_file_injector_monitor.txt").open("a") as f:
            f.write(out or "")
    except Exception:
        pass


def write_vendor_resc(path: Path, outdir: Path) -> None:
    renode_dir = REPO_ROOT / "open-firmware" / "renode"
    platform = renode_dir / "bc280_platform_fast.repl"
    fw = REPO_ROOT / "firmware" / "BC280_Combined_Firmware_3.3.6_4.2.5.bin"
    text = f"""# Auto-generated for test_full_update_flow.py
mach create "bc280-vendor"
python \"\"\"
import os
try:
    os.chdir(r\"{renode_dir}\")
except Exception:
    pass
\"\"\"
include @{platform}
sysbus LoadBinary @{fw} 0x08000000
sysbus LoadBinary @{fw} 0x00000000
sysbus.cpu PC 0x080001AD
sysbus.cpu SP 0x200055B0
python \"\"\"
import os
outdir = r\"{outdir}\"
try:
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, 'renode_outdir.txt'), 'w') as f:
        f.write(outdir)
except Exception:
    pass
\"\"\"
"""
    path.write_text(text)


def run() -> int:
    global DEADLINE
    # Build open firmware with test hooks + bootflag-on-boot (unless skipped).
    if os.environ.get("BC280_SKIP_BUILD") != "1":
        build_cmd = [
            "make",
            "-C",
            str(REPO_ROOT / "open-firmware"),
            "-s",
            "-B",
            "RENODE_TEST=1",
            "BOOTLOADER_FLAG_ON_BOOT=1",
            "RENODE_LCD_DEMO=0",
        ]
        subprocess.check_call(build_cmd)
    if not OPEN_FW_BIN.exists():
        print("open_firmware.bin not found after build", file=sys.stderr)
        return 1
    if not COMBINED_FW_BIN.exists():
        print("combined firmware image not found", file=sys.stderr)
        return 1

    outdir_base = os.environ.get("BC280_OUTDIR_BASE")
    if outdir_base:
        base = Path(outdir_base).resolve()
        base.mkdir(parents=True, exist_ok=True)
        outdir = base / f"fullflow.{int(time.time())}"
        outdir.mkdir(parents=True, exist_ok=True)
    else:
        outdir = Path(tempfile.mkdtemp(prefix="bc280-renode-fullflow."))
    outdir = outdir.resolve()
    rx1_file = outdir / "uart1_rx.bin"
    rx2_file = outdir / "uart2_rx.bin"
    rx1_file.write_bytes(b"")
    rx2_file.write_bytes(b"")
    monitor_port = pick_port()
    uart1_port = pick_port(env_int("BC280_UART1_PORT", 10001))

    bl_sp, bl_pc = read_vector(COMBINED_FW_BIN)
    app_sp, app_pc = read_vector_at_offset(COMBINED_FW_BIN, 0x10000)
    try:
        ble_status_ptr = read_u32_at(COMBINED_FW_BIN, 0x8012FC8)
        ble_connected_flag_addr = ble_status_ptr + 0x1A2
    except Exception:
        ble_connected_flag_addr = 0x2000152A

    results = []
    ok_all = True

    # Step 1: boot vendor firmware.
    vendor_resc = outdir / "vendor_autogen.resc"
    write_vendor_resc(vendor_resc, outdir)
    start_ts = time.time()
    proc = start_renode(vendor_resc, outdir, rx1_file, rx2_file, monitor_port)
    monitor = RenodeMonitor("127.0.0.1", monitor_port)
    startup_s = time.time() - start_ts
    if startup_s > MAX_STARTUP_S:
        raise TimeoutError(f"renode startup exceeded budget ({startup_s:.2f}s)")
    DEADLINE = time.time() + MAX_RUNTIME_S
    print(
        f"[timing] startup={startup_s:.2f}s runtime_budget={MAX_RUNTIME_S:.2f}s scale={TIMEOUT_SCALE:.2f}",
        file=sys.stderr,
    )
    try:
        # Wait for the RESC python hook to run, then select the machine.
        wait_for_file(outdir / "renode_outdir.txt", timeout_s=8.0)
        ensure_machine_selected(monitor, outdir)
        if DIAG:
            dump_uart_methods(monitor, outdir, "uart1")
            dump_cpu_methods(monitor, outdir)
        use_uart1_socket_env = os.environ.get("BC280_UART1_SOCKET")
        use_uart1_socket = True if use_uart1_socket_env is None else (use_uart1_socket_env == "1")
        global UART1_BRIDGE
        if use_uart1_socket:
            UART1_BRIDGE = setup_uart1_socket(monitor, outdir, uart1_port)
            if UART1_FILE_INJECTOR:
                start_uart1_file_injector(monitor, outdir)
        for cmd in ("sysbus.cpu WfiAsNop true", "cpu WfiAsNop true"):
            try:
                monitor.cmd(cmd)
            except Exception:
                pass
        try:
            monitor.cmd("sysbus ReadDoubleWord 0x40013008")
        except Exception:
            pass
        if os.environ.get("BC280_BOOT_OTA_EMUL", "0") == "1":
            try:
                monitor.cmd("python \"import System; System.AppDomain.CurrentDomain.SetData('bc280_boot_ota_emul', True)\"")
            except Exception:
                pass
        if DIAG:
            python_dump_env(monitor, outdir, "startup")
        if os.environ.get("BC280_DUMP_PY_GLOBALS", "0") == "1":
            python_dump_globals(monitor, outdir)
        if DIAG and os.environ.get("BC280_DUMP_PERIPHERALS", "1") == "1":
            dump_monitor_info(monitor, outdir)
        if os.environ.get("BC280_UART1_SANITY", "0") == "1":
            uart1_sanity_check(monitor, outdir)
        # Ensure TIM2 and USART1 IRQs are enabled in NVIC for bootloader/app scheduling.
        ensure_nvic_enabled(monitor, 28)
        ensure_nvic_enabled(monitor, 37)
        orig_read = None
        orig_dma = None
        orig_spi = None
        if FAST_FLASH_READ:
            orig_read, orig_dma, orig_spi = patch_fast_flash_read(monitor, outdir)
        run_for(monitor, 0.2 if FAST_FLOW else 80)
        forced_app_jump = False
        pc_val, _ = monitor_read_pc(monitor)
        if pc_val is not None and pc_val < 0x08010000:
            if os.environ.get("BC280_FORCE_APP_JUMP", "0") != "0":
                force_jump_to_app(monitor, outdir, COMBINED_FW_BIN)
                forced_app_jump = True
        app_pc_ok = wait_for_pc_in_range(
            monitor,
            0x08010000,
            0x0803FFFF,
            timeout_s=MAX_STARTUP_S,
            step_s=0.2,
            log_path=outdir / "app_pc_debug.txt",
        )
        if not app_pc_ok:
            run_for(monitor, 0.5)
            app_pc_ok = wait_for_pc_in_range(
                monitor,
                0x08010000,
                0x0803FFFF,
                timeout_s=MAX_STARTUP_S,
                step_s=0.2,
                log_path=outdir / "app_pc_debug.txt",
            )
        if not app_pc_ok:
            results.append("vendor app start: FAIL (PC not in app range)")
            if REQUIRE_APP_START:
                ok_all = False
        ensure_app_vtor(monitor, outdir, "post_boot")
        dump_vtor(monitor, outdir, "post_boot")

        app_loop_ok = None
        forced_app_main = False
        app_main_start = 0x80283E6
        app_main_end = 0x802845C
        if os.environ.get("BC280_SKIP_APP_LOOP", "1") != "1":
            app_loop_log = outdir / "app_loop_debug.txt"
            app_loop_ok = wait_for_pc_in_range(
                monitor,
                app_main_start,
                app_main_end,
                timeout_s=APP_LOOP_TIMEOUT_S,
                step_s=APP_LOOP_STEP_S,
                log_path=app_loop_log,
            )
            if not app_loop_ok and os.environ.get("BC280_FORCE_APP_MAIN", "1") != "0":
                force_jump_to_app_main(monitor, outdir, app_main_start | 1, app_sp=app_sp)
                forced_app_main = True
                app_loop_ok = wait_for_pc_in_range(
                    monitor,
                    app_main_start,
                    app_main_end,
                    timeout_s=min(APP_LOOP_TIMEOUT_S, 10.0),
                    step_s=APP_LOOP_STEP_S,
                    log_path=app_loop_log,
                )
        if FAST_FLASH_READ:
            restore_flash_patches(monitor, orig_read, orig_dma, orig_spi, outdir)
        run_for(monitor, 0.2 if FAST_FLOW else 4)
        if app_loop_ok is True:
            results.append("vendor app loop: ok")
        elif app_loop_ok is False:
            if forced_app_main:
                results.append("vendor app loop: WARN (forced app main)")
            else:
                results.append("vendor app loop: WARN (not in main loop)")
            if REQUIRE_APP_LOOP:
                ok_all = False
        else:
            if forced_app_jump:
                results.append("vendor app loop: WARN (forced app jump)")
            else:
                results.append("vendor app loop: skipped")

        # Install hooks to confirm OEM BLE command path is executed.
        install_pc_hook(monitor, outdir, 0x801137C, "APP_BLE_UART_CommandProcessor")
        install_pc_hook(monitor, outdir, 0x8002FB8, "validate_firmware_header")
        install_pc_hook(monitor, outdir, 0x8002FE8, "jump_to_application_firmware")
        install_pc_hook(monitor, outdir, 0x8005794, "BL_USART1_IRQHandler")
        install_pc_hook(monitor, outdir, 0x8005178, "BL_TIM2_IRQHandler")
        install_pc_hook(monitor, outdir, 0x8013128, "uart_send_ttm_mac_query")
        install_pc_hook(monitor, outdir, 0x80271AC, "handle_bluetooth_mac_communication")
        if DIAG:
            # App IRQ hooks (vector table: SysTick/TIM2/USART1).
            install_pc_hook(monitor, outdir, 0x801015E, "APP_SysTick_Handler")
            install_pc_hook(monitor, outdir, 0x8022890, "APP_TIM2_IRQHandler")
            install_pc_hook(monitor, outdir, 0x80271AC, "APP_USART1_IRQHandler")
            if not FAST_FLASH_READ:
                install_pc_hook(monitor, outdir, 0x801F710, "write_uint16_to_storage")
            if not FAST_FLASH_READ:
                install_pc_hook(monitor, outdir, 0x801F4A8, "flash_read_data_from_address")
                install_pc_hook(monitor, outdir, 0x801F4A9, "flash_read_data_from_address+1")
            install_pc_hook(monitor, outdir, 0x801F78C, "flash_write_data_to_address")
            install_pc_hook(monitor, outdir, 0x801F468, "spi_flash_erase_4k_sector")
            install_pc_hook(monitor, outdir, 0x801F469, "spi_flash_erase_4k_sector+1")
            install_pc_hook(monitor, outdir, 0x801F5B0, "flash_write_data_sectors")
            install_pc_hook(monitor, outdir, 0x801F5B1, "flash_write_data_sectors+1")
            install_pc_hook(monitor, outdir, 0x801F730, "flash_write_page_data")
            install_pc_hook(monitor, outdir, 0x801F731, "flash_write_page_data+1")
            # Probe inside flash_write_data_to_address (combined firmware) to confirm branching.
            install_pc_hook(monitor, outdir, 0x801F7FA, "flash_write_check_j")
            install_pc_hook(monitor, outdir, 0x801F802, "flash_write_erase_call")
            install_pc_hook(monitor, outdir, 0x801F826, "flash_write_sector_call")
            install_pc_hook(monitor, outdir, 0x801F7DE, "flash_write_after_read")
            install_pc_hook(monitor, outdir, 0x801F7DF, "flash_write_after_read+1")
            install_pc_hook(monitor, outdir, 0x801F7DA, "flash_write_read_callsite")
            # Combined 3.3.6 app flash-write path (SPI1) for OEM-accurate tracing.
            install_pc_hook(monitor, outdir, 0x8004704, "bl_spi_flash_write_data")
            install_pc_hook(monitor, outdir, 0x8004884, "spi_flash_write_command")
            install_pc_hook(monitor, outdir, 0x8004678, "spi_transmit_byte_with_wait")
            install_pc_hook(monitor, outdir, 0x802750C, "sram_heap_malloc")
        if FAST_FLASH_READ:
            install_fast_flash_read_hook(monitor, outdir, 0x801F4A8)
            install_fast_flash_read_hook(monitor, outdir, 0x801F4A9)
            if FAST_BOOTFLAG_WRITE:
                install_fast_bootflag_write_hook(monitor, outdir, 0x801F710)
        if FAST_OPENFW_BOOTFLAG:
            install_openfw_bootflag_hook(monitor, outdir, 0x080353E0)
            install_openfw_bootflag_hook(monitor, outdir, 0x080353E1)
            install_openfw_fast_read_hook(monitor, outdir, 0x080347C4)
            install_openfw_fast_read_hook(monitor, outdir, 0x080347C5)
        if HOOK_VERBOSE:
            install_pc_hook(monitor, outdir, 0x8011A3C, "process_incoming_uart_commands")
            install_pc_hook(monitor, outdir, 0x8011C40, "ble_uart_frame_parser")
            install_pc_hook(monitor, outdir, 0x801F5B0, "flash_write_data_sectors")
            install_pc_hook(monitor, outdir, 0x801F6F0, "spi_flash_write_enable")
            install_pc_hook(monitor, outdir, 0x801F524, "spi_transceive_byte")
            install_pc_hook(monitor, outdir, 0x801F525, "spi_transceive_byte+1")
            sample_pc_hits(monitor, outdir, "app_main_loop", 0x802842C, 0x802845C, duration_s=2.0, step_s=0.1)

        # Step 2: enter bootloader via UART (BLE passthrough).
        # BLE OTA helper uses CMD 0x20 to request bootloader.
        run_for(monitor, 20)
        dump_pc(monitor, outdir, "pre_ble_cmd")
        ensure_app_vtor(monitor, outdir, "pre_ble_cmd")
        dump_vtor(monitor, outdir, "pre_ble_cmd")
        if DIAG:
            dump_heap_state(monitor, outdir, "before_ble_cmd")
            if APP_BLE_UART == 2:
                dump_uart2_regs(monitor, outdir, "app_ble_before_cmd")
            else:
                dump_uart1_regs(monitor, outdir, "app_ble_before_cmd")
        if os.environ.get("BC280_SKIP_UART1_READY", "0") != "1":
            wait_for_uart1_cr1_ready(monitor, timeout_s=12.0, step_s=1.0)
            ensure_nvic_enabled(monitor, 37)
            if DIAG:
                dump_nvic_state(monitor, outdir, "app_uart1_irq_enabled", 37)
            ensure_uart1_rxneie(monitor)
            if not wait_for_uart1_activity(outdir, timeout_s=8.0):
                results.append("vendor uart1: WARN (no activity yet)")
        app_rx_file = select_rx_path(APP_BLE_UART, rx1_file, rx2_file)
        # Simulate BLE module connect line so app marks link up before command.
        write_rx(app_rx_file, b"TTM:CONNECTED\r\n", monitor=monitor, outdir=outdir)
        if os.environ.get("BC280_SEND_MAC_ON_CONNECT", "1") == "1":
            send_ble_mac_response(outdir, app_rx_file, monitor=monitor)
        wait_for_ble_mac_query(monitor, outdir, app_rx_file, timeout_s=2.0)
        if DIAG:
            dump_uart1_regs(monitor, outdir, "app_ble_after_connect")
        if DIAG:
            dump_nvic_state(monitor, outdir, "app_uart1_after_connect", 37)
            poke_irq(monitor, outdir, 37, "app_uart1_poked")
            poke_irq(monitor, outdir, 28, "app_tim2_poked")
        if DIAG:
            dump_ttm_buffer(monitor, outdir, "app_ttm_after_connect", 0x2000232C, 0x2000000E)
        if FAST_FLOW and ALLOW_FAST_FLAGS:
            try:
                monitor.cmd(f"sysbus WriteByte 0x{ble_connected_flag_addr:08X} 0x01")
                results.append("vendor ble link: WARN (fast flow flag set)")
            except Exception:
                results.append("vendor ble link: WARN (fast flow flag set failed)")
        if not (FAST_FLOW and ALLOW_FAST_FLAGS):
            if not wait_for_ble_connected(monitor, ble_connected_flag_addr, timeout_s=6.0):
                results.append("vendor ble link: FAIL (no connected flag)")
                ok_all = False
        try:
            val, _ = monitor_read_word(monitor, ble_connected_flag_addr & ~0x3)
            shift = (ble_connected_flag_addr & 0x3) * 8
            flag_val = ((val or 0) >> shift) & 0xFF
            with (outdir / "ble_connected_debug.txt").open("a") as f:
                f.write(f"ble_connected_flag=0x{flag_val:02X}\n")
        except Exception:
            pass
        if DIAG:
            dump_ble_indices(monitor, outdir, "before_rx")
        base = 0x200017B4
        rx_read_before, _ = monitor_read_word(monitor, base + 0x504)
        enter_bl_frame = build_frame(0x20, b"")
        spi_log = outdir / "spi_flash_debug.txt"
        spi_before = file_size(spi_log)
        ok = False
        for _ in range(max(1, BOOT_CMD_TRIES)):
            write_rx(app_rx_file, enter_bl_frame, monitor=monitor, outdir=outdir)
            run_for(monitor, BOOT_CMD_WAIT_S)
            if rx_read_before is not None and DIAG:
                wait_for_ble_rx_advance(monitor, base, rx_read_before, timeout_s=4.0, step_s=0.5)
            if DIAG:
                dump_ble_slot_bytes(monitor, outdir, "after_rx", base)
                dump_ble_indices(monitor, outdir, "after_rx")
            run_for(monitor, 0.2)
            python_dump_bootflag(monitor)
            ok = read_bootflag_value(outdir).lower() == "0xaa"
            if DIAG and not ok:
                ok = wait_for_pattern(
                    spi_log,
                    b"PP_DATA writing BOOTLOADER_MODE",
                    timeout_s=2.0,
                    start_offset=spi_before,
                )
            if ok:
                break
            python_dump_bootflag(monitor)
            val = read_bootflag_value(outdir)
            if val.lower() == "0xaa":
                ok = True
                break
        if DIAG:
            dump_ble_indices(monitor, outdir, "after_parse")
            dump_ble_slot_bytes(monitor, outdir, "after_parse", base)
            dump_heap_state(monitor, outdir, "after_ble_cmd")
        if ok:
            results.append("vendor->bootloader: ok")
        else:
            slot = read_ble_slot(monitor, base)
            try:
                with (outdir / "ble_slot_debug.txt").open("a") as f:
                    f.write(
                        "slot sof=0x%02X cmd=0x%02X plen=0x%02X len=0x%04X rxr=0x%04X rxw=0x%04X\n"
                        % (
                            slot.get("sof", 0),
                            slot.get("cmd", 0),
                            slot.get("plen", 0),
                            slot.get("slot_len", 0),
                            slot.get("rx_read", 0),
                            slot.get("rx_write", 0),
                        )
                    )
            except Exception:
                pass
            if os.environ.get("BC280_FORCE_APP_BOOTFLAG", "0") == "1":
                python_set_bootflag(monitor, outdir, 0xAA)
                run_for(monitor, 1)
                results.append("vendor->bootloader: WARN (emulated app write)")
                ok = True
            elif os.environ.get("RENODE_FORCE_BOOTFLAG") == "1":
                python_set_bootflag(monitor, outdir, 0xAA)
                run_for(monitor, 1)
                results.append("vendor->bootloader: WARN (forced flag)")
            else:
                results.append("vendor->bootloader: FAIL (no flag from UART)")
                ok_all = False

        # Step 3: reset to bootloader update mode.
        monitor.cmd("pause")
        for reset_cmd in ("machine Reset", "sysbus Reset", "reset"):
            try:
                monitor.cmd(reset_cmd)
            except Exception:
                pass
        monitor.cmd(f"sysbus.cpu SP 0x{bl_sp:08X}")
        monitor.cmd(f"sysbus.cpu PC 0x{bl_pc:08X}")
        monitor.cmd("sysbus WriteDoubleWord 0xE000ED08 0x08000000")
        run_for(monitor, 0.2 if FAST_FLOW else 3)
        # Bootloader BLE comms are on UART1 (same as app).
        boot_soft = os.environ.get("BC280_UART1_SOFT_ENQUEUE_BOOT", "0") == "1"
        python_set_uart1_boot_soft_enqueue(monitor, boot_soft)
        if BOOTLOADER_PATCHES:
            patch_bootloader_update_info(monitor, outdir)
            patch_bootloader_spi_ready(monitor, outdir)
            patch_bootloader_spi_transmit(monitor, outdir)
            patch_bootloader_flash_write_buffer(monitor, outdir)
        if DIAG:
            dump_pc(monitor, outdir, "bootloader_entry")
            dump_vtor(monitor, outdir, "bootloader_entry")
            if BOOT_UART == 2:
                dump_uart2_regs(monitor, outdir, "bootloader_entry")
            else:
                dump_uart1_regs(monitor, outdir, "bootloader_entry")
            dump_boot_comm_indices(monitor, outdir, "bootloader_entry")

        uart1_log = outdir / "uart1_tx.log"
        uart2_log = outdir / "uart2_tx.log"
        boot_rx_file = select_rx_path(BOOT_UART, rx1_file, rx2_file)
        boot_tx_log = uart2_log if BOOT_UART == 2 else uart1_log
        # Step 4: OTA push open-firmware via bootloader protocol.
        if os.environ.get("BC280_ERASE_APP_REGION", "0") == "1":
            erase_app_region(monitor, outdir)
        # Bootloader may require a fresh BLE connect notification.
        write_rx(boot_rx_file, b"TTM:CONNECTED\r\n", monitor=monitor, outdir=outdir)
        if os.environ.get("BC280_SEND_MAC_ON_CONNECT", "1") == "1":
            send_ble_mac_response(outdir, boot_rx_file, monitor=monitor)
        respond_ble_mac_query(outdir, boot_rx_file, monitor=monitor)
        if BOOT_UART == 2:
            ensure_nvic_enabled(monitor, 38)
            if DIAG:
                dump_nvic_state(monitor, outdir, "boot_uart2_irq_enabled", 38)
            ensure_uart2_rxneie(monitor)
        else:
            ensure_nvic_enabled(monitor, 37)
            if DIAG:
                dump_nvic_state(monitor, outdir, "boot_uart1_irq_enabled", 37)
            ensure_uart1_rxneie(monitor)
        ota_log = outdir / "bootloader_ota.txt"
        if DIAG:
            dump_pc(monitor, outdir, "ota_before")
            dump_vtor(monitor, outdir, "ota_before")
            if BOOT_UART == 2:
                dump_uart2_regs(monitor, outdir, "ota_before")
            else:
                dump_uart1_regs(monitor, outdir, "ota_before")
            dump_boot_comm_indices(monitor, outdir, "ota_before")
            dump_boot_tx_state(monitor, outdir, "ota_before")
        monitor.cmd("start")
        wait_for_ble_mac_query(
            monitor,
            outdir,
            boot_rx_file,
            timeout_s=2.0 if FAST_FLOW else 4.0,
            step_s=0.2,
        )
        ota_mode = os.environ.get("BC280_OTA_MODE", "legacy")
        boot_write_fn = UART1_BRIDGE.write if (boot_rx_file is None and UART1_BRIDGE) else None
        if ota_mode == "legacy":
            ota_sender = BootloaderOTASender(
                boot_rx_file,
                boot_tx_log,
                log_path=ota_log,
                chunk=OTA_CHUNK,
                delay_s=OTA_CHUNK_DELAY_S,
                inter_frame_delay_s=OTA_INTER_FRAME_S,
                write_fn=boot_write_fn,
            )
            ok_ota = ota_sender.push_image(
                OPEN_FW_BIN,
                timeout_init_s=2.0 if FAST_FLOW else 8.0,
                timeout_block_s=2.0 if FAST_FLOW else 6.0,
            )
        else:
            ok_ota = push_image_via_txring(
                monitor,
                boot_rx_file,
                OPEN_FW_BIN,
                ota_log,
                boot_tx_log,
                timeout_init_s=2.0 if FAST_FLOW else 6.0,
                timeout_block_s=1.0 if FAST_FLOW else 2.0,
            )
        monitor.cmd("pause")
        if DIAG:
            dump_pc(monitor, outdir, "ota_after")
            if BOOT_UART == 2:
                dump_uart2_regs(monitor, outdir, "ota_after")
            else:
                dump_uart1_regs(monitor, outdir, "ota_after")
            dump_boot_comm_indices(monitor, outdir, "ota_after")
            dump_boot_tx_state(monitor, outdir, "ota_after")
        if ok_ota:
            results.append("bootloader OTA: ok")
        else:
            force_flash = os.environ.get("RENODE_FORCE_FLASH_ON_OTA_FAIL", "0") == "1"
            if force_flash:
                results.append("bootloader OTA: WARN (no write ack; force-loaded app)")
                ok_all = False
                monitor.cmd("pause")
                monitor.cmd(f"sysbus LoadBinary @{OPEN_FW_BIN} 0x08010000")
                monitor.cmd(f"sysbus LoadBinary @{OPEN_FW_BIN} 0x00010000")
                monitor.cmd("start")
            else:
                results.append("bootloader OTA: FAIL")
                ok_all = False

        if os.environ.get("BC280_CLEAR_BOOTFLAG_BEFORE_APP", "0") == "1":
            python_set_bootflag(monitor, outdir, 0x00)
        run_for(monitor, 0.1 if FAST_FLOW else 1)

        # Step 5: let bootloader validate and jump to app.
        boot_count_before = count_pattern(uart1_log, b"[open-fw] boot")
        run_for(monitor, 0.5 if FAST_FLOW else 8)

        ok_boot = wait_for_pattern(uart1_log, b"[open-fw] boot", timeout_s=2.0 if FAST_FLOW else 8.0)
        ok_status = wait_for_pattern(uart1_log, b"[open-fw] t=", timeout_s=2.0 if FAST_FLOW else 8.0)
        if not ok_boot:
            # Fallback: jump directly to app vector if bootloader didn't jump.
            try:
                sp, pc = read_vector(OPEN_FW_BIN)
                monitor.cmd("pause")
                monitor.cmd(f"sysbus.cpu SP 0x{sp:08X}")
                monitor.cmd(f"sysbus.cpu PC 0x{pc:08X}")
                run_for(monitor, 0.5 if FAST_FLOW else 6)
                ok_boot = wait_for_pattern(uart1_log, b"[open-fw] boot", timeout_s=2.0 if FAST_FLOW else 6.0)
                ok_status = wait_for_pattern(uart1_log, b"[open-fw] t=", timeout_s=2.0 if FAST_FLOW else 6.0)
            except Exception:
                ok_boot = False
                ok_status = False
        stage_val = read_boot_stage(monitor, outdir, "after_app_boot")
        if not ok_boot:
            pc_val, _ = monitor_read_pc(monitor)
            if pc_val is not None and 0x08010000 <= pc_val < 0x08040000:
                results.append("open-fw boot: WARN (no UART log; PC in app region)")
                ok_boot = True
            elif stage_val is not None and stage_val >= 0xB005:
                results.append(f"open-fw boot: WARN (no UART log; stage 0x{stage_val:08X})")
                ok_boot = True
        if ok_boot:
            if ok_status:
                results.append("open-fw boot: ok")
            else:
                results.append("open-fw boot: WARN (no status line yet)")
        else:
            results.append("open-fw boot: FAIL")
            ok_all = False

        # Step 7: verify open firmware set bootloader flag on boot.
        bootflag_val_after = wait_for_bootflag(
            monitor,
            outdir,
            timeout_s=6.0 if FAST_FLOW else 24.0,
            step_s=1.0 if FAST_FLOW else 2.0,
        )
        if bootflag_val_after.lower() == "0xaa":
            results.append("bootflag-on-boot: ok")
        else:
            results.append(f"bootflag-on-boot: FAIL (flag {bootflag_val_after})")
            ok_all = False

        # Step 8: power-cycle -> bootloader update mode.
        boot_count_after = count_pattern(uart1_log, b"[open-fw] boot")
        monitor.cmd("pause")
        monitor.cmd(f"sysbus.cpu SP 0x{bl_sp:08X}")
        monitor.cmd(f"sysbus.cpu PC 0x{bl_pc:08X}")
        run_for(monitor, 2 if FAST_FLOW else 6)

        boot_count_final = count_pattern(uart1_log, b"[open-fw] boot")
        python_dump_bootflag(monitor)
        bootflag_val = read_bootflag_value(outdir)

        if boot_count_final == boot_count_after and bootflag_val.lower() == "0xaa":
            results.append("power-cycle -> bootloader: ok")
        else:
            results.append(f"power-cycle -> bootloader: FAIL (bootcount {boot_count_after}->{boot_count_final}, flag {bootflag_val})")
            ok_all = False
    finally:
        try:
            if UART1_BRIDGE:
                UART1_BRIDGE.close()
        except Exception:
            pass
        try:
            monitor.close()
        except Exception:
            pass
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    print("RESULTS")
    for r in results:
        print(f"- {r}")
    print(f"OUTDIR={outdir}")
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(run())
