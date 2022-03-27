/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <vnode.h>

/*
 * Put your function declarations and data types here ...
 */

#define FILE_TABLE_SIZE OPEN_MAX
#define FILE_TABLE_UNUSED -1 // if fp.offset in ftnode is -1, we assume the array index is unused
typedef struct fp
{
    int flag;
    off_t offset;
    int fd_count;
}FP;

struct ftnode{
    FP fp;
    struct vnode *vnode_ptr;
};


struct ftnode file_table[FILE_TABLE_SIZE];

//static struct lock lock;

/* 
 * initializes ft_init(). Meant to be called in boot(). 
 */

void ft_init(void);

/* 
 * assumes valid ft_index is passed. 
 * checks if file_table[ft_index] is unused. Returns 1 
 * if true, else 0. 
 */
int check_ft_unused(int ft_index);

/* 
 * Assumes valid ft_index is passed.
 * Modifies the ft_index in such a way so that 
 * check_ft_unused() returns 1. Also resets the flag
 * and offset. 
 */

void set_ft_unused(int ft_index);
/*
 *
 * 
 * using provided pointer, saves index of an unused fp_table node
 *returns 0 on success or ENFILE if fp_table is full
 * 
 */
int get_unused_ftindex(int *ftindex);

/*
 * 
 * using provided pointer, saves an unused fd for current process
 */
int get_unused_fd(int *fd);

/* 
 * assumes given ft_index is valid
 * decreases the fd_count for the specified file pointer
 * if fd_count is 0, also closes the vnode
 */

void ft_decrease_count(int ft_index);

/*
 * opens a file/device with given arguments, adding an entry to the global file_table
 * returns ENFILE if fp_table is full, or returns any error from vfs_open()
 * otherwise, using provided pointer, saves an index to fp_table where the file can be accessed
 */
int sys_open_fp(char* path, int flags, int mode, int *ftindex);

/**
 * system handler for open(), taking registers as input
 * using pointer, saves unused fd for current process
 * returns EMFILE if process file table is full or passes on error from sys_open_fp()
 */
int sys_open(int a0, int a1, int a2, int *retval);


/**
 * system handler for write(), taking registers as input
 * returns EBADF if fd is invalid or set to read-only, or passes on error from VOP_WRITE();
 * otherwise, using the given pointer, saves the number of bytes written
 */
int sys_write(int a0, int a1, int a2, ssize_t *retval);

int sys_close(int a0);

int sys_read(int a0, int a1, int a2, ssize_t *retval);


/**
 * the dup2() system call copy the file descriptor oldfd to newfd
 */
int sys_dup2(int oldfd, int newfd, ssize_t *retval);


/**
 * repositions the file offset of the open file description associated with the file descriptor fd to the argument offset according to the directive whence 
 */
int sys_lseek(int fd, off_t offset, int whence, off_t *retval);

#endif /* _FILE_H_ */
