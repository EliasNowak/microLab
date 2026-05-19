from ctypes import CDLL, POINTER, byref, c_int, c_uint16, c_void_p
from ctypes.util import find_library
from time import sleep

from SCons.Script import COMMAND_LINE_TARGETS


STLINK_VID = 0x0483
STLINK_PID = 0x374B
LIBUSB_BUSY = -6
STLINK_CONFIGURATION = 1


def configure_stlink():
    libusb_path = find_library("usb-1.0")
    if libusb_path is None:
        print("ST-Link USB check skipped: libusb-1.0 not found")
        return

    libusb = CDLL(libusb_path)
    libusb.libusb_init.argtypes = [POINTER(c_void_p)]
    libusb.libusb_init.restype = c_int
    libusb.libusb_exit.argtypes = [c_void_p]
    libusb.libusb_open_device_with_vid_pid.argtypes = [
        c_void_p,
        c_uint16,
        c_uint16,
    ]
    libusb.libusb_open_device_with_vid_pid.restype = c_void_p
    libusb.libusb_get_configuration.argtypes = [c_void_p, POINTER(c_int)]
    libusb.libusb_get_configuration.restype = c_int
    libusb.libusb_set_configuration.argtypes = [c_void_p, c_int]
    libusb.libusb_set_configuration.restype = c_int
    libusb.libusb_reset_device.argtypes = [c_void_p]
    libusb.libusb_reset_device.restype = c_int
    libusb.libusb_close.argtypes = [c_void_p]

    ctx = c_void_p()
    rc = libusb.libusb_init(byref(ctx))
    if rc != 0:
        print(f"ST-Link USB check skipped: libusb_init failed ({rc})")
        return

    try:
        handle = libusb.libusb_open_device_with_vid_pid(
            ctx,
            STLINK_VID,
            STLINK_PID,
        )
        if not handle:
            return

        try:
            configuration = c_int()
            rc = libusb.libusb_get_configuration(handle, byref(configuration))
            if rc == 0 and configuration.value == STLINK_CONFIGURATION:
                return

            rc = libusb.libusb_set_configuration(handle, STLINK_CONFIGURATION)
            if rc != 0:
                reset_rc = libusb.libusb_reset_device(handle)
                if reset_rc == 0:
                    sleep(1)
                    rc = libusb.libusb_set_configuration(
                        handle,
                        STLINK_CONFIGURATION,
                    )

            if rc == 0:
                print(f"ST-Link USB configuration set to {STLINK_CONFIGURATION}")
            elif rc != LIBUSB_BUSY:
                print(f"ST-Link USB configuration failed ({rc})")
        finally:
            libusb.libusb_close(handle)
    finally:
        libusb.libusb_exit(ctx)


if "upload" in COMMAND_LINE_TARGETS:
    configure_stlink()
