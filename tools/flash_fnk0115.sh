#!/bin/bash
# flash_fnk0115.sh - build the FNK0115 image (the 800 x 480 tank, CONFIG_POCKET_TANK_FNK0115)
# and flash it to a Freenove FNK0115 (the IPS variants: FNK0115L 4.3", FNK0115Q 5.0").
# Its USB port is a USB-UART bridge on UART0 (GPIO 19 / 20, the S3's own USB,
# carry the touch's I2C): the console and the director are there too.
#
#   tools/flash_fnk0115.sh               # build, flash the app
#   tools/flash_fnk0115.sh --build-only
#   tools/flash_fnk0115.sh --full        # a blank board: clear nvs, the model, bootloader + table + app
#   FNK_SERIAL=<usb serial> tools/flash_fnk0115.sh   # the board by its USB serial (or set it once in tools/boards.local.sh)
#   FNK_PORT=/dev/ttyUSB0 tools/flash_fnk0115.sh     # ... or by its port: many USB-UART bridges carry no serial number
#
# Its own build directory and sdkconfig (~/.cache/pocket-tank/fw-build-fnk0115):
# the tank's fw-build is never touched. No preflight: this board holds no tank
# anyone would miss yet. docs/board-fnk0115.md has the rest.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f tools/boards.local.sh ] && . tools/boards.local.sh   # your boards' USB serials (not tracked)
SERIAL=${FNK_SERIAL:-}
PORT=${FNK_PORT:-}
B=~/.cache/pocket-tank/fw-build-fnk0115
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash_fnk0115: ESP-IDF export failed"; exit 1; }
[ -f firmware/keys/ota_signing_key.pem ] || { echo "flash_fnk0115: no firmware/keys/ota_signing_key.pem - tools/ota_key.sh"; exit 1; }
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
( cd firmware && idf.py -B "$B" -DSDKCONFIG="$B/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.fnk0115" build ) > "$LOG" 2>&1 \
  || { echo "flash_fnk0115: build FAILED"; grep -Ei "error|fatal|failed" "$LOG" | head -30; exit 1; }
grep -E "pocket_tank.bin binary size" "$LOG" || true
grep -q "CONFIG_POCKET_TANK_FNK0115=y" "$B/sdkconfig" || { echo "flash_fnk0115: $B is not an FNK0115 build"; exit 1; }
[[ " $* " == *" --build-only "* ]] && exit 0
if [ -z "$PORT" ]; then
  [ -n "$SERIAL" ] || { echo "flash_fnk0115: set FNK_SERIAL=<the board's USB serial> or FNK_PORT=<its port> (in tools/boards.local.sh, or the environment)"; exit 1; }
  PORT=$(python - "$SERIAL" <<'PY'
import sys
from serial.tools import list_ports
want = sys.argv[1].upper()
for p in list_ports.comports():
    if (p.serial_number or "").upper() == want:
        print(p.device.replace("/dev/tty.", "/dev/cu.")); sys.exit(0)
sys.exit(1)
PY
  ) || { echo "flash_fnk0115: no board with USB serial $SERIAL on USB"; exit 1; }
fi
echo "flash_fnk0115: the FNK0115 is $PORT"
ESPTOOL="python -m esptool --chip esp32s3 -p $PORT -b 460800"
if [[ " $* " == *" --full "* ]]; then
  $ESPTOOL --after no_reset erase_region 0x9000 0x7000 | grep -E "Erase completed|rror"
  $ESPTOOL --after no_reset write_flash 0x290000 model/out/model_q4.bin | grep -E "Wrote|verified|rror"
  ( cd "$B" && $ESPTOOL write_flash "@flash_args" | grep -E "Wrote|verified|rror" )
else
  # otadata too: after an over-the-air update the board boots ota_1, and an app written to ota_0 alone would never run
  $ESPTOOL write_flash 0xA90000 "$B/ota_data_initial.bin" 0x10000 "$B/pocket_tank.bin" | grep -E "Wrote|verified|rror"
fi
echo "flash_fnk0115: done"
