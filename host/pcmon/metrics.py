"""Telemetry collection on the PC side.

CPU/RAM/uptime come from psutil, the GPU from NVML (the same library nvidia-smi
uses).  Every reading is optional: whatever cannot be determined is reported as
"unknown" through the protocol flags instead of a fake zero.
"""

from __future__ import annotations

import logging
import platform
import socket
import time

import psutil

from .protocol import Hello, Metrics

log = logging.getLogger(__name__)

# Order matters: the first sensor group found wins.  "Tctl"/"Package" style
# labels are preferred over individual core sensors.
_CPU_TEMP_SOURCES = (
    ("k10temp", ("Tctl", "Tdie")),      # AMD
    ("zenpower", ("Tdie", "Tctl")),     # AMD, out-of-tree driver
    ("coretemp", ("Package id 0",)),    # Intel
    ("cpu_thermal", ()),                # ARM SBCs
    ("acpitz", ()),                     # last resort
)

MIB = 1024 * 1024


def _cpu_model() -> str:
    """Human readable CPU name, shortened to what fits on a 240 px screen."""
    name = ""
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                if line.startswith("model name"):
                    name = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass

    if not name:
        name = platform.processor() or platform.machine()

    for noise in ("(R)", "(TM)", "(tm)", "CPU ", "Processor", "  "):
        name = name.replace(noise, " ")
    return " ".join(name.split())


class GpuReader:
    """NVIDIA GPU readings via NVML, with a graceful "no GPU" fallback."""

    def __init__(self, enabled: bool = True) -> None:
        self._nvml = None
        self._handle = None
        self.name = ""

        if not enabled:
            return

        try:
            import pynvml  # nvidia-ml-py
        except ImportError:
            log.warning("pynvml not installed - GPU metrics disabled "
                        "(pip install nvidia-ml-py)")
            return

        try:
            pynvml.nvmlInit()
            self._handle = pynvml.nvmlDeviceGetHandleByIndex(0)
            name = pynvml.nvmlDeviceGetName(self._handle)
            self.name = name.decode() if isinstance(name, bytes) else name
            self.name = self.name.replace("NVIDIA ", "")
            self._nvml = pynvml
            log.info("GPU: %s", self.name)
        except Exception as exc:  # NVMLError and friends
            log.warning("NVML init failed (%s) - GPU metrics disabled", exc)
            self._handle = None

    @property
    def available(self) -> bool:
        return self._nvml is not None and self._handle is not None

    def read(self, sample: Metrics) -> None:
        """Fill the GPU part of @sample; silently degrades on driver hiccups."""
        nv, handle = self._nvml, self._handle
        if nv is None or handle is None:
            return

        sample.gpu_present = True

        try:
            util = nv.nvmlDeviceGetUtilizationRates(handle)
            sample.gpu_load = float(util.gpu)
        except Exception:
            sample.gpu_present = False
            return

        try:
            mem = nv.nvmlDeviceGetMemoryInfo(handle)
            sample.vram_used_mb = mem.used // MIB
            sample.vram_total_mb = mem.total // MIB
        except Exception:
            pass

        try:
            sample.gpu_temp = float(nv.nvmlDeviceGetTemperature(handle, nv.NVML_TEMPERATURE_GPU))
        except Exception:
            pass

        try:
            sample.gpu_freq_mhz = int(nv.nvmlDeviceGetClockInfo(handle, nv.NVML_CLOCK_GRAPHICS))
        except Exception:
            pass

        try:
            sample.gpu_power_w = nv.nvmlDeviceGetPowerUsage(handle) / 1000.0
        except Exception:
            pass

        try:
            sample.gpu_fan = float(nv.nvmlDeviceGetFanSpeed(handle))
        except Exception:
            pass  # blower-less / passively cooled cards report nothing

    def close(self) -> None:
        if self._nvml is not None:
            try:
                self._nvml.nvmlShutdown()
            except Exception:
                pass


class Collector:
    """Produces one Metrics sample per call."""

    def __init__(self, gpu: bool = True) -> None:
        self.gpu = GpuReader(enabled=gpu)
        self._cpu_temp_warned = False
        # Prime psutil's internal counters so the first sample is a real delta.
        psutil.cpu_percent(percpu=True)
        psutil.cpu_percent()

    def hello(self) -> Hello:
        return Hello(
            hostname=socket.gethostname(),
            cpu_name=_cpu_model(),
            gpu_name=self.gpu.name,
        )

    def _cpu_temperature(self) -> float | None:
        try:
            sensors = psutil.sensors_temperatures()
        except (AttributeError, OSError):
            return None  # not implemented on Windows/macOS

        for chip, preferred in _CPU_TEMP_SOURCES:
            entries = sensors.get(chip)
            if not entries:
                continue
            for label in preferred:
                for entry in entries:
                    if entry.label == label and entry.current:
                        return float(entry.current)
            # No preferred label: fall back to the hottest reading of the chip.
            readings = [e.current for e in entries if e.current]
            if readings:
                return float(max(readings))

        if not self._cpu_temp_warned:
            log.warning("no CPU temperature sensor found (available: %s)",
                        ", ".join(sensors) or "none")
            self._cpu_temp_warned = True
        return None

    def sample(self) -> Metrics:
        vm = psutil.virtual_memory()
        per_core = psutil.cpu_percent(percpu=True)

        try:
            freq = psutil.cpu_freq()
            freq_mhz = int(freq.current) if freq else 0
        except (OSError, AttributeError):
            freq_mhz = 0

        sample = Metrics(
            uptime_s=int(time.time() - psutil.boot_time()),
            # "used" as the task manager shows it: everything not reclaimable.
            ram_used_mb=(vm.total - vm.available) // MIB,
            ram_total_mb=vm.total // MIB,
            cpu_load=sum(per_core) / len(per_core) if per_core else 0.0,
            cpu_temp=self._cpu_temperature(),
            cpu_freq_mhz=freq_mhz,
            core_loads=list(per_core),
        )
        self.gpu.read(sample)
        return sample

    def close(self) -> None:
        self.gpu.close()
