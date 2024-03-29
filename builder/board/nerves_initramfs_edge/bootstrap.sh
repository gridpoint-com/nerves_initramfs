#!/bin/sh
set -e

err_exit() {
  echo "Failed to flash firmware via USB" 1>&2
  echo 1>&2
  echo "You can try flashing via tftp with:" 1>&2
  echo "fwup-tftp" 1>&2
  sleep 1
}

trap 'err_exit' ERR

mount /dev/sda1 /mnt
fwup -ad /dev/mtdblock1 -i /mnt/edge_firmware.fw -t complete --unsafe
umount /mnt

reboot -f
