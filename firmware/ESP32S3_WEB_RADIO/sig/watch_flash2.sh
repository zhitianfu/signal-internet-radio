#!/bin/bash
# Flash watcher v2: waits for download mode, retries flash through
# transient USB/EIO flaps. Gives up only after 10 attempts (~2 min).
set -u
SK="$HOME/.cache/arduino/sketches/BC940045D882B6B8BADB81BE49510235"
ESP="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/esptool.py"
PORT=/dev/ttyACM0
echo "[watch5] waiting for download mode (HOLD BOOT on power-on)..."

# wait until the port exists AND opens cleanly twice in a row (stable link)
while true; do
  if [ -e "$PORT" ]; then
    sudo sh -c "chmod 666 $PORT" 2>/dev/null
    if stty -F "$PORT" 115200 >/dev/null 2>&1; then
      sleep 1
      if [ -e "$PORT" ] && stty -F "$PORT" 115200 >/dev/null 2>&1; then
        p=$(cat /sys/bus/usb/devices/9-1/product 2>/dev/null || echo "")
        case "$p" in
          *JTAG*) echo "[watch5] stable download mode detected"; break ;;
          ESP32S3*) echo "[watch5] app running - power cycle with BOOT held" ;;
        esac
      fi
    fi
  fi
  sleep 1
done

rc=1
for attempt in $(seq 1 10); do
  echo "[watch5] flash attempt $attempt/10"
  sudo sh -c "chmod 666 $PORT" 2>/dev/null
  "$ESP" --chip esp32s3 --port "$PORT" --baud 921600 --before no_reset --after hard_reset \
    write_flash -z --flash_mode keep --flash_freq keep --flash_size keep \
    0x0    "$SK/ESP32S3_WEB_RADIO.ino.bootloader.bin" \
    0x8000 "$SK/ESP32S3_WEB_RADIO.ino.partitions.bin" \
    0xe000 "$SK/boot_app0.bin" \
    0x10000 "$SK/ESP32S3_WEB_RADIO.ino.bin"
  rc=$?
  [ $rc -eq 0 ] && break
  echo "[watch5] attempt failed (rc=$rc) - waiting 4s for link to settle..."
  sleep 4
  # re-stabilize check inside loop
  for i in $(seq 1 20); do
    [ -e "$PORT" ] && stty -F "$PORT" 115200 >/dev/null 2>&1 && break
    sleep 1
  done
done

if [ $rc -eq 0 ]; then
  echo "[watch5] FLASH OK. Release nothing / keep cable seated; now power-cycle (unplug/replug) to boot."
else
  echo "[watch5] flash failed after 10 attempts (rc=$rc). Reseat the USB cable and redo: HOLD BOOT + power-on."
fi
exit $rc
