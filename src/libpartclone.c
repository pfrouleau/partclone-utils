/*
 * libpartclone.c	- Access individual blocks in a partclone image.
 */
/*
 * @(#) $RCSfile: libpartclone.c,v $ $Revision: 1.4 $ (Ideal World, Inc.) $Date: 2010/07/17 20:47:58 $
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
#ifdef	HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <errno.h>
#include <string.h>
#include "changefile.h"
#include "partclone.h"
#include "libbitmap.h"
#include "libpartclone.h"
#include "libimage.h"

static const char cf_trailer[] = ".cf";

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
    u_int64_t		pc_curblock;	/* Current position */
    u_int32_t		pc_flags;	/* Handle flags */
    sysdep_open_mode_t	pc_omode;	/* Open mode */
} pc_context_t;

/*
 * Macros to check state flags.
 */
#define	PCTX_FLAGS_SET(_p, _f)	((_p) && (((_p)->pc_flags & ((_f)|PC_VALID)) \
					  == ((_f)|PC_VALID)))
#define	PCTX_VALID(_p)		PCTX_FLAGS_SET(_p, 0)
#define	PCTX_OPEN(_p)		PCTX_FLAGS_SET(_p, PC_OPEN)
#define	PCTX_READ_ONLY(_p)	(((_p)->pc_flags & PC_READ_ONLY) == \
				 PC_READ_ONLY)
#define	PCTX_CF_OPEN(_p)	PCTX_FLAGS_SET(_p, PC_CF_OPEN)
#define	PCTX_VERIFIED(_p)	PCTX_FLAGS_SET(_p, PC_OPEN|PC_VERIFIED)
#define	PCTX_HEAD_VALID(_p)	PCTX_FLAGS_SET(_p, PC_OPEN|PC_VERIFIED|	\
					       PC_HEAD_VALID)
#define	PCTX_READREADY(_p)	PCTX_FLAGS_SET(_p, PC_OPEN|PC_VERIFIED|	\
					       PC_HEAD_VALID|PC_VERSION_INIT)
#define	PCTX_CFREADY(_p)	PCTX_FLAGS_SET(_p, PC_OPEN|PC_VERIFIED|	\
					       PC_HEAD_VALID|PC_VERSION_INIT| \
					       PC_HAVE_CFDEP|PC_CF_VERIFIED)
#define	PCTX_WRITEABLE(_p)	(!PCTX_READ_ONLY(_p) && PCTX_READREADY(_p))
#define	PCTX_WRITEREADY(_p)	(!PCTX_READ_ONLY(_p) && PCTX_CFREADY(_p))
#define	PCTX_HAVE_PATH(_p)	(PCTX_FLAGS_SET(_p, PC_HAVE_PATH) && \
				 (_p)->pc_path)
#define	PCTX_HAVE_CF_PATH(_p)	(PCTX_FLAGS_SET(_p, PC_HAVE_CF_PATH) && \
				 (_p)->pc_cf_path)
#define	PCTX_HAVE_VERDEP(_p)	(PCTX_FLAGS_SET(_p, PC_HAVE_VERDEP) &&	\
				 (_p)->pc_verdep)
#define	PCTX_HAVE_CFDEP(_p)	(PCTX_FLAGS_SET(_p, PC_HAVE_CFDEP) &&	\
				 (_p)->pc_cfdep)
#define	PCTX_HAVE_IVBLOCK(_p)	(PCTX_FLAGS_SET(_p, PC_HAVE_IVBLOCK) &&	\
				 (_p)->pc_ivblock)

/*
 * Version dispatch table - to handle different file format versions.
 */
typedef struct version_dispatch_table {
    char	version[IMAGE_VERSION_SIZE+1];
    int		(*version_init)(pc_context_t *pcp);
    int		(*version_verify)(pc_context_t *pcp);
    int		(*version_finish)(pc_context_t *pcp);
    int		(*version_seek)(pc_context_t *pcp, u_int64_t block);
    int		(*version_readblock)(pc_context_t *pcp, void *buffer);
    int		(*version_blockused)(pc_context_t *pcp);
    int		(*version_writeblock)(pc_context_t *pcp, void *buffer);
    int		(*version_sync)(pc_context_t *pcp);
} v_dispatch_table_t;

/*
 * Common parameters.
 */
#define	CRC_UNIT_BITS	8
#define	CRC_TABLE_LEN	(1<<CRC_UNIT_BITS)

/*
 * This really should be in partclone.h.
 */
static const char cmagicstr[] = "BiTmAgIc";

/*
 * partclone version 1 file format handling.
 */
#define	V1_DEFAULT_FACTOR	10		/* 1024 entries/index */

/*
 * Per-version specific handles.
 */
typedef struct version_1_context {
    unsigned char	*bitmap;		/* Usage bitmap */
    u_int64_t		*v1_sumcount;		/* Precalculated indices */
    u_int64_t		v1_nvbcount;		/* Preceding valid blocks */
    unsigned long	v1_crc_tab32[CRC_TABLE_LEN];
						/* Precalculated CRC table */
    u_int16_t		v1_bitmap_factor;	/* log2(entries)/index */
} v1_context_t;


void
init_crc32_table(v1_context_t *v1p)
{
    int i;
    for (i=0; i<CRC_TABLE_LEN; i++) {
	int j;
	unsigned long init_crc = (unsigned long) i;
	for (j=0; j<CRC_UNIT_BITS; j++) {
	    init_crc = (init_crc & 0x00000001L) ?
		(init_crc >> 1) ^ 0xEDB88320L :
		(init_crc >> 1);
	}
	v1p->v1_crc_tab32[i] = init_crc;
    }
    v1p->v1_bitmap_factor = V1_DEFAULT_FACTOR;
}

/*
 * This routine is so wrong.  This isn't actually a CRC over the buffer,
 * it's a CRC of the first byte, iterated "size" number of times.  This is
 * to mimic the behavior of partclone (ech).
 */
static inline unsigned long
v1_crc32(v1_context_t *v1p, unsigned long crc, void *buf, size_t size)
{
    size_t s;
    unsigned long tmp;
    const unsigned char *bufc = (unsigned char*)buf;

    for (s=0; s<size; s++) {
	/*
	 * The copied logic advanced s without using it to offset into buf.
	 * The correct thing to do here would be to move buf[s], but we
	 * want this to be able to successfully read the files generated
	 * by partclone.
	 */
	unsigned char c = bufc[0];
	tmp = crc ^ (((unsigned long) c) & 0x000000ffL);
	crc = (crc >> 8) ^ v1p->v1_crc_tab32[ tmp & 0xff ];
    }
    return(crc);
}

static inline unsigned long
v2_crc32(v1_context_t *v1p, unsigned long crc, void *buf, size_t size)
{
    size_t s;
    unsigned long tmp;
    const unsigned char *bufc = (unsigned char*)buf;

    for (s=0; s<size; ++s) {
	unsigned char c = bufc[s];
	tmp = crc ^ (((unsigned long) c) & 0x000000ffL);
	crc = (crc >> 8) ^ v1p->v1_crc_tab32[ tmp & 0xff ];
    }
    return(crc);
}

int
init_omode(pc_context_t *pcp)
{
    int error = 0;

    if (pcp->pc_cf_path &&
	((int) pcp->pc_omode >= (int) SYSDEP_OPEN_RW) &&
	((error = cf_init(pcp->pc_cf_path, pcp->pc_desc.fs.block_size,
			  pcp->pc_desc.fs.totalblock,
			  &pcp->pc_cf_handle)) == 0)) {
	pcp->pc_flags |= PC_CF_OPEN;
    } else {
	if ((int) pcp->pc_omode < (int) SYSDEP_OPEN_RW)
	    pcp->pc_flags |= PC_READ_ONLY;
	else
	    /*
	     * Completely discard errors here.
	     */
	    error = 0;
    }
    return(error);
}

int
alloc_v1_context(pc_context_t *pcp)
{
    int error;
    v1_context_t *v1p;

    if ((error = posix_malloc(&v1p, sizeof(*v1p))) == 0) {
	memset(v1p, 0, sizeof(*v1p));
	pcp->pc_verdep = v1p;
	pcp->pc_flags |= (PC_HAVE_VERDEP|PC_VERSION_INIT);
    }
    return(error);
}
/*
 * v1_init	- Initialize version 1 file handling.
 *
 * - Allocate and initialize version 1 handle.
 * - Precalculate the CRC table.
 */
static int
v1_init(pc_context_t *pcp)
{
    int error = EINVAL;
    u_int64_t r_size;

    if (PCTX_VALID(pcp)) {
	/*
	 * Read image details
	 */
	file_system_info_v1 fs_v1;
	image_options_v1 options_v1;

	error = posix_read(pcp->pc_fd, &fs_v1, sizeof(fs_v1), &r_size);
	if (error == 0 && r_size == sizeof(fs_v1)) {
	    error = posix_read(pcp->pc_fd, &options_v1,
			       sizeof(options_v1), &r_size);
	    if (error == 0 && r_size == sizeof(options_v1)) {
		pcp->pc_desc.fs.block_size = fs_v1.block_size;
		pcp->pc_desc.fs.device_size = fs_v1.device_size;
		pcp->pc_desc.fs.totalblock = fs_v1.totalblock;
		pcp->pc_desc.fs.usedblocks = fs_v1.usedblocks;
		strcpy(pcp->pc_desc.fs.fs, ((image_head_v1*)&(pcp->pc_desc.head))->fs);
		/* options_v1 contains only zeros
		   All v1 images uses the same options */
		pcp->pc_desc.options.image_version = 0x0001;
		pcp->pc_desc.options.checksum_mode = CSM_CRC32_0001;
		pcp->pc_desc.options.checksum_size = CRC32_SIZE;
		pcp->pc_desc.options.blocks_per_checksum = 1;
		pcp->pc_desc.options.reseed_checksum = 0;
		pcp->pc_desc.options.bitmap_mode = BM_BYTE;
		/* cleanup head to make it "v2"-like */
		pcp->pc_desc.head.magic[IMAGE_MAGIC_SIZE] = 0;
		memset(&pcp->pc_desc.head.ptc_version, 0,
			sizeof(pcp->pc_desc.head.ptc_version));
		pcp->pc_desc.head.endianess = 0;

		if ((error = alloc_v1_context(pcp)) == 0 &&
		    (error = init_omode(pcp)) == 0) {
		    init_crc32_table(pcp->pc_verdep);
		}
	    } else {
		error = EIO;
	    }
	} else {
	    error = EIO;
	}
    }
    return(error);
}

/*
 * v2_init - Initialize version 2 file handling.
 *
 * - Allocate and initialize version 2 handle.
 * - Precalculate the CRC table.
 */
static int
v2_init(pc_context_t *pcp)
{
    int error = EINVAL;
    u_int64_t r_size;

    if (PCTX_VALID(pcp)) {
	/*
	 * Read image details
	 */
	error = posix_read(pcp->pc_fd, &pcp->pc_desc.fs,
			   sizeof(pcp->pc_desc.fs), &r_size);
	if (error != 0 || r_size != sizeof(pcp->pc_desc.fs)) {
	    error = EIO;
	} else {

	    error = posix_read(pcp->pc_fd, &pcp->pc_desc.options,
			       sizeof(pcp->pc_desc.options), &r_size);
	    if (error != 0 || r_size != sizeof(pcp->pc_desc.options)) {
		error = EIO;
	    } else {
		if ((error = alloc_v1_context(pcp)) == 0 &&
		    (error = init_omode(pcp)) == 0) {
		    init_crc32_table(pcp->pc_verdep);

		    unsigned cs_r;
		    unsigned cs = CRC32_SEED_PARTCLONE;
		    v1_context_t *v1p = (v1_context_t *)pcp->pc_verdep;
		    cs = v2_crc32(v1p, cs, &pcp->pc_desc, sizeof(pcp->pc_desc));
		    if ((error =
			  posix_read(pcp->pc_fd, &cs_r, CRC32_SIZE, &r_size))
			 == 0) {
			if (r_size != CRC32_SIZE)
			    error = EIO;
			else if (cs != cs_r)
			    error = EINVAL;
		    }
		}
	    }
	}
    }
    return(error);
}

int
read_bitmap_byte(pc_context_t *pcp, u_int64_t to_read)
{
    unsigned char buf[16384];
    v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;
    u_int64_t next_read, offset = 0;
    u_int64_t r_size;
    int error;

    while(to_read > 0) {
	next_read = to_read <= sizeof(buf) ? to_read : sizeof(buf);
	error = posix_read(pcp->pc_fd, buf, next_read, &r_size);
	if ((error == 0) && (r_size == next_read)) {
	    u_int64_t idx = 0;

	    to_read -= next_read;
	    for(;idx < next_read; ++idx, ++offset) {
		/* [2011-08]
		 * ...sigh... the *bitmap* can have more than
		 * two values.  It can be 1, in which case it's
		 * definitely in the file.  It can be zero
		 * in which case, it's definitely not in the
		 * file.  And it can be anything else that fits
		 * into a byte?  What does it mean?  I don't
		 * know.  But it's not set.
		 */
		if (buf[idx] == 1)
		    bitmap_set_bit(v1p->bitmap, offset);
	    }
	} else {
	    if (r_size != next_read) {
		error = EINVAL;
	    }
	    break;
	}
    }
    return(error);
}

static int
v1_read_bitmap(pc_context_t *pcp)
{
    int error;
    v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;
    u_int64_t bitmap_size = BITS_TO_BYTES(pcp->pc_desc.fs.totalblock);

    memset(v1p->bitmap, 0, bitmap_size);

    /*
     * Fill the bitmap and look for the magic string
     */
    if ((error = read_bitmap_byte(pcp, pcp->pc_desc.fs.totalblock)) == 0) {
	u_int64_t r_size;
	char magicstr[MAGIC_LEN];

	if ((error = posix_read(pcp->pc_fd, magicstr,
			        sizeof(magicstr), &r_size)) == 0) {
	    if ((r_size != sizeof(magicstr)) ||
		(memcmp(magicstr, cmagicstr, sizeof(magicstr)) != 0)) {
		error = EINVAL;
	    }
	}
    }
    return(error);
}

int
generate_usedblocks_map(pc_context_t *pcp, u_int64_t* nsetp)
{
    int error;
    v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;

    if ((error =
	 posix_malloc(&v1p->v1_sumcount,
		      ((pcp->pc_desc.fs.totalblock >> v1p->v1_bitmap_factor)+1) *
		      sizeof(u_int64_t))) == 0) {
	/*
	 * Precalculate the count of preceding valid blocks
	 * for every 1k blocks.
	 */
	u_int64_t i, nset = 0;
	for (i=0; i<pcp->pc_desc.fs.totalblock; i++) {
	    if ((i & ((1<<v1p->v1_bitmap_factor)-1)) == 0) {
		v1p->v1_sumcount[i>>v1p->v1_bitmap_factor] = nset;
	    }
	    if (bitmap_test_bit(v1p->bitmap, i))
		++nset;
	}
	*nsetp = nset;
    }
    return(error);
}

/*
 * v1_verify	- Verify the currently open file.
 *
 * - Load the bitmap
 * - Precalculate the count of preceding valid blocks.
 */
static int
v1_verify(pc_context_t *pcp)
{
    int error = EINVAL;

    if (PCTX_OPEN(pcp)) {
	v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;

	posix_seek(pcp->pc_fd, sizeof(image_desc_v1),
		   SYSDEP_SEEK_ABSOLUTE, (u_int64_t *) NULL);
	/*
	 * Allocate the bitmap.
	 */
	if ((error = posix_malloc(&v1p->bitmap,
				  BITS_TO_BYTES(pcp->pc_desc.fs.totalblock)))
	    == 0) {
	    u_int64_t nset;

	    if ((error = v1_read_bitmap(pcp) == 0) &&
		(error = generate_usedblocks_map(pcp, &nset)) == 0) {
		/*
		 * Fixup device size...
		 */
		u_int64_t device_size;

		device_size = pcp->pc_desc.fs.totalblock *
			      pcp->pc_desc.fs.block_size;
		if (pcp->pc_desc.fs.device_size != device_size)
		    pcp->pc_desc.fs.device_size  = device_size;

		/*
		 * Is the count of used blocks good?
		 */
		if (pcp->pc_desc.fs.usedblocks != nset) {
		    /* [2011-08]
		     * What should we do in this case?  The old
		     * version punted, thinking that it is an
		     * inconsistency.  However, at the time of
		     * header construction it may not be possible
		     * to get a definitive count of used blocks
		     * without scanning the entire bitmap.  So
		     * some filesystems use a derived count, which
		     * can be off.  So, we don't turn on
		     * STRICT_HEADERS (for now).
		     */
#ifdef	STRICT_HEADERS
		    error = EFAULT;
#else	/* STRICT_HEADERS */
		    /* what?! - Fix it up silently. */
		    pcp->pc_desc.fs.usedblocks = nset;
#endif	/* STRICT_HEADERS */
		}
		if (!error && pcp->pc_cf_handle) {
		    /*
		     * Verify the change file, if present.
		     */
		    error = cf_verify(pcp->pc_cf_handle);
		    if (!error)
			pcp->pc_flags |= PC_CF_VERIFIED;
		}
	    }
	} else {
	    error = EINVAL;
	}
    }
    return(error);
}

static int
v2_verify(pc_context_t *pcp)
{
    int error = ENOSYS;

    return(error);
}

/*
 * v1_finish	- Finish version-specific handling.
 *
 * Free structures.
 */
static int
v1_finish(pc_context_t *pcp)
{
    int error = EINVAL;

    if (PCTX_HAVE_VERDEP(pcp)) {
	v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;

	if (v1p->bitmap)
	    (void) posix_free(v1p->bitmap);
	if (v1p->v1_sumcount)
	    (void) posix_free(v1p->v1_sumcount);
	(void) posix_free(v1p);
	pcp->pc_flags &= ~PC_HAVE_VERDEP;
	error = (pcp->pc_cf_handle) ? cf_finish(pcp->pc_cf_handle) : 0;
    }
    return(error);
}

static int
v2_finish(pc_context_t *pcp)
{
    int error = ENOSYS;

    return(error);
}

/*
 * v1_seek	- Version-specific handling for seeking to a particular block.
 *
 * Update the number of preceding valid blocks.
 */
static int
v1_seek(pc_context_t *pcp, u_int64_t blockno)
{
    int error = EINVAL;

    if (PCTX_HAVE_VERDEP(pcp)) {
	v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;
	u_int64_t pbn;

	/*
	 * Starting with the hint that is nearest, start calculating
	 * the preceding valid blocks.
	 */
	v1p->v1_nvbcount = v1p->v1_sumcount[blockno >> v1p->v1_bitmap_factor];
	for (pbn = blockno & ~((1<<v1p->v1_bitmap_factor)-1); 
	     pbn < blockno; 
	     pbn++) {
	    if (bitmap_test_bit(v1p->bitmap, pbn)) {
		v1p->v1_nvbcount++;
	    }
	}
	error = (pcp->pc_cf_handle) ? cf_seek(pcp->pc_cf_handle, blockno) : 0;
	    
    }
    return(error);
}

static int
v2_seek(pc_context_t *pcp, u_int64_t blockno)
{
    int error = ENOSYS;

    return(error);
}

/*
 * rblock2offset	- Calculate offset in image file of particular
 *			  block.
 */
static inline int64_t
rblock2offset(pc_context_t *pcp, u_int64_t rbnum)
{
    return(sizeof(image_desc_v1) + pcp->pc_desc.fs.totalblock + MAGIC_LEN +
	   (rbnum * (pcp->pc_desc.fs.block_size + pcp->pc_desc.options.checksum_size)));
}

/*
 * v1_readblock	- Read the block at the current position.
 */
static int
v1_readblock(pc_context_t *pcp, void *buffer)
{
    int error = EINVAL;

    /*
     * Check to see if we can get the result from the change file.
     */
    if (PCTX_HAVE_VERDEP(pcp) &&
	(!pcp->pc_cf_handle || 
	 ((error = cf_readblock(pcp->pc_cf_handle, buffer)) != 0))) {
	v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;

	/*
	 * Determine whether the block is used/valid.
	 */
	if (bitmap_test_bit(v1p->bitmap, pcp->pc_curblock)) {
	    /* block is valid */
	    int64_t boffs = rblock2offset(pcp, v1p->v1_nvbcount);
	    if ((error =
		 posix_seek(pcp->pc_fd, boffs, SYSDEP_SEEK_ABSOLUTE,
			    (u_int64_t *) NULL)) == 0) {
		u_int64_t r_size, c_size;
		unsigned long crc_ck = CRC32_SEED_PARTCLONE;
		unsigned long crc_ck2;
		/*
		 * The stored checksums are apparently incremental.  So,
		 * get the CRC from the previous block and use it as the
		 * starting checksum for this block.
		 */
		if (v1p->v1_nvbcount) {
		    (void) posix_seek(pcp->pc_fd,
				      -pcp->pc_desc.options.checksum_size,
				      SYSDEP_SEEK_RELATIVE, (u_int64_t *) NULL);
		    (void) posix_read(pcp->pc_fd, &crc_ck,
				      pcp->pc_desc.options.checksum_size,
				      &c_size);
		}
		(void) posix_read(pcp->pc_fd, buffer, pcp->pc_desc.fs.block_size,
				  &r_size);
		crc_ck = v1_crc32(v1p, crc_ck, buffer, r_size);
		(void) posix_read(pcp->pc_fd, &crc_ck2,
				  pcp->pc_desc.options.checksum_size, &c_size);
		/*
		 * XXX - endian?
		 */
		if ((r_size != pcp->pc_desc.fs.block_size) ||
		    (c_size != pcp->pc_desc.options.checksum_size) ||
		    (crc_ck != crc_ck2)) {
		    error = EIO;
		}
		v1p->v1_nvbcount++;
	    }
	} else {
	    /*
	     * If we're reading an invalid block, use the handy buffer.
	     */
	    memcpy(buffer, pcp->pc_ivblock, pcp->pc_desc.fs.block_size);
	    error = 0;	/* This shouldn't be necessary... */
	}
    }
    return(error);
}

static int
v2_readblock(pc_context_t *pcp, void *buffer)
{
    int error = ENOSYS;

    return(error);
}

/*
 * v1_blockused	- Is the current block in use?
 */
static int
v1_blockused(pc_context_t *pcp)
{
    int retval = BLOCK_ERROR;
    if (PCTX_HAVE_VERDEP(pcp)) {
	v1_context_t *v1p = (v1_context_t *) pcp->pc_verdep;

	retval = (pcp->pc_cf_handle && cf_blockused(pcp->pc_cf_handle)) ? 1 : 
	    bitmap_test_bit(v1p->bitmap, pcp->pc_curblock);
    }
    return(retval);
}

static int
v2_blockused(pc_context_t *pcp)
{
    int retval = ENOSYS;

    return(retval);
}

/*
 * v1_writeblock	- Write block at current location.
 */
static int
v1_writeblock(pc_context_t *pcp, void *buffer)
{
    int error = EINVAL;

    /*
     * Make sure we're initialized.
     */
    if (PCTX_HAVE_VERDEP(pcp)) {
	if (!PCTX_WRITEREADY(pcp)) {
	    if (!PCTX_HAVE_CF_PATH(pcp)) {
		/*
		 * We have to make up a name.
		 */
		if ((error =
		     posix_malloc(&pcp->pc_cf_path,
				  strlen(pcp->pc_path) + strlen(cf_trailer) + 1))
		    == 0) {
		    memcpy(pcp->pc_cf_path, pcp->pc_path, strlen(pcp->pc_path));
		    memcpy(&pcp->pc_cf_path[strlen(pcp->pc_path)],
			   cf_trailer, strlen(cf_trailer)+1);
		    pcp->pc_flags |= PC_HAVE_CF_PATH;
		}
	    }
	    error = cf_create(pcp->pc_cf_path, pcp->pc_desc.fs.block_size,
			      pcp->pc_desc.fs.totalblock, &pcp->pc_cf_handle);
	    if (!error) {
		pcp->pc_flags |= (PC_HAVE_CFDEP|PC_CF_VERIFIED);
	    }
	} else {
	    error = 0;
	}
	if (!error) {
	    error = cf_writeblock(pcp->pc_cf_handle, buffer);
	}
    }
    return(error);
}

static int
v2_writeblock(pc_context_t *pcp, void *buffer)
{
    int error = ENOSYS;

    return(error);
}

/*
 * v1_sync	- Flush changes to change file
 */
static int
v1_sync(pc_context_t *pcp)
{
    int error = EINVAL;
    if (PCTX_WRITEREADY(pcp)) {
	error = cf_sync(pcp->pc_cf_handle);
    }
    return(error);
}

static int
v2_sync(pc_context_t *pcp)
{
    int error = ENOSYS;
    return(error);
}

/*
 * Dispatch table for handling various versions.
 */
static const v_dispatch_table_t
version_table[] = {
    { "0001", 
      v1_init, v1_verify, v1_finish, v1_seek, v1_readblock, v1_blockused,
      v1_writeblock, v1_sync },
    { "0002",
      v2_init, v2_verify, v2_finish, v2_seek, v2_readblock, v2_blockused,
      v2_writeblock, v2_sync },
};

/*
 * partclone_close()	- Close the image handle.
 */
int
partclone_close(void *rp)
{
    int error = EINVAL;
    pc_context_t *pcp = (pc_context_t *) rp;

    if (PCTX_VALID(pcp)) {
	if (PCTX_CF_OPEN(pcp)) {
	    (void) (*pcp->pc_dispatch->version_sync)(pcp);
	}
	if (PCTX_OPEN(pcp)) {
	    (void) posix_close(pcp->pc_fd);
	}
	if (PCTX_HAVE_PATH(pcp)) {
	    (void) posix_free(pcp->pc_path);
	}
	if (PCTX_HAVE_CF_PATH(pcp)) {
	    (void) posix_free(pcp->pc_cf_path);
	}
	if (PCTX_HAVE_IVBLOCK(pcp)) {
	    (void) posix_free(pcp->pc_ivblock);
	}
	if (PCTX_HAVE_VERDEP(pcp)) {
	    if (pcp->pc_dispatch && pcp->pc_dispatch->version_finish)
		error = (*pcp->pc_dispatch->version_finish)(pcp);
	}
	(void) posix_free(pcp);
	error = 0;
    }
    return(error);
}

/*
 * partclone_open	- Open an image handle using the system-specific
 *			  interfaces.
 */
int
partclone_open(const char *path, const char *cfpath, sysdep_open_mode_t omode,
	       void **rpp)
{
    int error = EINVAL;
    pc_context_t *pcp;
    error = posix_malloc(&pcp, sizeof(*pcp));

    if (pcp) {
	memset(pcp, 0, sizeof(*pcp));
	pcp->pc_flags |= PC_VALID;

	if ((error = posix_open(&pcp->pc_fd, path, SYSDEP_OPEN_RO)) == 0) {
	    pcp->pc_flags |= PC_OPEN;
	    if ((error =
		 posix_malloc(&pcp->pc_path, strlen(path)+1)) == 0) {
		pcp->pc_flags |= PC_HAVE_PATH;
		pcp->pc_omode = omode;
		memcpy(pcp->pc_path, path, strlen(path)+1);
		if (cfpath &&
		    ((error =
		      posix_malloc(&pcp->pc_cf_path, strlen(cfpath)+1))
		     == 0)) {
		    pcp->pc_flags |= PC_HAVE_CF_PATH;
		    memcpy(pcp->pc_cf_path, cfpath, strlen(cfpath)+1);
		}
		if (!error)
		    *rpp = (void *) pcp;
	    }
	}
	if (error) {
	    partclone_close(pcp);
	}
    }
    if (error) {
	*rpp = (void *) NULL;
    }
    return(error);
}

/*
 * partclone_verify	- Determine the version of the file and verify it.
 */
int
partclone_verify(void *rp)
{
    int error = EINVAL;
    pc_context_t *pcp = (pc_context_t *) rp;

    if (PCTX_OPEN(pcp)) {
	u_int64_t r_size;

	/*
	 * Read the header.
	 */
	error = posix_read(pcp->pc_fd, &pcp->pc_desc.head,
			   sizeof(pcp->pc_desc.head), &r_size);
	if ((error == 0) &&
	    (r_size == sizeof(pcp->pc_desc.head))) {
	    int veridx;
	    int found = -1;

	    /*
	     * Verify the header magic.
	     */
	    if (memcmp(pcp->pc_desc.head.magic, IMAGE_MAGIC, IMAGE_MAGIC_SIZE) != 0) {
		error = EINVAL;
	    } else {
		int veridx;
		int found = -1;

		pcp->pc_flags |= PC_HEAD_VALID;
		/*
		 * Scan through the version table and find a match for the
		 * version string.
		 */
		for (veridx = 0;
		     veridx < sizeof(version_table)/sizeof(version_table[0]);
		     veridx++) {
		    if (memcmp(pcp->pc_desc.head.version,
			       version_table[veridx].version,
			       sizeof(pcp->pc_desc.head.version)) == 0) {
			found = veridx;
			break;
		    }
		}

		/*
		 * See if we found a match.
		 */
		if (found >= 0) {
		    pcp->pc_dispatch = (v_dispatch_table_t *) &version_table[found];
		    /*
		     * Initialize the per-version handle.
		     */
		    if (!(error = (*pcp->pc_dispatch->version_init)(pcp))) {
			/*
			 * Verify the version header.
			 */
			if (!(error = (*pcp->pc_dispatch->version_verify)(pcp))) {
			    pcp->pc_flags |= PC_VERIFIED;
			    pcp->pc_curblock = 0;
			    /*
			     * Allocate a buffer for reading blocks
			     */
			    if ((error =
				posix_malloc(&pcp->pc_ivblock,
					     pcp->pc_desc.fs.block_size)) == 0) {
				memset(pcp->pc_ivblock, 69, 
				       pcp->pc_desc.fs.block_size);
				pcp->pc_flags |= PC_HAVE_IVBLOCK;
			    }
			}
		    }
		} else {
		    error = ENOENT;
		}
	    }
	}
    } else {
	if (error == 0) {
	    /*
	     * Implies:
	     * (r_size != sizeof(pcp->pc_desc)
	     */
	    error = EIO;
	}
    }
    return(error);
}

/*
 * partclone_blocksize	- Return the blocksize.
 */
int64_t
partclone_blocksize(void *rp)
{
    pc_context_t *pcp = (pc_context_t *) rp;
    return((PCTX_VERIFIED(pcp)) ? pcp->pc_desc.fs.block_size : -1);
}

/*
 * partclone_blockcount	- Return the total count of blocks.
 */
int64_t
partclone_blockcount(void *rp)
{
    pc_context_t *pcp = (pc_context_t *) rp;
    return((PCTX_VERIFIED(pcp)) ? pcp->pc_desc.fs.totalblock : -1);
}

/*
 * partclone_seek	- Seek to a particular block.
 */
int
partclone_seek(void *rp, u_int64_t blockno)
{
    int error = EINVAL;
    pc_context_t *pcp = (pc_context_t *) rp;

    if (PCTX_READREADY(pcp) && (blockno <= pcp->pc_desc.fs.totalblock)) {
	/*
	 * Use the version-specific seek routine to do the heavy lifting.
	 */
	if (!(error = (*pcp->pc_dispatch->version_seek)(pcp, blockno))) {
	    pcp->pc_curblock = blockno;
	}
    }
    return(error);
}

/*
 * partclone_tell	- Obtain the current position.
 */
u_int64_t
partclone_tell(void *rp)
{
    pc_context_t *pcp = (pc_context_t *) rp;

    return((PCTX_READREADY(pcp)) ? pcp->pc_curblock : ~0);
}

/*
 * partclone_readblocks	- Read blocks from the current position.
 */
int
partclone_readblocks(void *rp, void *buffer, u_int64_t nblocks)
{
    int error = EINVAL;
    pc_context_t *pcp = (pc_context_t *) rp;
    if (PCTX_READREADY(pcp)) {
	u_int64_t bindex;
	void *cbp = buffer;

	/*
	 * Iterate and use the version-specific routine to do the heavy
	 * lifting.
	 */
	for (bindex = 0; bindex < nblocks; bindex++) {
	    if ((error = (*pcp->pc_dispatch->version_readblock)(pcp, cbp))) {
		break;
	    }
	    pcp->pc_curblock++;
	    cbp += pcp->pc_desc.fs.block_size;
	}
    }
    return(error);
}

/*
 * partclone_block_used	- Determine if the current block is used.
 */
int
partclone_block_used(void *rp)
{
    pc_context_t *pcp = (pc_context_t *) rp;
    return((PCTX_READREADY(pcp)) ? (*pcp->pc_dispatch->version_blockused)(pcp) :
	   BLOCK_ERROR);
}

/*
 * partclone_writeblocks	- Write blocks to the current position.
 */
int
partclone_writeblocks(void *rp, void *buffer, u_int64_t nblocks)
{
    int error = EINVAL;
    pc_context_t *pcp = (pc_context_t *) rp;

    if (PCTX_WRITEABLE(pcp)) {
	u_int64_t bindex;
	void *cbp = buffer;

	/*
	 * Iterate and use the version-specific routine to do the heavy
	 * lifting.
	 */
	for (bindex = 0; bindex < nblocks; bindex++) {
	    if ((error = (*pcp->pc_dispatch->version_writeblock)(pcp, cbp))) {
		break;
	    }
	    pcp->pc_curblock++;
	    cbp += pcp->pc_desc.fs.block_size;
	}
    }
    return(error);
}

/*
 * partclone_sync	- Commit changes to image.
 */
int
partclone_sync(void *rp)
{
    pc_context_t *pcp = (pc_context_t *) rp;

    return( (PCTX_WRITEREADY(pcp)) ?
	    (*pcp->pc_dispatch->version_sync)(pcp) :
	    EINVAL );
}

/*
 * partclone_probe	- Is this a partclone image?
 */
int
partclone_probe(const char *path)
{
    void *testh = (void *) NULL;
    int error = partclone_open(path, (char *) NULL, SYSDEP_OPEN_RO, &testh);
    if (!error) {
	error = partclone_verify(testh);
	partclone_close(testh);
    }
    return(error);
}

/*
 * The image type dispatch table.
 */
const image_dispatch_t partclone_image_type = {
    "partclone image",
    partclone_probe,
    partclone_open,
    partclone_close,
    partclone_verify,
    partclone_blocksize,
    partclone_blockcount,
    partclone_seek,
    partclone_tell,
    partclone_readblocks,
    partclone_block_used,
    partclone_writeblocks,
    partclone_sync
};
