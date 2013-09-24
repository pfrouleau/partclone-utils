/**
 * partclone.h - Part of Partclone project.
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * function and structure used by main.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef	_PARTCLONE_H_
#define	_PARTCLONE_H_	1

#include <stdint.h>

#define IMAGE_MAGIC "partclone-image"
#define IMAGE_MAGIC_SIZE 15

#define FS_MAGIC_SIZE 15
#define reiserfs_MAGIC "REISERFS"
#define reiser4_MAGIC "REISER4"
#define xfs_MAGIC "XFS"
#define extfs_MAGIC "EXTFS"
#define ext2_MAGIC "EXT2"
#define ext3_MAGIC "EXT3"
#define ext4_MAGIC "EXT4"
#define hfsplus_MAGIC "HFS Plus"
#define fat_MAGIC "FAT"
#define ntfs_MAGIC "NTFS"
#define ufs_MAGIC "UFS"
#define vmfs_MAGIC "VMFS"
#define jfs_MAGIC "JFS"
#define raw_MAGIC "raw"

#define IMAGE_VERSION_SIZE 4
#define IMAGE_VERSION "0001"
#define PARTCLONE_VERSION_SIZE (FS_MAGIC_SIZE-1)
#define SECTOR_SIZE 512
#define CRC32_SIZE 4
#define CRC32_SEED_PARTCLONE 0xFFFFFFFFU

/* Disable fields alignment for struct stored in the image */
#pragma pack(push, 1)

typedef struct
{
    char magic[IMAGE_MAGIC_SIZE];
    char fs[FS_MAGIC_SIZE];
    char version[IMAGE_VERSION_SIZE];
    char padding[2];

} image_head_v1;

typedef struct
{
    int block_size;
    unsigned long long device_size;
    unsigned long long totalblock;
    unsigned long long usedblocks;

} file_system_info_v1;

typedef struct
{
    char buff[4096];

} image_options_v1;

typedef struct
{
	char magic[IMAGE_MAGIC_SIZE+1];

	/// Partclone's version who created the image, ex: "2.61"
	char ptc_version[PARTCLONE_VERSION_SIZE];

	/// Image's version
	char version[IMAGE_VERSION_SIZE];

	/// 0xC0DE = little-endian, 0xDEC0 = big-endian
	uint16_t endianess;

} image_head_v2;

typedef struct
{
	/// File system type
	char fs[FS_MAGIC_SIZE+1];

	/// Size of the source device, in bytes
	unsigned long long device_size;

	/// Number of blocks in the file system
	unsigned long long totalblock;

	/// Number of blocks in use as reported by the file system
	unsigned long long usedblocks;

	/// Number of blocks in use in the bitmap
	unsigned long long used_bitmap;

	/// Number of bytes in each block
	unsigned int  block_size;

} file_system_info_v2;

typedef enum
{
	BM_NONE = 0x00,
	BM_BIT  = 0x01,
	BM_BYTE = 0x08,

} bitmap_mode_t;

typedef enum
{
	CSM_NONE  = 0x00,
	CSM_CRC32 = 0x20,
	CSM_CRC32_0001 = 0xFF,
} checksum_mode_enum;

typedef struct
{
	/// Number of bytes used by this struct
	uint32_t feature_size;

	/// version of the image
	uint16_t image_version;

	/// partclone's compilation architecture: 32 bits or 64 bits
	uint16_t cpu_bits;

	/// checksum algorithm used (see checksum_mode_enum
	uint16_t checksum_mode;

	/// Size of one checksum, in bytes. 0 when NONE, 4 with CRC32, etc.
	uint16_t checksum_size;

	/// How many consecutive blocks are checksumed together.
	uint32_t blocks_per_checksum;

	/// Reseed the checksum after each write (1 = yes; 0 = no)
	uint8_t reseed_checksum;

	/// Kind of bitmap stored in the image (see bitmap_mode_enum)
	uint8_t bitmap_mode;

} image_options_v2;

struct image_desc_v1
{
	image_head_v1       head;
	file_system_info_v1 fs;
	image_options_v1    options;
};
typedef struct image_desc_v1 image_desc_v1;

struct image_desc_v2
{
	image_head_v2       head;
	file_system_info_v2 fs;
	image_options_v2    options;
};
typedef struct image_desc_v2 image_desc_v2;

#pragma pack(pop)

#endif	/* _PARTCLONE_H_ */
