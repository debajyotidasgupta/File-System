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
File Name       disc.c
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

typedef struct
{
    uint32_t size;    // size of the disk
    uint32_t reads;   // number of block reads performed
    uint32_t blocks;  // number of usable blocks (except stat block)
    uint32_t writes;  // number of block writes performed
    char **block_arr; // pointer to the disk blocks
} disk;

const int block_size = 4096;
const int stats_size = 24;

disk *create_disk(int nbytes);
int read_block(disk *diskptr, int blocknr, void *block_data);
int write_block(disk *diskptr, int blocknr, void *block_data);
int free_disk(disk *diskptr);

int main() {}

disk *create_disk(int nbytes)
{
    disk *diskptr = malloc(sizeof(disk));

    diskptr->size = nbytes;
    diskptr->reads = 0;
    diskptr->writes = 0;
    diskptr->blocks = (nbytes - stats_size) / block_size;

    diskptr->block_arr = malloc(sizeof(char **) * diskptr->blocks);
    if (diskptr->block_arr == NULL)
    {
        printf("[ERROR]: malloc failed ... No more space left to allocate\n");
        return NULL;
    }

    for (int i = 0; i < diskptr->blocks; i++)
    {
        diskptr->block_arr[i] = malloc(sizeof(char) * block_size);
        if (diskptr->block_arr[i] == NULL)
        {
            printf("[ERROR]: malloc failed ... No more space left to allocate\n");
            return NULL;
        }
    }

    printf("[INFO]: Disk created successfully\n");
    return diskptr;
}

int read_block(disk *diskptr, int blocknr, void *block_data)
{
    if (blocknr < 0 || blocknr >= diskptr->blocks)
    {
        printf("[ERROR]: block number out of range\n");
        return -1;
    }

    if (block_data == NULL)
    {
        printf("[ERROR]: block data is NULL\n");
        return -1;
    }

    memcpy(block_data, diskptr->block_arr[blocknr], block_size);
    diskptr->reads++;
    printf("[INFO]: Block read successfully\n");
    return 0;
}

int write_block(disk *diskptr, int blocknr, void *block_data)
{
    if (blocknr < 0 || blocknr >= diskptr->blocks)
    {
        printf("[ERROR]: block number out of range\n");
        return -1;
    }

    memcpy(diskptr->block_arr[blocknr], block_data, block_size);
    diskptr->writes++;
    printf("[INFO]: Block written successfully\n");
    return 0;
}

int free_disk(disk *diskptr)
{
    for (int i = 0; i < diskptr->blocks; i++)
    {
        free(diskptr->block_arr[i]);
    }
    free(diskptr->block_arr);
    free(diskptr);
    printf("[INFO]: Disk freed successfully\n");
    return 0;
}