#!/bin/sh
set -e

err_exit() {
  echo "Failed to flash firmware via USB"
  echo
  echo "You can try flashing via tftp with:"
  echo "fwup-tftp"
}

trap 'err_exit' ERR

mount /dev/sda1 /mnt
fwup -ad /dev/mtdblock1 -i /mnt/edge_firmware.fw -t complete --unsafe
umount /mnt

reboot -f
