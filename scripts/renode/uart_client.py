#!/usr/bin/env python3
"""
Minimal UART debug-protocol client for BC280 open firmware.

Designed for Renode regression tests: speaks the 0x55-framed protocol over a
PTY (e.g. /tmp/uart1). UART1 maps to the BLE module passthrough on real hardware.
Exercises ping, state set/dump, streaming, and reboot-to-bootloader.

No external deps: uses the stdlib (os.open + termios) instead of pyserial.
"""

import argparse
import dataclasses
import os
import select
import socket
import struct
import termios
import tty
import time
from typing import Iterable, List, Optional

SOF = 0x55
MAX_PAYLOAD = 255

GRAPH_CHANNELS = {
    "speed": 0,
    "power": 1,
    "volt": 2,
    "cadence": 3,
    "temp": 4,
}

GRAPH_WINDOWS = {
    "30s": 0,
    "2m": 1,
    "10m": 2,
}

CRASH_DUMP_MAGIC = 0x43525348  # 'CRSH'
CRASH_DUMP_SIZE = 152
CRASH_DUMP_EVENT_RECORD_SIZE = 20

BLE_MITM_MAGIC = 0x424C434D  # 'BLCM'
BLE_MITM_VERSION = 1
BLE_MITM_HEADER_SIZE = 16
BLE_MITM_MAX_DATA = 16

BLE_MITM_MODE_PERIPHERAL = 0
BLE_MITM_MODE_CENTRAL = 1

BLE_MITM_STATE_OFF = 0
BLE_MITM_STATE_ADVERTISING = 1
BLE_MITM_STATE_SCANNING = 2
BLE_MITM_STATE_CONNECTED = 3

BLE_MITM_EVENT_NONE = 0
BLE_MITM_EVENT_ADV = 1
BLE_MITM_EVENT_CONNECT = 2
BLE_MITM_EVENT_RX = 3
BLE_MITM_EVENT_TX = 4
BLE_MITM_EVENT_DISCONNECT = 5

BLE_MITM_DIR_TX = 0
BLE_MITM_DIR_RX = 1

BLE_MITM_STATUS_OK = 0x00
BLE_MITM_STATUS_DISABLED = 0xF0
BLE_MITM_STATUS_BAD_LEN = 0xF1
BLE_MITM_STATUS_BAD_STATE = 0xF2
BLE_MITM_STATUS_BAD_PAYLOAD = 0xF3


class ProtocolError(RuntimeError):
    pass


def checksum(buf: bytes) -> int:
    x = 0
    for b in buf:
        x ^= b
    return (~x) & 0xFF


def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            mask = -(crc & 1)
            crc = (crc >> 1) ^ (0xEDB88320 & mask)
    return (~crc) & 0xFFFFFFFF


class RawSerial:
    """Tiny raw PTY wrapper with timeout reads.

    Only supports the subset we need (write/read with timeout)."""

    def __init__(self, path: str, baud: int = 115200, timeout: float = 0.2):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        # Configure raw mode.
        attrs = termios.tcgetattr(self.fd)
        tty.setraw(self.fd)
        # Set baud (best-effort; PTY ignores but keep for completeness).
        try:
            baud_const = {
                9600: termios.B9600,
                19200: termios.B19200,
                38400: termios.B38400,
                57600: termios.B57600,
                115200: termios.B115200,
            }.get(baud, termios.B115200)
            termios.cfsetispeed(attrs, baud_const)
            termios.cfsetospeed(attrs, baud_const)
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except Exception:
            pass
        self.timeout = timeout

    def close(self) -> None:
        try:
            os.close(self.fd)
        except Exception:
            pass

    def write(self, data: bytes) -> None:
        os.write(self.fd, data)

    def read_exact(self, n: int) -> bytes:
        deadline = time.time() + self.timeout
        chunks: List[bytes] = []
        remaining = n
        while remaining > 0 and time.time() < deadline:
            r, _, _ = select.select([self.fd], [], [], max(0, deadline - time.time()))
            if not r:
                continue
            chunk = os.read(self.fd, remaining)
            if not chunk:
                continue
            chunks.append(chunk)
            remaining -= len(chunk)
        if remaining != 0:
            raise ProtocolError(f"timeout waiting for {n} bytes (got {n-remaining})")
        return b"".join(chunks)

    def read_any(self, n: int = 1) -> bytes:
        r, _, _ = select.select([self.fd], [], [], self.timeout)
        if not r:
            return b""
        return os.read(self.fd, n)


class TcpSerial:
    """Tiny TCP socket wrapper with timeout reads."""

    def __init__(self, host: str, port: int, timeout: float = 0.2):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect((host, port))
        self.sock.setblocking(False)
        self.timeout = timeout

    def close(self) -> None:
        try:
            self.sock.close()
        except Exception:
            pass

    def write(self, data: bytes) -> None:
        self.sock.sendall(data)

    def read_exact(self, n: int) -> bytes:
        deadline = time.time() + self.timeout
        chunks: List[bytes] = []
        remaining = n
        while remaining > 0 and time.time() < deadline:
            r, _, _ = select.select([self.sock], [], [], max(0, deadline - time.time()))
            if not r:
                continue
            chunk = self.sock.recv(remaining)
            if not chunk:
                continue
            chunks.append(chunk)
            remaining -= len(chunk)
        if remaining != 0:
            raise ProtocolError(f"timeout waiting for {n} bytes (got {n-remaining})")
        return b"".join(chunks)

    def read_any(self, n: int = 1) -> bytes:
        r, _, _ = select.select([self.sock], [], [], self.timeout)
        if not r:
            return b""
        return self.sock.recv(n)


@dataclasses.dataclass
class State:
    ms: int
    rpm: int
    torque: int
    speed_dmph: int
    soc: int
    err: int
    last_ms: int

    @classmethod
    def from_payload(cls, payload: bytes) -> "State":
        if len(payload) < 14:
            raise ProtocolError("state payload too short")
        ms, rpm, tq, spd, soc, err, last = struct.unpack(
            ">IHHHBBH", payload[:14]
        )
        return cls(ms=ms, rpm=rpm, torque=tq, speed_dmph=spd, soc=soc, err=err, last_ms=last)


@dataclasses.dataclass
class TelemetryV1:
    version: int
    size: int
    ms: int
    speed_dmph: int
    cadence_rpm: int
    power_w: int
    batt_dV: int
    batt_dA: int
    ctrl_temp_dC: int
    assist_mode: int
    profile_id: int
    virtual_gear: int
    flags: int

    @classmethod
    def from_payload(cls, payload: bytes) -> "TelemetryV1":
        if len(payload) < 22:
            raise ProtocolError("telemetry payload too short")
        ver = payload[0]
        size = payload[1]
        if ver != 1 or size != len(payload):
            raise ProtocolError(f"telemetry version/size mismatch ver={ver} size={size}")
        ms = struct.unpack(">I", payload[2:6])[0]
        speed = struct.unpack(">H", payload[6:8])[0]
        cadence = struct.unpack(">H", payload[8:10])[0]
        power_w = struct.unpack(">H", payload[10:12])[0]
        batt_dV = struct.unpack(">h", payload[12:14])[0]
        batt_dA = struct.unpack(">h", payload[14:16])[0]
        ctrl_temp = struct.unpack(">h", payload[16:18])[0]
        assist_mode = payload[18]
        profile_id = payload[19]
        virtual_gear = payload[20]
        flags = payload[21]
        return cls(
            version=ver,
            size=size,
            ms=ms,
            speed_dmph=speed,
            cadence_rpm=cadence,
            power_w=power_w,
            batt_dV=batt_dV,
            batt_dA=batt_dA,
            ctrl_temp_dC=ctrl_temp,
            assist_mode=assist_mode,
            profile_id=profile_id,
            virtual_gear=virtual_gear,
            flags=flags,
        )


@dataclasses.dataclass
class CSCMeasurement:
    flags: int
    wheel_revs: int
    wheel_event_time: int
    crank_revs: int
    crank_event_time: int

    @classmethod
    def from_payload(cls, payload: bytes) -> "CSCMeasurement":
        if len(payload) < 2:
            raise ProtocolError("csc payload too short")
        flags = struct.unpack("<H", payload[0:2])[0]
        idx = 2
        wheel_revs = 0
        wheel_evt = 0
        crank_revs = 0
        crank_evt = 0
        if flags & 0x01:
            if len(payload) < idx + 6:
                raise ProtocolError("csc wheel payload too short")
            wheel_revs = struct.unpack("<I", payload[idx:idx+4])[0]
            idx += 4
            wheel_evt = struct.unpack("<H", payload[idx:idx+2])[0]
            idx += 2
        if flags & 0x02:
            if len(payload) < idx + 4:
                raise ProtocolError("csc crank payload too short")
            crank_revs = struct.unpack("<H", payload[idx:idx+2])[0]
            idx += 2
            crank_evt = struct.unpack("<H", payload[idx:idx+2])[0]
            idx += 2
        if idx != len(payload):
            raise ProtocolError("csc payload length mismatch")
        return cls(flags, wheel_revs, wheel_evt, crank_revs, crank_evt)


@dataclasses.dataclass
class CPSMeasurement:
    flags: int
    instant_power: int
    wheel_revs: int
    wheel_event_time: int
    crank_revs: int
    crank_event_time: int

    @classmethod
    def from_payload(cls, payload: bytes) -> "CPSMeasurement":
        if len(payload) < 4:
            raise ProtocolError("cps payload too short")
        flags = struct.unpack("<H", payload[0:2])[0]
        power = struct.unpack("<h", payload[2:4])[0]
        idx = 4
        wheel_revs = 0
        wheel_evt = 0
        crank_revs = 0
        crank_evt = 0
        if flags & 0x04:
            if len(payload) < idx + 6:
                raise ProtocolError("cps wheel payload too short")
            wheel_revs = struct.unpack("<I", payload[idx:idx+4])[0]
            idx += 4
            wheel_evt = struct.unpack("<H", payload[idx:idx+2])[0]
            idx += 2
        if flags & 0x08:
            if len(payload) < idx + 4:
                raise ProtocolError("cps crank payload too short")
            crank_revs = struct.unpack("<H", payload[idx:idx+2])[0]
            idx += 2
            crank_evt = struct.unpack("<H", payload[idx:idx+2])[0]
            idx += 2
        if idx != len(payload):
            raise ProtocolError("cps payload length mismatch")
        return cls(flags, power, wheel_revs, wheel_evt, crank_revs, crank_evt)


@dataclasses.dataclass
class RingSummary:
    count: int
    capacity: int
    min: int
    max: int
    latest: int


@dataclasses.dataclass
class GraphSummary:
    count: int
    capacity: int
    min: int
    max: int
    latest: int
    period_ms: int
    window_ms: int


@dataclasses.dataclass
class DebugState:
    version: int
    size: int
    ms: int
    inputs_ms: int
    speed_dmph: int
    cadence_rpm: int
    torque_raw: int
    throttle_pct: int
    brake: int
    buttons: int
    assist_mode: int
    profile_id: int
    virtual_gear: int
    cmd_power_w: int
    cmd_current_dA: int
    cruise_state: int
    adaptive_speed_delta_dmph: int = 0
    adaptive_clamp_active: int = 0
    adaptive_trend_active: int = 0
    cap_power_w: int = 0
    cap_current_dA: int = 0
    cap_speed_dmph: int = 0
    curve_power_w: int = 0
    curve_cadence_q15: int = 0
    cap_profile_speed_dmph: int = 0
    gear_limit_power_w: int = 0
    gear_scale_q15: int = 0
    cadence_bias_q15: int = 0
    mode: int = 0
    cap_effective_current_dA: int = 0
    cap_effective_speed_dmph: int = 0
    p_allow_user_w: int = 0
    p_allow_lug_w: int = 0
    p_allow_thermal_w: int = 0
    p_allow_sag_w: int = 0
    p_allow_final_w: int = 0
    limit_reason: int = 0
    duty_q16: int = 0
    i_phase_est_dA: int = 0
    thermal_state: int = 0
    sag_margin_dV: int = 0
    soft_start_active: int = 0
    soft_start_output_w: int = 0
    soft_start_target_w: int = 0
    walk_state: int = 0
    walk_cmd_power_w: int = 0
    walk_cmd_current_dA: int = 0
    reset_flags: int = 0
    reset_csr: int = 0
    range_wh_per_mile_d10: int = 0
    range_est_d10: int = 0
    range_confidence: int = 0
    range_samples: int = 0
    drive_mode: int = 0
    drive_setpoint: int = 0
    drive_cmd_power_w: int = 0
    drive_cmd_current_dA: int = 0
    boost_budget_ms: int = 0
    boost_active: int = 0
    boost_threshold_dA: int = 0
    boost_gain_q15: int = 0
    hw_caps: int = 0
    regen_supported: int = 0
    regen_level: int = 0
    regen_brake_level: int = 0
    regen_cmd_power_w: int = 0
    regen_cmd_current_dA: int = 0
    lock_enabled: int = 0
    lock_active: int = 0
    lock_mask: int = 0
    quick_action_last: int = 0
    fault_mask: int = 0
    fault_active: int = 0
    comm_retry_count: int = 0
    comm_flaky: int = 0
    derate_cap_w: int = 0

    @classmethod
    def from_payload(cls, payload: bytes) -> "DebugState":
        if len(payload) < 28:
            raise ProtocolError("debug state payload too short")
        ver = payload[0]
        size = payload[1]
        ms = struct.unpack(">I", payload[2:6])[0]
        inputs_ms = struct.unpack(">I", payload[6:10])[0]
        speed_dmph = struct.unpack(">H", payload[10:12])[0]
        cadence_rpm = struct.unpack(">H", payload[12:14])[0]
        torque_raw = struct.unpack(">H", payload[14:16])[0]
        throttle_pct = payload[16]
        brake = payload[17]
        buttons = payload[18]
        assist_mode = payload[19]
        profile_id = payload[20]
        virtual_gear = payload[21]
        cmd_power_w = struct.unpack(">H", payload[22:24])[0]
        cmd_current_dA = struct.unpack(">H", payload[24:26])[0]
        cruise_state = payload[26]
        adaptive_speed_delta_dmph = 0
        adaptive_clamp_active = 0
        adaptive_trend_active = 0
        cap_power_w = 0
        cap_current_dA = 0
        cap_speed_dmph = 0
        curve_power_w = 0
        curve_cadence_q15 = 0
        cap_profile_speed_dmph = 0
        gear_limit_power_w = 0
        gear_scale_q15 = 0
        cadence_bias_q15 = 0
        mode = 0
        cap_effective_current_dA = 0
        cap_effective_speed_dmph = 0
        if len(payload) >= 28 and ver >= 19:
            adaptive_clamp_active = payload[27]
        if len(payload) >= 34:
            cap_power_w = struct.unpack(">H", payload[28:30])[0]
            cap_current_dA = struct.unpack(">H", payload[30:32])[0]
            cap_speed_dmph = struct.unpack(">H", payload[32:34])[0]
        if len(payload) >= 38:
            curve_power_w = struct.unpack(">H", payload[34:36])[0]
            curve_cadence_q15 = struct.unpack(">H", payload[36:38])[0]
        if len(payload) >= 40:
            cap_profile_speed_dmph = struct.unpack(">H", payload[38:40])[0]
        if len(payload) >= 46:
            gear_limit_power_w = struct.unpack(">H", payload[40:42])[0]
            gear_scale_q15 = struct.unpack(">H", payload[42:44])[0]
            cadence_bias_q15 = struct.unpack(">H", payload[44:46])[0]
        walk_state = 0
        walk_cmd_power_w = 0
        walk_cmd_current_dA = 0
        p_allow_user_w = 0
        p_allow_lug_w = 0
        p_allow_thermal_w = 0
        p_allow_sag_w = 0
        p_allow_final_w = 0
        limit_reason = 0
        duty_q16 = 0
        i_phase_est_dA = 0
        thermal_state = 0
        sag_margin_dV = 0
        soft_start_active = 0
        soft_start_output_w = 0
        soft_start_target_w = 0
        reset_flags = 0
        reset_csr = 0
        range_wh_per_mile_d10 = 0
        range_est_d10 = 0
        range_confidence = 0
        range_samples = 0
        drive_mode = 0
        drive_setpoint = 0
        drive_cmd_power_w = 0
        drive_cmd_current_dA = 0
        boost_budget_ms = 0
        boost_active = 0
        boost_threshold_dA = 0
        boost_gain_q15 = 0
        hw_caps = 0
        regen_supported = 0
        regen_level = 0
        regen_brake_level = 0
        regen_cmd_power_w = 0
        regen_cmd_current_dA = 0
        lock_enabled = 0
        lock_active = 0
        lock_mask = 0
        quick_action_last = 0
        fault_mask = 0
        fault_active = 0
        comm_retry_count = 0
        comm_flaky = 0
        derate_cap_w = 0
        if len(payload) >= 52:
            walk_state = payload[46]
            walk_cmd_power_w = struct.unpack(">H", payload[47:49])[0]
            walk_cmd_current_dA = struct.unpack(">H", payload[49:51])[0]
        if len(payload) >= 52:
            mode = payload[51]
        if len(payload) >= 54:
            cap_effective_current_dA = struct.unpack(">H", payload[52:54])[0]
        if len(payload) >= 56:
            cap_effective_speed_dmph = struct.unpack(">H", payload[54:56])[0]
        if len(payload) >= 58 and ver >= 19:
            adaptive_speed_delta_dmph = struct.unpack(">h", payload[56:58])[0]
        if len(payload) >= 78:
            p_allow_user_w = struct.unpack(">H", payload[58:60])[0]
            p_allow_lug_w = struct.unpack(">H", payload[60:62])[0]
            p_allow_thermal_w = struct.unpack(">H", payload[62:64])[0]
            p_allow_sag_w = struct.unpack(">H", payload[64:66])[0]
            p_allow_final_w = struct.unpack(">H", payload[66:68])[0]
            limit_reason = payload[68]
            if ver >= 19:
                adaptive_trend_active = payload[69]
            duty_q16 = struct.unpack(">H", payload[70:72])[0]
            i_phase_est_dA = struct.unpack(">h", payload[72:74])[0]
            thermal_state = struct.unpack(">H", payload[74:76])[0]
            sag_margin_dV = struct.unpack(">h", payload[76:78])[0]
        if len(payload) >= 84 and ver >= 9:
            soft_start_active = payload[78]
            soft_start_output_w = struct.unpack(">H", payload[80:82])[0]
            soft_start_target_w = struct.unpack(">H", payload[82:84])[0]
        if len(payload) >= 90 and ver >= 10:
            reset_flags = struct.unpack(">H", payload[84:86])[0]
            reset_csr = struct.unpack(">I", payload[86:90])[0]
        if len(payload) >= 96 and ver >= 11:
            range_wh_per_mile_d10 = struct.unpack(">H", payload[90:92])[0]
            range_est_d10 = struct.unpack(">H", payload[92:94])[0]
            range_confidence = payload[94]
            range_samples = payload[95]
        if len(payload) >= 110 and ver >= 13:
            drive_mode = payload[96]
            drive_setpoint = struct.unpack(">H", payload[97:99])[0]
            drive_cmd_power_w = struct.unpack(">H", payload[99:101])[0]
            drive_cmd_current_dA = struct.unpack(">H", payload[101:103])[0]
            boost_budget_ms = struct.unpack(">H", payload[103:105])[0]
            boost_active = payload[105]
            boost_threshold_dA = struct.unpack(">H", payload[106:108])[0]
            boost_gain_q15 = struct.unpack(">H", payload[108:110])[0]
        if len(payload) >= 118 and ver >= 15:
            hw_caps = payload[110]
            regen_supported = payload[111]
            regen_level = payload[112]
            regen_brake_level = payload[113]
            regen_cmd_power_w = struct.unpack(">H", payload[114:116])[0]
            regen_cmd_current_dA = struct.unpack(">H", payload[116:118])[0]
        if len(payload) >= 122 and ver >= 17:
            lock_enabled = payload[118]
            lock_active = payload[119]
            lock_mask = payload[120]
            quick_action_last = payload[121]
        if len(payload) >= 128 and ver >= 18:
            fault_mask = payload[122]
            fault_active = payload[123]
            comm_retry_count = payload[124]
            comm_flaky = payload[125]
            derate_cap_w = struct.unpack(">H", payload[126:128])[0]
        elif len(payload) >= 124 and ver >= 16:
            fault_mask = payload[118]
            fault_active = payload[119]
            comm_retry_count = payload[120]
            comm_flaky = payload[121]
            derate_cap_w = struct.unpack(">H", payload[122:124])[0]
        elif len(payload) >= 116 and ver >= 14:
            fault_mask = payload[110]
            fault_active = payload[111]
            comm_retry_count = payload[112]
            comm_flaky = payload[113]
            derate_cap_w = struct.unpack(">H", payload[114:116])[0]
        return cls(
            version=ver,
            size=size,
            ms=ms,
            inputs_ms=inputs_ms,
            speed_dmph=speed_dmph,
            cadence_rpm=cadence_rpm,
            torque_raw=torque_raw,
            throttle_pct=throttle_pct,
            brake=brake,
            buttons=buttons,
            assist_mode=assist_mode,
            profile_id=profile_id,
            virtual_gear=virtual_gear,
            cmd_power_w=cmd_power_w,
            cmd_current_dA=cmd_current_dA,
            cruise_state=cruise_state,
            adaptive_speed_delta_dmph=adaptive_speed_delta_dmph,
            adaptive_clamp_active=adaptive_clamp_active,
            adaptive_trend_active=adaptive_trend_active,
            cap_power_w=cap_power_w,
            cap_current_dA=cap_current_dA,
            cap_speed_dmph=cap_speed_dmph,
            curve_power_w=curve_power_w,
            curve_cadence_q15=curve_cadence_q15,
            cap_profile_speed_dmph=cap_profile_speed_dmph,
            gear_limit_power_w=gear_limit_power_w,
            gear_scale_q15=gear_scale_q15,
            cadence_bias_q15=cadence_bias_q15,
            mode=mode,
            cap_effective_current_dA=cap_effective_current_dA,
            cap_effective_speed_dmph=cap_effective_speed_dmph,
            p_allow_user_w=p_allow_user_w,
            p_allow_lug_w=p_allow_lug_w,
            p_allow_thermal_w=p_allow_thermal_w,
            p_allow_sag_w=p_allow_sag_w,
            p_allow_final_w=p_allow_final_w,
            limit_reason=limit_reason,
            duty_q16=duty_q16,
            i_phase_est_dA=i_phase_est_dA,
            thermal_state=thermal_state,
            sag_margin_dV=sag_margin_dV,
            soft_start_active=soft_start_active,
            soft_start_output_w=soft_start_output_w,
            soft_start_target_w=soft_start_target_w,
            walk_state=walk_state,
            walk_cmd_power_w=walk_cmd_power_w,
            walk_cmd_current_dA=walk_cmd_current_dA,
            reset_flags=reset_flags,
            reset_csr=reset_csr,
            range_wh_per_mile_d10=range_wh_per_mile_d10,
            range_est_d10=range_est_d10,
            range_confidence=range_confidence,
            range_samples=range_samples,
            drive_mode=drive_mode,
            drive_setpoint=drive_setpoint,
            drive_cmd_power_w=drive_cmd_power_w,
            drive_cmd_current_dA=drive_cmd_current_dA,
            boost_budget_ms=boost_budget_ms,
            boost_active=boost_active,
            boost_threshold_dA=boost_threshold_dA,
            boost_gain_q15=boost_gain_q15,
            hw_caps=hw_caps,
            regen_supported=regen_supported,
            regen_level=regen_level,
            regen_brake_level=regen_brake_level,
            regen_cmd_power_w=regen_cmd_power_w,
            regen_cmd_current_dA=regen_cmd_current_dA,
            lock_enabled=lock_enabled,
            lock_active=lock_active,
            lock_mask=lock_mask,
            quick_action_last=quick_action_last,
            fault_mask=fault_mask,
            fault_active=fault_active,
            comm_retry_count=comm_retry_count,
            comm_flaky=comm_flaky,
            derate_cap_w=derate_cap_w,
        )


@dataclasses.dataclass
class ConfigBlob:
    version: int
    size: int
    reserved: int
    seq: int
    crc32: int
    wheel_mm: int
    units: int
    profile_id: int
    theme: int
    flags: int
    button_map: int
    button_flags: int
    mode: int
    pin_code: int
    cap_current_dA: int
    cap_speed_dmph: int
    log_period_ms: int
    soft_start_ramp_wps: int
    soft_start_deadband_w: int
    soft_start_kick_w: int
    drive_mode: int
    manual_current_dA: int
    manual_power_w: int
    boost_budget_ms: int
    boost_cooldown_ms: int
    boost_threshold_dA: int
    boost_gain_q15: int
    curve_count: int
    curve: list

    @classmethod
    def defaults(cls) -> "ConfigBlob":
        return cls(
            6,
            81,
            0,
            1,
            0,
            2100,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            1234,
            200,
            200,
            1000,
            0,
            0,
            0,
            0,
            180,
            400,
            6000,
            12000,
            180,
            1024,
            0,
            [(0, 0)] * 8,
        )

    @classmethod
    def from_payload(cls, payload: bytes) -> "ConfigBlob":
        if len(payload) < 81:
            raise ProtocolError("config payload too short")
        version = payload[0]
        size = payload[1]
        reserved = struct.unpack(">H", payload[2:4])[0]
        seq = struct.unpack(">I", payload[4:8])[0]
        crc = struct.unpack(">I", payload[8:12])[0]
        wheel_mm = struct.unpack(">H", payload[12:14])[0]
        units = payload[14]
        profile_id = payload[15]
        theme = payload[16]
        flags = payload[17]
        button_map = payload[18]
        button_flags = payload[19]
        mode = payload[20]
        pin_code = struct.unpack(">H", payload[21:23])[0]
        cap_current_dA = struct.unpack(">H", payload[23:25])[0]
        cap_speed_dmph = struct.unpack(">H", payload[25:27])[0]
        log_period_ms = struct.unpack(">H", payload[27:29])[0]
        soft_start_ramp_wps = struct.unpack(">H", payload[29:31])[0]
        soft_start_deadband_w = struct.unpack(">H", payload[31:33])[0]
        soft_start_kick_w = struct.unpack(">H", payload[33:35])[0]
        drive_mode = payload[35]
        manual_current_dA = struct.unpack(">H", payload[36:38])[0]
        manual_power_w = struct.unpack(">H", payload[38:40])[0]
        boost_budget_ms = struct.unpack(">H", payload[40:42])[0]
        boost_cooldown_ms = struct.unpack(">H", payload[42:44])[0]
        boost_threshold_dA = struct.unpack(">H", payload[44:46])[0]
        boost_gain_q15 = struct.unpack(">H", payload[46:48])[0]
        curve_count = payload[48]
        curve: list = []
        ptr = 49
        for _ in range(8):
            x = struct.unpack(">H", payload[ptr:ptr+2])[0]
            y = struct.unpack(">H", payload[ptr+2:ptr+4])[0]
            curve.append((x, y))
            ptr += 4
        return cls(
            version,
            size,
            reserved,
            seq,
            crc,
            wheel_mm,
            units,
            profile_id,
            theme,
            flags,
            button_map,
            button_flags,
            mode,
            pin_code,
            cap_current_dA,
            cap_speed_dmph,
            log_period_ms,
            soft_start_ramp_wps,
            soft_start_deadband_w,
            soft_start_kick_w,
            drive_mode,
            manual_current_dA,
            manual_power_w,
            boost_budget_ms,
            boost_cooldown_ms,
            boost_threshold_dA,
            boost_gain_q15,
            curve_count,
            curve,
        )

    def to_payload(self, recalc_crc: bool = True) -> bytes:
        self.size = 81
        self.version = 6
        if recalc_crc:
            self.crc32 = 0
            raw = struct.pack(
                ">BBHIIHBBBBBBBHHHHHHHBHHHHHHB",
                self.version,
                self.size,
                self.reserved,
                self.seq,
                self.crc32,
                self.wheel_mm,
                self.units,
                self.profile_id,
                self.theme,
                self.flags,
                self.button_map,
                self.button_flags,
                self.mode,
                self.pin_code,
                self.cap_current_dA,
                self.cap_speed_dmph,
                self.log_period_ms,
                self.soft_start_ramp_wps,
                self.soft_start_deadband_w,
                self.soft_start_kick_w,
                self.drive_mode,
                self.manual_current_dA,
                self.manual_power_w,
                self.boost_budget_ms,
                self.boost_cooldown_ms,
                self.boost_threshold_dA,
                self.boost_gain_q15,
                self.curve_count & 0xFF,
            )
            curve_bytes = bytearray()
            for i in range(8):
                x, y = self.curve[i] if i < len(self.curve) else (0, 0)
                curve_bytes += struct.pack(">HH", x & 0xFFFF, y & 0xFFFF)
            self.crc32 = crc32(raw + curve_bytes)
        payload = struct.pack(
            ">BBHIIHBBBBBBBHHHHHHHBHHHHHHB",
            self.version,
            self.size,
            self.reserved,
            self.seq,
            self.crc32,
            self.wheel_mm,
            self.units,
            self.profile_id,
            self.theme,
            self.flags,
            self.button_map,
            self.button_flags,
            self.mode,
            self.pin_code,
            self.cap_current_dA,
            self.cap_speed_dmph,
            self.log_period_ms,
            self.soft_start_ramp_wps,
            self.soft_start_deadband_w,
            self.soft_start_kick_w,
            self.drive_mode,
            self.manual_current_dA,
            self.manual_power_w,
            self.boost_budget_ms,
            self.boost_cooldown_ms,
            self.boost_threshold_dA,
            self.boost_gain_q15,
            self.curve_count & 0xFF,
        )
        curve_bytes = bytearray()
        for i in range(8):
            x, y = self.curve[i] if i < len(self.curve) else (0, 0)
            curve_bytes += struct.pack(">HH", x & 0xFFFF, y & 0xFFFF)
        return payload + bytes(curve_bytes)


@dataclasses.dataclass
class EventLogSummary:
    count: int
    capacity: int
    head: int
    record_size: int
    seq: int


@dataclasses.dataclass
class EventRecord:
    ms: int
    type: int
    flags: int
    speed_dmph: int
    batt_dV: int
    batt_dA: int
    temp_dC: int
    cmd_power_w: int
    cmd_current_dA: int


def _parse_event_record(chunk: bytes) -> EventRecord:
    ms = struct.unpack(">I", chunk[0:4])[0]
    rtype = chunk[4]
    flags = chunk[5]
    speed = struct.unpack(">h", chunk[6:8])[0]
    batt_dV = struct.unpack(">h", chunk[8:10])[0]
    batt_dA = struct.unpack(">h", chunk[10:12])[0]
    temp = struct.unpack(">h", chunk[12:14])[0]
    pwr = struct.unpack(">H", chunk[14:16])[0]
    cur = struct.unpack(">H", chunk[16:18])[0]
    return EventRecord(ms, rtype, flags, speed, batt_dV, batt_dA, temp, pwr, cur)


@dataclasses.dataclass
class CrashDump:
    magic: int
    version: int
    size: int
    flags: int
    seq: int
    crc32: int
    ms: int
    sp: int
    lr: int
    pc: int
    psr: int
    cfsr: int
    hfsr: int
    dfsr: int
    mmfar: int
    bfar: int
    afsr: int
    event_count: int
    event_record_size: int
    event_seq: int
    event_records: List[EventRecord]
    crc_ok: bool

    @classmethod
    def from_payload(cls, payload: bytes) -> "CrashDump":
        if len(payload) < 72:
            raise ProtocolError("crash dump payload too short")
        magic = struct.unpack(">I", payload[0:4])[0]
        version = struct.unpack(">H", payload[4:6])[0]
        size = struct.unpack(">H", payload[6:8])[0]
        flags = struct.unpack(">I", payload[8:12])[0]
        seq = struct.unpack(">I", payload[12:16])[0]
        crc_val = struct.unpack(">I", payload[16:20])[0]
        ms = struct.unpack(">I", payload[20:24])[0]
        sp = struct.unpack(">I", payload[24:28])[0]
        lr = struct.unpack(">I", payload[28:32])[0]
        pc = struct.unpack(">I", payload[32:36])[0]
        psr = struct.unpack(">I", payload[36:40])[0]
        cfsr = struct.unpack(">I", payload[40:44])[0]
        hfsr = struct.unpack(">I", payload[44:48])[0]
        dfsr = struct.unpack(">I", payload[48:52])[0]
        mmfar = struct.unpack(">I", payload[52:56])[0]
        bfar = struct.unpack(">I", payload[56:60])[0]
        afsr = struct.unpack(">I", payload[60:64])[0]
        event_count = struct.unpack(">H", payload[64:66])[0]
        event_rec_size = struct.unpack(">H", payload[66:68])[0]
        event_seq = struct.unpack(">I", payload[68:72])[0]
        events: List[EventRecord] = []
        if event_count:
            start = 72
            for i in range(event_count):
                off = start + i * event_rec_size
                if off + event_rec_size > len(payload):
                    break
                events.append(_parse_event_record(payload[off:off + event_rec_size]))
        crc_ok = False
        if len(payload) == size and size >= 20:
            tmp = bytearray(payload)
            tmp[16:20] = b"\x00\x00\x00\x00"
            crc_ok = crc32(bytes(tmp)) == crc_val
        return cls(
            magic=magic,
            version=version,
            size=size,
            flags=flags,
            seq=seq,
            crc32=crc_val,
            ms=ms,
            sp=sp,
            lr=lr,
            pc=pc,
            psr=psr,
            cfsr=cfsr,
            hfsr=hfsr,
            dfsr=dfsr,
            mmfar=mmfar,
            bfar=bfar,
            afsr=afsr,
            event_count=event_count,
            event_record_size=event_rec_size,
            event_seq=event_seq,
            event_records=events,
            crc_ok=crc_ok,
        )

@dataclasses.dataclass
class StreamLogSummary:
    count: int
    capacity: int
    head: int
    record_size: int
    period_ms: int
    enabled: bool
    seq: int


@dataclasses.dataclass
class StreamLogRecord:
    version: int
    flags: int
    dt_ms: int
    speed_dmph: int
    cadence_rpm: int
    power_w: int
    batt_dV: int
    batt_dA: int
    temp_dC: int
    assist_mode: int
    profile_id: int
    crc16: int


@dataclasses.dataclass
class BusCaptureSummary:
    version: int
    size: int
    count: int
    capacity: int
    head: int
    max_len: int
    enabled: bool
    seq: int


@dataclasses.dataclass
class BusCaptureRecord:
    dt_ms: int
    bus_id: int
    data: bytes


@dataclasses.dataclass
class BleMitmStatus:
    version: int
    enabled: bool
    mode: int
    state: int
    count: int
    seq: int

    @classmethod
    def from_payload(cls, payload: bytes) -> "BleMitmStatus":
        if len(payload) < 10:
            raise ProtocolError("ble_mitm_status payload too short")
        ver = payload[0]
        enabled = bool(payload[1])
        mode = payload[2]
        state = payload[3]
        count = struct.unpack(">H", payload[4:6])[0]
        seq = struct.unpack(">I", payload[6:10])[0]
        return cls(version=ver, enabled=enabled, mode=mode, state=state, count=count, seq=seq)


@dataclasses.dataclass
class BleMitmRecord:
    dt_ms: int
    direction: int
    data: bytes


@dataclasses.dataclass
class BleMitmCapture:
    magic: int
    version: int
    header_size: int
    record_count: int
    max_len: int
    seq: int
    flags: int
    records: List[BleMitmRecord]

    @classmethod
    def from_payload(cls, payload: bytes) -> "BleMitmCapture":
        if len(payload) < BLE_MITM_HEADER_SIZE:
            raise ProtocolError("ble_mitm_capture payload too short")
        magic = struct.unpack(">I", payload[0:4])[0]
        version = payload[4]
        header_size = payload[5]
        record_count = payload[6]
        max_len = payload[7]
        seq = struct.unpack(">I", payload[8:12])[0]
        flags = struct.unpack(">I", payload[12:16])[0]
        if header_size < BLE_MITM_HEADER_SIZE or header_size > len(payload):
            raise ProtocolError("ble_mitm_capture header size invalid")
        records: List[BleMitmRecord] = []
        ptr = header_size
        for _ in range(record_count):
            if ptr + 4 > len(payload):
                raise ProtocolError("ble_mitm_capture truncated record header")
            dt_ms = struct.unpack(">H", payload[ptr:ptr+2])[0]
            direction = payload[ptr+2]
            data_len = payload[ptr+3]
            ptr += 4
            if ptr + data_len > len(payload):
                raise ProtocolError("ble_mitm_capture truncated record data")
            data = bytes(payload[ptr:ptr+data_len])
            ptr += data_len
            records.append(BleMitmRecord(dt_ms=dt_ms, direction=direction, data=data))
        return cls(
            magic=magic,
            version=version,
            header_size=header_size,
            record_count=record_count,
            max_len=max_len,
            seq=seq,
            flags=flags,
            records=records,
        )


@dataclasses.dataclass
class AbStatus:
    version: int
    size: int
    active_slot: int
    pending_slot: int
    last_good_slot: int
    flags: int
    build_id: int

    @classmethod
    def from_payload(cls, payload: bytes) -> "AbStatus":
        if len(payload) < 12:
            raise ProtocolError("ab_status payload too short")
        version = payload[0]
        size = payload[1]
        active_slot = payload[2]
        pending_slot = payload[3]
        last_good_slot = payload[4]
        flags = payload[5]
        build_id = struct.unpack(">I", payload[6:10])[0]
        return cls(
            version=version,
            size=size,
            active_slot=active_slot,
            pending_slot=pending_slot,
            last_good_slot=last_good_slot,
            flags=flags,
            build_id=build_id,
        )


class UARTClient:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.2):
        if port.startswith("tcp:"):
            target = port[len("tcp:"):]
        else:
            target = port
        if ":" in target and not os.path.exists(target):
            host, port_s = target.rsplit(":", 1)
            try:
                port_i = int(port_s)
            except Exception:
                port_i = None
            if port_i is None:
                raise FileNotFoundError(f"invalid tcp endpoint: {port}")
            self.ser = TcpSerial(host, port_i, timeout=timeout)
        else:
            self.ser = RawSerial(port, baud=baud, timeout=timeout)

    def close(self) -> None:
        self.ser.close()

    def _send(self, cmd: int, payload: bytes = b"", expect_len: Optional[int] = None) -> bytes:
        if len(payload) > MAX_PAYLOAD:
            raise ValueError("payload too long")
        frame = bytes([SOF, cmd, len(payload)]) + payload
        frame += bytes([checksum(frame)])
        self.ser.write(frame)
        hdr = self.ser.read_exact(3)
        if hdr[0] != SOF:
            raise ProtocolError(f"bad SOF 0x{hdr[0]:02x}")
        resp_cmd = hdr[1]
        resp_len = hdr[2]
        payload_bytes = self.ser.read_exact(resp_len)
        cks = self.ser.read_exact(1)
        frame_bytes = hdr + payload_bytes
        if cks[0] != checksum(frame_bytes):
            raise ProtocolError("checksum mismatch")
        if expect_len is not None and resp_len != expect_len:
            raise ProtocolError(f"unexpected payload len {resp_len} vs {expect_len}")
        return bytes([resp_cmd]) + payload_bytes

    def ping(self) -> None:
        resp = self._send(0x01, b"", expect_len=1)
        if resp[0] != 0x81 or resp[1] != 0:
            raise ProtocolError("ping failed")

    def log_frame(self) -> bytes:
        resp = self._send(0x7D, b"", expect_len=None)
        if resp[0] != 0x7D:
            raise ProtocolError("log_frame bad cmd")
        return resp[1:]

    def set_state(self, rpm: int, torque: int, speed_dmph: int, soc: int, err: int,
                  cadence_rpm: Optional[int] = None, throttle_pct: Optional[int] = None,
                  brake: Optional[int] = None, buttons: Optional[int] = None,
                  power_w: Optional[int] = None, batt_dV: Optional[int] = None,
                  batt_dA: Optional[int] = None, ctrl_temp_dC: Optional[int] = None) -> None:
        payload = struct.pack(">HHHBB", rpm & 0xFFFF, torque & 0xFFFF, speed_dmph & 0xFFFF, soc & 0xFF, err & 0xFF)
        extras = []
        if cadence_rpm is not None:
            extras.append(struct.pack(">H", cadence_rpm & 0xFFFF))
        if throttle_pct is not None:
            extras.append(struct.pack("B", throttle_pct & 0xFF))
        if brake is not None:
            extras.append(struct.pack("B", brake & 0xFF))
        if buttons is not None:
            extras.append(struct.pack("B", buttons & 0xFF))
        if power_w is not None:
            extras.append(struct.pack(">H", power_w & 0xFFFF))
        if batt_dV is not None:
            extras.append(struct.pack(">h", batt_dV))
        if batt_dA is not None:
            extras.append(struct.pack(">h", batt_dA))
        if ctrl_temp_dC is not None:
            extras.append(struct.pack(">h", ctrl_temp_dC))
        if extras:
            payload += b"".join(extras)
        resp = self._send(0x0C, payload, expect_len=1)
        if resp[0] != 0x8C or resp[1] != 0:
            raise ProtocolError("set_state failed")

    def state_dump(self) -> State:
        resp = self._send(0x0A, b"", expect_len=16)
        if resp[0] != 0x8A:
            raise ProtocolError("state_dump bad cmd")
        return State.from_payload(resp[1:])

    def set_stream(self, period_ms: int) -> None:
        payload = struct.pack(">H", period_ms & 0xFFFF)
        resp = self._send(0x0D, payload, expect_len=1)
        if resp[0] != 0x8D or resp[1] != 0:
            raise ProtocolError("set_stream failed")

    def read_mem(self, addr: int, n: int) -> bytes:
        if n <= 0 or n > 192:
            raise ValueError("read_mem length must be 1..192")
        payload = struct.pack(">I", addr & 0xFFFFFFFF) + bytes([n & 0xFF])
        resp = self._send(0x04, payload, expect_len=n)
        if resp[0] != 0x84:
            raise ProtocolError("read_mem bad cmd")
        return resp[1:]

    def write_mem(self, addr: int, data: bytes) -> None:
        if not data:
            return
        max_chunk = 180  # keep below firmware MAX_PAYLOAD (192) minus addr/len fields
        offset = 0
        while offset < len(data):
            chunk = data[offset:offset + max_chunk]
            payload = struct.pack(">I", (addr + offset) & 0xFFFFFFFF)
            payload += bytes([len(chunk) & 0xFF]) + chunk
            resp = self._send(0x05, payload, expect_len=1)
            if resp[0] != 0x85 or resp[1] != 0:
                raise ProtocolError("write_mem failed")
            offset += len(chunk)

    def set_drive_mode(self, mode: int, setpoint: int = 0) -> None:
        payload = bytes([mode & 0xFF]) + struct.pack(">H", setpoint & 0xFFFF)
        resp = self._send(0x38, payload, expect_len=1)
        if resp[0] != 0xB8 or resp[1] != 0:
            raise ProtocolError("set_drive_mode failed")

    def set_regen(self, level: int, brake_level: int, allow_error: bool = False) -> int:
        payload = bytes([level & 0xFF, brake_level & 0xFF])
        resp = self._send(0x39, payload, expect_len=1)
        if resp[0] != 0xB9:
            raise ProtocolError("set_regen bad cmd")
        status = resp[1]
        if status != 0 and not allow_error:
            raise ProtocolError(f"set_regen failed status=0x{status:02x}")
        return status

    def set_hw_caps(self, caps: int) -> None:
        payload = bytes([caps & 0xFF])
        resp = self._send(0x3A, payload, expect_len=1)
        if resp[0] != 0xBA or resp[1] != 0:
            raise ProtocolError("set_hw_caps failed")

    def reboot_bootloader(self) -> None:
        resp = self._send(0x0E, b"", expect_len=1)
        if resp[0] != 0x8E or resp[1] != 0:
            raise ProtocolError("reboot_bootloader failed")

    def read_stream_frame(self, timeout_ms: int = 500) -> bytes:
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            b = self.ser.read_any(1)
            if not b:
                continue
            if b[0] != SOF:
                continue
            rest = self.ser.read_exact(2)
            cmd, plen = rest[0], rest[1]
            payload = self.ser.read_exact(plen)
            cks = self.ser.read_exact(1)
            frame = bytes([SOF, cmd, plen]) + payload
            if cks[0] != checksum(frame):
                continue
            if cmd != 0x81:
                continue
            return payload
        raise ProtocolError("timeout waiting for stream frame")

    def ring_summary(self) -> RingSummary:
        resp = self._send(0x20, b"", expect_len=10)
        if resp[0] != 0xA0:
            raise ProtocolError("ring_summary bad cmd")
        count, cap, mn, mx, latest = struct.unpack(">HHhhh", resp[1:1+10])
        return RingSummary(count=count, capacity=cap, min=mn, max=mx, latest=latest)

    def graph_summary(self) -> GraphSummary:
        resp = self._send(0x22, b"", expect_len=14)
        if resp[0] != 0xA2:
            raise ProtocolError("graph_summary bad cmd")
        count, cap, mn, mx, latest, period_ms, window_ms = struct.unpack(">HHhhhHH", resp[1:1+14])
        return GraphSummary(
            count=count,
            capacity=cap,
            min=mn,
            max=mx,
            latest=latest,
            period_ms=period_ms,
            window_ms=window_ms,
        )

    def graph_select(self, channel: int, window: int, reset: bool = False) -> None:
        flags = 0x01 if reset else 0x00
        payload = bytes([channel & 0xFF, window & 0xFF, flags])
        resp = self._send(0x23, payload, expect_len=1)
        if resp[0] != 0xA3 or resp[1] != 0:
            raise ProtocolError("graph_select failed")

    def histogram_trace(self) -> None:
        resp = self._send(0x24, b"", expect_len=1)
        if resp[0] != 0xA4 or resp[1] != 0:
            raise ProtocolError("histogram_trace failed")

    def marker_control(self, enable: bool = True) -> None:
        payload = bytes([1 if enable else 0])
        resp = self._send(0x25, payload, expect_len=1)
        if resp[0] != 0xA5 or resp[1] != 0:
            raise ProtocolError("marker_control failed")

    def debug_state(self) -> DebugState:
        resp = self._send(0x21, b"", expect_len=None)
        if resp[0] != 0xA1:
            raise ProtocolError("debug_state bad cmd")
        return DebugState.from_payload(resp[1:])

    def ble_csc_measurement(self) -> CSCMeasurement:
        resp = self._send(0x60, b"", expect_len=12)
        if resp[0] != 0xE0:
            raise ProtocolError("ble_csc_measurement bad cmd")
        return CSCMeasurement.from_payload(resp[1:])

    def ble_cps_measurement(self) -> CPSMeasurement:
        resp = self._send(0x61, b"", expect_len=14)
        if resp[0] != 0xE1:
            raise ProtocolError("ble_cps_measurement bad cmd")
        return CPSMeasurement.from_payload(resp[1:])

    def ble_bas_level(self) -> int:
        resp = self._send(0x62, b"", expect_len=1)
        if resp[0] != 0xE2:
            raise ProtocolError("ble_bas_level bad cmd")
        return resp[1]

    def ble_hacker_exchange(self, frame: bytes) -> bytes:
        resp = self._send(0x70, frame, expect_len=None)
        if resp[0] != 0xF0:
            raise ProtocolError("ble_hacker_exchange bad cmd")
        return resp[1:]

    def ble_mitm_status(self) -> BleMitmStatus:
        resp = self._send(0x73, b"", expect_len=10)
        if resp[0] != 0xF3:
            raise ProtocolError("ble_mitm_status bad cmd")
        return BleMitmStatus.from_payload(resp[1:])

    def ble_mitm_control(
        self,
        enable: bool,
        mode: int,
        event: Optional[int] = None,
        data: bytes = b"",
    ) -> int:
        payload = bytearray([1 if enable else 0, mode & 0xFF])
        if event is not None:
            payload.append(event & 0xFF)
            if event in (BLE_MITM_EVENT_RX, BLE_MITM_EVENT_TX):
                if len(data) > 255:
                    raise ValueError("ble_mitm data too long")
                payload.append(len(data) & 0xFF)
                payload += data
        resp = self._send(0x73, bytes(payload), expect_len=1)
        if resp[0] != 0xF3:
            raise ProtocolError("ble_mitm_control bad cmd")
        return resp[1]

    def ble_mitm_capture_read(self) -> BleMitmCapture:
        resp = self._send(0x74, b"", expect_len=None)
        if resp[0] != 0xF4:
            raise ProtocolError("ble_mitm_capture_read bad cmd")
        return BleMitmCapture.from_payload(resp[1:])

    def ab_status(self) -> AbStatus:
        resp = self._send(0x71, b"", expect_len=12)
        if resp[0] != 0xF1:
            raise ProtocolError("ab_status bad cmd")
        return AbStatus.from_payload(resp[1:])

    def ab_set_pending(self, slot: int) -> None:
        payload = bytes([slot & 0xFF])
        resp = self._send(0x72, payload, expect_len=1)
        if resp[0] != 0xF2 or resp[1] != 0:
            raise ProtocolError("ab_set_pending failed")

    def fault_inject(self, mask: int, comm_retry_budget: int = 3, derate_cap_w: int = 0) -> int:
        """
        Configure fault injection bitmask (developer/test builds only).
        mask bits: 0=speed dropout, 1=comm errors, 2=overtemp, 3=derate.
        Returns status byte.
        """
        payload = bytearray([mask & 0xFF, comm_retry_budget & 0xFF])
        if derate_cap_w:
            payload += struct.pack(">H", derate_cap_w & 0xFFFF)
        resp = self._send(0x43, bytes(payload), expect_len=1)
        if resp[0] != 0xC3:
            raise ProtocolError("fault_inject bad cmd")
        return resp[1]

    def set_profile(self, profile_id: int, persist: bool = True) -> None:
        payload = struct.pack("BB", profile_id & 0xFF, 1 if persist else 0)
        resp = self._send(0x33, payload, expect_len=1)
        if resp[0] != 0xB3 or resp[1] != 0:
            raise ProtocolError("set_profile failed")

    def set_gears(self, count: int, shape: int, min_q15: int, max_q15: int,
                  scales: Optional[List[int]] = None) -> None:
        if count < 1 or count > 12:
            raise ValueError("count must be 1..12")
        payload = bytearray([count & 0xFF, shape & 0xFF])
        payload += struct.pack(">HH", min_q15 & 0xFFFF, max_q15 & 0xFFFF)
        if scales:
            if len(scales) < count:
                raise ValueError("scales list too short")
            for i in range(count):
                payload += struct.pack(">H", scales[i] & 0xFFFF)
        resp = self._send(0x34, bytes(payload), expect_len=1)
        if resp[0] != 0xB4 or resp[1] != 0:
            raise ProtocolError("set_gears failed")

    def set_cadence_bias(self, enabled: bool, target_rpm: int, band_rpm: int,
                         min_bias_q15: int) -> None:
        payload = bytearray()
        payload.append(1 if enabled else 0)
        payload += struct.pack(">HHH", target_rpm & 0xFFFF, band_rpm & 0xFFFF, min_bias_q15 & 0xFFFF)
        resp = self._send(0x35, bytes(payload), expect_len=1)
        if resp[0] != 0xB5 or resp[1] != 0:
            raise ProtocolError("set_cadence_bias failed")

    def config_get(self) -> ConfigBlob:
        resp = self._send(0x30, b"", expect_len=68)
        if resp[0] != 0xB0:
            raise ProtocolError("config_get bad cmd")
        return ConfigBlob.from_payload(resp[1:])

    def config_stage(self, cfg: ConfigBlob) -> None:
        payload = cfg.to_payload(recalc_crc=True)
        resp = self._send(0x31, payload, expect_len=1)
        if resp[0] != 0xB1 or resp[1] != 0:
            raise ProtocolError("config_stage failed")

    def config_commit(self, reboot: bool = False) -> None:
        payload = bytes([1 if reboot else 0])
        resp = self._send(0x32, payload, expect_len=1)
        if resp[0] != 0xB2 or resp[1] != 0:
            raise ProtocolError("config_commit failed")

    # Event log helpers
    def event_log_summary(self) -> EventLogSummary:
        resp = self._send(0x40, b"", expect_len=16)
        if resp[0] != 0xC0:
            raise ProtocolError("event_log_summary bad cmd")
        count, cap, head, rec_sz = struct.unpack(">HHHH", resp[2:10])
        seq = struct.unpack(">I", resp[12:16])[0]
        return EventLogSummary(count, cap, head, rec_sz, seq)

    def event_log_read(self, offset: int, limit: int = 4) -> List[EventRecord]:
        payload = struct.pack(">HB", offset & 0xFFFF, limit & 0xFF)
        resp = self._send(0x41, payload, expect_len=None)
        if resp[0] != 0xC1:
            raise ProtocolError("event_log_read bad cmd")
        if len(resp) < 2:
            return []
        n = resp[1]
        records: List[EventRecord] = []
        expected_len = 1 + n * 20
        if len(resp) != expected_len:
            raise ProtocolError(f"event_log_read len mismatch {len(resp)} != {expected_len}")
        ptr = 2
        for _ in range(n):
            chunk = resp[ptr:ptr+20]
            ms = struct.unpack(">I", chunk[0:4])[0]
            rtype = chunk[4]
            flags = chunk[5]
            speed = struct.unpack(">h", chunk[6:8])[0]
            batt_dV = struct.unpack(">h", chunk[8:10])[0]
            batt_dA = struct.unpack(">h", chunk[10:12])[0]
            temp = struct.unpack(">h", chunk[12:14])[0]
            pwr = struct.unpack(">H", chunk[14:16])[0]
            cur = struct.unpack(">H", chunk[16:18])[0]
            records.append(EventRecord(ms, rtype, flags, speed, batt_dV, batt_dA, temp, pwr, cur))
            ptr += 20
        return records

    def log_event_mark(self, rtype: int, flags: int = 0) -> None:
        payload = struct.pack("BB", rtype & 0xFF, flags & 0xFF)
        resp = self._send(0x42, payload, expect_len=1)
        if resp[0] != 0xC2 or resp[1] != 0:
            raise ProtocolError("log_event_mark failed")

    # Stream log helpers
    def stream_log_summary(self) -> StreamLogSummary:
        resp = self._send(0x44, b"", expect_len=18)
        if resp[0] != 0xC4:
            raise ProtocolError("stream_log_summary bad cmd")
        count, cap, head, rec_sz, period_ms = struct.unpack(">HHHHH", resp[2:12])
        enabled = bool(resp[12])
        seq = struct.unpack(">I", resp[14:18])[0]
        return StreamLogSummary(count, cap, head, rec_sz, period_ms, enabled, seq)

    def stream_log_read(self, offset: int, limit: int = 4) -> List[StreamLogRecord]:
        payload = struct.pack(">HB", offset & 0xFFFF, limit & 0xFF)
        resp = self._send(0x45, payload, expect_len=None)
        if resp[0] != 0xC5:
            raise ProtocolError("stream_log_read bad cmd")
        if len(resp) < 2:
            return []
        n = resp[1]
        record_size = 20
        expected_len = 2 + n * record_size
        if len(resp) != expected_len:
            raise ProtocolError(f"stream_log_read len mismatch {len(resp)} != {expected_len}")
        records: List[StreamLogRecord] = []
        ptr = 2
        for _ in range(n):
            chunk = resp[ptr:ptr+record_size]
            ver = chunk[0]
            flags = chunk[1]
            dt_ms = struct.unpack(">H", chunk[2:4])[0]
            speed = struct.unpack(">H", chunk[4:6])[0]
            cadence = struct.unpack(">H", chunk[6:8])[0]
            power_w = struct.unpack(">H", chunk[8:10])[0]
            batt_dV = struct.unpack(">h", chunk[10:12])[0]
            batt_dA = struct.unpack(">h", chunk[12:14])[0]
            temp = struct.unpack(">h", chunk[14:16])[0]
            assist_mode = chunk[16]
            profile_id = chunk[17]
            crc16 = struct.unpack(">H", chunk[18:20])[0]
            records.append(
                StreamLogRecord(
                    ver,
                    flags,
                    dt_ms,
                    speed,
                    cadence,
                    power_w,
                    batt_dV,
                    batt_dA,
                    temp,
                    assist_mode,
                    profile_id,
                    crc16,
                )
            )
            ptr += record_size
        return records

    def stream_log_control(self, enable: bool, period_ms: Optional[int] = None) -> None:
        payload = bytearray([1 if enable else 0])
        if period_ms is not None:
            payload += struct.pack(">H", period_ms & 0xFFFF)
        resp = self._send(0x46, bytes(payload), expect_len=1)
        if resp[0] != 0xC6 or resp[1] != 0:
            raise ProtocolError("stream_log_control failed")

    # Crash dump helpers
    def crash_dump_read(self) -> CrashDump:
        resp = self._send(0x47, b"", expect_len=CRASH_DUMP_SIZE)
        if resp[0] != 0xC7:
            raise ProtocolError("crash_dump_read bad cmd")
        return CrashDump.from_payload(resp[1:])

    def crash_dump_clear(self) -> None:
        resp = self._send(0x48, b"", expect_len=1)
        if resp[0] != 0xC8 or resp[1] != 0:
            raise ProtocolError("crash_dump_clear failed")

    def crash_dump_trigger(self) -> int:
        resp = self._send(0x49, b"", expect_len=4)
        if resp[0] != 0xC9:
            raise ProtocolError("crash_dump_trigger bad cmd")
        return struct.unpack(">I", resp[1:5])[0]

    # Bus capture helpers
    def bus_capture_summary(self) -> BusCaptureSummary:
        resp = self._send(0x50, b"", expect_len=14)
        if resp[0] != 0xD0:
            raise ProtocolError("bus_capture_summary bad cmd")
        payload = resp[1:]
        ver = payload[0]
        size = payload[1]
        count = struct.unpack(">H", payload[2:4])[0]
        cap = struct.unpack(">H", payload[4:6])[0]
        head = struct.unpack(">H", payload[6:8])[0]
        max_len = payload[8]
        enabled = bool(payload[9])
        seq = struct.unpack(">I", payload[10:14])[0]
        return BusCaptureSummary(ver, size, count, cap, head, max_len, enabled, seq)

    def bus_capture_control(self, enable: bool, reset: bool = False) -> None:
        payload = bytearray([1 if enable else 0])
        if reset:
            payload.append(1)
        resp = self._send(0x52, bytes(payload), expect_len=1)
        if resp[0] != 0xD2 or resp[1] != 0:
            raise ProtocolError("bus_capture_control failed")

    def bus_capture_read(self, offset: int, limit: int = 4) -> List[BusCaptureRecord]:
        payload = struct.pack(">HB", offset & 0xFFFF, limit & 0xFF)
        resp = self._send(0x51, payload, expect_len=None)
        if resp[0] != 0xD1:
            raise ProtocolError("bus_capture_read bad cmd")
        if len(resp) < 2:
            return []
        n = resp[1]
        records: List[BusCaptureRecord] = []
        ptr = 2
        for _ in range(n):
            if ptr + 4 > len(resp):
                raise ProtocolError("bus_capture_read truncated header")
            dt_ms = struct.unpack(">H", resp[ptr:ptr+2])[0]
            bus_id = resp[ptr+2]
            data_len = resp[ptr+3]
            ptr += 4
            if ptr + data_len > len(resp):
                raise ProtocolError("bus_capture_read truncated data")
            data = bytes(resp[ptr:ptr+data_len])
            ptr += data_len
            records.append(BusCaptureRecord(dt_ms, bus_id, data))
        return records

    def bus_capture_inject(self, bus_id: int, dt_ms: int, data: bytes) -> int:
        payload = bytearray([bus_id & 0xFF])
        payload += struct.pack(">H", dt_ms & 0xFFFF)
        payload.append(len(data) & 0xFF)
        payload += data
        resp = self._send(0x53, bytes(payload), expect_len=1)
        if resp[0] != 0xD3:
            raise ProtocolError("bus_capture_inject bad cmd")
        return resp[1]

    def bus_monitor_control(
        self,
        flags: int,
        bus_id: Optional[int] = None,
        opcode: Optional[int] = None,
    ) -> None:
        payload = bytearray([flags & 0xFF])
        if bus_id is not None:
            payload.append(bus_id & 0xFF)
        if opcode is not None:
            payload.append(opcode & 0xFF)
        resp = self._send(0x54, bytes(payload), expect_len=1)
        if resp[0] != 0xD4 or resp[1] != 0:
            raise ProtocolError("bus_monitor_control failed")

    def bus_inject_arm(self, armed: bool, override: bool = False) -> None:
        payload = bytearray([1 if armed else 0])
        if override:
            payload.append(1)
        resp = self._send(0x55, bytes(payload), expect_len=1)
        if resp[0] != 0xD5 or resp[1] != 0:
            raise ProtocolError("bus_inject_arm failed")

    def bus_capture_replay(self, offset: int, rate_ms: int) -> int:
        payload = bytearray([1, offset & 0xFF])
        payload += struct.pack(">H", rate_ms & 0xFFFF)
        resp = self._send(0x56, bytes(payload), expect_len=1)
        if resp[0] != 0xD6:
            raise ProtocolError("bus_capture_replay bad cmd")
        return resp[1]

    def bus_capture_replay_stop(self) -> None:
        resp = self._send(0x56, b"\x00", expect_len=1)
        if resp[0] != 0xD6 or resp[1] != 0:
            raise ProtocolError("bus_capture_replay_stop failed")

    # Trip statistics helpers
    @dataclasses.dataclass
    class TripSnapshot:
        distance_mm: int
        elapsed_ms: int
        moving_ms: int
        energy_mwh: int
        max_speed_dmph: int
        avg_speed_dmph: int
        wh_per_mile_d10: int
        wh_per_km_d10: int

    @dataclasses.dataclass
    class TripStatus:
        version: int
        size: int
        last_valid: bool
        active: "UARTClient.TripSnapshot"
        last: "UARTClient.TripSnapshot"

    @staticmethod
    def _parse_trip_snapshot(buf: bytes) -> "UARTClient.TripSnapshot":
        if len(buf) < 24:
            raise ProtocolError("trip snapshot too short")
        dist, el, mov, mwh, max_spd, avg_spd, wh_mi, wh_km = struct.unpack(">IIIIHHHH", buf[:24])
        return UARTClient.TripSnapshot(dist, el, mov, mwh, max_spd, avg_spd, wh_mi, wh_km)

    def trip_get(self) -> "UARTClient.TripStatus":
        resp = self._send(0x36, b"", expect_len=None)
        if resp[0] != 0xB6:
            raise ProtocolError("trip_get bad cmd")
        if len(resp) < 3 + 24 + 24 + 1:
            raise ProtocolError("trip_get payload too short")
        version = resp[1]
        size = resp[2]
        flags = resp[3]
        active = self._parse_trip_snapshot(resp[4:28])
        last = self._parse_trip_snapshot(resp[28:52])
        return UARTClient.TripStatus(version, size, bool(flags & 0x01), active, last)

    def trip_reset(self) -> None:
        resp = self._send(0x37, b"", expect_len=1)
        if resp[0] != 0xB7 or resp[1] != 0:
            raise ProtocolError("trip_reset failed")


# ---------------- CLI ----------------


def _cmd_ping(client: UARTClient, args: argparse.Namespace) -> None:
    client.ping()
    print("ping: ok")


def _cmd_log(client: UARTClient, args: argparse.Namespace) -> None:
    payload = client.log_frame()
    if len(payload) < 2:
        print("")
        return
    code = payload[0]
    plen = payload[1]
    data = payload[2:]
    if plen != len(data):
        print(f"code=0x{code:02x} len={plen} data={data.hex()} (len_mismatch)")
        return
    print(f"code=0x{code:02x} len={plen} data={data.hex()}")


def _cmd_set_state(client: UARTClient, args: argparse.Namespace) -> None:
    client.set_state(
        args.rpm,
        args.torque,
        args.speed_dmph,
        args.soc,
        args.err,
        cadence_rpm=args.cadence_rpm,
        throttle_pct=args.throttle_pct,
        brake=args.brake,
        buttons=args.buttons,
        power_w=args.power_w,
        batt_dV=args.batt_dv,
        batt_dA=args.batt_da,
        ctrl_temp_dC=args.ctrl_temp_dc,
    )
    st = client.state_dump()
    print(dataclasses.asdict(st))


def _cmd_state_dump(client: UARTClient, args: argparse.Namespace) -> None:
    st = client.state_dump()
    print(dataclasses.asdict(st))


def _cmd_stream(client: UARTClient, args: argparse.Namespace) -> None:
    client.set_stream(args.period_ms)
    if args.period_ms == 0:
        print("stream disabled")
        return
    frames: List[TelemetryV1] = []
    deadline = time.time() + (args.duration_ms / 1000.0)
    while time.time() < deadline:
        payload = client.read_stream_frame(timeout_ms=args.period_ms + 50)
        frames.append(TelemetryV1.from_payload(payload))
    if not frames:
        raise ProtocolError("no frames captured")
    first = frames[0]
    print(
        f"captured {len(frames)} frames; first t={first.ms}ms spd={first.speed_dmph} "
        f"cad={first.cadence_rpm} pwr={first.power_w} batt={first.batt_dV/10:.1f}V {first.batt_dA/10:.1f}A "
        f"temp={first.ctrl_temp_dC/10:.1f}C profile={first.profile_id} gear={first.virtual_gear} flags=0x{first.flags:02x}"
    )


def _cmd_reboot_bootloader(client: UARTClient, args: argparse.Namespace) -> None:
    client.reboot_bootloader()
    print("reboot command sent")


def _cmd_ring_summary(client: UARTClient, args: argparse.Namespace) -> None:
    rs = client.ring_summary()
    print(dataclasses.asdict(rs))


def _cmd_graph_summary(client: UARTClient, args: argparse.Namespace) -> None:
    gs = client.graph_summary()
    print(dataclasses.asdict(gs))


def _cmd_graph_select(client: UARTClient, args: argparse.Namespace) -> None:
    channel = GRAPH_CHANNELS.get(args.channel, None)
    window = GRAPH_WINDOWS.get(args.window, None)
    if channel is None:
        raise ProtocolError(f"unknown channel '{args.channel}'")
    if window is None:
        raise ProtocolError(f"unknown window '{args.window}'")
    client.graph_select(channel, window, reset=args.reset)
    gs = client.graph_summary()
    print(dataclasses.asdict(gs))

def _cmd_histogram_trace(client: UARTClient, args: argparse.Namespace) -> None:
    client.histogram_trace()
    print("histogram trace emitted")

def _cmd_marker_control(client: UARTClient, args: argparse.Namespace) -> None:
    client.marker_control(enable=not args.disable)
    print("markers enabled" if not args.disable else "markers disabled")

def _cmd_stream_log_summary(client: UARTClient, args: argparse.Namespace) -> None:
    s = client.stream_log_summary()
    print(dataclasses.asdict(s))


def _cmd_stream_log_read(client: UARTClient, args: argparse.Namespace) -> None:
    recs = client.stream_log_read(args.offset, args.limit)
    for r in recs:
        print(dataclasses.asdict(r))


def _cmd_stream_log_control(client: UARTClient, args: argparse.Namespace) -> None:
    enable = not args.disable
    client.stream_log_control(enable=enable, period_ms=args.period_ms)
    print("stream log enabled" if enable else "stream log disabled")


def _cmd_crash_dump_read(client: UARTClient, args: argparse.Namespace) -> None:
    dump = client.crash_dump_read()
    print(dataclasses.asdict(dump))


def _cmd_crash_dump_clear(client: UARTClient, args: argparse.Namespace) -> None:
    client.crash_dump_clear()
    print("crash dump cleared")


def _cmd_crash_dump_trigger(client: UARTClient, args: argparse.Namespace) -> None:
    addr = client.crash_dump_trigger()
    print(f"crash trigger sent (pc_hint=0x{addr:08x})")


def _cmd_set_gears(client: UARTClient, args: argparse.Namespace) -> None:
    scales = None
    if args.scales:
        scales = [int(x) for x in args.scales.split(",")]
    client.set_gears(args.count, args.shape, args.min_q15, args.max_q15, scales=scales)
    st = client.debug_state()
    print(dataclasses.asdict(st))


def _cmd_set_cadence_bias(client: UARTClient, args: argparse.Namespace) -> None:
    client.set_cadence_bias(args.enabled, args.target_rpm, args.band_rpm, args.min_bias_q15)
    st = client.debug_state()
    print(dataclasses.asdict(st))


def make_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default=os.environ.get("BC280_UART_PORT", "/tmp/uart1"))
    p.add_argument("--baud", type=int, default=115200)
    sub = p.add_subparsers(dest="subcmd", required=True)

    sub.add_parser("ping")
    sub.add_parser("log")

    ps = sub.add_parser("set-state")
    ps.add_argument("--rpm", type=int, required=True)
    ps.add_argument("--torque", type=int, required=True)
    ps.add_argument("--speed-dmph", type=int, required=True)
    ps.add_argument("--soc", type=int, required=True)
    ps.add_argument("--err", type=int, required=True)
    ps.add_argument("--cadence-rpm", type=int)
    ps.add_argument("--throttle-pct", type=int)
    ps.add_argument("--brake", type=int)
    ps.add_argument("--buttons", type=int)
    ps.add_argument("--power-w", type=int)
    ps.add_argument("--batt-dv", type=int, help="battery voltage in 0.1 V")
    ps.add_argument("--batt-da", type=int, help="battery current in 0.1 A (signed)")
    ps.add_argument("--ctrl-temp-dc", type=int, help="controller temperature in 0.1 C")

    sub.add_parser("state-dump")

    pst = sub.add_parser("stream")
    pst.add_argument("--period-ms", type=int, required=True)
    pst.add_argument("--duration-ms", type=int, default=1000)

    sub.add_parser("reboot-bootloader")
    sub.add_parser("ring-summary")
    sub.add_parser("graph-summary")
    pgs = sub.add_parser("graph-select")
    pgs.add_argument("--channel", required=True, choices=sorted(GRAPH_CHANNELS.keys()))
    pgs.add_argument("--window", required=True, choices=sorted(GRAPH_WINDOWS.keys()))
    pgs.add_argument("--reset", action="store_true", help="reset selected channel buffers")
    sub.add_parser("histogram-trace")
    pmark = sub.add_parser("marker-control")
    pmark.add_argument("--disable", action="store_true")
    sub.add_parser("debug-state")
    sub.add_parser("config-get")
    psw = sub.add_parser("set-profile")
    psw.add_argument("--id", type=int, required=True)
    psw.add_argument("--no-persist", action="store_true")

    pgear = sub.add_parser("set-gears")
    pgear.add_argument("--count", type=int, required=True)
    pgear.add_argument("--shape", type=int, choices=[0, 1], required=True)
    pgear.add_argument("--min-q15", type=int, required=True)
    pgear.add_argument("--max-q15", type=int, required=True)
    pgear.add_argument("--scales", help="comma-separated explicit Q15 scales", default=None)

    pcb = sub.add_parser("cadence-bias")
    pcb.add_argument("--enabled", action="store_true")
    pcb.add_argument("--target-rpm", type=int, required=True)
    pcb.add_argument("--band-rpm", type=int, required=True)
    pcb.add_argument("--min-bias-q15", type=int, required=True)

    pcfg = sub.add_parser("config-apply")
    pcfg.add_argument("--wheel-mm", type=int, required=True)
    pcfg.add_argument("--units", type=int, choices=[0, 1], required=True)
    pcfg.add_argument("--profile-id", type=int, default=0)
    pcfg.add_argument("--theme", type=int, default=0)
    pcfg.add_argument("--flags", type=int, default=0)
    pcfg.add_argument("--reboot", action="store_true")

    sub.add_parser("event-log-summary")
    pel = sub.add_parser("event-log-read")
    pel.add_argument("--offset", type=int, default=0)
    pel.add_argument("--limit", type=int, default=4)
    pelm = sub.add_parser("event-log-mark")
    pelm.add_argument("--type", type=int, required=True)
    pelm.add_argument("--flags", type=int, default=0)

    sub.add_parser("stream-log-summary")
    psl = sub.add_parser("stream-log-read")
    psl.add_argument("--offset", type=int, default=0)
    psl.add_argument("--limit", type=int, default=4)
    psc = sub.add_parser("stream-log-control")
    psc.add_argument("--disable", action="store_true", help="disable stream logging")
    psc.add_argument("--period-ms", type=int, default=None)

    sub.add_parser("crash-dump-read")
    sub.add_parser("crash-dump-clear")
    sub.add_parser("crash-dump-trigger")

    return p


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = make_arg_parser().parse_args(list(argv) if argv is not None else None)
    client = UARTClient(args.port, baud=args.baud)
    try:
        if args.subcmd == "ping":
            _cmd_ping(client, args)
        elif args.subcmd == "log":
            _cmd_log(client, args)
        elif args.subcmd == "set-state":
            _cmd_set_state(client, args)
        elif args.subcmd == "state-dump":
            _cmd_state_dump(client, args)
        elif args.subcmd == "stream":
            _cmd_stream(client, args)
        elif args.subcmd == "reboot-bootloader":
            _cmd_reboot_bootloader(client, args)
        elif args.subcmd == "ring-summary":
            _cmd_ring_summary(client, args)
        elif args.subcmd == "graph-summary":
            _cmd_graph_summary(client, args)
        elif args.subcmd == "graph-select":
            _cmd_graph_select(client, args)
        elif args.subcmd == "histogram-trace":
            _cmd_histogram_trace(client, args)
        elif args.subcmd == "marker-control":
            _cmd_marker_control(client, args)
        elif args.subcmd == "debug-state":
            st = client.debug_state()
            print(dataclasses.asdict(st))
        elif args.subcmd == "config-get":
            cfg = client.config_get()
            print(dataclasses.asdict(cfg))
        elif args.subcmd == "set-profile":
            client.set_profile(args.id, persist=not args.no_persist)
            st = client.debug_state()
            print(dataclasses.asdict(st))
        elif args.subcmd == "set-gears":
            _cmd_set_gears(client, args)
        elif args.subcmd == "cadence-bias":
            _cmd_set_cadence_bias(client, args)
        elif args.subcmd == "config-apply":
            cfg = ConfigBlob.defaults()
            cfg.wheel_mm = args.wheel_mm
            cfg.units = args.units
            cfg.profile_id = args.profile_id
            cfg.theme = args.theme
            cfg.flags = args.flags
            cfg.seq = client.config_get().seq + 1
            client.config_stage(cfg)
            client.config_commit(reboot=args.reboot)
            print("config applied")
        elif args.subcmd == "event-log-summary":
            s = client.event_log_summary()
            print(dataclasses.asdict(s))
        elif args.subcmd == "event-log-read":
            recs = client.event_log_read(args.offset, args.limit)
            for r in recs:
                print(dataclasses.asdict(r))
        elif args.subcmd == "event-log-mark":
            client.log_event_mark(args.type, args.flags)
            print("event marked")
        elif args.subcmd == "stream-log-summary":
            _cmd_stream_log_summary(client, args)
        elif args.subcmd == "stream-log-read":
            _cmd_stream_log_read(client, args)
        elif args.subcmd == "stream-log-control":
            _cmd_stream_log_control(client, args)
        elif args.subcmd == "crash-dump-read":
            _cmd_crash_dump_read(client, args)
        elif args.subcmd == "crash-dump-clear":
            _cmd_crash_dump_clear(client, args)
        elif args.subcmd == "crash-dump-trigger":
            _cmd_crash_dump_trigger(client, args)
        else:
            raise AssertionError("unknown subcmd")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
