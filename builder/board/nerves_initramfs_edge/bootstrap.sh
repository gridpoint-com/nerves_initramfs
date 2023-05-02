#!/bin/sh

set -e

mount /dev/sda1 /mnt
fwup -ad /dev/mtdblock1 -i /mnt/ec3_firmware.fw -t complete --unsafe
umount /mnt

reboot -f
