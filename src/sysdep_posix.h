/*
 * sysdep_posix.h	- POSIX system dependent module interface.
 */
/*
 * @(#) $RCSfile: sysdep_posix.h,v $ $Revision: 1.1 $ (Ideal World, Inc.) $Date: 2010/07/14 22:36:35 $
 */
/*
 * Copyright (c) 2010, Ideal World, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef	_SYSDEP_POSIX_H_
#define	_SYSDEP_POSIX_H_	1

#include <sys/types.h>
#include <errno.h>

typedef enum sysdep_open_mode {
    SYSDEP_OPEN_NONE = 0,
    SYSDEP_OPEN_RO = 1,
    SYSDEP_OPEN_RW = 2,
    SYSDEP_OPEN_WO = 3,
    SYSDEP_CREATE = 4
} sysdep_open_mode_t;

typedef enum sysdep_whence {
    SYSDEP_SEEK_ABSOLUTE = 0,
    SYSDEP_SEEK_RELATIVE = 1,
    SYSDEP_SEEK_END = 2
} sysdep_whence_t;

int
posix_open(void *rhp, const char *p, sysdep_open_mode_t omode);

int
posix_close(void *rh);

int
posix_seek(void *rh, int64_t offset, sysdep_whence_t whence, u_int64_t *resoffp);

int
posix_read(void *rh, void *buf, u_int64_t len, u_int64_t *nr);

int
posix_write(void *rh, void *buf, u_int64_t len, u_int64_t *nw);

int
posix_malloc(void *nmpp, u_int64_t nbytes);

int
posix_free(void *mp);

#endif	/* _SYSDEP_POSIX_H_ */
