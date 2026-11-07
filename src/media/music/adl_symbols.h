/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define ST_ADL_SYMBOLS(X) \
	X(struct ADL_MIDIPlayer *, adl_init, (long)) \
	X(void, adl_close, (struct ADL_MIDIPlayer *)) \
	X(int, adl_getBanksCount, (void)) \
	X(int, adl_setBank, (struct ADL_MIDIPlayer *, int)) \
	X(int, adl_generate, (struct ADL_MIDIPlayer *, int, short *)) \
	X(int, adl_rt_noteOn, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt8, ADL_UInt8)) \
	X(void, adl_rt_noteOff, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt8)) \
	X(void, adl_rt_noteAfterTouch, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt8, ADL_UInt8)) \
	X(void, adl_rt_channelAfterTouch, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt8)) \
	X(void, adl_rt_controllerChange, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt8, ADL_UInt8)) \
	X(void, adl_rt_patchChange, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt8)) \
	X(void, adl_rt_pitchBend, (struct ADL_MIDIPlayer *, ADL_UInt8, ADL_UInt16)) \
	X(int, adl_rt_systemExclusive, (struct ADL_MIDIPlayer *, const ADL_UInt8 *, size_t))
