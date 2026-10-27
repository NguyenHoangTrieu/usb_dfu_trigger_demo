#!/usr/bin/env bash
# Flash the companion/target board over USB DFU, triggered remotely by
# sending the string "DFU" to the firmware's USB-CDC port (no BOOT/RST
# button press needed -- see main/main.c and README.md).
#
# Usage: ./flash_dfu.sh --cdc-port /dev/ttyACM1 [--dfu-path BUS-PORT] [--timeout SECONDS]
#
# Flow:
#   1. idf.py dfu            (build/refresh build/dfu.bin)
#   2. send "DFU" to --cdc-port
#   3. poll `idf.py dfu-list` until the ESP32-P4 DFU device (VID:PID 303a:0012,
#      see dfu_pid in esp-idf/tools/cmake/dfu.cmake for esp32p4) shows up, or
#      --timeout elapses
#   4. idf.py dfu-flash [--path --dfu-path]
source ~/esp/esp-idf/export.sh
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CDC_PORT=""
DFU_PATH=""
TIMEOUT=15
ESP_DFU_VID_PID="303a:0012"   # esp32p4, see dfu.cmake (dfu_pid=12 -> hex "0012")

usage() {
    echo "Usage: $0 --cdc-port /dev/ttyACMx [--dfu-path BUS-PORT] [--timeout SECONDS]"
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --cdc-port) CDC_PORT="$2"; shift 2 ;;
        --dfu-path) DFU_PATH="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown argument: $1"; usage ;;
    esac
done

if [ -z "${CDC_PORT}" ]; then
    echo "Error: --cdc-port is required (the USB-CDC serial port exposed by the firmware)."
    usage
fi

if [ -z "${IDF_PATH:-}" ]; then
    echo "Error: ESP-IDF environment not sourced. Run 'source \$IDF_PATH/export.sh' first."
    exit 1
fi

if ! command -v dfu-util >/dev/null 2>&1; then
    echo "Error: dfu-util not found in PATH. Install it (e.g. 'apt install dfu-util') first."
    exit 1
fi

cd "${SCRIPT_DIR}" || exit 1

echo "=========================================================="
echo " USB-HS DFU flash (no BOOT/RST button press)"
echo " CDC trigger port : ${CDC_PORT}"
echo " DFU path filter  : ${DFU_PATH:-<any>}"
echo " Wait timeout     : ${TIMEOUT}s"
echo "=========================================================="

echo "[1/4] Building DFU image (idf.py dfu)..."
if ! idf.py dfu; then
    echo "Error: 'idf.py dfu' failed. Fix the build before retrying."
    exit 1
fi

echo "[2/4] Sending \"DFU\" trigger to ${CDC_PORT}..."
if ! python3 - "${CDC_PORT}" <<'PYEOF'
import sys, time
import serial  # pyserial

port = sys.argv[1]
try:
    s = serial.Serial(port, 115200, timeout=1)
    s.write(b"DFU")
    s.flush()
    time.sleep(0.2)
    s.close()
except Exception as e:
    print(f"[flash_dfu] Failed to write DFU trigger to {port}: {e}", file=sys.stderr)
    sys.exit(1)
PYEOF
then
    echo "Error: could not send the \"DFU\" trigger over ${CDC_PORT}."
    echo "       Check the port name and that the firmware's CDC device is enumerated"
    echo "       (see README.md for how to find it, e.g. 'ls /dev/serial/by-id/')."
    exit 1
fi

echo "[3/4] Waiting up to ${TIMEOUT}s for the ESP32-P4 DFU device (${ESP_DFU_VID_PID})..."
found=0
elapsed=0
while [ "${elapsed}" -lt "${TIMEOUT}" ]; do
    if idf.py dfu-list 2>/dev/null | grep -qi "${ESP_DFU_VID_PID}"; then
        found=1
        break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done

if [ "${found}" -ne 1 ]; then
    echo ""
    echo "Error: DFU device ${ESP_DFU_VID_PID} did not appear within ${TIMEOUT}s."
    echo "       Possible causes (see README.md 'Hạn chế'):"
    echo "       - eFuse DIS_FORCE_DOWNLOAD is burned (force-download-boot permanently disabled)"
    echo "       - secure boot / flash encryption is on (may block USB-OTG download per README caveat)"
    echo "       - firmware was hung/crashed and never reached the CDC RX handler"
    echo "       - the board did not auto re-enumerate after the reset"
    echo "       As a fallback: hold BOOT, tap RST, then run 'idf.py dfu-flash' manually."
    exit 1
fi

echo "[4/4] DFU device found. Flashing (idf.py dfu-flash)..."
if [ -n "${DFU_PATH}" ]; then
    idf.py dfu-flash --path "${DFU_PATH}"
else
    idf.py dfu-flash
fi
RET=$?

if [ ${RET} -eq 0 ]; then
    echo ""
    echo "Flash successful."
else
    echo ""
    echo "Error: 'idf.py dfu-flash' failed (exit ${RET}). See the dfu-util output above."
fi
exit ${RET}
