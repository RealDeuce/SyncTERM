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
ddofx-audio)
	stty raw -echo
	printf '\035{dSOUNDON}'
	test "$(dd bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 594553ff
	printf '\035{MPREAL,ramp.rlp,44100}!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{mtone.dat}\035{MICMF}'
	test "$(dd bs=1 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 59ff
	printf '!'
	dd bs=1 count=1 >/dev/null 2>&1
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
	marker='\0253'
	if [ "$3" = utf8 ]; then
		marker='\0302\0275'
	fi
	printf '%b?_2|' "$marker"
	if [ "$2" = normal ]; then
		dd bs=1 count=3 2>/dev/null | od -An -tx1 | tr -d ' \n'
	fi
	printf '%ba|' "$marker"
	dd bs=1 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n'
	printf '%b\\|AFTER' "$marker"
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
	printf '\251\033\303\251\360\237\033[32mZ'
	sleep 2
	;;
capture)
	stty raw -echo
	printf 'A#XZ\033[32m\200\377\000\357\277\275\303'
	;;
*) exit 1 ;;
esac
