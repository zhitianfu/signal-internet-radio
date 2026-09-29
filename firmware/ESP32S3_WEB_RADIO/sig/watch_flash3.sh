#!/bin/bash
# Tight-fire flasher: poll every 0.1 s, fire esptool the INSTANT the port
# exists. Built for BOOT-held ROM sessions (stable) but also tries to win
# short windows. Gives up after ~10 minutes of trying.
set -u
SK="$HOME/.cache/arduino/sketches/BC940045D882B6B8BADB81BE49510235"
ESP="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/esptool.py"
PORT=/dev/ttyACM0
echo "[watch6] tight-fire flasher armed (HOLD BOOT + power-on)."
rc=1
attempt=0
end=$(( $(date +%s) + 600 ))
while [ "$(date +%s)" -lt "$end" ]; do
  [ -e "$PORT" ] || { sleep 0.1; continue; }
  # only fire in download mode (ROM identity), never at the running app
  prod=$(cat /sys/bus/usb/devices/9-1/product 2>/dev/null || echo "")
  case "$prod" in
    *JTAG*) ;;
    *) { sleep 0.1; continue; } ;;
  esac
  attempt=$((attempt + 1))
  echo "[watch6] attempt $attempt (port present)"
  "$ESP" --chip esp32s3 --port "$PORT" --baud 921600 --before no_reset --after hard_reset \
    write_flash -z --flash_mode keep --flash_freq keep --flash_size keep \
    0x0    "$SK/ESP32S3_WEB_RADIO.ino.bootloader.bin" \
    0x8000 "$SK/ESP32S3_WEB_RADIO.ino.partitions.bin" \
    0xe000 "$SK/boot_app0.bin" \
    0x10000 "$SK/ESP32S3_WEB_RADIO.ino.bin"
  rc=$?
  [ $rc -eq 0 ] && break
  # swallow this failure, go straight back to polling (no long sleeps)
  sleep 0.2
done

if [ $rc -eq 0 ]; then
  echo "[watch6] FLASH OK after $attempt attempt(s). Release BOOT and power-cycle (unplug/replug) to boot SIGNAL."
else
  echo "[watch6] gave up after $attempt attempts in 10 min (rc=$rc)."
fi
exit $rc
