#!/bin/sh

set -e

mount /dev/sda1 /mnt
fwup -ad /dev/mtdblock1 -i /mnt/edge_firmware.fw -t complete --unsafe
umount /mnt

reboot -f
