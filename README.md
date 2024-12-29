implement a file system on top of a virtual disk
implement a librrary that offers a set oof basic file system calls (open, read, write, ...) to applications
file data and file system meta-information will be stores on a virtual disk
virtual disk in a single file that is stored on a "real" file system provided by the linux of
thus, implementing file system on top of the linux file system

to create and access virtual disk, provided def and helper funcitons on disk.h and disk.c
have to use helper functions and store all the data that you need on the virtual disk.
virtual disk: 8192 blocks, each block holds 4KB.
create an empty disk, open and close a desk, read and write entire blocks (bloock number in range between 0 and 8191 inclusive)

file system does not have too support a directory hierarchy. instead, all files are stored in a single root directory on the virtual disk.
your file system does not have to store more than 64 files (create and delete files, and deleted files musts not count against the 64 file limit)
reserve 4096 blocks as data blocks, and free when coorresponding file is deleted.
maximum file size is 16 megabytes (4096 data blocks, each is 4kb)

need a number of data structures on disk, superblock root directory, information about free and empty blocks on disk, file meta-information (file size), and mapping from files to data blocks

DISC.C
make_disk
memset -> fills the buffer with 4096 bytes of 0
write -> writes up to 4096 bytes from buffer to file pointed to by file descriptor f

open_disk
handle is the file handle to the virtual disk and turns active on

close_disk
closes handle file descriptor and sets active and handle to 0

block_write
lseek -> reposition offset of open file assoc with file descriptor handle to block * block_size, seek_set says that the offset is set to offset bytes; returns resulting offset location measured in byters from the beginning of the file
    errir comes from:
    fd not valid
    offset cannot be represented in an off_t
    fd assoc with pipe, socket, FIFO

block_read
lseek: finds the start of the block to read, as given by block * block_size in bytes
read a block from handle into buf

FAT
tracks where data for each file is located on the disk by keeping a record of which blocks are used by whch file -- table of contents for the hard drive
fat entry represents status of a block, usually an integer and needs to represent status of all blocks on disk -> 8192 blocks
32 KB space for FAT

I found this homework to be one of the harder ones in terms of the amount of time spent on this. I spent a lot of time trying to figure out a proper stucture for the meta-information as well as trying to come up with coherent logic for the FAT.
Specifically with the FAT, I found myself especially confused on the EOF logic, relating to truncating. However, a lot more time was spent on debugging read and write, as my logic was initally flawed. A lot of this assignment had parts that relied on each other which made it seem harder to figure out things piece by piece

We had quite a few functions, the ones for managing the file system were:
1. make_fs
2. mount_fs
3. umount_fs

1. make_fs: the instructions for this one were fairly straightforward. We were to use the given disk functions to make a disk and open the disk, and then had to write the meta-information that would be used for the file system so that it could be later mounted.
My superblock contains 5 entires: fat index, fat length, directory index, directory length, and data index. These were used for keepng track of the metadata positions and lengths, as well as whenthe data started.
I opted to cram all of my metadata information into the first 10 blocks of the disk, with the superblock being the first, directory being the second and FAT being 8 others. Because this was all that I needed, I could start the data part of my disk at the 11th block.
We had to write our data to each block on the disk, so I used block_write for each of the different blocks in the metadata.
The directory had 4 values that I initalized at this point, with used, head, size, and reference count. These are all of the important thing that a directory needs to know for a file is.

2. mount_fs: mounting the file system to be stored on a disk just needed loading of the meta data. so, one could load in the FAT and directory with the indexing, as well as loading the first for the superblock. The FAT addressing was a little interesting to figure out since we had to make sure that we started at each block

3: umount_fs: this funciton uses block_write to write back all of the meta-information to the disk, that way it has been saved in state for next time.

the next functions were given to be file system functions
4: fs_open: the file specified by name is opened for reading and writingf, and a file descriptor corresponding to the file is returned to the function. I had a little bit of an issue with getting multiple fd for the same files to work, but was fixed. For this one, I found the file in the firectory first by checking to see if there is a name match, as there can only be one file of a name in our FS.
After that, I checked to see if there were any free file descriptors, as we need to open multiple ones when opening the same file. This had an upper limit of 32 fd being open at a time. After checking that those conditions were both true, I could update my directory entry with the new file entry, for making sure that the file is used, is mapped in the directory and fd list, and also update the reference count for the file.

5: fs_close: the file descriptor was to be closed and cannot be used to access a file. It required out of range checks and to see if it was actually being used, before I unlinked it with the reference counter and the metadata in the fd list

6: fs_create: To create a new file with the name in the root directory, I had to check if the name was able to fit, as well as seeing if there was any other file that had the same name. After that, I had to check if we had any available file slots, as there was a maximum number of files we could have in the FS at once.
I then had to check if there was enough free space in the FAT for one more entry, as it needs to be assigned to a block even if there is no space utilized. If there was a block, I assigned the meta infromation as needed.

7: fs_delete: this function deletes a file with name from the root directory of the file system and frees all of the corresponding file info. For this one, I had to check if there was an entry for the file in the directorym as well as making sure that there was no open file descriptors for the file. If that was the case, I used the FS buffer to create a block of null terminators to 'free' the data,
and then used a two pointer approach for traversing the FAT and freeing all blocks of memory attatched to the file that we are deleting. This meant writing an empty buffer to each block, and changing the directory and FAT metadata to reflect that no file was in that block.

8: fs_read: this function attempts to read up to nbytes of data from the file referenced by the file descriptor into the buffer poimnted to by buff. This and write required more math and thinking about how to access blocks without going out of bounds with the files or reading other information from other files.
I had come up with logic for not reading past EOF, as well as figuring out where to start reading from and where to end. I read the current block into the buffer and copied the data from the buffer to the user buffer, if the remaining bytes to read as less than the block size, then this was the last time we had to do this read and I could get out of the function.
else, I had to update the number of bytes that had been read, and figure out the byte alignment for the bytes to read
After this, I had to move the file pointer offset since the pointer for the fd not inherently is later in the file, and I cleared the buffer for good measured

9: fs_write: this also proved to be a hard time but similar logical ideas for fs_read. I had to figure out where in the file that I needed to go to in terms of blocks, and figure out if the length that we are writing would increse the number of blocks used by the file. I ran a similar logic for fs_run inwhich I checked every time to see if the byes to write is greater than nbyte that I could just write the entire thing, and read the block into the buffer
after that, I copied the data from the buffer into the user buffer, to have the offset amount, and updted my counters for keeping track of how much I had read and well as what is left.
If the offset was block algigned, it meant that we had to walk the FAT table again to find a free block that I could write the excess to. After that, I updated the file offset since if it had increased from the writing, and cleared the buffer for good measure.

10: fs_get_filesize: just required to return the value in the filesize, which was simple if the update was done correctly for the write

11: fs_listfiles: I originally had issue with understanding this function because of the triple pointer status, but once I talked about it with a TA it helped. I made a dumy to fit in all of the files and copied over each directory name if it was used. I had to make sure that I nullterminated the file and did this with memset, before assigning the list of files I created to files

12: fs_lseek: this moves arond the pointer offset and was also simpler as it only required a check to see if it was out of bounds for the file before assigning it to the fd list offset.

13: fs_truncate: this was a little harder to debug but I just had to make that this does not try to extend the file and that it would only shorten it if anything. I traversed to the last block that would be truncated off by walking the FAT table and had to write to the buffer before adding null terminator to the bytes in the block which are not included in the truncation before writing that back to the block. After that,I just continued to walk the FAT table to clear out the rest of the blocks of the file and set them to available. This means traversing until the end, which was found because the index in the FAT table is the same as the iterator that goes through it.
