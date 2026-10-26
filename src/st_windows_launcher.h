/* The sole desktop-launcher entry point across the Windows DLL boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_WINDOWS_LAUNCHER_H_
#define ST_WINDOWS_LAUNCHER_H_
#include "st_application_export.h"
ST_APPLICATION_API int st_application_main(int argc, char **argv, int console_mode);
#endif
