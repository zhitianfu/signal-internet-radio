#!/bin/bash
# Wait for the 1.75C to enter ROM download mode, then flash SIGNAL.
# Entry (physical): hold BOOT, tap RESET, release both.
set -u
SK="$HOME/.cache/arduino/sketches/BC940045D882B6B8BADB81BE49510235"
ESP="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/esptool.py"
PORT=/dev/ttyACM0
echo "[watch] SIGNAL firmware ready to flash."
echo "[watch] waiting for download mode: hold BOOT, tap RESET, release."
tick=0
while true; do
  state=""
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] || continue
    [ "$(cat "$d/idVendor" 2>/dev/null)" = "303a" ] || continue
    state="$(cat "$d/product" 2>/dev/null)"
    break
  done
  case "$state" in
    "USB JTAG/serial debug unit")
      if [ -e "$PORT" ]; then
        echo "[watch] download mode detected on $PORT - flashing..."
        break
      fi
      ;;
    "ESP32S3_DEV")
      ((tick % 10 == 0)) && echo "[watch] app running - not flashable: hold BOOT, tap RESET, release."
      ;;
    "")
      ((tick % 10 == 0)) && echo "[watch] no device yet (tick $tick) - is the USB cable plugged in?"
      ;;
  esac
  tick=$((tick + 1))
  sleep 1
done

rc=1
for attempt in 1 2 3; do
  echo "[watch] flash attempt $attempt..."
  sleep 1
  "$ESP" --chip esp32s3 --port "$PORT" --baud 921600 --before no_reset --after hard_reset \
    write_flash -z --flash_mode keep --flash_freq keep --flash_size keep \
    0x0    "$SK/ESP32S3_WEB_RADIO.ino.bootloader.bin" \
    0x8000 "$SK/ESP32S3_WEB_RADIO.ino.partitions.bin" \
    0xe000 "$SK/boot_app0.bin" \
    0x10000 "$SK/ESP32S3_WEB_RADIO.ino.bin"
  rc=$?
  [ $rc -eq 0 ] && break
  echo "[watch] attempt $attempt failed (rc=$rc), retrying..."
done
if [ $rc -eq 0 ]; then
  echo "[watch] FLASH OK. Power-cycle the board (unplug/replug USB) to boot SIGNAL."
else
  echo "[watch] flash failed (rc=$rc). Keep holding BOOT and retry."
fi
exit $rc
