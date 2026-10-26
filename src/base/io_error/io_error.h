/* Stable negative error values shared by I/O-oriented components. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */


#ifndef SYNCTERM_IO_ERROR_H_
#define SYNCTERM_IO_ERROR_H_

/* Existing values are part of the component API and must not be renumbered. */

#define IO_ERROR_INVALID_ARGUMENT	(-1)
#define IO_ERROR_INVALID_ENCODING	(-2)
#define IO_ERROR_NOT_FOUND		(-3)
#define IO_ERROR_PERMISSION_DENIED	(-4)
#define IO_ERROR_ALREADY_EXISTS		(-5)
#define IO_ERROR_NOT_DIRECTORY		(-6)
#define IO_ERROR_IS_DIRECTORY		(-7)
#define IO_ERROR_DIRECTORY_NOT_EMPTY	(-8)
#define IO_ERROR_OUT_OF_MEMORY		(-9)
#define IO_ERROR_NO_SPACE		(-10)
#define IO_ERROR_NAME_TOO_LONG		(-11)
#define IO_ERROR_OUT_OF_RANGE		(-12)
#define IO_ERROR_BUSY			(-13)
#define IO_ERROR_READ_ONLY		(-14)
#define IO_ERROR_CROSS_DEVICE		(-15)
#define IO_ERROR_TOO_MANY_LINKS		(-16)
#define IO_ERROR_INTERRUPTED		(-17)
#define IO_ERROR_NOT_SUPPORTED		(-18)
#define IO_ERROR_IO			(-19)
#define IO_ERROR_TIMED_OUT		(-20)
#define IO_ERROR_CANCELLED		(-21)
#define IO_ERROR_WOULD_BLOCK		(-22)
#define IO_ERROR_IN_PROGRESS		(-23)
#define IO_ERROR_NAME_RESOLUTION	(-24)
#define IO_ERROR_CONNECTION_REFUSED	(-25)
#define IO_ERROR_CONNECTION_RESET	(-26)
#define IO_ERROR_CONNECTION_ABORTED	(-27)
#define IO_ERROR_NOT_CONNECTED		(-28)
#define IO_ERROR_NETWORK_UNREACHABLE	(-29)
#define IO_ERROR_HOST_UNREACHABLE	(-30)
#define IO_ERROR_ADDRESS_IN_USE		(-31)
#define IO_ERROR_ADDRESS_NOT_AVAILABLE	(-32)
#define IO_ERROR_MESSAGE_TOO_LARGE	(-33)
#define IO_ERROR_PROTOCOL		(-34)
#define IO_ERROR_PASSWORD_REQUIRED	(-35)
#define IO_ERROR_DECRYPTION_FAILED	(-36)
#define IO_ERROR_INVALID_FORMAT		(-37)
#define IO_ERROR_CONFLICT		(-38)

/*
 * Translate errors from the host's general I/O namespaces.  Components keep
 * operation-specific interpretations (for example, ENODEV from mmap versus
 * a serial-device open) at the call site.
 */
int io_error_from_errno(int error);
/* The argument is a Win32 DWORD value without requiring <windows.h> here. */
int io_error_from_windows(unsigned long error);

#endif
