#!/bin/sh

set -e

BOARD_DIR=../board/nerves_initramfs_edge/
NERVES_INITRAMFS_CONF=${BOARD_DIR}/nerves_initramfs.conf
BOOTSTRAP_SH=${BOARD_DIR}/bootstrap.sh

cp ${NERVES_INITRAMFS_CONF} ${TARGET_DIR}
cp ${BOOTSTRAP_SH} ${TARGET_DIR}/bin
