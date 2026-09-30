#!/usr/bin/env python3
r"""
Test application for FX2G3 USB-QSPI bridge (Windows-only).

This script uses the Windows SetupAPI and WinUSB APIs via ctypes to:
- locate the device interface path for a VID/PID
- open the device with CreateFile and initialize WinUSB
- send START_WRITE (0xB0) on EP1 OUT
- submit N overlapped WinUsb_WritePipe transfers concurrently to EP2 OUT
- wait for completions and measure throughput
- send STOP_WRITE (0xB1) on EP1 OUT to end the test.

Requirements: Windows host
"""

import ctypes
import ctypes.wintypes as wintypes
# Define ULONG_PTR for portability
if hasattr(wintypes, 'ULONG_PTR'):
    ULONG_PTR = wintypes.ULONG_PTR
else:
    # pointer-sized unsigned integer
    ULONG_PTR = ctypes.c_size_t
import sys
import time
import argparse
import json
import logging
from datetime import datetime

# Vendor commands and endpoints (matches project)
CMD_START_WRITE = 0xB0
CMD_STOP_WRITE = 0xB1
EP_CMD = 0x01
EP_DATA_OUT = 0x02
CMD_START_READ = 0xB2
CMD_STOP_READ = 0xB3
CMD_SET_CLK_FREQ = 0xC0
EP_DATA_IN = 0x83

# Limits and defaults
USB_MAX_PACKET = 512            # HS packet size from descriptors
MAX_XFER_SIZE = 4 * 1024 * 1024 # 4 MB per transfer (safety cap)
MAX_QUEUE_DEPTH = 64            # Windows WaitForMultipleObjects limit for event handles
MAX_OUTSTANDING_BYTES = 256 * 1024 * 1024  # 256 MB
MAX_TIMEOUT = 30 * 1000  # 30 seconds default timeout in ms

SCRIPT_NAME = 'qspi_bridge_test_app'
VERSION = '1.0'
COLS = 80  # maximum display width for boxes and lines

def _truncate(s, width):
    """Truncate string s to at most width characters, adding ellipsis if needed."""
    if s is None:
        return ''
    s = str(s)
    if len(s) <= width:
        return s
    if width <= 3:
        return s[:width]
    return s[:width-3] + '...'


def human_bytes(n):
    # return human readable bytes
    for unit in ['B', 'KB', 'MB', 'GB']:
        if n < 1024.0:
            return f"{n:.2f} {unit}"
        n /= 1024.0
    return f"{n:.2f} TB"


def print_banner():
    cols = COLS
    title = f" {SCRIPT_NAME} v{VERSION} "
    print('\n' + '=' * cols)
    print(title.center(cols))
    print(('Run date: ' + datetime.now().isoformat()).center(cols))
    print('=' * cols + '\n')


def print_params_box(opts, provided):
    # Pretty two-column parameter box with fixed width (no outer vertical bars)
    box_width = COLS
    title = ' RUN PARAMETERS '
    print('\n' + '-' * box_width)
    print(title.center(box_width))
    print('-' * box_width)

    def kv(k, v):
        return (str(k), str(v))

    # Build rows by checking which options the user provided (provided dict uses sys.argv)
    keys = ['test', 'clk-mhz', 'chart-min-freq', 'chart-max-freq', 'bus-width', 'size', 'xfer-size', 'queue-depth', 'timeout', 'vid', 'pid']
    def fmt(k):
        if k == 'test':
            return opts['test']
        if k == 'clk-mhz':
            return f"{opts['clk-mhz']} MHz"
        if k == 'chart-min-freq':
            return f"{opts['chart-min-freq']} MHz"
        if k == 'chart-max-freq':
            return f"{opts['chart-max-freq']} MHz"
        if k == 'bus-width':
            return str(opts['bus-width'])
        if k == 'size':
            return f"{opts['size']:.2f} MB"
        if k == 'xfer-size':
            xs = int(opts['xfer-size'])
            mb = xs / (1024.0 * 1024.0)
            return f"{xs} bytes ({mb:.2f} MB)"
        if k == 'queue-depth':
            return str(opts['queue-depth'])
        if k == 'timeout':
            return f"{opts['timeout']} ms"
        if k == 'vid':
            return f"0x{opts['vid']:04x}"
        if k == 'pid':
            return f"0x{opts['pid']:04x}"
        return ''

    user_rows = []
    default_rows = []
    for k in keys:
        if provided.get(k):
            user_rows.append(kv(k, fmt(k)))
        else:
            default_rows.append(kv(k, fmt(k)))

    sep = ' | '
    sep_w = len(sep)

    if user_rows and default_rows:
        left_w = (box_width - sep_w) // 2
        right_w = box_width - sep_w - left_w
        # compute key width (max key length) per column, cap sensibly
        left_key_w = min(max((len(k) for k, _ in user_rows), default=0) + 1, left_w // 2)
        right_key_w = min(max((len(k) for k, _ in default_rows), default=0) + 1, right_w // 2)
        # header
        left_h = _truncate('USER-SPECIFIED', left_w).center(left_w)
        right_h = _truncate('DEFAULTS', right_w).center(right_w)
        print(f"{left_h}{sep}{right_h}")
        print('-' * box_width)
        rows = max(len(user_rows), len(default_rows))
        for i in range(rows):
            lk, lv = user_rows[i] if i < len(user_rows) else ('', '')
            rk, rv = default_rows[i] if i < len(default_rows) else ('', '')
            # format: key left, value right
            left_val_w = left_w - left_key_w - 1
            right_val_w = right_w - right_key_w - 1
            left_fmt = f"{lk.ljust(left_key_w)} {str(lv).rjust(left_val_w)}"
            right_fmt = f"{rk.ljust(right_key_w)} {str(rv).rjust(right_val_w)}"
            print(f"{_truncate(left_fmt, left_w)}{sep}{_truncate(right_fmt, right_w)}")
    else:
        rows = user_rows if user_rows else default_rows
        header = 'USER-SPECIFIED' if user_rows else 'DEFAULTS'
        print(header.center(box_width))
        print('-' * box_width)
        for k, v in rows:
            # key left, value right
            key_w = min(len(k) + 1, box_width // 3)
            val_w = box_width - key_w - 1
            line = f"{k.ljust(key_w)} {str(v).rjust(val_w)}"
            print(_truncate(line, box_width).center(box_width))

    print('-' * box_width + '\n')


def print_summary_box(summary):
    cols = COLS
    print('\n' + '-' * cols)
    title = 'TRANSFER SUMMARY'
    print(title.center(cols))
    print('-' * cols)

    # pretty key/value with alignment
    def kv(key, val):
        k = f"{key}:".ljust(18)
        v = str(val)
        print(f"{k} {v}")

    # Format numeric fields with separators where useful
    total_bytes = int(summary.get('bytes', 0))
    packets = int(summary.get('packets', 0))
    elapsed = float(summary.get('time', 0.0))
    throughput = float(summary.get('throughput', 0.0))
    xs = int(summary.get('xfer_size', 0))
    xsm = xs / (1024.0 * 1024.0)

    kv('Test', summary.get('test'))
    if summary.get('test') == 'set-clk':
        device_path = summary.get('device_path', '(unknown)')
        kv('Device', device_path)
        kv('Frequency', f"{summary.get('frequency_mhz', 0)} MHz")
        status = summary.get('status')
        if status:
            kv('Status', status)
        errors = summary.get('errors')
        if errors:
            kv('Errors', errors)
        print('=' * cols + '\n')
        return
    kv('Data', f"{total_bytes:,} bytes ({total_bytes/(1024*1024):.2f} MB) in {packets:,} packets")
    kv('Elapsed', f"{elapsed:.3f} s")
    kv('Throughput', f"{throughput:.3f} MB/s")
    kv('xfer-size', f"{xs:,} bytes ({xsm:.2f} MB)")
    kv('queue-depth', f"{int(summary.get('queue_depth', 0))}")
    kv('timeout', f"{int(summary.get('timeout', 0))} ms")
    if summary.get('errors'):
        kv('Errors', summary.get('errors'))

    print('=' * cols + '\n')


def print_execution_plan(opts):
    """Print a compact execution plan that explains how total size, xfer-size and queue-depth interact."""
    box = COLS
    print('\n' + '-' * box)
    title = 'EXECUTION PLAN'
    print(title.center(box))
    print('-' * box)

    total_bytes = int(opts.get('size', 0) * 1024 * 1024)
    xfer = int(opts.get('xfer-size', 0))
    qd = int(opts.get('queue-depth', 0))

    # transfers required (ceiling)
    transfers = (total_bytes + xfer - 1) // xfer if xfer > 0 else 0
    initial_submissions = min(transfers, qd)
    outstanding_bytes = xfer * qd

    rows = [
        ('Total requested', f"{total_bytes:,} bytes ({total_bytes/(1024*1024):.2f} MB)"),
        ('Per-transfer (xfer-size)', f"{xfer:,} bytes ({xfer/(1024*1024):.2f} MB)"),
        ('Queue depth', f"{qd:,}"),
        ('Transfers required', f"{transfers:,} (ceiling(total/xfer))"),
        ('Initial concurrent submissions', f"{initial_submissions:,} (min(transfers, queue-depth))"),
        ('Potential outstanding memory', f"{outstanding_bytes:,} bytes ({outstanding_bytes/(1024*1024):.2f} MB)"),
    ]

    sep = ' | '
    left_w = (box - len(sep)) // 2
    right_w = box - len(sep) - left_w
    left_h = _truncate('ITEM', left_w).center(left_w)
    right_h = _truncate('VALUE', right_w).center(right_w)
    print(f"{left_h}{sep}{right_h}")
    print('-' * box)
    for k, v in rows:
        # Prepare fixed-width cells to ensure separator is exactly between columns
        left_cell = _truncate(k, left_w).ljust(left_w)
        right_cell = _truncate(v, right_w).rjust(right_w)
        print(f"{left_cell}{sep}{right_cell}")

    print('-' * box + '\n')


def write_json_summary(path, summary):
    try:
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2, default=str)
        print(f'Wrote JSON summary to {path}')
    except Exception as e:
        print('Failed to write JSON summary:', e)


# GUID_DEVINTERFACE_USB_DEVICE = {A5DCBF10-6530-11D2-901F-00C04FB951ED}
GUID_DEVINTERFACE_USB_DEVICE = (ctypes.c_ubyte * 16)(
    0x10, 0xBF, 0xDC, 0xA5, 0x30, 0x65, 0xD2, 0x11, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED
)

kernel32 = ctypes.windll.kernel32
setupapi = ctypes.windll.setupapi
winusb = ctypes.windll.winusb


def format_last_error(err_code=None):
    """Return a readable message for a Windows error code using FormatMessageW."""
    if err_code is None:
        err_code = kernel32.GetLastError()
    FORMAT_MESSAGE_FROM_SYSTEM = 0x00001000
    FORMAT_MESSAGE_IGNORE_INSERTS = 0x00000200
    buf = wintypes.LPWSTR()
    flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS
    num = kernel32.FormatMessageW(flags, None, err_code, 0, ctypes.byref(buf), 0, None)
    if num == 0:
        return f'Error {err_code}'
    try:
        msg = buf.value
        # strip trailing CRLF
        return msg.rstrip('\r\n')
    finally:
        # LocalFree the buffer if necessary
        try:
            kernel32.LocalFree(buf)
        except Exception:
            pass

# Constants
DIGCF_PRESENT = 0x02
DIGCF_DEVICEINTERFACE = 0x10
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [
        ('cbSize', wintypes.DWORD),
        ('InterfaceClassGuid', ctypes.c_byte * 16),
        ('Flags', wintypes.DWORD),
        ('Reserved', ctypes.POINTER(wintypes.ULONG)),
    ]


class SP_DEVICE_INTERFACE_DETAIL_DATA_W(ctypes.Structure):
    _fields_ = [
        ('cbSize', wintypes.DWORD),
        ('DevicePath', ctypes.c_wchar * 260),
    ]


class OVERLAPPED(ctypes.Structure):
    _fields_ = [
        ('Internal', ULONG_PTR),
        ('InternalHigh', ULONG_PTR),
        ('Offset', wintypes.DWORD),
        ('OffsetHigh', wintypes.DWORD),
        ('hEvent', wintypes.HANDLE),
    ]


WinUsb_WritePipe = winusb.WinUsb_WritePipe
WinUsb_WritePipe.argtypes = [
    wintypes.HANDLE,
    ctypes.c_ubyte,
    ctypes.c_void_p,
    ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(OVERLAPPED),
]
WinUsb_WritePipe.restype = wintypes.BOOL

WinUsb_ReadPipe = winusb.WinUsb_ReadPipe
WinUsb_ReadPipe.argtypes = [
    wintypes.HANDLE,
    ctypes.c_ubyte,
    ctypes.c_void_p,
    ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(OVERLAPPED),
]
WinUsb_ReadPipe.restype = wintypes.BOOL

WinUsb_GetOverlappedResult = winusb.WinUsb_GetOverlappedResult
WinUsb_GetOverlappedResult.argtypes = [
    wintypes.HANDLE,
    ctypes.POINTER(OVERLAPPED),
    ctypes.POINTER(wintypes.ULONG),
    wintypes.BOOL,
]
WinUsb_GetOverlappedResult.restype = wintypes.BOOL

ERROR_IO_PENDING = 997


def find_device_interface_path(vid, pid):
    """Find a device interface path string containing vid_xxxx&pid_yyyy"""
    # Delegate to the SetupAPI helper which handles GUID and enumeration correctly
    return _find_device_by_vid_pid_setupapi(vid, pid)


def _find_device_by_vid_pid_setupapi(vid, pid):
    # Use SetupDi APIs properly via ctypes
    class GUID(ctypes.Structure):
        _fields_ = [('Data1', wintypes.DWORD), ('Data2', wintypes.WORD), ('Data3', wintypes.WORD), ('Data4', ctypes.c_ubyte * 8)]

    # Use device-specific interface GUID from usb_descriptors.c (MS OS feature):
    # {01234567-2A4F-49EE-8DD3-FADEA377234A}
    guid = GUID()
    guid.Data1 = 0x01234567
    guid.Data2 = 0x2A4F
    guid.Data3 = 0x49EE
    data4 = (ctypes.c_ubyte * 8)(0x8D, 0xD3, 0xFA, 0xDE, 0xA3, 0x77, 0x23, 0x4A)
    guid.Data4 = data4

    SetupDiGetClassDevs = setupapi.SetupDiGetClassDevsW
    SetupDiEnumDeviceInterfaces = setupapi.SetupDiEnumDeviceInterfaces
    SetupDiGetDeviceInterfaceDetail = setupapi.SetupDiGetDeviceInterfaceDetailW
    SetupDiDestroyDeviceInfoList = setupapi.SetupDiDestroyDeviceInfoList

    # Define argtypes/restype for SetupAPI functions
    SetupDiGetClassDevs.argtypes = [ctypes.POINTER(GUID), wintypes.LPCWSTR, wintypes.HWND, wintypes.DWORD]
    SetupDiGetClassDevs.restype = ctypes.c_void_p

    SetupDiEnumDeviceInterfaces.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(GUID), wintypes.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
    SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL

    SetupDiGetDeviceInterfaceDetail.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.POINTER(SP_DEVICE_INTERFACE_DETAIL_DATA_W), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    SetupDiGetDeviceInterfaceDetail.restype = wintypes.BOOL

    SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
    SetupDiDestroyDeviceInfoList.restype = wintypes.BOOL

    hDevInfo = SetupDiGetClassDevs(ctypes.byref(guid), None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    if not hDevInfo:
        raise RuntimeError("SetupDiGetClassDevs failed")

    try:
        index = 0
        while True:
            interface_data = SP_DEVICE_INTERFACE_DATA()
            interface_data.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)
            res = SetupDiEnumDeviceInterfaces(hDevInfo, None, ctypes.byref(guid), index, ctypes.byref(interface_data))
            if not res:
                break

            # Get required size
            required_size = wintypes.DWORD()
            # call once to get required size
            SetupDiGetDeviceInterfaceDetail(hDevInfo, ctypes.byref(interface_data), None, 0, ctypes.byref(required_size), None)

            detail = SP_DEVICE_INTERFACE_DETAIL_DATA_W()
            # cbSize for SP_DEVICE_INTERFACE_DETAIL_DATA_W: 6 on x86, 8 on x64 (sizeof(DWORD)+sizeof(WCHAR) vs alignment)
            if ctypes.sizeof(ctypes.c_voidp) == 8:
                detail.cbSize = 8
            else:
                detail.cbSize = 6

            res2 = SetupDiGetDeviceInterfaceDetail(hDevInfo, ctypes.byref(interface_data), ctypes.byref(detail), required_size, None, None)
            if not res2:
                index += 1
                continue

            path = detail.DevicePath
            p = path.lower()
            if f"vid_{vid:04x}" in p and f"pid_{pid:04x}" in p:
                return path

            index += 1

    finally:
        SetupDiDestroyDeviceInfoList(hDevInfo)

    return None


def create_overlapped_event():
    # Create manual-reset event
    return kernel32.CreateEventW(None, True, False, None)


def open_device_winusb(path):
    CreateFile = kernel32.CreateFileW
    CreateFile.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    CreateFile.restype = wintypes.HANDLE

    # Try to open with overlapped flag first
    handle = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, None)
    if handle == INVALID_HANDLE_VALUE or handle == 0:
        err = kernel32.GetLastError()
        logging.debug(f'CreateFile(overlapped) failed: GetLastError={err}')
        # Try fallback without overlapped to give more diagnostic info
        handle2 = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
        if handle2 == INVALID_HANDLE_VALUE or handle2 == 0:
            err2 = kernel32.GetLastError()
            logging.debug(f'CreateFile(fallback) failed: GetLastError={err2}')
            raise RuntimeError(f'CreateFile failed (overlapped err={err}, fallback err={err2})')
        else:
            logging.debug('Opened device without FILE_FLAG_OVERLAPPED; overlapped I/O may not be supported by this handle')
            handle = handle2

    # Initialize WinUSB
    winusb_handle = wintypes.HANDLE()
    ok = winusb.WinUsb_Initialize(handle, ctypes.byref(winusb_handle))
    if not ok:
        err = kernel32.GetLastError()
        kernel32.CloseHandle(handle)
        logging.debug(f'WinUsb_Initialize failed: GetLastError={err}')
        raise RuntimeError('WinUsb_Initialize failed')

    return handle, winusb_handle


class WinUsbSession:
    """Persistent WinUSB session that keeps the device handle alive across operations."""

    def __init__(self, vid, pid):
        path = find_device_interface_path(vid, pid)
        if not path:
            raise RuntimeError('Device interface path not found')
        self.device_path = path
        self._handle, self._winusb_handle = open_device_winusb(path)
        self._closed = False

    @property
    def handle(self):
        return self._handle

    @property
    def winusb_handle(self):
        return self._winusb_handle

    def close(self):
        if self._closed:
            return
        try:
            if self._winusb_handle:
                winusb.WinUsb_Free(self._winusb_handle)
        finally:
            try:
                if self._handle:
                    kernel32.CloseHandle(self._handle)
            finally:
                self._closed = True
                self._winusb_handle = None
                self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


def winusb_write_blocking(winusb_handle, pipe_id, data, timeout_ms=1000):
    """Send data on a WinUSB pipe and wait until the transfer is fully completed."""
    payload = bytes(data)
    if not payload:
        raise ValueError('winusb_write_blocking requires non-empty payload')

    overlapped = OVERLAPPED()
    overlapped.hEvent = create_overlapped_event()
    if not overlapped.hEvent:
        err = kernel32.GetLastError()
        raise RuntimeError(f'CreateEventW failed: {format_last_error(err)} ({err})')

    buf = ctypes.create_string_buffer(payload)
    transferred = wintypes.ULONG(0)
    try:
        ok = WinUsb_WritePipe(
            winusb_handle,
            pipe_id,
            ctypes.cast(buf, ctypes.c_void_p),
            len(payload),
            ctypes.byref(transferred),
            ctypes.byref(overlapped),
        )
        if not ok:
            err = kernel32.GetLastError()
            if err != ERROR_IO_PENDING:
                raise RuntimeError(f'WinUsb_WritePipe failed: {format_last_error(err)} ({err})')

            wait_res = kernel32.WaitForSingleObject(overlapped.hEvent, timeout_ms)
            if wait_res == 0xFFFFFFFF:
                wait_err = kernel32.GetLastError()
                raise RuntimeError(f'WaitForSingleObject failed: {format_last_error(wait_err)} ({wait_err})')
            if wait_res == 0x00000102:  # WAIT_TIMEOUT
                raise TimeoutError(f'WinUSB write timed out after {timeout_ms} ms on pipe 0x{pipe_id:02X}')

            ok = WinUsb_GetOverlappedResult(winusb_handle, ctypes.byref(overlapped), ctypes.byref(transferred), False)
            if not ok:
                ov_err = kernel32.GetLastError()
                raise RuntimeError(f'GetOverlappedResult failed: {format_last_error(ov_err)} ({ov_err})')

        if transferred.value != len(payload):
            raise RuntimeError(
                f'Short write on pipe 0x{pipe_id:02X}: requested {len(payload)} bytes, got {transferred.value}'
            )
        return transferred.value
    finally:
        if overlapped.hEvent:
            kernel32.CloseHandle(overlapped.hEvent)


def winusb_write_overlapped(winusb_handle, pipe_id, buffer, overlapped_ptr):
    length_transferred = wintypes.ULONG(0)
    buf = ctypes.create_string_buffer(buffer)
    ok = WinUsb_WritePipe(
        winusb_handle,
        pipe_id,
        ctypes.cast(buf, ctypes.c_void_p),
        len(buffer),
        ctypes.byref(length_transferred),
        overlapped_ptr,
    )
    if not ok:
        err = kernel32.GetLastError()
        # ERROR_IO_PENDING (=997) is expected for overlapped, log the error
        logging.debug('WinUsb_WritePipe failed, error %d', err)
        if err != ERROR_IO_PENDING:
            raise RuntimeError(f'WinUsb_WritePipe failed, error {err}')
    return length_transferred.value, buf


def winusb_read_overlapped(winusb_handle, pipe_id, buf_len, overlapped_ptr):
    length_transferred = wintypes.ULONG(0)
    buf = ctypes.create_string_buffer(buf_len)
    ok = WinUsb_ReadPipe(
        winusb_handle,
        pipe_id,
        ctypes.cast(buf, ctypes.c_void_p),
        buf_len,
        ctypes.byref(length_transferred),
        overlapped_ptr,
    )
    if not ok:
        err = kernel32.GetLastError()
        # ERROR_IO_PENDING (=997) is expected for overlapped, log the error
        logging.debug('WinUsb_ReadPipe failed, error %d', err)
        if err != ERROR_IO_PENDING:
            raise RuntimeError(f'WinUsb_ReadPipe failed, error {err}')
    return buf, length_transferred.value


def run_overlapped_write(session, total_bytes, xfer_size, queue_depth, timeout):
    if session is None:
        print('WinUSB session not available')
        return

    handle = session.handle
    winusb_handle = session.winusb_handle
    path = session.device_path

    # send START_WRITE via WinUsb_WritePipe to EP_CMD
    # construct single-byte command
    cmdbuf = (ctypes.c_ubyte * 1)(CMD_START_WRITE)
    overlapped = OVERLAPPED()
    overlapped.hEvent = create_overlapped_event()
    try:
        # send start command synchronously
        WinUsb_WritePipeSync = winusb.WinUsb_WritePipe
        WinUsb_WritePipeSync.argtypes = [wintypes.HANDLE, ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(OVERLAPPED)]
        len_written = ctypes.c_ulong(0)
        ok = WinUsb_WritePipeSync(winusb_handle, EP_CMD, ctypes.cast(cmdbuf, ctypes.c_void_p), 1, ctypes.byref(len_written), None)
        if not ok:
            print('Failed to send START_WRITE')
            return

        # prepare buffers
        pattern = bytes([i & 0xFF for i in range(256)])
        packet = (pattern * (xfer_size // 256))[:xfer_size]

        outstanding = []  # list of tuples (OVERLAPPED, ctypes_buffer)
        bytes_enqueued = 0
        bytes_completed = 0
        packets_completed = 0
        start_time = time.time()

        # submit initial queue
        for i in range(queue_depth):
            if bytes_enqueued >= total_bytes:
                break
            remaining = total_bytes - bytes_enqueued
            chunk = packet if remaining >= xfer_size else packet[:remaining]
            ov = OVERLAPPED()
            ov.hEvent = create_overlapped_event()
            try:
                _, buf = winusb_write_overlapped(winusb_handle, EP_DATA_OUT, chunk, ctypes.byref(ov))
            except RuntimeError as e:
                print('Write submit error:', e)
                break
            outstanding.append((ov, buf))
            bytes_enqueued += len(chunk)

        # loop: wait for events and resubmit until all bytes sent
        while outstanding:
            # collect event handles
            events = (wintypes.HANDLE * len(outstanding))(
                *[entry[0].hEvent for entry in outstanding]
            )

            WAIT_OBJECT_0 = 0
            res = kernel32.WaitForMultipleObjects(len(outstanding), events, False, timeout)
            if res == 0xFFFFFFFF:
                err = kernel32.GetLastError()
                print('WaitForMultipleObjects failed', err, format_last_error(err))
                break

            signaled_index = res - WAIT_OBJECT_0
            if signaled_index < 0 or signaled_index >= len(outstanding):
                # timeout or unexpected
                continue

            ov, buf = outstanding.pop(signaled_index)
            # retrieve result via WinUSB helper
            transferred = wintypes.ULONG(0)
            ok = WinUsb_GetOverlappedResult(winusb_handle, ctypes.byref(ov), ctypes.byref(transferred), False)
            if not ok:
                err = kernel32.GetLastError()
                logging.debug('GetOverlappedResult failed %d', err)
            else:
                bytes_completed += transferred.value
                packets_completed += 1
                # a slot freed, submit next chunk if any
                if bytes_enqueued < total_bytes:
                    remaining = total_bytes - bytes_enqueued
                    chunk = packet if remaining >= xfer_size else packet[:remaining]
                    newov = OVERLAPPED()
                    newov.hEvent = create_overlapped_event()
                    try:
                        _, newbuf = winusb_write_overlapped(winusb_handle, EP_DATA_OUT, chunk, ctypes.byref(newov))
                        outstanding.append((newov, newbuf))
                        bytes_enqueued += len(chunk)
                    except RuntimeError as e:
                        print('Resubmit error', e)
                        # stop resubmitting
                        pass
            # close the event handle for the completed overlapped
            try:
                if ov.hEvent:
                    kernel32.CloseHandle(ov.hEvent)
            except Exception as e:
                logging.debug('CloseHandle(ev) failed: %s', e)

        end_time = time.time()
        elapsed = end_time - start_time
        mb = bytes_completed / (1024 * 1024)
        summary = {
            'test': 'write',
            'device_path': path,
            'bytes': bytes_completed,
            'packets': packets_completed,
            'time': elapsed,
            'throughput': mb/elapsed if elapsed > 0 else 0.0,
            'xfer_size': xfer_size,
            'queue_depth': queue_depth,
            'timeout': timeout,
            'errors': None,
        }
        return summary

    finally:
        # send STOP
        try:
            sbuf = (ctypes.c_ubyte * 1)(CMD_STOP_WRITE)
            tmp = ctypes.c_ulong(0)
            winusb.WinUsb_WritePipe(winusb_handle, EP_CMD, ctypes.cast(sbuf, ctypes.c_void_p), 1, ctypes.byref(tmp), None)
        except Exception:
            pass
        # Strong cleanup: cancel outstanding overlapped I/O, wait for completions, then close event handles
        try:
            if 'outstanding' in locals() and outstanding:
                # Issue CancelIoEx for each outstanding OVERLAPPED
                for ov2, _ in list(outstanding):
                    try:
                        ok_cancel = kernel32.CancelIoEx(handle, ctypes.byref(ov2))
                        if not ok_cancel:
                            logging.debug('CancelIoEx returned false for ov=%s, err=%d', ov2, kernel32.GetLastError())
                    except Exception:
                        pass

                # Collect remaining event handles and wait for them (bounded)
                try:
                    events = [ov2.hEvent for ov2, _ in outstanding if ov2.hEvent]
                    if events:
                        evt_arr = (wintypes.HANDLE * len(events))(*events)
                        WAIT_TIMEOUT_MS = 5000
                        res = kernel32.WaitForMultipleObjects(len(events), evt_arr, True, WAIT_TIMEOUT_MS)
                        logging.debug('WaitForMultipleObjects(res)=%s', res)
                except Exception:
                    pass

                # Close event handles
                for ov2, _ in list(outstanding):
                    try:
                        if ov2.hEvent:
                            kernel32.CloseHandle(ov2.hEvent)
                    except Exception:
                        pass
        except Exception:
            pass


def run_overlapped_read(session, total_bytes, xfer_size, queue_depth, timeout):
    if session is None:
        print('WinUSB session not available')
        return

    handle = session.handle
    winusb_handle = session.winusb_handle
    path = session.device_path

    # send START_READ via WinUsb_WritePipe to EP_CMD
    cmdbuf = (ctypes.c_ubyte * 1)(CMD_START_READ)
    try:
        len_written = ctypes.c_ulong(0)
        ok = winusb.WinUsb_WritePipe(winusb_handle, EP_CMD, ctypes.cast(cmdbuf, ctypes.c_void_p), 1, ctypes.byref(len_written), None)
        if not ok:
            print('Failed to send START_READ')
            return

        # prepare local buffers
        outstanding = []  # list of tuples (OVERLAPPED, ctypes_buffer, requested_len)
        bytes_recv = 0
        bytes_queued = 0  # bytes we've already requested/submitted
        packets_completed = 0
        start_time = time.time()

        # submit initial queue
        for i in range(queue_depth):
            if bytes_queued >= total_bytes:
                break
            # request size: cap to remaining bytes we haven't requested yet
            remaining_to_request = total_bytes - bytes_queued
            req = xfer_size if remaining_to_request >= xfer_size else remaining_to_request
            ov = OVERLAPPED()
            ov.hEvent = create_overlapped_event()
            try:
                buf, _ = winusb_read_overlapped(winusb_handle, EP_DATA_IN, req, ctypes.byref(ov))
            except RuntimeError as e:
                print('Read submit error:', e)
                break
            outstanding.append((ov, buf, req))
            bytes_queued += req

        # loop: wait for events and resubmit until all bytes received
        while outstanding:
            events = (wintypes.HANDLE * len(outstanding))(
                *[entry[0].hEvent for entry in outstanding]
            )

            WAIT_OBJECT_0 = 0
            res = kernel32.WaitForMultipleObjects(len(outstanding), events, False, timeout)
            if res == 0xFFFFFFFF:
                print('WaitForMultipleObjects failed')
                break

            signaled_index = res - WAIT_OBJECT_0
            if signaled_index < 0 or signaled_index >= len(outstanding):
                # timeout or unexpected
                continue

            ov, buf, buflen = outstanding.pop(signaled_index)
            transferred = wintypes.ULONG(0)
            ok = WinUsb_GetOverlappedResult(winusb_handle, ctypes.byref(ov), ctypes.byref(transferred), False)
            if not ok:
                err = kernel32.GetLastError()
                print('GetOverlappedResult failed', err, format_last_error(err))
            else:
                bytes_recv += transferred.value
                packets_completed += 1
                # resubmit if we haven't already requested all bytes
                if bytes_queued < total_bytes:
                    remaining_to_request = total_bytes - bytes_queued
                    req = xfer_size if remaining_to_request >= xfer_size else remaining_to_request
                    newov = OVERLAPPED()
                    newov.hEvent = create_overlapped_event()
                    try:
                        newbuf, _ = winusb_read_overlapped(winusb_handle, EP_DATA_IN, req, ctypes.byref(newov))
                        outstanding.append((newov, newbuf, req))
                        bytes_queued += req
                    except RuntimeError as e:
                        print('Resubmit error', e)
                        pass
            # close the event handle for the completed overlapped
            try:
                if ov.hEvent:
                    kernel32.CloseHandle(ov.hEvent)
            except Exception:
                pass

        end_time = time.time()
        elapsed = end_time - start_time
        mb = bytes_recv / (1024 * 1024)
        summary = {
            'test': 'read',
            'device_path': path,
            'bytes': bytes_recv,
            'packets': packets_completed,
            'time': elapsed,
            'throughput': mb/elapsed if elapsed > 0 else 0.0,
            'xfer_size': xfer_size,
            'queue_depth': queue_depth,
            'timeout': timeout,
            'errors': None,
        }
        return summary

    finally:
        # send STOP
        try:
            sbuf = (ctypes.c_ubyte * 1)(CMD_STOP_READ)
            tmp = ctypes.c_ulong(0)
            winusb.WinUsb_WritePipe(winusb_handle, EP_CMD, ctypes.cast(sbuf, ctypes.c_void_p), 1, ctypes.byref(tmp), None)
        except Exception:
            pass
        # Strong cleanup: cancel outstanding overlapped I/O, wait for completions, then close event handles
        try:
            if 'outstanding' in locals() and outstanding:
                for ov2, _, _ in list(outstanding):
                    try:
                        kernel32.CancelIoEx(handle, ctypes.byref(ov2))
                    except Exception:
                        pass
                try:
                    events = [ov2.hEvent for ov2, _, _ in outstanding if ov2.hEvent]
                    if events:
                        evt_arr = (wintypes.HANDLE * len(events))(*events)
                        WAIT_TIMEOUT_MS = 5000
                        res = kernel32.WaitForMultipleObjects(len(events), evt_arr, True, WAIT_TIMEOUT_MS)
                except Exception:
                    pass
                for ov2, _, _ in list(outstanding):
                    try:
                        if ov2.hEvent:
                            kernel32.CloseHandle(ov2.hEvent)
                    except Exception:
                        pass
        except Exception:
            pass


def run_set_clock_frequency(session, frequency_mhz):
    if session is None:
        print('WinUSB session not available')
        return

    winusb_handle = session.winusb_handle
    path = session.device_path

    status = 'OK'
    errors = None
    cmd_timeout_ms = 2000

    try:
        # Send command byte followed by frequency byte on EP1 (command endpoint)
        
        freq_value = frequency_mhz & 0xFF
        cmd_packet = bytes([CMD_SET_CLK_FREQ, freq_value])
        winusb_write_blocking(winusb_handle, EP_CMD, cmd_packet, timeout_ms=cmd_timeout_ms)
    except Exception as exc:
        status = 'FAILED'
        errors = f'SET_SMIF_CLK_FREQ command failed: {exc}'
        return {
            'test': 'set-clk',
            'device_path': path,
            'frequency_mhz': frequency_mhz,
            'status': status,
            'errors': errors,
        }

    return {
        'test': 'set-clk',
        'device_path': path,
        'frequency_mhz': frequency_mhz,
        'status': status,
        'errors': errors,
    }


def run_performance_chart(session, size_mb, xfer_size, queue_depth, timeout, min_freq=11, max_freq=39, bus_width=4):
    """Run comprehensive performance chart by testing multiple clock frequencies."""
    print('\n' + '=' * COLS)
    title = f' PERFORMANCE CHART: {min_freq}-{max_freq} MHz '
    print(title.center(COLS))
    print('=' * COLS + '\n')
    
    # Initialize results table
    results = []
    
    # Table header
    header = f"{'Freq (MHz)':>10} {'BusWidth':>10} {'Write Rate (Mbps)':>18} {'Read Rate (Mbps)':>17} {'Max Rate Possible (Mbps)':>23} {'Write Achievement (%)':>21} {'Read Achievement (%)':>20}"
    print(header)
    print('-' * len(header))
    
    total_bytes = int(size_mb * 1024 * 1024)
    
    for freq in range(min_freq, max_freq + 1):
        # Set clock frequency immediately before the test run.
        clk_result = run_set_clock_frequency(session, freq)
        if not clk_result or clk_result.get('status') != 'OK':
            print(f"Failed to set clock to {freq} MHz, skipping...")
            continue
        
        # Small delay to allow clock to stabilize before streaming.
        time.sleep(0.1)
        
        # Run write test
        write_result = run_overlapped_write(session, total_bytes, xfer_size, queue_depth, timeout)
        write_mbps = 0.0
        if write_result and write_result.get('throughput'):
            write_mbps = write_result['throughput'] * 8  # Convert MB/s to Mbps
        
        # Small delay between tests to allow firmware to settle.
        time.sleep(0.1)
        
        # Run read test
        read_result = run_overlapped_read(session, total_bytes, xfer_size, queue_depth, timeout)
        read_mbps = 0.0
        if read_result and read_result.get('throughput'):
            read_mbps = read_result['throughput'] * 8  # Convert MB/s to Mbps
        
        # Calculate theoretical maximum (freq * bus_width)
        max_possible_mbps = freq * bus_width
        
        # Calculate achievement percentages
        write_achievement = (write_mbps / max_possible_mbps * 100) if max_possible_mbps > 0 else 0.0
        read_achievement = (read_mbps / max_possible_mbps * 100) if max_possible_mbps > 0 else 0.0
        
        # Store result
        result_row = {
            'frequency_mhz': freq,
            'bus_width': bus_width,
            'write_rate_mbps': write_mbps,
            'read_rate_mbps': read_mbps,
            'max_rate_possible_mbps': max_possible_mbps,
            'write_achievement_pct': write_achievement,
            'read_achievement_pct': read_achievement
        }
        results.append(result_row)
        
        # Print row in table format
        row = f"{freq:>10} {bus_width:>10} {write_mbps:>18.2f} {read_mbps:>17.2f} {max_possible_mbps:>23.2f} {write_achievement:>21.2f} {read_achievement:>20.2f}"
        print(row)
    
    print('-' * len(header))
    print(f"\nPerformance chart completed. Tested {len(results)} frequencies from {min_freq} to {max_freq} MHz.")
    
    return results
def write_chart_csv(results, filename):
    """Write performance chart results to CSV file."""
    try:
        import csv
        with open(filename, 'w', newline='', encoding='utf-8') as csvfile:
            fieldnames = ['Frequency (MHz)', 'BusWidth', 'Write Rate (Mbps)', 'Read Rate (Mbps)', 
                         'Max Rate Possible (Mbps)', 'Write achievement (%)', 'Read achievement (%)']
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            
            writer.writeheader()
            for result in results:
                writer.writerow({
                    'Frequency (MHz)': result['frequency_mhz'],
                    'BusWidth': result['bus_width'],
                    'Write Rate (Mbps)': f"{result['write_rate_mbps']:.2f}",
                    'Read Rate (Mbps)': f"{result['read_rate_mbps']:.2f}",
                    'Max Rate Possible (Mbps)': f"{result['max_rate_possible_mbps']:.2f}",
                    'Write achievement (%)': f"{result['write_achievement_pct']:.2f}",
                    'Read achievement (%)': f"{result['read_achievement_pct']:.2f}"
                })
        print(f"Performance chart data exported to: {filename}")
        return True
    except Exception as e:
        print(f"Failed to write CSV file: {e}")
        return False


def main():
    # Use ArgumentDefaultsHelpFormatter so default values (vid/pid etc.) are shown in help,
    # but with width limited to COLS for neat wrapping.
    from functools import partial
    parser = argparse.ArgumentParser(
        description='FX2G3 QSPI Bridge Test Application\n\rExample usage (PowerShell):\n\t.venv\\Scripts\\python.exe .\\qspi_bridge_test_app.py --test read --size 100\n\t.venv\\Scripts\\python.exe .\\qspi_bridge_test_app.py --test write --size 100',
        formatter_class=partial(argparse.ArgumentDefaultsHelpFormatter, width=COLS),
    )
    parser.add_argument('--vid', type=lambda x: int(x, 0), default=0x04B4,
                        help=f"Vendor ID (hex or decimal). Example: 0x04b4. Default: 0x{0x04B4:04x}")
    parser.add_argument('--pid', type=lambda x: int(x, 0), default=0x490C,
                        help=f"Product ID (hex or decimal). Example: 0x490C. Default: 0x{0x490C:04x}")
    parser.add_argument('--size', type=float, default=100.0, help='MB (total amount to transfer)')
    parser.add_argument('--xfer-size', type=int, default=MAX_XFER_SIZE, help=f'Transfer size per overlapped operation in bytes (must be a multiple of USB max-packet-size). Defaults to {MAX_XFER_SIZE}.')
    parser.add_argument('--queue-depth', type=int, default=MAX_QUEUE_DEPTH, help=f'Number of concurrent overlapped transfers to submit (<= {MAX_QUEUE_DEPTH} for this script). Defaults to {MAX_QUEUE_DEPTH}.')
    parser.add_argument('--timeout', type=int, default=MAX_TIMEOUT, help=f'Wait timeout in milliseconds for overlapped completions. Defaults to {MAX_TIMEOUT} ms')
    parser.add_argument('--test', choices=['write', 'read', 'both', 'set-clk', 'chart'], default='write', help='Which test to run')
    parser.add_argument('--json-output', type=str, default=None, help='Write JSON summary to this path')
    parser.add_argument('--verbose', action='store_true', help='Enable verbose logging')
    parser.add_argument('--dry-run', action='store_true', help='Print formatted output and exit without touching device')
    parser.add_argument('--clk-mhz', type=int, default=24, help='QSPI clock frequency (MHz) to set when --test set-clk is used')
    parser.add_argument('--chart-min-freq', type=int, default=11, help='Minimum frequency for chart test (MHz)')
    parser.add_argument('--chart-max-freq', type=int, default=39, help='Maximum frequency for chart test (MHz)')
    parser.add_argument('--bus-width', type=int, default=4, help='Bus width for theoretical max calculation in chart test')
    parser.add_argument('--csv-output', type=str, default=None, help='Write chart results to CSV file (only for --test chart)')
    
    args = parser.parse_args()

    if sys.platform != 'win32':
        print('This script must be run on Windows')
        return 1


    total_bytes = int(args.size * 1024 * 1024)

    # If user omitted xfer-size or queue-depth, use maxima (we show which values are user-specified in the params box)
    used_xfer_size = args.xfer_size if args.xfer_size is not None else MAX_XFER_SIZE
    used_queue_depth = args.queue_depth if args.queue_depth is not None else MAX_QUEUE_DEPTH
    used_timeout = args.timeout if args.timeout is not None else MAX_TIMEOUT

    # summary of parameters
    def was_provided(option_name):
        for a in sys.argv[1:]:
            if a == f'--{option_name}' or a.startswith(f'--{option_name}='):
                return True
        return False

    opts = {
        'vid': args.vid,
        'pid': args.pid,
        'size': args.size,
        'xfer-size': used_xfer_size,
        'queue-depth': used_queue_depth,
        'timeout': used_timeout,
        'test': args.test,
        'clk-mhz': args.clk_mhz,
        'chart-min-freq': args.chart_min_freq,
        'chart-max-freq': args.chart_max_freq,
        'bus-width': args.bus_width,
    }

    provided = {k: was_provided(k.replace('_', '-')) for k in opts.keys()}

    # xfer-size must be positive, multiple of USB_MAX_PACKET, and not absurdly large
    if args.test != 'set-clk':
        if used_xfer_size <= 0:
            parser.error('--xfer-size must be > 0')
        if used_xfer_size % USB_MAX_PACKET != 0:
            parser.error(f'--xfer-size must be a multiple of USB max-packet-size ({USB_MAX_PACKET})')
        if used_xfer_size > MAX_XFER_SIZE:
            parser.error(f'--xfer-size too large (max {MAX_XFER_SIZE} bytes)')

    # queue-depth must be >=1 and <= MAX_QUEUE_DEPTH for this waiting model
    if args.test != 'set-clk':
        if used_queue_depth < 1 or used_queue_depth > MAX_QUEUE_DEPTH:
            parser.error(f'--queue-depth must be between 1 and {MAX_QUEUE_DEPTH} for this script (Windows WaitForMultipleObjects limit)')

    # Check outstanding memory not excessive
    # Validate timeout
    if used_timeout <= 0 or used_timeout > (5 * 60 * 1000):
        parser.error('--timeout must be >0 and <= 300000 ms (5 minutes)')

    if args.test != 'set-clk':
        outstanding_bytes = used_xfer_size * used_queue_depth
        if outstanding_bytes > MAX_OUTSTANDING_BYTES:
            parser.error(f'Outstanding memory ({outstanding_bytes} bytes) is > {MAX_OUTSTANDING_BYTES} bytes; reduce xfer-size or queue-depth')

    if args.clk_mhz < 11 or args.clk_mhz > 39:
        parser.error('--clk-mhz must be between 11 and 39 MHz (inclusive)')
    
    # Validate chart parameters
    if args.test == 'chart':
        if args.chart_min_freq < 11 or args.chart_min_freq > 39:
            parser.error('--chart-min-freq must be between 11 and 39 MHz (inclusive)')
        if args.chart_max_freq < 11 or args.chart_max_freq > 39:
            parser.error('--chart-max-freq must be between 11 and 39 MHz (inclusive)')
        if args.chart_min_freq > args.chart_max_freq:
            parser.error('--chart-min-freq must be <= --chart-max-freq')
        if args.bus_width < 1 or args.bus_width > 8:
            parser.error('--bus-width must be between 1 and 8')
    
    # Print banner and parameter box
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    print_banner()
    print_params_box(opts, provided)
    # Resolve and print device path preview (do not open device here) so the user sees which device will be used
    try:
        devpath = find_device_interface_path(opts['vid'], opts['pid'])
    except Exception:
        devpath = None
    print('Opening device:')
    if devpath:
        print(devpath)
    else:
        print('(device interface path not found)')
    # Show execution plan that explains how size, xfer-size and queue-depth interact
    if args.test == 'set-clk':
        print('\nClock set command will be issued with frequency {} MHz\n'.format(args.clk_mhz))
    elif args.test == 'chart':
        print(f'\nPerformance chart will test frequencies from {args.chart_min_freq} to {args.chart_max_freq} MHz')
        print(f'Each frequency will run write and read tests with {args.size} MB transfers\n')
    else:
        print_execution_plan(opts)

    # Dry-run: exercise display and validation without opening the device
    if args.dry_run:
        print('\nDry-run: no device operations will be performed. Exiting.')
        return 0

    session = None
    summaries = {}
    try:
        session = WinUsbSession(args.vid, args.pid)
    except Exception as exc:
        print(f'Failed to open WinUSB session: {exc}')
        return 1

    try:
        if args.test == 'set-clk':
            csum = run_set_clock_frequency(session, args.clk_mhz)
            if csum:
                print_summary_box(csum)
                summaries['set-clk'] = csum
        elif args.test == 'chart':
            chart_results = run_performance_chart(
                session, args.size, used_xfer_size, used_queue_depth, used_timeout,
                args.chart_min_freq, args.chart_max_freq, args.bus_width
            )
            summaries['chart'] = chart_results

            # Export to CSV if requested
            if args.csv_output and chart_results:
                write_chart_csv(chart_results, args.csv_output)
        else:
            # Run write then read (if requested). Print each summary separately so one doesn't overwrite the other.
            if args.test in ('write', 'both'):
                wsum = run_overlapped_write(session, total_bytes, used_xfer_size, used_queue_depth, used_timeout)
                if wsum:
                    print_summary_box(wsum)
                    summaries['write'] = wsum
            if args.test in ('read', 'both'):
                rsum = run_overlapped_read(session, total_bytes, used_xfer_size, used_queue_depth, used_timeout)
                if rsum:
                    print_summary_box(rsum)
                    summaries['read'] = rsum
    finally:
        if session:
            session.close()

    # Write JSON summary: if both ran, write a dict with keys 'write'/'read', else write the single summary
    if args.json_output and summaries:
        if len(summaries) == 1:
            write_json_summary(args.json_output, list(summaries.values())[0])
        else:
            write_json_summary(args.json_output, summaries)
    return 0


if __name__ == '__main__':
    sys.exit(main())
