"""Wire protocol shared with the firmware.

This is the Python mirror of ``components/pcmon_proto/include/pcmon_proto.h``.
The struct layouts and the CRC must stay byte-for-byte identical; the module
self-checks the sizes on import.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

SYNC = b"\xa5\x5a"
VERSION = 1
MAX_PAYLOAD = 128

MSG_METRICS = 0x01
MSG_HELLO = 0x02
MSG_PING = 0x03
MSG_PONG = 0x83

MAX_CORES = 32

# uptime, ram_used, ram_total | cpu load/temp/freq | gpu load/temp/freq/power/fan
# | vram used/total | core_count, flags | 32 per-core percentages
METRICS_FMT = "<IIIHhHHhHHHHHBB32B"
HELLO_FMT = "<24s40s40s"

assert struct.calcsize(METRICS_FMT) == 66, "metrics layout drifted from pcmon_proto.h"
assert struct.calcsize(HELLO_FMT) == 104, "hello layout drifted from pcmon_proto.h"

FLAG_CPU_TEMP_VALID = 1 << 0
FLAG_GPU_PRESENT = 1 << 1
FLAG_GPU_TEMP_VALID = 1 << 2
FLAG_GPU_FAN_VALID = 1 << 3
FLAG_GPU_POWER_VALID = 1 << 4


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(msg_type: int, payload: bytes = b"") -> bytes:
    """Wrap a payload into a framed message."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too large: {len(payload)} > {MAX_PAYLOAD}")
    body = bytes((VERSION, msg_type, len(payload))) + payload
    return SYNC + body + struct.pack("<H", crc16(body))


def _clamp(value: float, low: int, high: int) -> int:
    return max(low, min(high, int(round(value))))


def _pm(percent: float | None) -> int:
    """Percent -> per mille, clamped to the protocol range."""
    return 0 if percent is None else _clamp(percent * 10.0, 0, 1000)


def _dc(celsius: float | None) -> int:
    """Celsius -> deci-degrees, clamped to int16."""
    return 0 if celsius is None else _clamp(celsius * 10.0, -32768, 32767)


@dataclass
class Metrics:
    """One telemetry sample, in human units; converted on pack()."""

    uptime_s: int = 0
    ram_used_mb: int = 0
    ram_total_mb: int = 0

    cpu_load: float = 0.0          # percent
    cpu_temp: float | None = None  # celsius
    cpu_freq_mhz: int = 0
    core_loads: list[float] = field(default_factory=list)  # percent per thread

    gpu_present: bool = False
    gpu_load: float = 0.0
    gpu_temp: float | None = None
    gpu_freq_mhz: int = 0
    gpu_power_w: float | None = None
    gpu_fan: float | None = None
    vram_used_mb: int = 0
    vram_total_mb: int = 0

    def _flags(self) -> int:
        flags = 0
        if self.cpu_temp is not None:
            flags |= FLAG_CPU_TEMP_VALID
        if self.gpu_present:
            flags |= FLAG_GPU_PRESENT
            if self.gpu_temp is not None:
                flags |= FLAG_GPU_TEMP_VALID
            if self.gpu_fan is not None:
                flags |= FLAG_GPU_FAN_VALID
            if self.gpu_power_w is not None:
                flags |= FLAG_GPU_POWER_VALID
        return flags

    def pack(self) -> bytes:
        cores = [_clamp(load, 0, 100) for load in self.core_loads[:MAX_CORES]]
        cores += [0] * (MAX_CORES - len(cores))

        payload = struct.pack(
            METRICS_FMT,
            _clamp(self.uptime_s, 0, 0xFFFFFFFF),
            _clamp(self.ram_used_mb, 0, 0xFFFFFFFF),
            _clamp(self.ram_total_mb, 0, 0xFFFFFFFF),
            _pm(self.cpu_load),
            _dc(self.cpu_temp),
            _clamp(self.cpu_freq_mhz, 0, 0xFFFF),
            _pm(self.gpu_load),
            _dc(self.gpu_temp),
            _clamp(self.gpu_freq_mhz, 0, 0xFFFF),
            _clamp((self.gpu_power_w or 0.0) * 10.0, 0, 0xFFFF),
            _pm(self.gpu_fan),
            _clamp(self.vram_used_mb, 0, 0xFFFF),
            _clamp(self.vram_total_mb, 0, 0xFFFF),
            min(len(self.core_loads), MAX_CORES),
            self._flags(),
            *cores,
        )
        return encode(MSG_METRICS, payload)


@dataclass
class Hello:
    """Static host description, sent right after (re)connecting."""

    hostname: str = ""
    cpu_name: str = ""
    gpu_name: str = ""

    @staticmethod
    def _fit(text: str, size: int) -> bytes:
        return text.encode("ascii", "replace")[: size - 1].ljust(size, b"\x00")

    def pack(self) -> bytes:
        payload = struct.pack(
            HELLO_FMT,
            self._fit(self.hostname, 24),
            self._fit(self.cpu_name, 40),
            self._fit(self.gpu_name, 40),
        )
        return encode(MSG_HELLO, payload)
