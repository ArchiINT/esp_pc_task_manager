#!/usr/bin/env python3
"""ESP32-S3 PC Monitor - host agent.

Samples CPU / RAM / GPU telemetry and pushes it to the board over USB or BLE.

    ./pcmon_agent.py                 # auto-detect the USB port, 10 Hz
    ./pcmon_agent.py --rate 20       # faster updates
    ./pcmon_agent.py --transport ble # talk to the BLE build of the firmware
    ./pcmon_agent.py --once          # print one sample and exit (no board needed)
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
import time

from pcmon.metrics import Collector
from pcmon.transport import BleTransport, SerialTransport, Transport

log = logging.getLogger("pcmon")

# The board declares the link dead after CONFIG_PCMON_LINK_TIMEOUT_MS (3 s), so
# re-announcing the host every 5 s also repopulates the header after a reboot.
HELLO_PERIOD_S = 5.0
RECONNECT_DELAY_S = 2.0


def build_transport(args: argparse.Namespace) -> Transport:
    if args.transport == "ble":
        return BleTransport(name=args.ble_name, address=args.ble_address)
    return SerialTransport(port=args.port)


def print_sample(collector: Collector) -> None:
    """Human readable dump of a single sample - handy without hardware."""
    hello = collector.hello()
    sample = collector.sample()
    time.sleep(0.3)          # give psutil a real interval to diff against
    sample = collector.sample()

    def fmt(value, unit: str, scale: float = 1.0) -> str:
        return "n/a" if value is None else f"{value * scale:.1f}{unit}"

    print(f"host      : {hello.hostname}")
    print(f"cpu       : {hello.cpu_name}")
    print(f"gpu       : {hello.gpu_name or 'not detected'}")
    print(f"uptime    : {sample.uptime_s // 3600}h {(sample.uptime_s % 3600) // 60}m")
    print(f"cpu load  : {sample.cpu_load:.1f}%  @ {sample.cpu_freq_mhz} MHz  "
          f"({len(sample.core_loads)} threads)")
    print(f"cpu temp  : {fmt(sample.cpu_temp, ' C')}")
    print(f"ram       : {sample.ram_used_mb} / {sample.ram_total_mb} MiB")
    if sample.gpu_present:
        print(f"gpu load  : {sample.gpu_load:.1f}%  @ {sample.gpu_freq_mhz} MHz")
        print(f"gpu temp  : {fmt(sample.gpu_temp, ' C')}")
        print(f"gpu power : {fmt(sample.gpu_power_w, ' W')}")
        print(f"gpu fan   : {fmt(sample.gpu_fan, '%')}")
        print(f"vram      : {sample.vram_used_mb} / {sample.vram_total_mb} MiB")
    print(f"frame size: {len(sample.pack())} bytes")


def list_ports() -> None:
    for info in SerialTransport.list_candidates():
        vid = f"{info.vid:04x}" if info.vid else "----"
        pid = f"{info.pid:04x}" if info.pid else "----"
        print(f"{info.device}  {vid}:{pid}  {info.description}")


async def stream(collector: Collector, transport: Transport, interval: float) -> None:
    """Reconnecting send loop; runs until cancelled."""
    while True:
        try:
            await transport.connect()
            log.info("connected via %s", transport.description)

            last_hello = 0.0
            while True:
                started = time.monotonic()

                if started - last_hello >= HELLO_PERIOD_S:
                    await transport.send(collector.hello().pack())
                    last_hello = started

                await transport.send(collector.sample().pack())

                # Pace the loop on absolute time so sampling jitter does not
                # accumulate into a drifting update rate.
                await asyncio.sleep(max(0.0, interval - (time.monotonic() - started)))

        except (ConnectionError, OSError) as exc:
            log.warning("%s - retrying in %.0f s", exc, RECONNECT_DELAY_S)
            await transport.close()
            await asyncio.sleep(RECONNECT_DELAY_S)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--transport", choices=("usb", "ble"), default="usb",
                        help="link to the board (default: usb)")
    parser.add_argument("--port", help="serial port, e.g. /dev/ttyACM0 (default: auto-detect)")
    parser.add_argument("--ble-name", default="ESP-PCMON", help="advertised BLE name")
    parser.add_argument("--ble-address", help="BLE MAC address, skips scanning")
    parser.add_argument("--rate", type=float, default=10.0,
                        help="updates per second (default: 10)")
    parser.add_argument("--no-gpu", action="store_true", help="do not query NVML")
    parser.add_argument("--once", action="store_true",
                        help="print one sample and exit")
    parser.add_argument("--list-ports", action="store_true",
                        help="show serial ports that look like an ESP32-S3")
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s",
        datefmt="%H:%M:%S",
    )

    if args.list_ports:
        list_ports()
        return 0

    if args.rate <= 0:
        log.error("--rate must be positive")
        return 2

    collector = Collector(gpu=not args.no_gpu)
    try:
        if args.once:
            print_sample(collector)
            return 0

        transport = build_transport(args)
        try:
            asyncio.run(stream(collector, transport, 1.0 / args.rate))
        except KeyboardInterrupt:
            log.info("stopping")
            asyncio.run(transport.close())
    finally:
        collector.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
