#!/bin/bash
# Catch the boot banner + panic of a crash-looping app: the port only lives
# ~1.3 s per cycle, so slam it open the instant it appears, repeatedly.
LOG="$HOME/.hermes/cache/scratch/sig_crash_log.txt"
: > "$LOG"
for i in $(seq 1 45); do
  for j in $(seq 1 60); do
    [ -e /dev/ttyACM0 ] && break
    sleep 0.1
  done
  if [ ! -e /dev/ttyACM0 ]; then continue; fi
  stty -F /dev/ttyACM0 115200 raw -echo 2>/dev/null
  echo "--- cycle $i ---" >> "$LOG"
  timeout 1.4 cat /dev/ttyACM0 >> "$LOG" 2>/dev/null
done
echo "==== captured $LOG ($(wc -c < "$LOG") bytes) ===="
cat "$LOG"
