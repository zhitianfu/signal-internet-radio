#!/usr/bin/env python3
"""Flash SIGNAL firmware to a Waveshare ESP32-S3-Touch-AMOLED-1.75C.

Linux-only helper (uses TIOCM on the USB-CDC port). It:

  1. locates the serial port (or takes --port),
  2. puts the chip into the ROM download mode WITHOUT the BOOT button
     (DTR/RTS sequence 0x04, 0x06, 0x02, 0x00 @150 ms — works both from the
     stock firmware and from a running SIGNAL build; the device re-enumerates
     on the SAME /dev/ttyACMx),
  3. runs esptool write_flash 0x10000 (the app slot used by huge_app),
  4. re-binds cdc_acm if the serial node vanished.

If the sequence ever fails (e.g. a truly virgin board in an odd state), the
classic fallback is: hold BOOT while plugging the cable in, then re-run this
script (or plain esptool --before default_reset).

Usage:
  python3 tools/flash_device.py --bin path/to/ESP32S3_WEB_RADIO.ino.bin
  python3 tools/flash_device.py                 # newest build output found
"""
import argparse, glob, os, struct, subprocess, sys, termios, time

BAUD = 921600
APP_OFFSET = "0x10000"
DL_PRODUCT = "USB JTAG/serial debug unit"   # ROM download-mode USB product
APP_PRODUCT = "ESP32-S3_DEV"                # running-app product string


def find_port(hint=None):
    if hint:
        return hint
    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return ports[0] if ports else None


def find_bin(hint=None):
    if hint:
        if not os.path.isfile(hint):
            sys.exit(f"bin not found: {hint}")
        return hint
    cands = glob.glob(os.path.expanduser(
        "~/.cache/arduino/sketches/*/ESP32S3_WEB_RADIO.ino.bin"))
    if not cands:
        sys.exit("no build output found — compile first or pass --bin")
    return max(cands, key=os.path.getmtime)


def find_esptool(hint=None):
    if hint:
        return [hint, ]
    for name in ("esptool.py", "esptool"):
        p = subprocess.run(["which", name], capture_output=True, text=True).stdout.strip()
        if p:
            return [p]
    envs = glob.glob(os.path.expanduser("~/.espressif/python_env/*/bin/esptool.py"))
    if envs:
        return [envs[-1]]
    return [sys.executable, "-m", "esptool"]


def usb_product():
    for dev in glob.glob("/sys/bus/usb/devices/*"):
        try:
            p = os.path.join(dev, "product")
            if os.path.isfile(p):
                name = open(p).read().strip()
                if name in (DL_PRODUCT, APP_PRODUCT):
                    return name
        except OSError:
            pass
    return "?"


def enter_download_mode(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = (attrs[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)) | termios.CS8
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        time.sleep(0.2)
        for bits in (0x04, 0x06, 0x02, 0x00):      # RTS, RTS|DTR, DTR, none
            fcntl_ioctl(fd, bits)
            time.sleep(0.15)
    finally:
        os.close(fd)
    time.sleep(1.5)


def fcntl_ioctl(fd, bits):
    import fcntl
    fcntl.ioctl(fd, termios.TIOCMSET, struct.pack("I", bits))


def rebind_cdc():
    """Re-attach cdc_acm if /dev/ttyACM0 disappeared (needs sudo)."""
    if os.path.exists("/dev/ttyACM0"):
        return
    for iface in glob.glob("/sys/bus/usb/devices/*/bInterfaceNumber"):
        try:
            if open(iface).read().strip() != "02":
                continue
            dev = os.path.dirname(iface)
            if not os.path.exists(os.path.join(dev, "driver")):
                name = os.path.basename(dev)
                subprocess.run(["sudo", "sh", "-c",
                                f"echo -n {name} > /sys/bus/usb/drivers/cdc_acm/bind"])
                time.sleep(1.5)
                return
        except OSError:
            continue


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    ap.add_argument("--bin", help="firmware .bin (default: newest build output)")
    ap.add_argument("--esptool", help="esptool executable (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=BAUD)
    args = ap.parse_args()

    bin_path = find_bin(args.bin)
    esptool = find_esptool(args.esptool)
    print(f"bin:    {bin_path}")
    print(f"esptool: {' '.join(esptool)}")

    port = find_port(args.port)
    if not port:
        sys.exit("no serial port found — plug the device in (or hold BOOT while plugging)")
    print(f"port:   {port} (product: {usb_product()})")

    print("entering download mode (DTR/RTS sequence, no BOOT button) ...")
    enter_download_mode(port)
    prod = usb_product()
    print(f"        product now: {prod}")
    if prod != DL_PRODUCT:
        sys.exit("download mode not reached — hold BOOT while plugging the cable, then re-run")

    cmd = esptool + ["--chip", "esp32s3", "-p", port, "--before", "no_reset",
                     "--after", "watchdog_reset", "--baud", str(args.baud),
                     "write_flash", APP_OFFSET, bin_path]
    print("running:", " ".join(cmd))
    rc = subprocess.call(cmd)
    if rc != 0:
        sys.exit(f"esptool failed (rc={rc})")

    time.sleep(3)
    rebind_cdc()
    print("flash OK — device reboots into SIGNAL")
    if usb_product() != APP_PRODUCT:
        print(f"note: product is '{usb_product()}' — it may still be booting; "
              "if no /dev/ttyACM0, re-bind cdc_acm (see README 3.3)")


if __name__ == "__main__":
    main()
