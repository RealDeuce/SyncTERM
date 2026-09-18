/* Native errno translation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "io_error.h"

#include <errno.h>

int
io_error_from_errno(int error)
{
	if (error == 0)
		return 0;
	if (error == EINVAL || error == EBADF)
		return IO_ERROR_INVALID_ARGUMENT;
	if (error == ENOENT)
		return IO_ERROR_NOT_FOUND;
	if (error == EACCES || error == EPERM)
		return IO_ERROR_PERMISSION_DENIED;
	if (error == EEXIST)
		return IO_ERROR_ALREADY_EXISTS;
	if (error == ENOTDIR)
		return IO_ERROR_NOT_DIRECTORY;
#ifdef EISDIR
	if (error == EISDIR)
		return IO_ERROR_IS_DIRECTORY;
#endif
#ifdef ENOTEMPTY
	if (error == ENOTEMPTY)
		return IO_ERROR_DIRECTORY_NOT_EMPTY;
#endif
	if (error == ENOMEM)
		return IO_ERROR_OUT_OF_MEMORY;
	if (error == ENOSPC)
		return IO_ERROR_NO_SPACE;
#ifdef EDQUOT
	if (error == EDQUOT)
		return IO_ERROR_NO_SPACE;
#endif
	if (error == ENAMETOOLONG)
		return IO_ERROR_NAME_TOO_LONG;
	if (error == ERANGE)
		return IO_ERROR_OUT_OF_RANGE;
#ifdef EOVERFLOW
	if (error == EOVERFLOW)
		return IO_ERROR_OUT_OF_RANGE;
#endif
#ifdef EFBIG
	if (error == EFBIG)
		return IO_ERROR_OUT_OF_RANGE;
#endif
	if (error == EBUSY)
		return IO_ERROR_BUSY;
#ifdef EROFS
	if (error == EROFS)
		return IO_ERROR_READ_ONLY;
#endif
#ifdef EXDEV
	if (error == EXDEV)
		return IO_ERROR_CROSS_DEVICE;
#endif
#ifdef ELOOP
	if (error == ELOOP)
		return IO_ERROR_TOO_MANY_LINKS;
#endif
	if (error == EINTR)
		return IO_ERROR_INTERRUPTED;
#ifdef ENOSYS
	if (error == ENOSYS)
		return IO_ERROR_NOT_SUPPORTED;
#endif
#ifdef ENOTSUP
	if (error == ENOTSUP)
		return IO_ERROR_NOT_SUPPORTED;
#endif
#ifdef EOPNOTSUPP
	if (error == EOPNOTSUPP)
		return IO_ERROR_NOT_SUPPORTED;
#endif
#ifdef ENOTTY
	if (error == ENOTTY)
		return IO_ERROR_NOT_SUPPORTED;
#endif
#ifdef EAGAIN
	if (error == EAGAIN)
		return IO_ERROR_WOULD_BLOCK;
#endif
#ifdef EWOULDBLOCK
	if (error == EWOULDBLOCK)
		return IO_ERROR_WOULD_BLOCK;
#endif
#ifdef ETIMEDOUT
	if (error == ETIMEDOUT)
		return IO_ERROR_TIMED_OUT;
#endif
#ifdef ECANCELED
	if (error == ECANCELED)
		return IO_ERROR_CANCELLED;
#endif
	return IO_ERROR_IO;
}
