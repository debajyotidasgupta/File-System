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
#include "disk.h"
#include "sfs.h"

disk *mounted_disk;
int root_file_inode = 0;

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

    // Create the root directory
    root_file_inode = create_file();
    return 0;
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

    // Write the inode to disk
    if (write_block(mounted_disk, inode_idx, inode_ptr))
    {
        printf("{SFS} -- [ERROR] Failed to write inode to disk -- File not created\n");
        return -1;
    }

    return inode_idx;
}

int remove_file(int inumber)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- File not removed\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not removed\n");
        return -1;
    }

    // Read the inode
    int inode_block_idx = sb.inode_block_idx + (inumber / 128);
    int inode_offset = (inumber % 128);

    inode *inode_ptr = (inode *)malloc(128 * sizeof(inode));
    read_block(mounted_disk, inode_block_idx, inode_ptr);

    // Check if the inode is valid
    if (inode_ptr[inode_offset].valid == 0)
    {
        printf("{SFS} -- [ERROR] Inode is not valid -- File not removed\n");
        return -1;
    }

    // Clear the data bitmap
    int data_block_bitmap_idx = sb.data_block_bitmap_idx;
    int file_blocks = (int)ceil((double)inode_ptr[inode_offset].size / BLOCKSIZE);

    uint32_t *indirect_ptr = NULL;
    for (int i = 0; i < file_blocks; i++)
    {
        if (i < 5)
        {
            if (clear_bitmap(inode_ptr[inode_offset].direct[i], data_block_bitmap_idx))
            {
                printf("{SFS} -- [ERROR] Failed to clear data bitmap -- File not removed\n");
                return -1;
            }
        }
        else
        {
            if (indirect_ptr == NULL)
            {
                indirect_ptr = (uint32_t *)malloc(BLOCKSIZE);
                if (read_block(mounted_disk, sb.data_block_idx + inode_ptr[inode_offset].indirect, indirect_ptr))
                {
                    printf("{SFS} -- [ERROR] Failed to read indirect block -- File not removed\n");
                    return -1;
                }

                if (clear_bitmap(inode_ptr[inode_offset].indirect, data_block_bitmap_idx))
                {
                    printf("{SFS} -- [ERROR] Failed to clear data bitmap -- File not removed\n");
                    return -1;
                }
            }
            if (clear_bitmap(indirect_ptr[i - 5], data_block_bitmap_idx))
            {
                printf("{SFS} -- [ERROR] Failed to clear data bitmap -- File not removed\n");
                return -1;
            }
        }
    }

    // Free the inode
    inode_ptr[inode_offset].valid = 0;
    inode_ptr[inode_offset].size = 0;

    // Write the inode to disk
    if (write_block(mounted_disk, inode_block_idx, inode_ptr))
    {
        printf("{SFS} -- [ERROR] Failed to write inode to disk -- File not removed\n");
        return -1;
    }

    // Clere the inode bitmap
    if (clear_bitmap(inumber, sb.inode_bitmap_block_idx))
    {
        printf("{SFS} -- [ERROR] Failed to clear inode bitmap -- File not removed\n");
        return -1;
    }

    return 0;
}

int stat(int inumber)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- File not stat\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not stat\n");
        return -1;
    }

    // Read the inode
    int inode_block_idx = sb.inode_block_idx + (inumber / 128);
    int inode_offset = (inumber % 128);

    inode *inode_ptr = (inode *)malloc(128 * sizeof(inode));
    read_block(mounted_disk, inode_block_idx, inode_ptr);

    // Check if the inode is valid
    if (inode_ptr[inode_offset].valid == 0)
    {
        printf("{SFS} -- [ERROR] Inode is not valid -- File not stat\n");
        return -1;
    }

    int blocks = (int)ceil((double)inode_ptr[inode_offset].size / BLOCKSIZE);
    int direct_blocks = (blocks < 5) ? blocks : 5;
    int indirect_blocks = blocks - direct_blocks;

    printf("\
    +---------------------[FILE STAT]---------------------+\n\
    |                                                     |\n\
    |  Valid:                              %8d       |\n\
    |  File Size (Logical):                %8d       |\n\
    |  Number of Data Blocks in use:       %8d       |\n\
    |  Number of Direct Pointers in use:   %8d       |\n\
    |  Number of Indirect Pointers in use: %8d       |\n\
    |                                                     |\n\
    +-----------------------------------------------------+\n",
           inode_ptr[inode_offset].valid,
           inode_ptr[inode_offset].size,
           blocks,
           direct_blocks,
           indirect_blocks);

    return 0;
}

int read_i(int inumber, char *data, int length, int offset)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- File not read\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not read\n");
        return -1;
    }

    // Read the inode
    int inode_block_idx = sb.inode_block_idx + (inumber / 128);
    int inode_offset = (inumber % 128);

    inode *inode_ptr = (inode *)malloc(128 * sizeof(inode));
    read_block(mounted_disk, inode_block_idx, inode_ptr);

    // Check if the inode is valid
    if (inode_ptr[inode_offset].valid == 0)
    {
        printf("{SFS} -- [ERROR] Inode is not valid -- File not read\n");
        return -1;
    }

    // Check if the offset is valid
    if (offset < 0 || offset >= inode_ptr[inode_offset].size)
    {
        printf("{SFS} -- [ERROR] Offset is not valid -- File not read\n");
        return -1;
    }

    // Truncate the length if it is too long
    if (length > inode_ptr[inode_offset].size - offset)
    {
        length = inode_ptr[inode_offset].size - offset;
    }

    // Read the data
    int start_block = offset / BLOCKSIZE;
    uint32_t *data_ptr = NULL;

    if (start_block < 5)
    {
        data_ptr = inode_ptr[inode_offset].direct + start_block;
    }
    else
    {
        data_ptr = (uint32_t *)malloc(BLOCKSIZE);
        if (read_block(mounted_disk, sb.data_block_idx + inode_ptr[inode_offset].indirect, data_ptr))
        {
            printf("{SFS} -- [ERROR] Failed to read indirect block -- File not read\n");
            return -1;
        }
    }

    char *temp_data = (char *)malloc(BLOCKSIZE);
    int bytes_read = 0;

    while (length > 0)
    {
        if (read_block(mounted_disk, sb.data_block_idx + *data_ptr, temp_data))
        {
            printf("{SFS} -- [ERROR] Failed to read data block -- File not read\n");
            return -1;
        }

        int copy_length = ((offset % BLOCKSIZE) + length > BLOCKSIZE) ? BLOCKSIZE - (offset % BLOCKSIZE) : length;
        memcpy(data, temp_data + (offset % BLOCKSIZE), copy_length);
        data += copy_length;
        length -= copy_length;
        offset += copy_length;
        bytes_read += copy_length;

        if (length > 0)
            if (data_ptr == inode_ptr[inode_offset].direct + 4)
            {
                data_ptr = (uint32_t *)malloc(BLOCKSIZE);
                if (read_block(mounted_disk, sb.data_block_idx + inode_ptr[inode_offset].indirect, data_ptr))
                {
                    printf("{SFS} -- [ERROR] Failed to read indirect block -- File not read\n");
                    return -1;
                }
            }
            else
            {
                data_ptr++;
            }
    }
    free(temp_data);
    free(data_ptr);

    return bytes_read;
}

int write_i(int inumber, char *data, int length, int offset)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- File not write\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not write\n");
        return -1;
    }

    // Read the inode
    int inode_block_idx = sb.inode_block_idx + (inumber / 128);
    int inode_offset = (inumber % 128);

    inode *inode_ptr = (inode *)malloc(128 * sizeof(inode));
    read_block(mounted_disk, inode_block_idx, inode_ptr);

    // Check if the inode is valid
    if (inode_ptr[inode_offset].valid == 0)
    {
        printf("{SFS} -- [ERROR] Inode is not valid -- File not write\n");
        return -1;
    }

    // Check if the offset is valid
    if (offset < 0 || offset >= inode_ptr[inode_offset].size)
    {
        printf("{SFS} -- [ERROR] Offset is not valid -- File not write\n");
        return -1;
    }

    // Truncate the length if it is too long
    if (length > MAX_FILE_SIZE - offset)
    {
        length = MAX_FILE_SIZE - offset;
    }

    // Write the data
    int start_block = offset / BLOCKSIZE;
    int acquired_blocks = (int)ceil((double)inode_ptr[inode_offset].size / BLOCKSIZE) - start_block;
    if (acquired_blocks < 0)
        acquired_blocks = 0;
    uint32_t *data_ptr = NULL;

    if (start_block < 5)
    {
        data_ptr = inode_ptr[inode_offset].direct + start_block;
    }
    else
    {
        if (acquired_blocks && (inode_ptr[inode_offset].indirect = find_free_data_block()) == -1)
        {
            return 0;
        }

        data_ptr = (uint32_t *)malloc(BLOCKSIZE);
        if (read_block(mounted_disk, sb.data_block_idx + inode_ptr[inode_offset].indirect, data_ptr))
        {
            printf("{SFS} -- [ERROR] Failed to read indirect block -- File not write\n");
            return -1;
        }
    }

    char *temp_data = (char *)malloc(BLOCKSIZE);
    int bytes_written = 0;

    char *indirect_pointer_block = NULL;
    while (length > 0)
    {
        if (acquired_blocks == 0 && (*data_ptr = find_free_data_block()) == -1)
        {
            break;
        }

        if (read_block(mounted_disk, sb.data_block_idx + *data_ptr, temp_data))
        {
            printf("{SFS} -- [ERROR] Failed to read data block -- File not write\n");
            return -1;
        }

        if (read_block(mounted_disk, sb.data_block_idx + *data_ptr, temp_data))
        {
            printf("{SFS} -- [ERROR] Failed to read data block -- File not write\n");
            return -1;
        }
        int copy_length = ((offset % BLOCKSIZE) + length > BLOCKSIZE) ? BLOCKSIZE - (offset % BLOCKSIZE) : length;
        memcpy(temp_data + (offset % BLOCKSIZE), data + (offset % BLOCKSIZE), copy_length);
        if (write_block(mounted_disk, sb.data_block_idx + *data_ptr, temp_data))
        {
            printf("{SFS} -- [ERROR] Failed to write data block -- File not write\n");
            return -1;
        }

        data += copy_length;
        length -= copy_length;
        offset += copy_length;
        bytes_written += copy_length;

        if (length > 0)
            if (data_ptr == inode_ptr[inode_offset].direct + 4)
            {
                if (acquired_blocks == 0 && (inode_ptr[inode_offset].indirect = find_free_data_block()) == -1)
                {
                    break;
                }

                data_ptr = (uint32_t *)malloc(BLOCKSIZE);
                indirect_pointer_block = (char *)data_ptr;
                if (read_block(mounted_disk, sb.data_block_idx + inode_ptr[inode_offset].indirect, data_ptr))
                {
                    printf("{SFS} -- [ERROR] Failed to read indirect block -- File not write\n");
                    return -1;
                }
            }
            else
            {
                data_ptr++;
            }
        if (acquired_blocks > 0)
            acquired_blocks--;
    }

    // Write the indirect pointer block (if it exists)
    if (indirect_pointer_block != NULL)
    {
        if (write_block(mounted_disk, sb.data_block_idx + inode_ptr[inode_offset].indirect, indirect_pointer_block))
        {
            printf("{SFS} -- [ERROR] Failed to write indirect block -- File not write\n");
            free(indirect_pointer_block);
            return -1;
        }
        free(indirect_pointer_block);
    }

    inode_ptr[inode_offset].size += bytes_written;

    // Write the inode
    if (write_block(mounted_disk, inode_block_idx, inode_ptr))
    {
        printf("{SFS} -- [ERROR] Failed to write inode -- File not write\n");
        return -1;
    }

    free(temp_data);
    free(inode_ptr);

    return bytes_written;
}

int fit_to_size(int inumber, int size)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- File not fit_to_size\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not fit_to_size\n");
        return -1;
    }

    // Read the inode
    int inode_block_idx = sb.inode_block_idx + (inumber / 128);
    int inode_offset = (inumber % 128);

    inode *inode_ptr = (inode *)malloc(128 * sizeof(inode));
    if (read_block(mounted_disk, inode_block_idx, inode_ptr))
    {
        printf("{SFS} -- [ERROR] Failed to read inode from disk -- File not fit_to_size\n");
        return -1;
    }

    // Check if the inode is valid
    if (inode_ptr[inode_offset].valid == 0)
    {
        printf("{SFS} -- [ERROR] Inode is not valid -- File not fit_to_size\n");
        return -1;
    }

    if (inode_ptr[inode_offset].size > size)
    {
        inode_ptr[inode_offset].size = size;
        if (write_block(mounted_disk, inode_block_idx, inode_ptr))
        {
            printf("{SFS} -- [ERROR] Failed to write inode to disk -- File not fit_to_size\n");
            return -1;
        }
    }

    return 0;
}

int create_dir(char *dirpath)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- Directory not created\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- Directory not created\n");
        return -1;
    }

    int parent_inumber = get_inumber(dirpath, 1);
    if (parent_inumber == -1)
    {
        printf("{SFS} -- [ERROR] Directory not found -- Directory not created\n");
        return -1;
    }

    if (dirpath[strlen(dirpath) - 1] != '/')
    {
        dirpath[strlen(dirpath) - 1] = '\0';
    }

    // Get the name of the directory to be created
    char *dirname = (char *)malloc(MAX_FILENAME_LENGTH);
    char *token = strtok(dirpath, "/");
    while (token != NULL)
    {
        strcpy(dirname, token);
        token = strtok(NULL, "/");
    }

    // Check if the directory already exists
    if (find_file(sb.inode_block_idx, parent_inumber, dirname) != -1)
    {
        printf("{SFS} -- [ERROR] Directory already exists -- Directory not created\n");
        return -1;
    }

    // Check if there is a free directory entry and Create the directory
    dir_entry dir;
    dir.inode_idx = create_file();
    if (dir.inode_idx == -1)
    {
        printf("{SFS} -- [ERROR] No free space -- Directory not created\n");
        return -1;
    }

    dir.type = 1;
    dir.valid = 1;
    strcpy(dir.name, dirname);
    dir.name_len = strlen(dirname);

    // read parent inode to compute offset
    inode *parent_inode = (inode *)malloc(BLOCKSIZE);
    int offset = parent_inumber % 128;

    if (read_block(mounted_disk, sb.inode_block_idx + parent_inumber / 128, parent_inode))
    {
        printf("{SFS} -- [ERROR] Failed to read inode -- Directory not created\n");
        return -1;
    }

    int file_size = parent_inode[offset].size;

    dir_entry *dir_files = (dir_entry *)malloc(file_size);
    if (read_i(parent_inumber, (char *)dir_files, file_size, 0))
    {
        printf("{SFS} -- [ERROR] Failed to read data block -- Directory not created\n");
        return -1;
    }

    for (int i = 0; i < file_size / sizeof(dir_entry); i++)
    {
        if (dir_files[i].valid == 0)
        {
            dir_files[i] = dir;
            if (write_i(parent_inumber, (char *)dir_files, file_size, 0))
            {
                printf("{SFS} -- [ERROR] Failed to write data block -- Directory not created\n");
                return -1;
            }

            free(dir_files);
            free(parent_inode);
            return dir.inode_idx;
        }
    }

    // Write the directory entry
    if (write_i(parent_inumber, (char *)&dir, sizeof(dir_entry), file_size / sizeof(dir_entry)))
    {
        printf("{SFS} -- [ERROR] Failed to write data block -- Directory not created\n");

        free(dir_files);
        free(parent_inode);
        return -1;
    }

    free(dir_files);
    free(parent_inode);
    return dir.inode_idx;
}

int remove_dir(char *dirpath)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- Directory not created\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- Directory not created\n");
        return -1;
    }

    int parent_inumber = get_inumber(dirpath, 1);
    if (parent_inumber == -1)
    {
        printf("{SFS} -- [ERROR] Directory not found -- Directory not removed\n");
        return -1;
    }

    if (dirpath[strlen(dirpath) - 1] != '/')
    {
        dirpath[strlen(dirpath) - 1] = '\0';
    }

    // Get the name of the directory to be created
    char *dirname = (char *)malloc(MAX_FILENAME_LENGTH);
    char *token = strtok(dirpath, "/");
    while (token != NULL)
    {
        strcpy(dirname, token);
        token = strtok(NULL, "/");
    }

    // Check if the directory already exists
    if (find_file(sb.inode_block_idx, parent_inumber, dirname) < 0)
    {
        printf("{SFS} -- [ERROR] Directory does not exists -- Directory not removed\n");
        return -1;
    }

    // read parent inode to compute offset
    inode *parent_inode = (inode *)malloc(BLOCKSIZE);
    int offset = parent_inumber % 128;

    if (read_block(mounted_disk, sb.inode_block_idx + parent_inumber / 128, parent_inode))
    {
        printf("{SFS} -- [ERROR] Failed to read inode -- Directory not created\n");
        return -1;
    }

    int file_size = parent_inode[offset].size;

    dir_entry *dir_files = (dir_entry *)malloc(file_size);
    if (read_i(parent_inumber, (char *)dir_files, file_size, 0))
    {
        printf("{SFS} -- [ERROR] Failed to read data block -- Directory not created\n");
        return -1;
    }

    for (int i = 0; i < file_size / sizeof(dir_entry); i++)
    {
        if (strcmp(dirname, dir_files[i].name) == 0)
        {
            dir_files[i].valid = 0;
            if (recursive_remove(sb.inode_block_idx, dir_files[i].inode_idx, dir_files[i].type) < 0)
            {
                printf("{SFS} -- [ERROR] Failed to recursively remove directory -- Directory not removed\n");
                return -1;
            }
            break;
        }
    }

    // Write the directory entry
    if (write_i(parent_inumber, (char *)dir_files, file_size, 0))
    {
        printf("{SFS} -- [ERROR] Failed to write data block -- Directory not created\n");

        free(dir_files);
        free(parent_inode);
        return -1;
    }

    free(dir_files);
    free(parent_inode);
    return 0;
}

int read_file(char *filepath, char *data, int length, int offset)
{
    int inumber = get_inumber(filepath, 0);
    if (inumber == -1)
    {
        printf("{SFS} -- [ERROR] File not found -- File not read\n");
        return -1;
    }

    if (data == NULL)
    {
        printf("{SFS} -- [ERROR] Invalid data pointer -- Failed Read File\n");
        return -1;
    }

    // Read data from the inode in data buffer
    int bytes_read = read_i(inumber, data, length, offset);
    if (bytes_read == -1)
    {
        printf("{SFS} -- [ERROR] Failed to read file -- File not read\n");
        return -1;
    }

    return bytes_read;
}

int write_file(char *filepath, char *data, int length, int offset)
{
    int parent_inumber = get_inumber(filepath, 1);
    if (parent_inumber == -1)
    {
        printf("{SFS} -- [ERROR] File not found -- File not written\n");
        return -1;
    }

    if (data == NULL)
    {
        printf("{SFS} -- [ERROR] Invalid data pointer -- Failed Write File\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- File not written\n");
        return -1;
    }

    if (filepath[strlen(filepath) - 1] != '/')
    {
        filepath[strlen(filepath) - 1] = '\0';
    }

    // Get the name of the File to be created
    char *filename = (char *)malloc(MAX_FILENAME_LENGTH);
    char *token = strtok(filepath, "/");
    while (token != NULL)
    {
        strcpy(filename, token);
        token = strtok(NULL, "/");
    }

    // Check if the File already exists
    int inumber = -1;
    if (find_file(sb.inode_block_idx, parent_inumber, filename) < 0)
    {
        if (inumber = create_file() < 0)
        {
            printf("{SFS} -- [ERROR] Failed to write file -- File not written\n");
            return -1;
        }
    }

    // Write data to the inode in data buffer
    int bytes_written = write_i(inumber, data, length, offset);
    if (bytes_written == -1)
    {
        printf("{SFS} -- [ERROR] Failed to write file -- File not written\n");
        return -1;
    }

    dir_entry e;
    e.inode_idx = inumber;
    strcpy(e.name, filename);
    e.name_len = strlen(filename);
    e.type = 0;
    e.valid = 1;

    // Write the directory entry
    inode *parent_inode = (inode *)malloc(BLOCKSIZE);
    int off = parent_inumber % 128;
    if (read_block(mounted_disk, sb.inode_block_idx + parent_inumber / 128, parent_inode))
    {
        printf("{SFS} -- [ERROR] Failed to read inode -- File not written\n");
        return -1;
    }

    int file_size = parent_inode[off].size;

    dir_entry *dir_files = (dir_entry *)malloc(file_size);
    if (read_i(parent_inumber, (char *)dir_files, file_size, 0))
    {
        printf("{SFS} -- [ERROR] Failed to read data block -- File not written\n");
        return -1;
    }

    for (int i = 0; i < file_size / sizeof(dir_entry); i++)
    {
        if (strcmp(filename, dir_files[i].name) == 0)
        {
            dir_files[i] = e;
            if (write_i(parent_inumber, (char *)dir_files, file_size, 0))
            {
                printf("{SFS} -- [ERROR] Failed to write data block -- File not written\n");
                return -1;
            }
            free(dir_files);
            free(parent_inode);
            return bytes_written;
        }
    }

    // Add the directory entry
    if (write_i(parent_inumber, (char *)dir_files, file_size, file_size / sizeof(dir_entry)))
    {
        printf("{SFS} -- [ERROR] Failed to write data block -- File not written\n");
        return -1;
    }

    free(dir_files);
    free(parent_inode);
    return bytes_written;
}

void set(bitset *bitmap, int index)
{
    int byte_index = index >> 3;
    int bit_index = index & 7;
    bitmap[byte_index] |= (1 << bit_index);
}

void unset(bitset *bitmap, int index)
{
    int byte_index = index >> 3;
    int bit_index = index & 7;
    bitmap[byte_index] &= ~(1 << bit_index);
}

int is_set(bitset *bitmap, int index)
{
    int byte_index = index >> 3;
    int bit_index = index & 7;
    return (bitmap[byte_index] & (1 << bit_index)) != 0;
}

int clear_bitmap(int block, int bitmap_start)
{
    int block_number = block / (8 * BLOCKSIZE);
    int bit_number = block % (8 * BLOCKSIZE);

    bitset *bitmap = (bitset *)malloc(BLOCKSIZE);
    if (read_block(mounted_disk, bitmap_start + block_number, bitmap))
    {
        printf("{SFS} -- [ERROR] Failed to read bitmap -- Failed to clear bitmap\n");
        free(bitmap);
        return -1;
    }

    unset(bitmap, bit_number);

    if (write_block(mounted_disk, bitmap_start + block_number, bitmap))
    {
        printf("{SFS} -- [ERROR] Failed to write bitmap -- Failed to clear bitmap\n");
        free(bitmap);
        return -1;
    }

    free(bitmap);
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
    int IB = (sb.data_block_bitmap_idx - sb.inode_bitmap_block_idx);
    bitset *inode_bitmap = (bitset *)malloc(IB * BLOCKSIZE);
    for (int i = 0; i < IB; i++)
    {
        if (read_block(mounted_disk, sb.inode_bitmap_block_idx + i, &inode_bitmap[i * BLOCKSIZE]))
        {
            printf("{SFS} -- [ERROR] Failed to read inode bitmap -- Failed to find free inode\n");
            free(inode_bitmap);
            return -1;
        }
    }

    // Find the first free inode
    for (int i = 0; i < IB * BLOCKSIZE * 8; i++)
    {
        int index = (i / BLOCKSIZE) >> 3;
        if (is_set(&inode_bitmap[index * BLOCKSIZE], i % (BLOCKSIZE * 8)))
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

int find_free_data_block()
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- Failed to find free data block\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- Failed to find free data block\n");
        return -1;
    }

    // Read the data block bit map
    int DB = (sb.data_block_bitmap_idx - sb.data_block_idx);
    bitset *data_block_bitmap = (bitset *)malloc(DB * BLOCKSIZE);

    for (int i = 0; i < DB; i++)
    {
        if (read_block(mounted_disk, sb.data_block_bitmap_idx + i, &data_block_bitmap[i * BLOCKSIZE]))
        {
            printf("{SFS} -- [ERROR] Failed to read data block bitmap -- Failed to find free data block\n");
            free(data_block_bitmap);
            return -1;
        }
    }

    // Find the first free data block
    for (int i = 0; i < DB * BLOCKSIZE * 8; i++)
    {
        int index = (i / BLOCKSIZE) >> 3;
        if (is_set(&data_block_bitmap[index * BLOCKSIZE], i & (BLOCKSIZE * 8)))
        {
            continue;
        }
        else
        {
            free(data_block_bitmap);
            return i;
        }
    }

    free(data_block_bitmap);
    return -1;
}

int find_file(int start_inode, int inumber, char *filename)
{
    // Read the inode
    inode *in = (inode *)malloc(BLOCKSIZE);
    int off = inumber % 128;

    if (read_block(mounted_disk, start_inode + inumber / 128, in))
    {
        printf("{SFS} -- [ERROR] Failed to read inode -- Failed to get inumber\n");
        return -1;
    }

    if (in[off].valid == 0)
    {
        printf("{SFS} -- [ERROR] Invalid inode -- Failed to get inumber\n");
        return -1;
    }

    // Read the data block
    dir_entry *dir = (dir_entry *)malloc(in[off].size);
    int len = read_i(inumber, (char *)dir, in[off].size, 0);
    free(in);

    if (len < 0)
    {
        printf("{SFS} -- [ERROR] Failed to read data block -- Failed to get inumber\n");
        return -1;
    }

    int num_entries = len / sizeof(dir_entry);
    inumber = -1;
    for (int j = 0; j < num_entries; j++)
    {
        if (strcmp(filename, dir[j].name) == 0)
        {
            inumber = dir[j].inode_idx;
            break;
        }
    }

    return inumber;
}

int get_inumber(char *filepath, int parent)
{
    if (mounted_disk == NULL)
    {
        printf("{SFS} -- [ERROR] Disk is not mounted -- Failed to get inumber\n");
        return -1;
    }

    // Read the super block
    super_block sb;
    if (read_block(mounted_disk, 0, &sb))
    {
        printf("{SFS} -- [ERROR] Failed to read super block -- Failed to find free inode\n");
        return -1;
    }

    int start_inode = sb.inode_block_idx;

    int n_parts;
    char **files = path_parse(filepath, &n_parts);

    if (files == NULL)
    {
        printf("{SFS} -- [ERROR] Failed to parse path -- Failed to get inumber\n");
        return -1;
    }

    if (n_parts == 0)
    {
        if (files)
            free(files);
        printf("{SFS} -- [ERROR] Access to create or remove root directory is not allowed\n");
        return -1;
    }

    int inumber = root_file_inode;
    for (int i = 0; i < n_parts - parent; i++)
    {
        inumber = find_file(start_inode, inumber, files[i]);
        if (inumber < 0)
        {
            printf("{SFS} -- [ERROR] File or Directory [%s] not found -- Failed to get inumber\n", files[i]);
            free(files);
            return -1;
        }

        free(files[i]);
    }

    free(files);
    return inumber;
}

char **path_parse(char *path, int *n_parts)
{
    int path_len = strlen(path);

    if (path_len > 1 && path[path_len - 1] == '/')
    {
        path[path_len - 1] = '\0';
        path_len--;
    }

    if (path_len > 0 && path[0] == '/')
    {
        path++;
        path_len--;
    }
    else
    {
        printf("{SFS} -- [ERROR] Invalid path -- Failed to parse path\n");
        return NULL;
    }

    *n_parts = 1;
    for (int i = 0; i < strlen(path); i++)
    {
        if (path[i] == '/')
        {
            (*n_parts)++;
        }
    }

    char **path_array = (char **)malloc(sizeof(char *) * *n_parts);
    char *token = strtok(path, "/");
    int i = 0;
    while (token != NULL)
    {
        path_array[i] = (char *)malloc(sizeof(char) * MAX_FILENAME_LENGTH);
        strcpy(path_array[i], token);
        token = strtok(NULL, "/");
        i++;
    }
    return path_array;
}

int recursive_remove(int start_inode, int inumber, int type)
{
    if (type)
    {
        // This is a Directory, so remove recursvely

        // Read the inode
        inode *in = (inode *)malloc(BLOCKSIZE);
        int off = inumber % 128;

        if (read_block(mounted_disk, inumber / 128 + start_inode, in))
        {
            printf("{SFS} -- [ERROR] Failed to read inode -- Failed to get inumber\n");
            return -1;
        }

        if (in[off].valid == 0)
        {
            printf("{SFS} -- [ERROR] Invalid inode -- Failed to get inumber\n");
            return -1;
        }

        // Read the data block
        dir_entry *dir = (dir_entry *)malloc(in[off].size);
        int len = read_i(inumber, (char *)dir, in[off].size, 0);
        free(in);

        if (len < 0)
        {
            printf("{SFS} -- [ERROR] Failed to read data block -- Failed to get inumber\n");
            return -1;
        }

        int num_entries = len / sizeof(dir_entry);
        for (int j = 0; j < num_entries; j++)
        {
            if (dir[j].inode_idx != 0)
            {
                recursive_remove(start_inode, dir[j].inode_idx, dir[j].type);
            }
        }

        free(dir);
    }

    if (remove_file(inumber) < 0)
    {
        printf("{SFS} -- [ERROR] Failed to remove file -- Failed to remove file\n");
        return -1;
    }
    return 0;
}