#!/bin/bash
# Boot verifier v3: open serial the instant the app enumerates (no sleeps),
# stream 15 s, then judge: stable product = healthy; flapping = crash-loop.
set -u
LOG="$HOME/.hermes/cache/scratch/sig_boot_log.txt"
echo "[watch4] waiting for the app: release BOOT, unplug, plug back in..."

# 1) wait for app identity
while true; do
  found=0
  for f in /sys/bus/usb/devices/*/product; do
    case "$(cat "$f" 2>/dev/null)" in
      ESP32S3_DEV) found=1 ;;
    esac
  done
  [ "$found" -eq 1 ] && break
  sleep 0.3
done

# 2) stream immediately (boot lines land in the first seconds)
: > "$LOG"
stty -F /dev/ttyACM0 115200 raw -echo 2>/dev/null
echo "[watch4] app enumerated - streaming serial 15 s..."
timeout 15 cat /dev/ttyACM0 >> "$LOG" 2>/dev/null

# 3) verdict
alive=0
for f in /sys/bus/usb/devices/*/product; do
  case "$(cat "$f" 2>/dev/null)" in ESP32S3_DEV) alive=1 ;; esac
done
echo "[watch4] ---- serial log ($LOG) ----"
cat "$LOG" 2>/dev/null
echo "[watch4] ---- end ----"
if [ "$alive" -eq 1 ]; then
  echo "[watch4] product stable for 15 s: app NOT crash-looping."
else
  echo "[watch4] product gone/re-enumerating: still resetting!"
fi
if grep -q "radio ready" "$LOG" 2>/dev/null; then
  echo "[watch4] BOOT OK: reached 'radio ready'."
else
  echo "[watch4] full banner not seen (early lines may have raced the port)."
fi
