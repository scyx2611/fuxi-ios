"""Use the Windows radio to check the known fuxi controller during an iOS test boot."""
import argparse
import asyncio
import ctypes
import json
import re
import struct
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from ctypes import wintypes

from bt_controller_identity import controller_from_console

from bleak import BleakScanner
from winrt.windows.devices.bluetooth import (
    BluetoothAdapter, BluetoothAddressType, BluetoothCacheMode, BluetoothDevice, BluetoothLEDevice,
)


def classic_inquiry(address):
    # Microsoft BluetoothFindFirstDevice/NextDevice. Only report whether the
    # known controller was seen; a discovery hit alone does not prove a link.
    class Search(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("authenticated", wintypes.BOOL),
                    ("remembered", wintypes.BOOL), ("unknown", wintypes.BOOL),
                    ("connected", wintypes.BOOL), ("inquiry", wintypes.BOOL),
                    ("timeout", wintypes.BYTE), ("radio", wintypes.HANDLE)]

    class Info(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("address", ctypes.c_uint64), ("device_class", wintypes.DWORD),
                    ("connected", wintypes.BOOL), ("remembered", wintypes.BOOL), ("authenticated", wintypes.BOOL),
                    ("seen", wintypes.WORD * 8), ("used", wintypes.WORD * 8), ("name", wintypes.WCHAR * 248)]

    api = ctypes.WinDLL("bthprops.cpl", use_last_error=True)
    api.BluetoothFindFirstDevice.argtypes = [ctypes.POINTER(Search), ctypes.POINTER(Info)]
    api.BluetoothFindFirstDevice.restype = wintypes.HANDLE
    api.BluetoothFindNextDevice.argtypes = [wintypes.HANDLE, ctypes.POINTER(Info)]
    api.BluetoothFindDeviceClose.argtypes = [wintypes.HANDLE]
    search = Search(ctypes.sizeof(Search), True, True, True, True, True, 5, None)
    item = Info(ctypes.sizeof(Info))
    handle = api.BluetoothFindFirstDevice(ctypes.byref(search), ctypes.byref(item))
    if not handle:
        code = ctypes.get_last_error()
        if code != 259:  # ERROR_NO_MORE_ITEMS
            raise ctypes.WinError(code)
        return False
    try:
        while True:
            if item.address == address:
                return True
            if not api.BluetoothFindNextDevice(handle, ctypes.byref(item)):
                return False
    finally:
        api.BluetoothFindDeviceClose(handle)


async def query(address, classic):
    device = None
    try:
        if classic:
            device = await BluetoothDevice.from_bluetooth_address_async(address)
        else:
            device = await BluetoothLEDevice.from_bluetooth_address_async(address, BluetoothAddressType.PUBLIC)
        if device is None:
            return {"status": "device_unavailable"}
        if device.bluetooth_address != address:
            return {"status": "identity_mismatch"}
        if classic:
            result = await device.get_rfcomm_services_async(BluetoothCacheMode.UNCACHED)
            services = list(result.services)
            try:
                report = {"status": result.error.name, "services": len(services),
                          "connected": device.connection_status.name}
                try:
                    report["service_ids"] = sorted(str(service.service_id.uuid) for service in services)
                    report["paired"] = device.device_information.pairing.is_paired
                except OSError as error:
                    report["metadata_winerror"] = error.winerror
                return report
            finally:
                for service in services:
                    service.close()
        result = await device.get_gatt_services_async(BluetoothCacheMode.UNCACHED)
        services = list(result.services)
        try:
            return {"status": result.status.name, "services": len(services),
                    "connected": device.connection_status.name}
        finally:
            for service in services:
                service.close()
    finally:
        if device is not None:
            device.close()


def audio_source_sdp(address):
    """WinSock uncached SDP search for the A2DP Audio Source class on this phone."""
    ptr, dword = ctypes.c_void_p, wintypes.DWORD
    class Blob(ctypes.Structure):
        _fields_ = [("size", dword), ("data", ptr)]

    class Query(ctypes.Structure):
        _fields_ = [("size", dword), ("name", ptr), ("service_class", ptr), ("version", ptr),
                    ("comment", ptr), ("namespace", dword), ("provider", ptr),
                    ("context", wintypes.LPWSTR), ("protocol_count", dword), ("protocols", ptr),
                    ("query", ptr), ("addr_count", dword), ("addresses", ptr),
                    ("flags", dword), ("blob", ctypes.POINTER(Blob))]

    ws = ctypes.WinDLL("Ws2_32")
    ws.WSAAddressToStringW.argtypes = [ptr, dword, ptr, wintypes.LPWSTR, ctypes.POINTER(dword)]
    ws.WSALookupServiceBeginW.argtypes = [ctypes.POINTER(Query), dword, ctypes.POINTER(wintypes.HANDLE)]
    ws.WSALookupServiceNextW.argtypes = [wintypes.HANDLE, dword, ctypes.POINTER(dword), ptr]
    ws.WSALookupServiceEnd.argtypes = [wintypes.HANDLE]
    # Windows SDK ws2bth.h uses one-byte packing for SOCKADDR_BTH.
    sockaddr = ctypes.create_string_buffer(struct.pack("<HQ16sI", 32, address, bytes(16), 0))
    context = ctypes.create_unicode_buffer(128)
    chars = dword(len(context))
    if ws.WSAAddressToStringW(sockaddr, 30, None, context, ctypes.byref(chars)):
        raise ctypes.WinError(ws.WSAGetLastError())
    service_class = ctypes.create_string_buffer(uuid.UUID("0000110a-0000-1000-8000-00805f9b34fb").bytes_le)
    q = Query(size=ctypes.sizeof(Query), namespace=16, context=context.value,
              service_class=ctypes.addressof(service_class))
    handle = wintypes.HANDLE()
    if ws.WSALookupServiceBeginW(ctypes.byref(q), 0x1000 | 0x200, ctypes.byref(handle)):
        raise ctypes.WinError(ws.WSAGetLastError())
    sizes = []
    try:
        # LUP_RETURN_BLOB yields full SDP records for the exact service class.
        for _ in range(16):
            buf = ctypes.create_string_buffer(65536)
            size = dword(len(buf))
            reply = Query.from_buffer(buf)
            reply.size = ctypes.sizeof(Query)
            if ws.WSALookupServiceNextW(handle, 0x200, ctypes.byref(size), buf):
                error = ws.WSAGetLastError()
                if error in (10110, 10102):  # WSA_E_NO_MORE / WSAENOMORE
                    break
                raise ctypes.WinError(error)
            start = ctypes.addressof(buf)
            end = start + min(size.value, len(buf))
            blob = ctypes.cast(reply.blob, ptr).value
            if not blob or not start <= blob <= end - ctypes.sizeof(Blob):
                raise ValueError("Missing audio source SDP record")
            b = reply.blob.contents
            if not b.data or not b.size or not start <= b.data <= end - b.size:
                raise ValueError("Invalid SDP record buffer")
            sizes.append(b.size)
        else:
            raise ValueError("Unexpected number of Audio Source records")
    finally:
        ws.WSALookupServiceEnd(handle)
    return {"service_class": "110a", "records": len(sizes), "record_bytes": sizes}


def ui_ready(console, marker, require_cycle):
    if marker not in console:
        return False
    if not require_cycle:
        return True
    states = re.findall(rb"Sending 'power state changed' event for local device - \{powerstate: ([01])\}", console)
    transitions = [state for index, state in enumerate(states) if not index or states[index - 1] != state]
    return transitions[-3:] == [b"1", b"0", b"1"]


async def main(args):
    addresses = {controller_from_console(path) for path in args.reference}
    if len(addresses) != 1 or len({p.resolve() for p in args.reference}) < 2:
        raise ValueError("Two independent boot captures must identify the same phone")
    address = addresses.pop()
    adapter = await BluetoothAdapter.get_default_async()
    if adapter is None or adapter.bluetooth_address == address:
        raise ValueError("Expected a distinct Windows Bluetooth radio")
    report = {"classic_supported": adapter.is_classic_supported, "le_supported": adapter.is_low_energy_supported}
    if args.preflight:
        print(json.dumps(report))
        return
    boot_log = args.run / "boot.log"
    # boot-exp-v can spend six minutes cooling in Android before boot.log exists.
    # The UI timeout counts only time in the new test boot.
    startup_deadline = time.monotonic() + 540
    while not boot_log.exists():
        if time.monotonic() >= startup_deadline:
            raise TimeoutError("iOS monitor did not start")
        await asyncio.sleep(1)
    deadline = time.monotonic() + args.marker_timeout
    marker = args.marker.encode("ascii")
    while not ui_ready(boot_log.read_bytes(), marker, args.require_cycle):
        if time.monotonic() >= deadline:
            raise TimeoutError("iOS UI test marker not reached")
        await asyncio.sleep(1)
    target = address.to_bytes(6, "big").hex(":").upper()
    report["started_utc"] = datetime.now(timezone.utc).isoformat()
    checks = [
        ("advertisement", lambda: BleakScanner.find_device_by_address(target, timeout=7), 12),
        ("gatt", lambda: query(address, False), 22),
        ("classic_inquiry_or_cache", lambda: asyncio.to_thread(classic_inquiry, address), 14),
        ("sdp", lambda: query(address, True), 22),
    ]
    if args.audio_source:
        checks.append(("audio_source_sdp", lambda: asyncio.to_thread(audio_source_sdp, address), 18))
    for key, action, timeout in checks:
        try:
            result = await asyncio.wait_for(action(), timeout)
            report[key] = (result is not None) if key == "advertisement" else result
        except Exception as error:
            report[key] = {"error": type(error).__name__, "winerror": getattr(error, "winerror", None)}
        report["updated_utc"] = datetime.now(timezone.utc).isoformat()
        (args.run / "windows-peer.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, nargs=2, required=True)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--marker", default="FUXI TAPS: done,")
    parser.add_argument("--marker-timeout", type=int, default=240)
    parser.add_argument("--require-cycle", action="store_true", help="Wait for iOS Bluetooth power on/off/on")
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--audio-source", action="store_true", help="Query the phone's A2DP Audio Source service class")
    asyncio.run(main(parser.parse_args()))
