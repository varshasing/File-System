/* all definitions from the hw */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_F_NAME 15
#define MAX_FILDES 32
#define MAX_FILES 64

/* superblock */
/* refers to a number of blocks for directory information
   refers to a number of blocks that contain the FAT
   reference to first block of file data
*/
struct super_block {
    int fat_idx;    // first block of the FAT
    int fat_len;    // length of FAT in blocks
    int dir_idx;    // first block of directory
    int dir_len;    // length of directory in blocks
    int data_idx;   // first block of file-data
}super_block;

/* directory entry (file metadata) */
struct dir_entry {
    int used;       // is this file-"slot" used?
    char name[MAX_F_NAME + 1];
    int size;       // file size
    int head;       // first data block of file
    int ref_cnt;
    // how many open file descriptors are there?
    // ref_cnt > 0 -> cannot delete file
}dir_entry;

struct file_descriptor {
int used;           // fd in use
int file;           // the first block of the file
                    // (f) to which fd refers to, index of dir_entry
int offset;         // position of fd within file
}file_descriptor;

/* functions to manage file system */
int make_fs(char *disk_name);   // creates a fresh (and empty) file system on the virtual disk with name disk_name

int mount_fs(char *disk_name);  // mounts a file system that is stored on the virtual disk with name disk_name

int umount_fs(char *disk_name); // unmounts the file system from a virtual disk with name disk_name

/* file system functions, require that a file system was previously mounted */
int fs_open(char *name);        // file specified by name is opened for reading and writing, and the file descriptor corresponding to this file is returned to the calling function

int fs_close(int fildes);       // file descriptor fildes is closed

int fs_create(char *name);      // creates a new file with name name in the root directory of the file system. The file is initally empty

int fs_delete(char *name);      // deletes the file with name name from the root directory of the file system and frees all data blocks and meta-information that correspond to that file

int fs_read(int fildes, void *buf, size_t nbyte);   // attempts to read nbyte bytes from the file referenced by the descriptor fildes into the buffer pointed to by buf

int fs_write(int fildes, void *buf, size_t nbyte);  // attempts to write nbyte bytes of datat to the file referenced by the descriptor fildes from the buffer poointed to by buf

int fs_get_filesize(int fildes);    // returns current size of the file referenced by the file descriptor fildes

int fs_listfiles(char ***files);    // creates and populates an array of all filenames currently known to the file system

int fs_lseek(int fildes, off_t offset); // sets the file pointer (offset used for read and write operations) associated with the file descriptor fildes to the argument offset

int fs_truncate(int fildes, off_t length);  // causes the file referenced by fildes to be truncated to length bytes in size