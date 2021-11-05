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
#include "disc.c"

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

typedef struct
{
    uint32_t size;    // size of the disk
    uint32_t reads;   // number of block reads performed
    uint32_t blocks;  // number of usable blocks (except stat block)
    uint32_t writes;  // number of block writes performed
    char **block_arr; // pointer to the disk blocks
} disk;

typedef unsigned char bitset;

const int block_size = 4096;

int format(disk *diskptr);
int mount(disk *diskptr);
int create_file();
int remove_file(int inumber);
int stat(int inumber);
int read_i(int inumber, char *data, int length, int offset);
int write_i(int inumber, char *data, int length, int offset);
int fit_to_size(int inumber, int size);

int read_file(char *filepath, char *data, int length, int offset);
int write_file(char *filepath, char *data, int length, int offset);
int create_dir(char *dirpath);
int remove_dir(char *dirpath);

int main() {}

int format(disk *diskptr)
{
    int N = diskptr->blocks;
    int M = N - 1;
    int I = (int)(0.1 * N);
    int n_inodes = I * 128;
    int IB = (int)ceil(n_inodes / (8. * block_size));

    int R = M - I - IB;
    int DBB = (int)ceil(R / (8. * 4096));
    int DB = R - DBB;

    // Initialize the super block
    super_block sb;
    sb.magic_number = 12345;
    sb.blocks = M;
    sb.inode_blocks = I;
    sb.inodes = n_inodes;
    sb.inode_bitmap_block_idx = 1;
    sb.inode_block_idx = 1 + IB + DBB;
    sb.data_block_bitmap_idx = 1 + IB;
    sb.data_block_idx = 1 + IB + DBB + I;
    sb.data_blocks = DB;

    // Initialize the inode bit map
    char *inode_bitmap = (char *)malloc(sb.inode_blocks * 512);
    memset(inode_bitmap, 0, sb.inode_blocks * 512);

    // Initialize the data block bit map
    char *data_bitmap = (char *)malloc(sb.data_blocks * 512);
    memset(data_bitmap, 0, sb.data_blocks * 512);

    // Initialize the inodes
    inode *inodes = (inode *)malloc(sb.inodes * sizeof(inode));
    for (int i = 0; i < sb.inodes; i++)
    {
        inodes[i].valid = 0;
        inodes[i].size = 0;
        for (int j = 0; j < 5; j++)
        {
            inodes[i].direct[j] = 0;
        }
        inodes[i].indirect = 0;
    }

    // Initialize the disk
    diskptr->block_arr = (char **)malloc(sb.blocks * sizeof(char *));
    for (int i = 0; i < sb.blocks; i++)
    {
        diskptr->block_arr[i] = (char *)malloc(512);
        memset(diskptr->block_arr[i], 0, 512);
    }

    // Write the super block
    memcpy(diskptr->block_arr[0], &sb, sizeof(super_block));

    // Write the inode bit map
    memcpy(diskptr->block_arr[sb.inode_bitmap_block_idx], inode_bitmap, sb.inode_blocks * 512);

    // Write the data block bit map
    memcpy(diskptr->block_arr[sb.data_block_bitmap_idx], data_bitmap, sb.data_blocks * 512);

    // Write the inodes
    for (int i = 0; i < sb.inodes; i++)
    {
        memcpy(diskptr->block_arr[sb.inode_block_idx + i], &inodes[i], sizeof(inode));
    }

    // Write the disk
    for (int i = 0; i < sb.blocks; i++)
    {
        write(1, diskptr->block_arr[i], 512);
    }

    return 0;
}