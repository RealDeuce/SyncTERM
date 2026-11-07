/* Public FluidSynth C entry points shared by the probe and both link modes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define ST_FLUID_SYMBOLS(X) \
	X(fluid_settings_t *, new_fluid_settings, (void)) \
	X(void, delete_fluid_settings, (fluid_settings_t *)) \
	X(int, fluid_settings_setnum, (fluid_settings_t *, const char *, double)) \
	X(int, fluid_settings_getnum_default, (fluid_settings_t *, const char *, double *)) \
	X(int, fluid_settings_dupstr, (fluid_settings_t *, const char *, char **)) \
	X(void, fluid_free, (void *)) \
	X(fluid_synth_t *, new_fluid_synth, (fluid_settings_t *)) \
	X(void, delete_fluid_synth, (fluid_synth_t *)) \
	X(float, fluid_synth_get_gain, (fluid_synth_t *)) \
	X(fluid_cmd_handler_t *, new_fluid_cmd_handler2, (fluid_settings_t *, fluid_synth_t *, fluid_midi_router_t *, fluid_player_t *)) \
	X(void, delete_fluid_cmd_handler, (fluid_cmd_handler_t *)) \
	X(char *, fluid_get_userconf, (char *, int)) \
	X(int, fluid_source, (fluid_cmd_handler_t *, const char *)) \
	X(int, fluid_synth_sfcount, (fluid_synth_t *)) \
	X(int, fluid_synth_sfload, (fluid_synth_t *, const char *, int)) \
	X(int, fluid_synth_noteon, (fluid_synth_t *, int, int, int)) \
	X(int, fluid_synth_noteoff, (fluid_synth_t *, int, int)) \
	X(int, fluid_synth_cc, (fluid_synth_t *, int, int, int)) \
	X(int, fluid_synth_program_change, (fluid_synth_t *, int, int)) \
	X(int, fluid_synth_channel_pressure, (fluid_synth_t *, int, int)) \
	X(int, fluid_synth_key_pressure, (fluid_synth_t *, int, int, int)) \
	X(int, fluid_synth_pitch_bend, (fluid_synth_t *, int, int)) \
	X(int, fluid_synth_sysex, (fluid_synth_t *, const char *, int, char *, int *, int *, int)) \
	X(int, fluid_synth_write_s16, (fluid_synth_t *, int, void *, int, int, void *, int, int)) \
	X(int, fluid_synth_get_program, (fluid_synth_t *, int, int *, int *, int *)) \
	X(int, fluid_synth_program_select, (fluid_synth_t *, int, int, int, int))
