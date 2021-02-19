/*
 * partclone_imageinfo	- A cursory check that a file looks OK.
 */
/*
 * Copyright (c) 2013, Ideal World, Inc.  All Rights Reserved.
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
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "libpartclone.h"
#include "sysdep_posix.h"
#include "partclone.h"

#define CRC_UNIT_BITS 8
#define CRC_TABLE_LEN (1 << CRC_UNIT_BITS)
#define CRC_SIZE      4

typedef struct version_1_context {
    unsigned char	*v1_bitmap;		/* Usage bitmap */
    uint64_t		*v1_sumcount;		/* Precalculated indices */
    uint64_t		v1_nvbcount;		/* Preceding valid blocks */
    unsigned long	v1_crc_tab32[CRC_TABLE_LEN];
						/* Precalculated CRC table */
    uint16_t		v1_bitmap_factor;	/* log2(entries)/index */
} v1_context_t;

int
countBlockTypes(pc_context_t *p, const char filename[],
    uint64_t *lastset_)
{
    v1_context_t *v = (v1_context_t *)p->pc_verdep;
    uint64_t bmi;
    uint64_t bmscanned = 0, unset = 0, set = 0, strange = 0;
    int anomalies = 0, lastset = 0;

    for (bmi = 0; bmi < p->pc_head.totalblock; ++bmi)
    {
        switch (v->v1_bitmap[bmi])
        {
        case 0:
            ++unset;
            break;
        case 1:
            ++set;
            lastset = bmi;
            break;
        default:
            ++strange;
            fprintf(stderr,
                    "%s: block %lu (0x%016lx) bitmap %d (0x%02x)?\n",
                    filename, bmi, bmi,
                    v->v1_bitmap[bmi], v->v1_bitmap[bmi]);
            ++anomalies;
            break;
        }
        ++bmscanned;
    }

    fprintf(stdout,
        "%s: %llu blocks, %" PRIu64 " blocks scanned, %" PRIu64 " unset, %" PRIu64 " set, %" PRIu64 " strange\n",
        filename, p->pc_head.totalblock, bmscanned, unset, set, strange);

    *lastset_ = lastset;
    return anomalies;
}

int
showImageInfo(const char filename[])
{
    void *pctx;
    int error;
    int dontcare = 0;
    int anomalies = 0;

    if ((error = partclone_open(filename, (char *)NULL, SYSDEP_OPEN_RO,
                                &posix_dispatch, &pctx)))
    {
        fprintf(stderr, "%s: cannot open image (error(%d) = %s)\n",
            filename, error, strerror(error));
        anomalies++;
    }
    else
    {
        pc_context_t *p = (pc_context_t *)pctx;
        uint64_t lastset = 0;
        if (((error = partclone_verify(pctx))) && !dontcare)
        {
            fprintf(stderr, "%s: cannot verify image (error(%d) = %s)\n",
                filename, error, strerror(error));
            anomalies++;
        }
        else
        {
            unsigned char *iob;

            if (error)
                p->pc_flags |= 4; //PC_VERIFIED

            anomalies = countBlockTypes(p, filename, &lastset);

            if ((iob = (unsigned char *)malloc(partclone_blocksize(pctx))) == NULL)
            {
                fprintf(stderr, "%s: cannot malloc %" PRId64 " bytes\n", filename,
                    partclone_blocksize(pctx));
                anomalies++;
            }
            else
            {
                int *fd = (int *)p->pc_fd;
                off_t sblkpos;
                off_t fsize;
                struct stat sbuf;
                error = partclone_seek(pctx, 0);
                error = partclone_readblocks(pctx, iob, 1);
                sblkpos = lseek(*fd, 0, SEEK_CUR);
                sblkpos -= (partclone_blocksize(pctx) + CRC_SIZE);
                fstat(*fd, &sbuf);
                fsize = sbuf.st_size;
                fprintf(stdout,
                    "%s: size is %lld bytes, blocks (%lld bytes) start at %lld: ",
                    filename, (long long)fsize,
                    (long long)partclone_blocksize(pctx), (long long)sblkpos);
                fsize -= sblkpos;
                fprintf(stdout, " %ld blocks written",
                    fsize / (partclone_blocksize(pctx) + CRC_SIZE));
                if (fsize % (partclone_blocksize(pctx) + CRC_SIZE))
                {
                    fprintf(stdout, ": %ld byte trailer\n",
                        fsize % (partclone_blocksize(pctx) + CRC_SIZE));
                }
                else
                {
                    fprintf(stdout, "\n");
                }
                if ((error = partclone_seek(pctx, lastset)))
                {
                    fprintf(stderr,
                        "%s: cannot seek to block %" PRIu64 ", error(%d) = %s\n",
                        filename, lastset, error, strerror(error));
                    anomalies++;
                }
                else
                {
                    if ((error = partclone_readblocks(pctx, iob, 1)))
                    {
                        fprintf(stderr,
                            "%s: cannot read block %" PRIu64 ", error(%d) = %s\n",
                            filename, lastset, error, strerror(error));
                        anomalies++;
                    }
                    else
                    {
                        off_t cpos, eofpos;

                        cpos = lseek(*fd, 0, SEEK_CUR);
                        eofpos = lseek(*fd, 0, SEEK_END);
                        if (cpos != eofpos)
                        {
                            fprintf(stderr,
                                "%s: position after last block = %ld, eof position = %ld, blocksize = %ld\n",
                                filename, cpos, eofpos,
                                partclone_blocksize(pctx) + CRC_SIZE);
                            anomalies++;
                        }
                        else
                        {
                            fprintf(stdout, "%s: read last block at end of file\n",
                                filename);
                        }
                    }
                }
                free(iob);
            }
        }
    }
    return anomalies;
}

int
main(int argc, char *argv[])
{
    int i;

    for (i = 1; i < argc; i++) {
        int anomalies = showImageInfo(argv[i]);
        if (anomalies) {
            fprintf(stdout, "!!! %s: %d problems\n", argv[i], anomalies);
        } else {
            fprintf(stdout, "%s: OK\n", argv[i]);
        }
    }
}
