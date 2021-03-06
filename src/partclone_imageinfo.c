/*
 * partclone_imageinfo.c - A cursory check that a file looks OK.
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
#ifdef HAVE_CONFIG_H
#    include "config.h"
#endif /* HAVE_CONFIG_H */
#include "libpartclone.h"
#include "partclone.h"
#include "sysdep_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define CRC_UNIT_BITS 8
#define CRC_TABLE_LEN (1 << CRC_UNIT_BITS)
#define CRC_SIZE      4

typedef struct version_1_context {
    unsigned char *v1_bitmap;                   /* Usage bitmap */
    uint64_t *     v1_sumcount;                 /* Precalculated indices */
    uint64_t       v1_nvbcount;                 /* Preceding valid blocks */
    unsigned long  v1_crc_tab32[CRC_TABLE_LEN]; /* Precalculated CRC table */
    uint16_t       v1_bitmap_factor;            /* log2(entries)/index */
} v1_context_t;

off_t
get_written_block_count(pc_context_t *pctx, off_t const data_size,
                        off_t *const extra_size) {
    off_t const bpc        = pctx->pc_head.blocks_per_checksum;
    off_t const crc_size   = pctx->pc_head.checksum_size;
    off_t const block_size = partclone_blocksize(pctx);

    if (bpc <= 1) {
        off_t const strip_size = block_size + crc_size;

        *extra_size = data_size % strip_size;

        return data_size / strip_size;
    }

    off_t const strip_size          = bpc * block_size + crc_size;
    off_t const strip_count         = data_size / strip_size;
    off_t const trailing_strip_size = data_size % strip_size;

    *extra_size = (trailing_strip_size - crc_size) % block_size;

    return strip_count * bpc + (trailing_strip_size - crc_size) / block_size;
}

int
main(int argc, char *argv[]) {
    int i;

    for (i = 1; i < argc; i++) {
        int   error;
        void *pctx;
        int   dontcare  = 0;
        int   anomalies = 0;

        if (i > 1) {
            fprintf(stdout, "\n");
        }

        if ((error = partclone_open(argv[i], (char *)NULL, SYSDEP_OPEN_RO,
                                    &posix_dispatch, &pctx)) == 0) {
            uint64_t bmscanned = 0, unset = 0, set = 0, strange = 0;
            uint64_t lastset = 0;
            if (((error = partclone_verify(pctx)) == 0) || dontcare) {
                pc_context_t * p = (pc_context_t *)pctx;
                v1_context_t * v = (v1_context_t *)p->pc_verdep;
                uint64_t       bmi;
                unsigned char *iob;

                if (dontcare && error)
                    p->pc_flags |= 4;
                for (bmi = 0; bmi < p->pc_head.totalblock; bmi++) {
                    switch (v->v1_bitmap[bmi]) {
                    case 0:
                        unset++;
                        break;
                    case 1:
                        set++;
                        lastset = bmi;
                        break;
                    default:
                        strange++;
                        fprintf(
                            stderr,
                            "%s: block %lu (0x%016lx) bitmap %d (0x%02x)?\n",
                            argv[i], bmi, bmi, v->v1_bitmap[bmi],
                            v->v1_bitmap[bmi]);
                        anomalies++;
                        break;
                    }
                    bmscanned++;
                }
                fprintf(stdout,
                        "%s: %llu blocks, %" PRIu64 " blocks scanned, %" PRIu64
                        " unset, %" PRIu64 " set, %" PRIu64 " strange\n",
                        argv[i], p->pc_head.totalblock, bmscanned, unset, set,
                        strange);
                if ((iob =
                         (unsigned char *)malloc(partclone_blocksize(pctx)))) {
                    int *       fd       = (int *)p->pc_fd;
                    int const   crc_size = p->pc_head.checksum_size;
                    off_t       sblkpos;
                    off_t       data_size;
                    off_t       extra_size;
                    struct stat sbuf;
                    error   = partclone_seek(pctx, 0);
                    error   = partclone_readblocks(pctx, iob, 1);
                    sblkpos = lseek(*fd, 0, SEEK_CUR);
                    sblkpos -= partclone_blocksize(pctx);
                    fstat(*fd, &sbuf);
                    fprintf(stdout,
                            "%s: size is %lld bytes, blocks (%lld bytes) start "
                            "at %lld",
                            argv[i], (long long)sbuf.st_size,
                            (long long)partclone_blocksize(pctx),
                            (long long)sblkpos);
                    data_size = sbuf.st_size - sblkpos;
                    fprintf(
                        stdout, ", %ld blocks written",
                        get_written_block_count(pctx, data_size, &extra_size));
                    if (extra_size) {
                        fprintf(stdout, ", %ld byte trailer\n", extra_size);
                        // Possible reasons:
                        // - could be extra data if the written block count
                        //   match with the value present in the header,
                        //   otherwise if the count is smaller the image file
                        //   may have been trunked.
                        //   This may be a sign that the image is corrupted.
                    } else {
                        fprintf(stdout, "\n");
                    }
                    if ((error = partclone_seek(pctx, lastset)) == 0) {
                        if ((error = partclone_readblocks(pctx, iob, 1)) == 0) {
                            off_t cpos, eofpos;

                            cpos   = lseek(*fd, 0, SEEK_CUR);
                            eofpos = lseek(*fd, 0, SEEK_END);
                            if (cpos == (eofpos - crc_size)) {
                                fprintf(stdout,
                                        "%s: read last block at end of file - "
                                        "success\n",
                                        argv[i]);
                            } else {
                                fprintf(stderr,
                                        "%s: position after last block = %ld, "
                                        "eof position = %ld\n",
                                        argv[i], cpos, eofpos);
                                anomalies++;
                            }
                        } else {
                            fprintf(stderr,
                                    "%s: cannot read block %" PRIu64
                                    ", error(%d) = %s\n",
                                    argv[i], lastset, error, strerror(error));
                            anomalies++;
                        }
                    } else {
                        fprintf(stderr,
                                "%s: cannot seek to block %" PRIu64
                                ", error(%d) = %s\n",
                                argv[i], lastset, error, strerror(error));
                        anomalies++;
                    }
                    free(iob);
                } else {
                    fprintf(stderr, "%s: cannot malloc %" PRId64 " bytes\n",
                            argv[i], partclone_blocksize(pctx));
                    anomalies++;
                }
            } else {
                fprintf(stderr, "%s: cannot verify image (error(%d) = %s)\n",
                        argv[i], error, strerror(error));
                anomalies++;
            }
            partclone_close(pctx);
        } else {
            fprintf(stderr, "%s: cannot open image (error(%d) = %s)\n", argv[i],
                    error, strerror(error));
            anomalies++;
        }
        if (anomalies) {
            fprintf(stdout, "!!! %s: %d problems\n", argv[i], anomalies);
        } else {
            fprintf(stdout, "%s: OK\n", argv[i]);
        }
    }
}
