/*
max 64 files at a time
exactly 1 directory that contains all files
max 15 character filenames

superblock hard-coded to block 0
dir: d
fat: f

directory and FAT in metadata half
directory:
name: f1, start: a, length: 10000

FAT:
x->y->z->eof

make file (f1) of size 3 blocks (10,000 bytes)

superblock:
    refers to a number of blocks for directiory information (dir_idx, dir_len)
    refers to a number of blocks that contains the FAT (fat_idx, fat_len)
    reference to first block of file data (data_idx)

directory:
    array of dir_entry-s
    dir_entry:
        filename
        filesize
        first block
FAT
    array of block_idx'es (or eof, or free)
*/
#include "fs.h"
#include "disk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>


#define MBYTE 1048576

/* globals */
struct super_block fs;
struct file_descriptor fildes[MAX_FILDES];
int FAT[DISK_BLOCKS];       // populated with the FAT data
struct dir_entry DIR[64];   // populated with the directory data
char fs_buffer[BLOCK_SIZE]; // for reading and writing to disk

/* first invoke make_disk(disk_name) to create a new disk */
int make_fs(char* disk_name)
{
    if(make_disk(disk_name) != 0)           // create a new disk
    {
        perror("make_fs: make_disk failed");
        return -1;
    }

    /* open disk */
    if(open_disk(disk_name) != 0)
    {
        perror("make_fs: open_disk failed");
        return -1;
    }
    /* write/initalize necessary meta-information for file-system so that it can be later used (mounted) */
    /* superblock, zero-indexing */
    fs.fat_idx = 2;
    fs.fat_len = 8;
    fs.dir_idx = 1;
    fs.dir_len = 1;
    fs.data_idx = 10;

    block_write(0, (char*)&fs);

    /* FAT, 0 is eof; -1 is free; FAT needs next block; EOF metadata is -2 */
    int i;
    for(i = 0; i < 2; i++)      // -2: EOF for meta-information
    {
        FAT[i] = -2;
    }
    for(i = 2; i < 9; i++)      // FAT blocks
    {
        FAT[i] = i+1;
    }
    FAT[9] = -2;                // end of FAT
    for(i = 10; i < DISK_BLOCKS; i++)
    {
        FAT[i] = -1;             // unallocated block
    }

    /* write FAT to disk */
    for(i = 0; i < fs.fat_len; i++)
    {
        block_write(i + fs.fat_idx, (char*)&FAT[(i * BLOCK_SIZE)/sizeof(int)]);
    }

    /* directory */
    memset(DIR, 0, sizeof(DIR));

    /*
    If 0, not used
    head -1 when empty
    might be able to not set all and use dirty bit logic?
    */
    for(i = 0; i < MAX_FILES; i++)
    {
        DIR[i].used = 0;
        DIR[i].head = -1;
        DIR[i].size = 0;
        DIR[i].ref_cnt = 0;
    }
    /* directory is written to the first block */
    block_write(1, (char*)&DIR);

    /* close disk */
    if(close_disk() != 0)
    {
        perror("make_fs: close_disk failed");
        return -1;
    }
    return 0;
}


/* mounts a file system that is stored on a virtual disk with name disk_name */
int mount_fs(char* disk_name)
{
    /* open the disk */
    if(open_disk(disk_name) != 0)
    {
        perror("mount_fs: open_disk failed");
        return -1;
    }
    /* load meta-information that is necessary to handle the file system operations */
    /* load superblock */
    block_read(0, (char*)&fs);

    /* load FAT, because metadata is contiguous in memory, might not need to assign structure in FAT */
    int i;
    for(i = 0; i < fs.fat_len; i++)
    {
        block_read(fs.fat_idx + i, (char*)&FAT[i*BLOCK_SIZE/sizeof(int)]);
    }
    /* load directory */
    block_read(fs.dir_idx, (char*)&DIR);

    return 0;
}

/* unmounts file system from a virtual disk with name disk_name */
int umount_fs(char* disk_name)
{
    /* write back all meta-information so disk persistently reflects all changes that were made to the file system */
    // write back superblock
    block_write(0, (char*)&fs);
    int i;
    // write back FAT
    for(i = 0; i < fs.fat_len; i++)
    {
        if(block_write(fs.fat_idx + i, (char*)&FAT[i * BLOCK_SIZE/sizeof(int)]) != 0)
        {
            perror("umount_fs: block_write failed");
            return -1;
        }
    }
    // write back directory
    if(block_write(fs.dir_idx, (char*)&DIR) != 0)
    {
        perror("umount_fs: block_write failed");
        return -1;
    }
    /* everything should be written to disk, I believe */
    close_disk();
    return 0;
}

/* file system functions */
/* REQUIRE a file system was previously mounted */

/* file specified by name is opened for reading and writing, and the file descriptor corresponding to this file is returned to the calling function */
int fs_open(char* name)
{

    /* find file in directory */
    int directory_index;
    for(directory_index = 0; directory_index < MAX_FILES; directory_index++)
    {
        if(DIR[directory_index].used)
        {
            if(strcmp(DIR[directory_index].name, name) == 0)
            {
                break;
            }
        }
    }
    if(directory_index == MAX_FILES)
    {
        perror("fs_open: file not found");
        return -1;
    }

    /* opening same file provides multiple independent file descriptors */
    int i;
    for(i = 0; i < MAX_FILDES; i++)
    {
        if(fildes[i].used == 0)
        {
            break;
        }
    }
    if(i == MAX_FILDES)
    {
        perror("fs_open: no free file descriptors");
        return -1;
    }

    /* update DIR entry and new file descriptor entry */
    DIR[directory_index].ref_cnt++;
    fildes[i].used = 1;
    fildes[i].file = directory_index;
    fildes[i].offset = 0;

    return i;
}

/* file descriptor fildes is closed */
int fs_close(int fd)
{
    /* failure for file descriptor OOR */
    if(fd < 0 || fd >= MAX_FILDES)
    {
        perror("fs_close: invalid file descriptor");
        return -1;
    }
    /* failure for fd being unopen */
    if(fildes[fd].used == 0)
    {
        perror("fs_close: file descriptor not in use");
        return -1;
    }

    /* decrement reference and unlink fd */
    DIR[fildes[fd].file].ref_cnt--;
    fildes[fd].used = 0;
    fildes[fd].file = -1;

    return 0;
}

/* creates new file with name name in the root directory of the file system */
int fs_create(char* name)
{
    /* FAILURE checks */
    // name is too long
    if(strlen(name) > MAX_F_NAME)
    {
        perror("fs_create: file name too long");
        return -1;
    }
    // file with name already exists
    int i;
    for(i = 0; i < MAX_FILES; i++)
    {
        if(DIR[i].used)
        {
            if(strcmp(DIR[i].name, name) == 0)
            {
                perror("fs_create: file already exists");
                return -1;
            }
        }
    }
    // no free file slots, already 64 files
    int freeFile = -1;
    for(i = 0; i < MAX_FILES; i++)
    {
        if(DIR[i].used == 0)
        {
            freeFile = i;
            break;
        }
    }
    if(freeFile == -1)
    {
        perror("fs_create: no free file slots");
        return -1;
    }

    /* if there is free space in the FAT, has memory for this file and can update file infromation */
    for(i = fs.data_idx; i < DISK_BLOCKS; i++)
    {
        if(FAT[i] == -1)         // free block to start file
        {
            DIR[freeFile].head = i;
            FAT[i] = i;
            DIR[freeFile].used = 1;
            strncpy(DIR[freeFile].name, name, MAX_F_NAME);
            DIR[freeFile].name[MAX_F_NAME] = '\0';
            DIR[freeFile].size = 0;
            DIR[freeFile].ref_cnt = 0;

            break;
        }
    }
    if(i == DISK_BLOCKS)        // FAILURE: no free blocks
    {
        perror("fs_create: no free blocks");
        return -1;
    }
    return 0;
}

/* deletes the file with name from the root directory of the file system and frees all info corresponding to the file */
int fs_delete(char* name)
{
    /* FAILURE checks */
    // file with name does not exist
    int dirindex;
    for(dirindex = 0; dirindex < MAX_FILES; dirindex++)
    {
        if(strcmp(DIR[dirindex].name, name) == 0)
        {
            break;
        }
    }
    if(dirindex >= MAX_FILES)
    {
        perror("fs_delete: file not found");
        return -1;
    }

    // there exists at least one open file descriptor associated with this file
    if(DIR[dirindex].ref_cnt > 0)
    {
        perror("fs_delete: file is open");
        return -1;
    }

    /* 'frees' all data blocks associated with the data entry */
    int j;
    for(j = 0; j < BLOCK_SIZE; j++)
    {
        fs_buffer[j] = '\0';
    }
    /* clear all directory entries with two ptr approach */
    int head = DIR[dirindex].head;
    int next = head;
    while(FAT[next] != next)    /* EOF points to its index position */
    {
        /* advance next pointer and free the current block, free with -1 */
        next = FAT[next];
        block_write(head, fs_buffer);
        FAT[head] = -1;
        head = next;
    }
    block_write(next, fs_buffer);
    FAT[next] = -1;

    /* clear out directory metainformation */
    DIR[dirindex].used = 0;
    DIR[dirindex].head = -1;
    DIR[dirindex].name[0] = '\0';

    return 0;
}

/* attempts to read nbytes of data from the file referenced by the descriptor fildes into the buffer pointed to by buf */
int fs_read(int fd, void* buf, size_t nbyte)
{
    /* FAILURE checks */
    // file descriptor OOR
    if(fd < 0 || fd >= MAX_FILDES)
    {
        perror("fs_read: invalid file descriptor OOR");
        return -1;
    }

    /* file descriptor not in use */
    if(fildes[fd].used == 0)
    {
        perror("fs_read: file descriptor not in use");
        return -1;
    }

    /* find associated file */
    int filekey = fildes[fd].file;
    if(DIR[filekey].used == 0)
    {
        perror("fs_read: file not found");
        return -1;
    }

    /* attempting to read past EOF, do not allow */
    int bytes_to_read = nbyte;
    if(nbyte > (DIR[filekey].size - fildes[fd].offset))
    {
        bytes_to_read = DIR[filekey].size - fildes[fd].offset;
    }
    /* blocks until start, offset within the block */
    int off_block = fildes[fd].offset / BLOCK_SIZE;
    int off_file = fildes[fd].offset % BLOCK_SIZE;
    /* iterator through blocks and a counter to reach end of file */
    int temp = DIR[fildes[fd].file].head;
    int count = 0;

    /* starting block for reading with offset */
    int i;
    for(i = 0; i < off_block; i++)
    {
        temp = FAT[temp];
    }
    /* while loop for bytes to read from file, updated per block */
    int amt_reading;
    while(bytes_to_read > 0)
    {
        // read the current block into the buffer
        block_read(temp, fs_buffer);
        // copy the data from the buffer to the user buffer
        if(bytes_to_read < BLOCK_SIZE - off_file)
        {
            amt_reading = bytes_to_read;
        }
        else
        {
            amt_reading = BLOCK_SIZE - off_file;
        }
        memcpy(buf + count, fs_buffer + off_file, amt_reading);

        // if the remaining bytes to read is less than the block size, done
        if(BLOCK_SIZE - off_file > bytes_to_read)
        {
            // update count and bytes to reads
            count += bytes_to_read;
            bytes_to_read -= bytes_to_read;
        }
        else
        {
            count += BLOCK_SIZE - off_file;
            bytes_to_read -= BLOCK_SIZE - off_file;
            off_file = BLOCK_SIZE;
        }

        // update offset
        off_file %= BLOCK_SIZE;

        // if at the end of the file, break
        if(temp == FAT[temp])
        {
            break;
        }
        else
        {
            temp = FAT[temp];
        }
    }
    // update file descriptor offset
    fildes[fd].offset += count;

    // clear buffer
    for(i = 0; i < sizeof(fs_buffer); i++)
    {
        fs_buffer[i] = '\0';
    }
    // return the number of bytes read
    return count;
}

/* attempts to write nbyte bytes of data to the file referenced by the descriptor fildes from the buffer pointed to by buf */
int fs_write(int fd, void* buf, size_t nbyte)
{
    /* FAILURE check */
    // file descriptor OOR
    if(fd < 0 || fd > MAX_FILDES)
    {
        perror("fs_write: invalid file descriptor OOR");
        return -1;
    }
    // file descriptor not in use
    if(fildes[fd].used == 0)
    {
        perror("fs_write: file descriptor not in use");
        return -1;
    }
    // find associated file
    int count = 0;
    int i, j;
    int off_block = fildes[fd].offset / BLOCK_SIZE;
    int off_offset = fildes[fd].offset % BLOCK_SIZE;

    // sizing: file needs to be extended to hold if we write beyond EOF
    // file not allowed to be larger than 16MB
    if(nbyte > (16 * MBYTE) - fildes[fd].offset)
    {
        nbyte = (16 * MBYTE) - fildes[fd].offset;
    }

    /* walk FAT table to correct block */
    int temp = DIR[fildes[fd].file].head;
    if(DIR[fildes[fd].file].size != 0)
    {
        for(i = 0; i < off_block; i++)
        {
            temp = FAT[temp];
        }
    }

    // write data to file
    while(nbyte > 0)
    {
        int bytes_to_write = BLOCK_SIZE - off_offset;
        // if bytes to write is greater than nbyte, write nbyte
        if(bytes_to_write > nbyte)
        {
            bytes_to_write = nbyte;
        }

        // read the block into the buffer
        block_read(temp, fs_buffer);
        // copy the data from the buffer to the user buffer
        memcpy(fs_buffer + off_offset, buf + count, bytes_to_write);
        block_write(temp, fs_buffer);
        // update counters, need to update offset for the file as going
        count += bytes_to_write;
        nbyte -= bytes_to_write;
        fildes[fd].offset += bytes_to_write;

        /* offset puts at the start of needing to write to a new block */
        if(fildes[fd].offset % BLOCK_SIZE == 0)
        {
            int first_time = 1;
            off_offset = 0;
            // find a free block to write to
            for(j = fs.data_idx; j < DISK_BLOCKS; j++)
            {
                // if the block is free and not the current block
                if(FAT[j] == -1 && j != temp)
                {
                    FAT[temp] = j;
                    temp = j;
                    FAT[j] = j;
                    first_time = 0;
                    break;
                }
            }
            // if no free block found, break
            if(first_time)
            {
                break;
            }
        }
    }

    // update file size if necessary
    if(fildes[fd].offset > DIR[fildes[fd].file].size)
    {
        DIR[fildes[fd].file].size = fildes[fd].offset;
    }

    /* clear fs buffer */
    for(i = 0; i < BLOCK_SIZE; i++)
    {
        fs_buffer[i] = '\0';
    }
    return count;
}

/* returns the current size of the file referenced by the file descriptor fd */
int fs_get_filesize(int fd)
{
    /* FAILURE check */
    // file descriptor DNE or is not open
    if(fd < 0 || fd >= MAX_FILDES)
    {
        perror("fs_get_filesize: invalid file descriptor OOR");
        return -1;
    }
    if(fildes[fd].used == 0)
    {
        perror("fs_get_filesize: file descriptor not in use");
        return -1;
    }
    // find associated file
    return DIR[fildes[fd].file].size;
}

/* creates and populates an array of all filenames currently known to the filesystem */
int fs_listfiles(char ***files)
{
    /* try to make a copy and assign it to the files 2d array of names */
    char** list_files = calloc(MAX_FILES+1, sizeof(char*));
    if(list_files == NULL)
    {
        perror("fs_listfiles: malloc failed");
        return -1;
    }

    int file_count = 0;
    int i;
    for(i = 0; i < MAX_FILES; i++)
    {
        if(DIR[i].used)
        {
            list_files[file_count] = malloc(MAX_F_NAME+1);
            memcpy(list_files[file_count], DIR[i].name, MAX_F_NAME);
            memset(list_files[file_count] + MAX_F_NAME, '\0', 1);
            file_count++;
        }
    }
    // NULL terminate the list
    list_files[file_count] = malloc(1);
    memset(list_files[file_count], '\0', 1);

    /* assign back to files*/
    *files = list_files;

    return 0;
}

/* sets the file pointer (offset used for read and write operations) associated with the file descriptor fildes to the argument offset */
int fs_lseek(int fildesc, off_t offset)
{
    /* FAILURE check */
    // file descriptor is invalid
    if(fildesc < 0 || fildesc >= MAX_FILDES)
    {
        perror("fs_lseek: invalid file descriptor OOR");
        return -1;
    }
    // offset is larger than the filesize, or when offset is less than zero
    if(offset < 0 || offset > DIR[fildes[fildesc].file].size)
    {
        perror("fs_lseek: offset OOR");
        return -1;
    }
    fildes[fildesc].offset = offset;
    return 0;
}

/* file referenced by fildes to be truncated to length bytes in size */
int fs_truncate(int fildesc, off_t length)
{
    /* FAILURE check */
    // file descriptor is invalid
    if(fildesc < 0 || fildesc >= MAX_FILDES)
    {
        perror("fs_truncate: invalid file descriptor OOR");
        return -1;
    }
    // file descriptor is not in use
    if(!fildes[fildesc].used)
    {
        perror("fs_truncate: file descriptor not in use");
        return -1;
    }
    // cannot extend the file with fs_truncate
    if(length > DIR[fildes[fildesc].file].size)
    {
        perror("fs_truncate: length > file size");
        return -1;
    }

    /* find new endblock offset info */
    int last_block = length / BLOCK_SIZE;
    int length_offset = length % BLOCK_SIZE;
    int head = DIR[fildes[fildesc].file].head;

    // adjust offset if extra data will be lost
    if(fildes[fildesc].offset > length)
    {
        fildes[fildesc].offset = length;
    }

    // walk the FAT to get to first block that will be truncated
    int i;
    for(i = 0; i < last_block; i++)
    {
        head = FAT[head];
    }

    // setup buffer
    for(i = 0; i < BLOCK_SIZE; i++)
    {
        fs_buffer[i] = '\0';
    }

    /* if the offset to truncate from is not block-aligned, will need to just free the part of the block that was truncated */
    block_read(head, fs_buffer);
    for(i = length_offset+1; i < BLOCK_SIZE; i++)
    {
        fs_buffer[i] = '\0';
    }
    // write back truncated version of last valid block
    block_write(head, fs_buffer);

    // clear buffer
    for(i = 0; i < BLOCK_SIZE; i++)
    {
        fs_buffer[i] = '\0';
    }
    /* walking the FAT to clear out the following blocks from the file which have been truncated */
    int linker = 1;
    int temp = head;
    while(FAT[temp] != temp)
    {
        block_write(temp, fs_buffer);
        // if at the end of the file, break
        if(temp == FAT[temp])
        {
            break;
        }
        else
        {
            temp = FAT[temp];
            /* when getting rid of last block that was truncated, need to update the FAT to point to itself */
            if(linker)
            {
                FAT[head] = head;
                linker = 0;
            }
            else
            {
                FAT[head] = -1; // free block
            }
            head = temp;        // update head to next block
        }
    }
    /* update file length metadata */
    DIR[fildes[fildesc].file].size = length;
    return 0;
}