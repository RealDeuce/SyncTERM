#!/bin/sh
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
# Keep shell programs out of the deliberately bounded BBS address field.
set -eu
case "$1" in
rip)
	stty raw -echo
	exec "$ST_RIP_TEST_PYTHON" "@CMAKE_CURRENT_BINARY_DIR@/../../scripting/wren_host/rip-peer.py" "$2"
	;;
auto-transfer)
	stty raw -echo
	printf 'BEFORE**\030B00000000000000\r\n\021'
	dd bs=1 count=21 >/dev/null 2>&1
	printf '**\030B0800000000022d\r\nOOAFTER'
	sleep 6
	;;
transfer)
	stty raw -echo
	printf 'BEFORE~**\030B0800000000022d\r\nOOBETWEEN~**\030B0800000000022d\r\nOOAFTER'
	while [ "$(dd bs=1 count=1 2>/dev/null)" != '!' ]; do :; done
	;;
responses)
	stty raw -echo
	printf 'READY'
	dd bs=1 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n'
	;;
ooii)
	stty raw -echo
	printf '\253?_2|'
	if [ "$2" = normal ]; then
		dd bs=1 count=3 2>/dev/null | od -An -tx1 | tr -d ' \n'
	fi
	printf '\253a|'
	dd bs=1 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n'
	printf '\253\\|AFTER'
	;;
apc)
	stty raw -echo
	printf '\033_SyncTERM:VER\033\\'
	dd bs=1 count=18 2>/dev/null | od -An -tx1 | tr -d ' \n'
	printf '\r\nREADY'
	dd bs=1 count=9 2>/dev/null | od -An -tx1 | tr -d ' \n'
	;;
prestel)
	stty raw -echo
	printf 'READY\r\n'
	dd bs=1 count=6 2>/dev/null | od -An -tx1 | tr -d ' \n'
	;;
utf8)
	printf '%s:' "$MM_CHARSET"
	printf '\303'
	sleep 1
	printf '\251'
	sleep 2
	;;
rip-query)
	stty raw -echo
	printf '!|1\0330000\202^M\r\n'
	dd bs=1 count=2 2>/dev/null
	;;
capture)
	stty raw -echo
	printf 'A#XZ\033[32m\200\377\000\357\277\275\303'
	;;
*) exit 1 ;;
esac
