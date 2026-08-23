"""Transports from the PC to the board.

Both implementations expose the same tiny async interface so the agent's main
loop does not care which cable (or radio) the frames travel over.
"""

from __future__ import annotations

import asyncio
import logging

log = logging.getLogger(__name__)

# Nordic UART Service, as implemented by components/pcmon_link/link_ble.c
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # PC -> board
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # board -> PC

ESPRESSIF_VID = 0x303A


class Transport:
    """Minimal async byte sink."""

    description = "none"

    async def connect(self) -> None:
        raise NotImplementedError

    async def send(self, data: bytes) -> None:
        raise NotImplementedError

    async def close(self) -> None:
        raise NotImplementedError


class SerialTransport(Transport):
    """USB CDC (the board's native USB-Serial-JTAG port)."""

    def __init__(self, port: str | None = None, baudrate: int = 921600) -> None:
        self._requested_port = port
        self._baudrate = baudrate
        self._serial = None
        self.description = port or "auto"

    @staticmethod
    def list_candidates() -> list:
        from serial.tools import list_ports

        ports = []
        for info in list_ports.comports():
            is_esp = info.vid == ESPRESSIF_VID
            looks_like_cdc = "USB JTAG" in (info.description or "") or \
                             "CDC" in (info.description or "")
            if is_esp or looks_like_cdc:
                ports.insert(0, info)  # best guesses first
            elif info.device.startswith("/dev/ttyACM"):
                ports.append(info)
        return ports

    def _open(self) -> None:
        import serial

        port = self._requested_port
        if port is None:
            candidates = self.list_candidates()
            if not candidates:
                raise ConnectionError("no ESP32-S3 CDC port found (try --port)")
            port = candidates[0].device

        # The USB-Serial-JTAG bridge interprets DTR/RTS as reset/boot lines;
        # keeping both deasserted avoids rebooting the board on every connect.
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = self._baudrate  # ignored by CDC, but pyserial wants one
        ser.timeout = 1.0
        ser.write_timeout = 1.0
        ser.dtr = False
        ser.rts = False
        ser.open()
        ser.reset_input_buffer()

        self._serial = ser
        self.description = port

    async def connect(self) -> None:
        await asyncio.to_thread(self._open)

    async def send(self, data: bytes) -> None:
        if self._serial is None:
            raise ConnectionError("serial port is not open")
        try:
            await asyncio.to_thread(self._serial.write, data)
        except Exception as exc:
            raise ConnectionError(f"serial write failed: {exc}") from exc

    async def close(self) -> None:
        if self._serial is not None:
            try:
                await asyncio.to_thread(self._serial.close)
            except Exception:
                pass
            self._serial = None


class BleTransport(Transport):
    """BLE GATT client writing into the board's NUS RX characteristic."""

    def __init__(self, name: str = "ESP-PCMON", address: str | None = None,
                 scan_timeout: float = 10.0) -> None:
        self._name = name
        self._address = address
        self._scan_timeout = scan_timeout
        self._client = None
        self.description = address or name

    async def connect(self) -> None:
        from bleak import BleakClient, BleakScanner

        target = self._address
        if target is None:
            log.info("scanning for \"%s\" ...", self._name)
            # Passing the discovered BLEDevice (instead of just its address)
            # saves the backend a second lookup on connect.
            device = await BleakScanner.find_device_by_name(self._name,
                                                            timeout=self._scan_timeout)
            if device is None:
                raise ConnectionError(f"BLE device \"{self._name}\" not found")
            target, address = device, device.address
        else:
            address = target

        client = BleakClient(target, disconnected_callback=self._on_disconnect)
        await client.connect()
        self._client = client
        self.description = f"{self._name} [{address}]"

    def _on_disconnect(self, _client) -> None:
        log.warning("BLE disconnected")
        self._client = None

    async def send(self, data: bytes) -> None:
        client = self._client
        if client is None or not client.is_connected:
            raise ConnectionError("BLE link is down")
        try:
            # Write-without-response: no round trip, which keeps the 10 Hz
            # stream well inside a single connection interval.
            await client.write_gatt_char(NUS_RX_UUID, data, response=False)
        except Exception as exc:
            raise ConnectionError(f"BLE write failed: {exc}") from exc

    async def close(self) -> None:
        client, self._client = self._client, None
        if client is not None:
            try:
                await client.disconnect()
            except Exception:
                pass
