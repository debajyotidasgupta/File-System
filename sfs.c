/*
------------------------------------------
Assignment 3
------------------------------------------
subject:        Advanced Operating Systems
year:           2021 - 2022
------------------------------------------
Name:           Debajyoti Dasgupta
Roll No:        18CS30051
------------------------------------------
Kernel Version: 5.11.0-37-generic
System:         Ubuntu 20.04.3 LTS
------------------------------------------
File Name       sfs.c
------------------------------------------
*/

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

typedef struct super_block
{
    uint32_t magic_number; // File system magic number
    uint32_t blocks;       // Number of blocks in file system (except super block)

    uint32_t inode_blocks;           // Number of blocks reserved for inodes == 10% of Blocks
    uint32_t inodes;                 // Number of inodes in file system == length of inode bit map
    uint32_t inode_bitmap_block_idx; // Block Number of the first inode bit map block
    uint32_t inode_block_idx;        // Block Number of the first inode block

    uint32_t data_block_bitmap_idx; // Block number of the first data bitmap block
    uint32_t data_block_idx;        // Block number of the first data block
    uint32_t data_blocks;           // Number of blocks reserved as data blocks
} super_block;

typedef struct inode
{
    uint32_t valid;     // 0 if invalid
    uint32_t size;      // logical size of the file
    uint32_t direct[5]; // direct data block pointer
    uint32_t indirect;  // indirect pointer
} inode;

int main() {}