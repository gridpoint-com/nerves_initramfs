#!/bin/sh

set -e

BOARD_DIR=../board/nerves_initramfs_edge/
NERVES_INITRAMFS_CONF=${BOARD_DIR}/nerves_initramfs.conf
BOOTSTRAP_SH=${BOARD_DIR}/bootstrap.sh

cp ${NERVES_INITRAMFS_CONF} ${TARGET_DIR}
cp ${BOOTSTRAP_SH} ${TARGET_DIR}/bin
cp ${BOARD_DIR}/serverid.hook ${TARGET_DIR}/usr/share/udhcpc/default.script.d
chmod +x ${TARGET_DIR}/usr/share/udhcpc/default.script.d/serverid.hook
cp ${BOARD_DIR}/fwup-tftp ${TARGET_DIR}/bin
chmod +x ${TARGET_DIR}/bin/fwup-tftp
