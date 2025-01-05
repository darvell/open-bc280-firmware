import errno
import os
import select
import socket
import threading
import time
import sys

import System

sys.path.append(os.path.dirname(__file__))
from renode_utils import b2i as _b2i, tohex as _tohex

OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
# UART1 = BLE module passthrough (TTM:CONNECTED + 0x55-framed commands).
SOFT_ENQUEUE = os.environ.get("BC280_UART1_SOFT_ENQUEUE") == "1"
BOOT_SOFT_ENQUEUE_ENV = os.environ.get("BC280_UART1_SOFT_ENQUEUE_BOOT") == "1"
BOOT_SOFT_ENQUEUE_KEY = "bc280_uart1_boot_soft_enqueue"
BOOT_TX_BRIDGE = os.environ.get("BC280_BOOT_TX_BRIDGE", "1") != "0"
BOOT_TX_BRIDGE_KEY = "bc280_boot_tx_bridge"
BOOT_TX_POLL_S = float(os.environ.get("BC280_BOOT_TX_POLL_S", "0.02"))
BOOT_OTA_EMUL = os.environ.get("BC280_BOOT_OTA_EMUL", "0") == "1"
BOOT_OTA_EMUL_KEY = "bc280_boot_ota_emul"
SOFT_TTM = os.environ.get("BC280_UART1_SOFT_TTM") == "1"
BLE_MAC = os.environ.get("BC280_BLE_MAC", "112233445566")
UART_IRQ_POLL_S = float(os.environ.get("BC280_UART1_IRQ_POLL_S", "0.005"))

# Vendor app BLE context layout (see IDA: g_app_ble_comm_ctx).
def _env_int(name, default):
    raw = os.environ.get(name)
    if raw in (None, ""):
        return int(default)
    try:
        return int(raw, 0)
    except Exception:
        return int(default)


BLE_CTX_BASE = _env_int("BC280_APP_BLE_CTX_BASE", 0x20002108)
BLE_RX_RING_SIZE = 200
BLE_RX_RD_IDX_OFF = 0xC8
BLE_RX_WR_IDX_OFF = 0xCA

# App-side TTM string scan buffer (combined firmware 3.3.6/4.2.5).
APP_TTM_LEN_ADDR = _env_int("BC280_APP_TTM_LEN_ADDR", 0x2000000E)
APP_TTM_BUF_ADDR = _env_int("BC280_APP_TTM_BUF_ADDR", 0x2000232C)
APP_TTM_MAX = 0x64

# Bootloader BLE context layout (g_boot_ble_comm_ctx).
BOOT_CTX_BASE = 0x20000474
BOOT_RX_RING_SIZE = 200
BOOT_RX_RD_IDX_OFF = 0xC8
BOOT_RX_WR_IDX_OFF = 0xCA
BOOT_TX_RD_IDX_OFF = 0x46E
BOOT_TX_WR_IDX_OFF = 0x470
BOOT_TX_SLOT_BASE = 0x20000714
BOOT_TX_SLOT_SIZE = 0x9A
BOOT_TX_SLOT_LEN_OFF = 0x98

# Bootloader TTM string scan buffer (BL v0.2.0).
BOOT_TTM_LEN_ADDR = 0x20000016
BOOT_TTM_BUF_ADDR = 0x20000DFC
BOOT_TTM_MAX = 0x14

UART_IDLE_S = float(os.environ.get("BC280_UART1_IDLE_S", "0.002"))

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


KEY = "usart1_state"
state = System.AppDomain.CurrentDomain.GetData(KEY)


def _init_state():
    s = {
        # Minimal STM32F1-ish USART register set (offsets from base 0x40013800).
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
        "last_rx_ts": 0.0,
        "idle_pending": False,
        "idle_flag": False,
        # Optional PTY bridge to host (set BC280_UART1_PTY to enable).
        "pty_master": None,
        "pty_slave_path": None,
        "pty_link_path": None,
        "pty_thread": None,
        "pty_failed": False,
        # Optional TCP bridge to host (set BC280_UART1_TCP=host:port).
        "tcp_server": None,
        "tcp_client": None,
        "tcp_thread": None,
        "tcp_failed": False,
        "tcp_addr": None,
        # Optional RX file bridge (set BC280_UART1_RX to a file path).
        "rx_file_failed": False,
        "rx_file_path": None,
        "rx_file_thread": None,
        "rx_file_offset": 0,
        "rx_log_left": 20,
        "boot_tx_thread": None,
        "boot_tx_log_left": 20,
        "boot_ota_buf": bytearray(),
        "boot_ota_log_left": 20,
        # Logging
        "log_budget": 400,
        "seen": False,
    }
    System.AppDomain.CurrentDomain.SetData(KEY, s)
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "usart1_debug.txt"), "a") as f:
            f.write("USART1 init SOFT_ENQUEUE=%s BOOT_SOFT_ENQ_ENV=%s\n" % (str(SOFT_ENQUEUE), str(BOOT_SOFT_ENQUEUE_ENV)))
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
        with open(os.path.join(OUTDIR, "usart1_debug.txt"), "a") as f:
            f.write("%s\n" % msg)
        state["log_budget"] -= 1
    except Exception:
        pass


def _log_rx(msg):
    try:
        if not os.path.isdir(OUTDIR):
            os.makedirs(OUTDIR)
        with open(os.path.join(OUTDIR, "usart1_rx_debug.txt"), "a") as f:
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
        with open(os.path.join(OUTDIR, "uart1_tx.log"), "ab") as f:
            f.write(bytes([b]))
        # Also forward TX bytes to a PTY master if enabled so host tools can read.
        master_fd = state.get("pty_master")
        if master_fd is not None:
            try:
                os.write(master_fd, bytes([b]))
            except Exception:
                pass
        # Forward TX bytes to TCP client if connected.
        client = state.get("tcp_client")
        if client is not None:
            try:
                client.sendall(bytes([b]))
            except Exception:
                try:
                    client.close()
                except Exception:
                    pass
                state["tcp_client"] = None
    except Exception:
        pass
    _ble_handle_tx_byte(b)


def _enqueue_rx_bytes(rx_bytes):
    # If bootloader soft-enqueue is active, bypass the USART RX FIFO to avoid
    # duplicating bytes into the raw ring via both IRQ and soft path.
    boot_enq = _boot_soft_enqueue_enabled()
    bus = _get_sysbus()
    dma_used = False
    if not boot_enq:
        state.setdefault("rx_fifo", [])
    now = time.time()
    if rx_bytes:
        state["last_rx_ts"] = now
        state["idle_pending"] = True
        state["idle_flag"] = False
    for b in rx_bytes:
        if not boot_enq and _dma_rx_push_byte(b):
            dma_used = True
        elif not boot_enq:
            state.setdefault("rx_fifo", []).append(int(b) & 0xFF)
        _ble_soft_enqueue_byte(b)
        _boot_soft_enqueue_byte(b)
        _boot_ota_emul_byte(b)
        if bus is not None and SOFT_TTM:
            _app_cmd_buf_put_byte(bus, b)
            _boot_cmd_buf_put_byte(bus, b)
    if not boot_enq and not dma_used:
        _recompute_sr()
        _update_irq_lines()


def _ble_handle_tx_byte(b):
    try:
        if not state.get("ble_tx_seen"):
            state["ble_tx_seen"] = True
            _log("BLE TX handler active")
        buf = state.get("ble_tx_buf")
        if buf is None:
            buf = bytearray()
            state["ble_tx_buf"] = buf
        buf.append(int(b) & 0xFF)
        if len(buf) > 128:
            buf[:] = buf[-64:]
        if b"TTM:MAC-?" in bytes(buf):
            buf[:] = bytearray()
            mac = BLE_MAC.strip().replace(":", "").replace("-", "")
            if len(mac) != 12:
                mac = "112233445566"
            resp = ("TTM:MAC-" + mac + "\r\n").encode("ascii", "ignore")
            _enqueue_rx_bytes([int(x) & 0xFF for x in resp])
            try:
                state["ble_mac_resp_count"] = int(state.get("ble_mac_resp_count", 0)) + 1
                if state.get("ble_mac_resp_count", 0) <= 5:
                    _log("BLE MAC response injected mac=%s" % mac)
                    _log_rx("BLE MAC response injected")
            except Exception:
                pass
    except Exception:
        return


def _pty_reader():
    master_fd = state.get("pty_master")
    if master_fd is None:
        return
    while True:
        try:
            r, _, _ = select.select([master_fd], [], [], 0.05)
            if not r:
                continue
            data = os.read(master_fd, 256)
            if not data:
                time.sleep(0.01)
                continue
            # Push bytes into RX FIFO.
            rx_bytes = [_b2i(b) & 0xFF for b in data]
            _enqueue_rx_bytes(rx_bytes)
        except OSError as e:
            if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                time.sleep(0.01)
                continue
            break
        except Exception as e:
            try:
                _log("USART1 TCP reader error: %s" % str(e))
            except Exception:
                pass
            break


def _start_thread(target, args=()):
    t = threading.Thread(target=target, args=args)
    try:
        t.setDaemon(True)
    except Exception:
        pass
    t.start()
    return t


def _tcp_reader(sock):
    while True:
        try:
            r, _, _ = select.select([sock], [], [], 0.05)
            if not r:
                continue
            data = sock.recv(256)
            if not data:
                break
            if state.get("tcp_log_left", 0) > 0:
                try:
                    _log("USART1 TCP rx len=%d bytes=%s" % (len(data), _tohex(data[:16])))
                except Exception:
                    pass
                state["tcp_log_left"] = state.get("tcp_log_left", 1) - 1
            rx_bytes = [int(b) & 0xFF for b in data]
            _enqueue_rx_bytes(rx_bytes)
        except Exception:
            break
    try:
        sock.close()
    except Exception:
        pass
    state["tcp_client"] = None


def _tcp_acceptor(server_sock):
    while True:
        try:
            r, _, _ = select.select([server_sock], [], [], 0.05)
            if not r:
                continue
            client, _addr = server_sock.accept()
            client.setblocking(False)
            # Drop old client if still connected.
            old = state.get("tcp_client")
            if old is not None:
                try:
                    old.close()
                except Exception:
                    pass
            state["tcp_client"] = client
            if "tcp_log_left" not in state:
                state["tcp_log_left"] = 50
            _log("USART1 TCP client connected")
            _start_thread(_tcp_reader, (client,))
        except Exception as e:
            try:
                _log("USART1 TCP accept error: %s" % str(e))
            except Exception:
                pass
            time.sleep(0.05)


def _ensure_tcp_bridge():
    if state.get("tcp_server") is not None or state.get("tcp_failed"):
        return
    addr = os.environ.get("BC280_UART1_TCP")
    if not addr:
        return
    host = "127.0.0.1"
    port = None
    if ":" in addr:
        host, port_s = addr.rsplit(":", 1)
        try:
            port = int(port_s)
        except Exception:
            port = None
    else:
        try:
            port = int(addr)
        except Exception:
            port = None
    if port is None:
        state["tcp_failed"] = True
        _log("USART1 TCP bridge invalid addr '%s'" % addr)
        return
    try:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen(1)
        server.setblocking(False)
        state["tcp_server"] = server
        state["tcp_addr"] = (host, port)
        state["tcp_thread"] = _start_thread(_tcp_acceptor, (server,))
        _log("USART1 TCP bridge listening on %s:%d" % (host, port))
    except Exception as e:
        state["tcp_failed"] = True
        _log("USART1 TCP bridge init failed: %s" % str(e))


def _consume_rx_file():
    if state.get("rx_file_failed"):
        return
    path = os.environ.get("BC280_UART1_RX")
    if not path:
        return
    state["rx_file_path"] = path
    try:
        if not os.path.isfile(path):
            return
        offset = int(state.get("rx_file_offset", 0))
        with open(path, "rb") as f:
            f.seek(offset)
            data = f.read()
        if not data:
            return
        state["rx_file_offset"] = offset + len(data)
        if state.get("rx_log_left", 0) > 0:
            _log_rx("RX file len=%d bytes=%s" % (len(data), _tohex(data[:16])))
            state["rx_log_left"] = state.get("rx_log_left", 1) - 1
        rx_bytes = [_b2i(b) & 0xFF for b in data]
        _enqueue_rx_bytes(rx_bytes)
    except Exception as e:
        state["rx_file_failed"] = True
        _log("USART1 RX file read failed: %s" % str(e))


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
    if not os.environ.get("BC280_UART1_RX"):
        return
    state["rx_file_thread"] = _start_thread(_rx_file_worker)


def _ensure_pty_bridge():
    if state.get("pty_master") is not None:
        return
    if state.get("pty_failed"):
        return
    path = os.environ.get("BC280_UART1_PTY")
    if not path:
        return
    try:
        import pty
        import fcntl

        master_fd, slave_fd = pty.openpty()
        slave_path = os.ttyname(slave_fd)
        # Symlink the caller-visible path to the created slave for convenience.
        try:
            if os.path.lexists(path):
                os.remove(path)
            os.symlink(slave_path, path)
        except Exception:
            pass

        # Non-blocking read on the master.
        flags = fcntl.fcntl(master_fd, fcntl.F_GETFL)
        fcntl.fcntl(master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

        state["pty_master"] = master_fd
        state["pty_slave_path"] = slave_path
        state["pty_link_path"] = path

        state["pty_thread"] = _start_thread(_pty_reader)
        _log("USART1 PTY bridged at %s -> %s" % (path, slave_path))
    except Exception:
        state["pty_failed"] = True
        _log("USART1 PTY bridge init failed")


def _get_sysbus():
    # Similar to renode/dma1_stub.py: tolerate different Python host surfaces.
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


def _get_cpu():
    try:
        bus = _get_sysbus()
        if bus is None:
            return None
        cpus = list(bus.GetCPUs())
        return cpus[0] if cpus else None
    except Exception:
        return None


def _write_u32(bus, addr, value):
    try:
        bus.WriteDoubleWord(int(addr) & 0xFFFFFFFF, int(value) & 0xFFFFFFFF)
    except Exception:
        v = int(value) & 0xFFFFFFFF
        try:
            bus.WriteWord(int(addr) & 0xFFFFFFFF, v & 0xFFFF)
            bus.WriteWord(int(addr + 2) & 0xFFFFFFFF, (v >> 16) & 0xFFFF)
        except Exception:
            pass


def _write_u8(bus, addr, value):
    try:
        base = int(addr) & 0xFFFFFFFC
        shift = (int(addr) & 3) * 8
        word = int(bus.ReadDoubleWord(base)) & 0xFFFFFFFF
        word = (word & ~(0xFF << shift)) | ((int(value) & 0xFF) << shift)
        bus.WriteDoubleWord(base, word & 0xFFFFFFFF)
    except Exception:
        pass


def _read_u16(bus, addr):
    try:
        return int(bus.ReadWord(int(addr) & 0xFFFFFFFF)) & 0xFFFF
    except Exception:
        try:
            b = bus.ReadBytes(int(addr) & 0xFFFFFFFF, 2)
            return (int(b[0]) & 0xFF) | ((int(b[1]) & 0xFF) << 8)
        except Exception:
            return 0


def _read_u8(bus, addr):
    try:
        return int(bus.ReadByte(int(addr) & 0xFFFFFFFF)) & 0xFF
    except Exception:
        try:
            b = bus.ReadBytes(int(addr) & 0xFFFFFFFF, 1)
            return int(b[0]) & 0xFF
        except Exception:
            return 0


def _read_bytes(bus, addr, length):
    try:
        data = bus.ReadBytes(int(addr) & 0xFFFFFFFF, int(length))
        return [int(b) & 0xFF for b in data]
    except Exception:
        return [_read_u8(bus, int(addr) + i) for i in range(int(length))]


def _set_nvic_pending(irq):
    bus = _get_sysbus()
    if bus is None:
        return
    try:
        irq = int(irq)
        if irq < 0:
            return
        cpu = _get_cpu()
        if cpu is not None:
            for meth in ("SetPendingIRQ", "SetPendingInterrupt", "SetPendingException", "SetPending"):
                try:
                    fn = getattr(cpu, meth)
                except Exception:
                    fn = None
                if fn is not None:
                    try:
                        fn(int(irq))
                        return
                    except Exception:
                        pass
        reg = 0xE000E200 + 4 * (irq // 32)  # NVIC_ISPRx
        bit = 1 << (irq % 32)
        _write_u32(bus, reg, bit)
    except Exception:
        return


def _dma_rx_push_byte(b):
    try:
        if not (int(state.get("cr3", 0)) & (1 << 6)):  # CR3.DMAR
            return False
    except Exception:
        return False
    bus = _get_sysbus()
    if bus is None:
        return False
    try:
        dstate = System.AppDomain.CurrentDomain.GetData("dma1_state")
    except Exception:
        dstate = None
    if dstate is None or not hasattr(dstate, "get"):
        return False
    ch_list = dstate.get("ch") or []
    for idx, ch in enumerate(ch_list):
        try:
            ccr = int(ch.get("ccr", 0)) & 0xFFFFFFFF
            if not (ccr & 1):
                continue
            if ccr & (1 << 4):
                continue  # DIR=1 mem->periph
            paddr = int(ch.get("cpar", 0)) & 0xFFFFFFFF
            if paddr != 0x40013804:
                continue
            count = int(ch.get("cndtr", 0)) & 0xFFFF
            if count == 0:
                if ccr & (1 << 5):  # CIRC reload
                    count = int(ch.get("cndtr_init", 0)) & 0xFFFF
                    ch["cndtr"] = count
                    init_m = int(ch.get("cmar_init", 0)) & 0xFFFFFFFF
                    if init_m:
                        ch["cmar"] = init_m
            if count == 0:
                return False
            maddr = int(ch.get("cmar", 0)) & 0xFFFFFFFF
            try:
                bus.WriteBytes(bytearray([int(b) & 0xFF]), maddr)
            except Exception:
                _write_u8(bus, maddr, int(b) & 0xFF)
            if ccr & (1 << 7):  # MINC
                ch["cmar"] = (maddr + 1) & 0xFFFFFFFF
            count = (count - 1) & 0xFFFF
            ch["cndtr"] = count
            if count == 0:
                tcif_bit = 1 << (1 + 4 * idx)
                try:
                    dstate["isr"] = int(dstate.get("isr", 0)) | tcif_bit
                except Exception:
                    pass
                if ccr & (1 << 1):  # TCIE
                    _set_nvic_pending(11 + idx)
                if ccr & (1 << 5):  # CIRC reload
                    ch["cndtr"] = int(ch.get("cndtr_init", 0)) & 0xFFFF
                    init_m = int(ch.get("cmar_init", 0)) & 0xFFFFFFFF
                    if init_m:
                        ch["cmar"] = init_m
            return True
        except Exception:
            continue
    return False


def _write_u16(bus, addr, value):
    try:
        bus.WriteWord(int(addr) & 0xFFFFFFFF, int(value) & 0xFFFF)
    except Exception:
        v = int(value) & 0xFFFF
        try:
            base = int(addr) & 0xFFFFFFFC
            shift = (int(addr) & 3) * 8
            word = int(bus.ReadDoubleWord(base)) & 0xFFFFFFFF
            word = (word & ~(0xFFFF << shift)) | ((v & 0xFFFF) << shift)
            bus.WriteDoubleWord(base, word & 0xFFFFFFFF)
        except Exception:
            pass


def _cmd_buf_put_byte(bus, len_addr, buf_addr, max_len, b):
    try:
        length = _read_u8(bus, len_addr) & 0xFF
        if length + 1 >= max_len:
            return
        _write_u8(bus, buf_addr + length, int(b) & 0xFF)
        length = (length + 1) & 0xFF
        _write_u8(bus, len_addr, length)
        _write_u8(bus, buf_addr + length, 0)
    except Exception:
        return


def _app_cmd_buf_put_byte(bus, b):
    _cmd_buf_put_byte(bus, APP_TTM_LEN_ADDR, APP_TTM_BUF_ADDR, APP_TTM_MAX, b)


def _boot_cmd_buf_put_byte(bus, b):
    _cmd_buf_put_byte(bus, BOOT_TTM_LEN_ADDR, BOOT_TTM_BUF_ADDR, BOOT_TTM_MAX, b)


def _ble_soft_enqueue_byte(b):
    if not SOFT_ENQUEUE:
        return
    bus = _get_sysbus()
    if bus is None:
        return
    try:
        rd_idx = _read_u16(bus, BLE_CTX_BASE + BLE_RX_RD_IDX_OFF)
        wr_idx = _read_u16(bus, BLE_CTX_BASE + BLE_RX_WR_IDX_OFF)
        next_idx = (wr_idx + 1) % BLE_RX_RING_SIZE
        if next_idx == rd_idx:
            return
        _write_u8(bus, BLE_CTX_BASE + int(wr_idx), int(b) & 0xFF)
        _write_u16(bus, BLE_CTX_BASE + BLE_RX_WR_IDX_OFF, next_idx)
        try:
            state["ble_soft_count"] = int(state.get("ble_soft_count", 0)) + 1
            if state.get("ble_soft_count", 0) <= 10:
                if not os.path.isdir(OUTDIR):
                    os.makedirs(OUTDIR)
                with open(os.path.join(OUTDIR, "ble_soft_debug.txt"), "a") as f:
                    f.write(
                        "ble_soft_enqueue byte=0x%02X wr=0x%04X next=0x%04X rd=0x%04X\n"
                        % (int(b) & 0xFF, int(wr_idx) & 0xFFFF, int(next_idx) & 0xFFFF, int(rd_idx) & 0xFFFF)
                    )
        except Exception:
            pass
    except Exception as e:
        try:
            state["boot_soft_err"] = int(state.get("boot_soft_err", 0)) + 1
            if state.get("boot_soft_err", 0) <= 5:
                if not os.path.isdir(OUTDIR):
                    os.makedirs(OUTDIR)
                with open(os.path.join(OUTDIR, "boot_soft_debug.txt"), "a") as f:
                    f.write("boot_soft_error %s\n" % str(e))
        except Exception:
            pass
        return


def _boot_soft_enqueue_enabled():
    if BOOT_SOFT_ENQUEUE_ENV:
        return True
    try:
        val = System.AppDomain.CurrentDomain.GetData(BOOT_SOFT_ENQUEUE_KEY)
        return bool(val)
    except Exception:
        return False


def _boot_tx_bridge_enabled():
    if BOOT_TX_BRIDGE:
        return True
    try:
        val = System.AppDomain.CurrentDomain.GetData(BOOT_TX_BRIDGE_KEY)
        return bool(val)
    except Exception:
        return False


def _boot_ota_emul_enabled():
    if BOOT_OTA_EMUL:
        return True
    try:
        val = System.AppDomain.CurrentDomain.GetData(BOOT_OTA_EMUL_KEY)
        return bool(val)
    except Exception:
        return False


def _boot_ota_checksum(data):
    acc = 0
    for b in data:
        acc ^= int(b) & 0xFF
    return (~acc) & 0xFF


def _boot_ota_emit_response(cmd):
    payload = [1]
    # Bootloader protocol: CMD 0x24 (write) responds with status 0 on success.
    if (int(cmd) & 0xFF) == 0x25:
        payload = [0]
    frame = [0x55, int(cmd) & 0xFF, len(payload) & 0xFF] + payload
    frame.append(_boot_ota_checksum(frame))
    for b in frame:
        _emit_tx_byte(b)


def _boot_ota_emul_byte(b):
    if not _boot_ota_emul_enabled():
        return
    try:
        buf = state.setdefault("boot_ota_buf", bytearray())
        buf.append(int(b) & 0xFF)
        # Simple framing: 0x55, cmd, len, payload..., checksum.
        while True:
            if len(buf) < 4:
                return
            if buf[0] != 0x55:
                del buf[0]
                continue
            plen = buf[2]
            total = 4 + plen
            if len(buf) < total:
                return
            frame = buf[:total]
            chk = _boot_ota_checksum(frame[:-1])
            if chk != frame[-1]:
                del buf[0]
                continue
            cmd = frame[1]
            if cmd == 0x22:
                _boot_ota_emit_response(0x23)
            elif cmd == 0x24:
                _boot_ota_emit_response(0x25)
            elif cmd == 0x26:
                _boot_ota_emit_response(0x27)
            del buf[:total]
            if state.get("boot_ota_log_left", 0) > 0:
                _log("BOOT_OTA_EMUL cmd=0x%02X len=%d" % (cmd, plen))
                state["boot_ota_log_left"] = state.get("boot_ota_log_left", 1) - 1
    except Exception:
        return


def _boot_soft_enqueue_byte(b):
    if not _boot_soft_enqueue_enabled():
        return
    bus = _get_sysbus()
    if bus is None:
        return
    try:
        rd_idx = _read_u16(bus, BOOT_CTX_BASE + BOOT_RX_RD_IDX_OFF)
        wr_idx = _read_u16(bus, BOOT_CTX_BASE + BOOT_RX_WR_IDX_OFF)
        next_idx = (wr_idx + 1) % BOOT_RX_RING_SIZE
        if next_idx == rd_idx:
            return
        _write_u8(bus, BOOT_CTX_BASE + int(wr_idx), int(b) & 0xFF)
        _write_u16(bus, BOOT_CTX_BASE + BOOT_RX_WR_IDX_OFF, next_idx)
        try:
            state["boot_soft_count"] = int(state.get("boot_soft_count", 0)) + 1
            if state.get("boot_soft_count", 0) <= 10:
                if not os.path.isdir(OUTDIR):
                    os.makedirs(OUTDIR)
                with open(os.path.join(OUTDIR, "boot_soft_debug.txt"), "a") as f:
                    f.write("boot_soft_enqueue byte=0x%02X wr=0x%04X next=0x%04X\n" % (int(b) & 0xFF, int(wr_idx) & 0xFFFF, int(next_idx) & 0xFFFF))
        except Exception:
            pass
    except Exception:
        return


def _boot_tx_drain_once():
    if not _boot_tx_bridge_enabled():
        return
    bus = _get_sysbus()
    if bus is None:
        return
    try:
        rd = _read_u16(bus, BOOT_CTX_BASE + BOOT_TX_RD_IDX_OFF)
        wr = _read_u16(bus, BOOT_CTX_BASE + BOOT_TX_WR_IDX_OFF)
        if rd == wr:
            return
        slot_idx = rd % 3
        slot_base = BOOT_TX_SLOT_BASE + slot_idx * BOOT_TX_SLOT_SIZE
        slot_len = _read_u16(bus, slot_base + BOOT_TX_SLOT_LEN_OFF) & 0xFFFF
        if slot_len == 0:
            # Writer may not have finalized the slot yet.
            return
        if slot_len > 0x96:
            _write_u16(bus, BOOT_CTX_BASE + BOOT_TX_RD_IDX_OFF, (rd + 1) % 3)
            return
        data = _read_bytes(bus, slot_base + 2, slot_len)
        for b in data:
            _emit_tx_byte(b)
        _write_u16(bus, slot_base + BOOT_TX_SLOT_LEN_OFF, 0)
        _write_u16(bus, BOOT_CTX_BASE + BOOT_TX_RD_IDX_OFF, (rd + 1) % 3)
        if state.get("boot_tx_log_left", 0) > 0:
            _log("BOOT_TX drain len=%d rd=%d wr=%d" % (slot_len, rd, wr))
            state["boot_tx_log_left"] = state.get("boot_tx_log_left", 1) - 1
    except Exception:
        return


def _boot_tx_worker():
    while True:
        try:
            _boot_tx_drain_once()
        except Exception:
            pass
        time.sleep(BOOT_TX_POLL_S)


def _ensure_boot_tx_thread():
    if state.get("boot_tx_thread") is not None:
        return
    if not BOOT_TX_BRIDGE:
        return
    state["boot_tx_thread"] = _start_thread(_boot_tx_worker)


def _irq_poll_worker():
    while True:
        try:
            _update_irq_lines()
        except Exception:
            pass
        time.sleep(UART_IRQ_POLL_S)


def _ensure_irq_thread():
    if state.get("irq_thread") is not None:
        return
    state["irq_thread"] = _start_thread(_irq_poll_worker)


def _update_irq_lines():
    # If RXNEIE/TXEIE are enabled while their flag is already set, hardware generates an interrupt.
    # Implement minimal behavior needed by BOOT_UART_IRQ_handler (IRQ37).
    try:
        sr = int(state.get("sr", 0)) & 0xFFFFFFFF
        cr1 = int(state.get("cr1", 0)) & 0xFFFFFFFF
        # CR1 bits: UE(13), RE(2), TE(3), RXNEIE(5), TCIE(6), TXEIE(7)
        ue = bool(cr1 & (1 << 13))
        rxneie = bool(cr1 & (1 << 5))
        tcie = bool(cr1 & (1 << 6))
        txeie = bool(cr1 & (1 << 7))
        rxne = bool(sr & (1 << 5))
        idle = bool(sr & (1 << 4))
        idleie = bool(cr1 & (1 << 4))
        tc = bool(sr & (1 << 6))
        txe = bool(sr & (1 << 7))
        if ue and rxneie and rxne:
            _set_nvic_pending(37)
        if ue and idleie and idle:
            _set_nvic_pending(37)
        if ue and tcie and tc:
            _set_nvic_pending(37)
        if ue and txeie and txe:
            _set_nvic_pending(37)
    except Exception:
        pass


def _recompute_sr():
    # RXNE depends on rx_fifo contents.
    sr = int(state.get("sr", 0)) & 0xFFFFFFFF
    if state.get("rx_fifo"):
        sr |= (1 << 5)  # RXNE
    else:
        sr &= ~(1 << 5)
    # IDLE line detection: set once after RX activity goes quiet.
    try:
        if state.get("idle_flag"):
            sr |= (1 << 4)
        elif state.get("idle_pending") and not state.get("rx_fifo"):
            last = float(state.get("last_rx_ts") or 0.0)
            if last > 0.0 and (time.time() - last) >= UART_IDLE_S:
                state["idle_flag"] = True
                state["idle_pending"] = False
                sr |= (1 << 4)
        else:
            sr &= ~(1 << 4)
    except Exception:
        sr &= ~(1 << 4)
    # If we're not explicitly simulating a busy transmitter, keep TXE/TC asserted.
    if not state.get("tx_busy"):
        sr |= (1 << 7)  # TXE
        sr |= (1 << 6)  # TC
    state["sr"] = sr & 0xFFFFFFFF
    return sr


off = int(_get_req(OFFSET_ATTRS, 0))
is_write = bool(_get_req(IS_WRITE_ATTRS, False))
val = int(_get_req(REQUEST_ATTRS, 0)) if is_write else 0

_ensure_pty_bridge()
_ensure_tcp_bridge()
_ensure_rx_file_thread()
_consume_rx_file()
_ensure_boot_tx_thread()
_ensure_irq_thread()

if is_write:
    if not state.get("seen"):
        _log("USART1 first access (write)")
        state["seen"] = True
    if state.get("log_budget", 0) > 0:
        _log("W off=0x%X val=0x%X" % (off, val))

    if off == 0x04:
        # DR write (TX). Support 9-bit writes; store low 8 for wire.
        dr = int(val) & 0x1FF
        state["dr"] = dr
        state["tx_fifo"].append(dr & 0xFF)
        _emit_tx_byte(dr & 0xFF)
        # Writing DR clears TC/TXE briefly on real hardware; we emulate a "busy then complete"
        # cycle without timing by toggling the bits and re-asserting them immediately.
        sr = int(state.get("sr", 0)) & 0xFFFFFFFF
        sr &= ~(1 << 7)  # TXE = 0
        sr &= ~(1 << 6)  # TC = 0
        state["sr"] = sr
        state["tx_busy"] = True
        # Immediately complete.
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
        # Unhandled register offsets are ignored.
        pass
else:
    if not state.get("seen"):
        _log("USART1 first access (read)")
        state["seen"] = True

    if off == 0x00:
        state["sr_read"] = True
        sr_val = int(_recompute_sr()) & 0xFFFFFFFF
        if (sr_val & (1 << 5)) != 0:
            _log_rx("SR read RXNE=1 len=%d" % len(state.get("rx_fifo") or []))
        _set_req(REQUEST_ATTRS, sr_val)
    elif off == 0x04:
        # DR read (RX). Pop from RX FIFO, clear RXNE if empty.
        b = 0
        try:
            if state.get("rx_fifo"):
                b = int(state["rx_fifo"].pop(0)) & 0xFF
        except Exception:
            b = 0
        state["dr"] = b
        if b != 0 or state.get("rx_fifo"):
            _log_rx("DR read byte=0x%02X remaining=%d" % (b, len(state.get("rx_fifo") or [])))
        # Clear ORE if firmware did the standard SR->DR read sequence.
        try:
            if state.get("sr_read"):
                state["sr"] = int(state.get("sr", 0)) & ~(1 << 3)
                state["sr"] = int(state.get("sr", 0)) & ~(1 << 4)
                state["idle_flag"] = False
        except Exception:
            pass
        state["sr_read"] = False
        _recompute_sr()
        _set_req(REQUEST_ATTRS, int(b) & 0xFFFFFFFF)
        # If more bytes remain and RXNEIE is enabled, ensure IRQ stays pending.
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

# Start RX file bridge early so soft enqueue works even before UART init.
try:
    _ensure_rx_file_thread()
except Exception:
    pass
try:
    _ensure_boot_tx_thread()
except Exception:
    pass

# Record boot soft enqueue env at import for debugging.
try:
    if not os.path.isdir(OUTDIR):
        os.makedirs(OUTDIR)
    with open(os.path.join(OUTDIR, "boot_soft_env.txt"), "a") as f:
        f.write("BOOT_SOFT_ENQUEUE_ENV=%s\n" % ("1" if BOOT_SOFT_ENQUEUE_ENV else "0"))
        f.write("BOOT_TX_BRIDGE=%s\n" % ("1" if BOOT_TX_BRIDGE else "0"))
except Exception:
    pass
