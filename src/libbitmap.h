/*
 * changefile.h	- bitmap interface
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

#ifndef	_LIBBITMAP_H_
#define	_LIBBITMAP_H_ 1

#define BITS_PER_BYTE 8
#define BITS_TO_BYTES(bits) (((bits)+BITS_PER_BYTE-1)/BITS_PER_BYTE)

static inline void bitmap_set_bit(unsigned char *bitmap, u_int64_t pos) {
    u_int64_t idx = pos /  BITS_PER_BYTE;
    u_int64_t bit = pos & (BITS_PER_BYTE-1);
    bitmap[idx]  |= 1U << bit;
}

static inline int bitmap_test_bit(unsigned char *bitmap, u_int64_t pos) {
    u_int64_t idx = pos /  BITS_PER_BYTE;
    u_int64_t bit = pos & (BITS_PER_BYTE-1);

    return((bitmap[idx] >> bit) & 1);
}

#endif /* _LIBBITMAP_H_ */
