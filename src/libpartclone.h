/*
 * libpartclone.h	- Interfaces to partclone library.
 */
/*
 * @(#) $RCSfile: libpartclone.h,v $ $Revision: 1.2 $ (Ideal World, Inc.) $Date: 2010/07/17 01:21:27 $
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

#ifndef	_LIBPARTCLONE_H_
#define	_LIBPARTCLONE_H_	1
#include "partclone.h"
#include "sysdep_posix.h"

/*
 * pc_context_t	- Handle to access partclone images.  Used internally.
 */
struct version_dispatch_table;
struct change_file_context;
#define	PC_OPEN		0x0001		/* Image is open. */
#define	PC_CF_OPEN	0x0002		/* Change file is open */
#define	PC_VERIFIED	0x0004		/* Image verified */
#define	PC_HAVE_CFDEP	0x0040		/* Image has change file handle */
#define	PC_HAVE_VERDEP	0x0080		/* Image has version-dependent handle */
#define	PC_HAVE_IVBLOCK	0x0100		/* Image has invalid block. */
#define	PC_CF_VERIFIED	0x0200		/* Change file verified. */
#define	PC_CF_INIT	0x0400		/* Change file init done. */
#define	PC_VERSION_INIT	0x0800		/* Version-dependent init done. */
#define	PC_HEAD_VALID	0x1000		/* Image header valid. */
#define	PC_HAVE_PATH	0x2000		/* Path string allocated */
#define	PC_HAVE_CF_PATH	0x4000		/* Path string allocated */
#define	PC_VALID	0x8000		/* Header is valid */
#define	PC_READ_ONLY	0x80000		/* Open read only */
typedef struct libpc_context {
    void		*pc_fd;		/* File handle */
    char 		*pc_path;	/* Path to image */
    char		*pc_cf_path;	/* Path to change file */
    void		*pc_cf_handle;	/* Change file handle */
    unsigned char	*pc_ivblock;	/* Convenient invalid block */
    void		*pc_verdep;	/* Version-dependent handle */
    struct version_dispatch_table
			*pc_dispatch;	/* Version-dependent dispatch */
    image_desc_v2	pc_desc;	/* Image desc */
    u_int64_t		pc_block_offset;/* offset of the first block */
    u_int64_t		pc_curblock;	/* Current position */
    u_int32_t		pc_flags;	/* Handle flags */
    sysdep_open_mode_t	pc_omode;	/* Open mode */
} pc_context_t;

int partclone_open(const char *path, const char *cfpath, 
		   sysdep_open_mode_t omode, void **rpp);
int partclone_close(void *rp);
int partclone_verify(void *rp);
int64_t partclone_blocksize(void *rp);
int64_t partclone_blockcount(void *rp);
int partclone_seek(void *rp, u_int64_t blockno);
u_int64_t partclone_tell(void *rp);
int partclone_readblocks(void *rp, void *buffer, u_int64_t nblocks);
int partclone_block_used(void *rp);
int partclone_writeblocks(void *rp, void *buffer, u_int64_t nblocks);
int partclone_sync(void *rp);

#endif	/* _LIBPARTCLONE_H_ */
