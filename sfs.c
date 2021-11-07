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
#include "disk.h"
#include "sfs.h"

disk *mounted_disk;

int format(disk *diskptr)
{
    if (diskptr == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not initialized -- Disk is not formatted\n");
        return -1;
    }

    int N = diskptr->blocks;
    int M = N - 1;
    int I = (int)(0.1 * N);
    int n_inodes = I * 128;
    int IB = (int)ceil(n_inodes / (8. * BLOCKSIZE));

    int R = M - I - IB;
    int DBB = (int)ceil(R / (8. * BLOCKSIZE));
    int DB = R - DBB;

    // Initialize the super block
    super_block sb;
    sb.magic_number = MAGIC;
    sb.blocks = M;
    sb.inode_blocks = I;
    sb.inodes = n_inodes;
    sb.inode_bitmap_block_idx = 1;
    sb.inode_block_idx = 1 + IB + DBB;
    sb.data_block_bitmap_idx = 1 + IB;
    sb.data_block_idx = 1 + IB + DBB + I;
    sb.data_blocks = DB;

    // Write the super block
    if (write_block(diskptr, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to write super block -- Disk is not formatted\n");
        return -1;
    }

    // Initialize the inode bit map
    bitset *inode_bitmap = (bitset *)malloc(IB * BLOCKSIZE);
    memset(inode_bitmap, 0, IB * BLOCKSIZE);

    // Write inode bit map to disk
    for (int i = 0; i < IB; i++)
    {
        int ret = write_block(diskptr, sb.inode_bitmap_block_idx + i, &inode_bitmap[i * BLOCKSIZE]);
        if (ret < 0)
        {
            printf("{SFS} -- [ERROR]: Error writing inode bitmap to disk -- Disk is not formatted\n");
            return -1;
        }
    }

    // Initialize the data block bit map
    bitset *data_bitmap = (char *)malloc(DBB * BLOCKSIZE);
    memset(data_bitmap, 0, DBB * BLOCKSIZE);

    // Write data block bit map to disk
    for (int i = 0; i < DBB; i++)
    {
        int ret = write_block(diskptr, sb.data_block_bitmap_idx + i, &data_bitmap[i]);
        if (ret < 0)
        {
            printf("{SFS} -- [ERROR]: Error writing data bitmap to disk -- Disk is not formatted\n");
            return -1;
        }
    }

    // Initialize the inodes
    inode *inodes = (inode *)malloc(128 * sizeof(inode));
    for (int i = 0; i < 128; i++)
    {
        inodes[i].valid = 0;
        inodes[i].size = -1;
        for (int j = 0; j < 5; j++)
        {
            inodes[i].direct[j] = -1;
        }
        inodes[i].indirect = -1;
    }

    // Write the inodes
    for (int i = 0; i < sb.inode_blocks; i++)
    {
        write_block(diskptr, sb.inode_block_idx + i, inodes);
    }

    // Free the memory
    free(inodes);
    free(inode_bitmap);
    free(data_bitmap);

    return 0;
}

int mount(disk *diskptr)
{
    if (diskptr == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not initialized -- Disk is not mounted\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(diskptr, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- Disk is not mounted\n");
        return -1;
    }

    // Check the magic number
    if (sb.magic_number != MAGIC)
    {
        printf("{SFS} -- [ERROR] Magic number is incorrect -- Disk is not mounted\n");
        return -1;
    }

    mounted_disk = diskptr;
    return 0;
}

int find_free_inode()
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- Failed to find free inode\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- Failed to find free inode\n");
        return -1;
    }

    // Read the inode bit map
    int IB = (sb.data_block_bitmap_idx - 1);
    bitset *inode_bitmap = (bitset *)malloc(IB * BLOCKSIZE);
    for (int i = 0; i < IB; i++)
    {
        if (read_block(mounted_disk, sb.inode_bitmap_block_idx + i, &inode_bitmap[i * BLOCKSIZE]))
        {
            printf("{SFS} -- [ERROR] Failed to read inode bitmap -- Failed to find free inode\n");
            return -1;
        }
    }

    // Find the first free inode
    for (int i = 0; i < IB * BLOCKSIZE * 8; i++)
    {
        int index = (i / BLOCKSIZE) >> 3;
        if (is_set(inode_bitmap[index * BLOCKSIZE], i & 7))
        {
            continue;
        }
        else
        {
            free(inode_bitmap);
            return i;
        }
    }

    free(inode_bitmap);
    return -1;
}

int create_file()
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- File not created\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not created\n");
        return -1;
    }

    // Find the first free inode
    int inode_idx = find_free_inode();
    if (inode_idx < 0)
    {
        printf("{SFS} -- [ERROR] No free inodes -- File not created\n");
        return -1;
    }

    // Initialize the inode
    int inode_block_idx = sb.inode_block_idx + (inode_idx / 128);
    int inode_offset = (inode_idx % 128);

    inode *inode_ptr = (inode *)malloc(128 * sizeof(inode));
    read_block(mounted_disk, inode_block_idx, inode_ptr);

    inode_ptr[inode_offset].valid = 1;
    inode_ptr[inode_offset].size = 0;
    for (int i = 0; i < 5; i++)
    {
        inode_ptr->direct[i] = -1;
    }
    inode_ptr->indirect = -1;

    // Write the inode to disk
    if (write_block(mounted_disk, inode_idx, inode_ptr))
    {
        printf("{SFS} -- [ERROR] Failed to write inode to disk -- File not created\n");
        return -1;
    }

    return 0;
}

void set(bitset *bitmap, int index)
{
    int byte_index = index >> 3;
    int bit_index = index & 7;
    bitmap[byte_index] |= (1 << bit_index);
    return 0;
}

void unset(bitset *bitmap, int index)
{
    int byte_index = index >> 3;
    int bit_index = index & 7;
    bitmap[byte_index] &= ~(1 << bit_index);
    return 0;
}

int is_set(bitset *bitmap, int index)
{
    int byte_index = index >> 3;
    int bit_index = index & 7;
    return (bitmap[byte_index] & (1 << bit_index)) != 0;
}