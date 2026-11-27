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
	printf '\035{dSOUNDON}\035{dACARD}'
	test "$(dd bs=1 count=10 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 594553ff5342203136ff
	printf '\035{MPREAL,ramp.rlp,44100}!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{mtone.dat}\035{MICMF}'
	test "$(dd bs=1 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 59ff
	printf '!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{LIGMODE}\035{LIGMODE}\035{2C,9}\035{2aFILL}\035{2CPYWP2VP}\035{mBARS.MOD}'
	printf '\035{7DEFBAR,1,20,60,4,3,30,0,0,0,0,1}\035{7DEFBAR,2,40,60,4,3,40,0,0,0,0,2}'
	printf '\035{7DEFBAR,3,60,60,4,3,50,0,0,0,0,3}\035{7DEFBAR,4,100,60,4,3,60,0,0,0,0,4}'
	printf '\035{7DEFBAR,5,120,60,4,3,70,0,0,0,0,1}\035{7MUSBAR,1}!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{7DEFBAR,1,40,60,4,3,32,1,32,34,1,2}!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{7MUSBAR,0}\035{7CLRBAR}!'
	dd bs=1 count=1 >/dev/null 2>&1
	# Define the audible track on the desktop, then assign the meter group
	# to window 1 by defining an unused track in the alerted window.
	printf '\035{7DEFBAR,1,40,60,4,3,80,0,0,0,0,2}\035{2GWIN,1,130,229,80,179,1000,750,0,1,0,0,0,0,0,0,0,0,1,Meters}'
	printf '\035{2CHVGP,1,20,40}\035{2ALERTGWIN,1}\035{2C,18}\035{2aFILL}\035{2CPYVP2HP}\035{2C,27}\035{2aFILL}\035{2RGWINI}'
	printf '\035{7DEFBAR,16,910,660,4,2,60,0,0,0,0,2}\035{2CALMGWIN}\035{7MUSBAR,1}\035{w8}\035{7MUSBAR,0}!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{2ALERTGWIN,1}\035{7DEFBAR,1,910,660,4,3,81,0,0,0,0,2}\035{2CHVGP,1,900,650}\035{2CALMGWIN}'
	printf '\035{7MUSBAR,1}\035{w8}\035{7MUSBAR,0}!'
	dd bs=1 count=1 >/dev/null 2>&1
	printf '\035{7CLRBAR}!'
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
