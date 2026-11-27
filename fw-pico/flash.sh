#!/bin/sh
# Reflash a running BusCraft Pico without pressing BOOTSEL: opening its serial port at 1200 baud reboots it into the
# USB bootloader, then the image is copied to the RPI-RP2 drive (macOS: /Volumes, Linux: set RP2=/media/.../RPI-RP2).
#   sh flash.sh [port] [image]      defaults: the only "BusCraft" serial port found, build/buscraft-pico.uf2
set -eu
cd "$(dirname "$0")"
IMG=${2:-build/buscraft-pico.uf2}
RP2=${RP2:-/Volumes/RPI-RP2}
if [ ! -d "$RP2" ]; then
  PORT=${1:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)}
  python3 -I -c "import os, sys, termios
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd); a[4] = a[5] = termios.B1200; termios.tcsetattr(fd, termios.TCSANOW, a); os.close(fd)" "$PORT"
  for i in 1 2 3 4 5 6 7 8 9 10; do [ -d "$RP2" ] && break; sleep 1; done
fi
cp "$IMG" "$RP2/"
echo "flashed $IMG"
