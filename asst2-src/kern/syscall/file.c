#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>
#include <stat.h>

/*
 * Add your file-related functions here ...
 */

void ft_init(void){
    for (int i = 0; i < FILE_TABLE_SIZE; i++){
		set_ft_unused(i);
	}
}

int check_ft_unused(int ft_index){
    return file_table[ft_index].fp.offset == FILE_TABLE_UNUSED;
}

void set_ft_unused(int ft_index){
    file_table[ft_index].fp.flag = 0;
    file_table[ft_index].fp.offset = FILE_TABLE_UNUSED;
    file_table[ft_index].vnode_ptr = 0;
    file_table[ft_index].fp.fd_count = 0;
}

int get_unused_ftindex(int *ftindex){
    for (int i = 0; i < FILE_TABLE_SIZE; i++){
        if (check_ft_unused(i)){
            *ftindex = i;
            return 0;
        }
    }
    return ENFILE;
}

int get_unused_fd(int *fd){
    int *fd_table = curproc->fd_table;
    for (int i = 0; i < FD_TABLE_SIZE; i++){
        if (fd_table[i] == FD_UNUSED){
            *fd = i;
            return 0;
        }
    }
    return EMFILE;
}

void ft_decrease_count(int ft_index){
    file_table[ft_index].fp.fd_count--;
    if (file_table[ft_index].fp.fd_count <= 0){
        vfs_close(file_table[ft_index].vnode_ptr);
        set_ft_unused(ft_index);
    }
}

int sys_open_fp(char *path, int flags, int mode, int *ftindex){
    // check if flag is valid
    if (flags < 0 || flags >= 128 || ((flags & 1) > 0 && (flags & 2) > 0)){
        return EINVAL;
    }

    struct vnode *open_vnode;
    int err;

    err = vfs_open(path, flags, mode, &open_vnode);

    // error with vfs_open
    
    if (err){
        return err;
    }

    int ind;
    err = get_unused_ftindex(&ind);
    
    // global file table is full
    if (err){
        return err;
    }

    file_table[ind].fp.offset = 0;
    file_table[ind].fp.flag = flags;
    file_table[ind].fp.fd_count++;
    file_table[ind].vnode_ptr = open_vnode;
    *ftindex = ind;

    VOP_INCREF(open_vnode);

    return 0;
}

int sys_open(int a0, int a1, int a2, int *retval){
    char path[__NAME_MAX];
    int flags = a1, mode = a2;
    int err;
    size_t got;
    err = copyinstr((const_userptr_t) a0, path, __NAME_MAX, &got);
    
    // error with user_ptr
    if (err){
        return err;
    }
    
    int fd;
    err = get_unused_fd(&fd);
    
    // EMFILE error
    if (err){
        return err;
    }
    
    int ftindex;
    err = sys_open_fp(path, flags, mode, &ftindex);
    
    // pass on error from sys_open_fp()
    if (err){
        return err;
    }
    
    curproc->fd_table[fd] = ftindex;

    *retval = fd;

    return 0;
}

int sys_write(int a0, int a1, int a2, ssize_t *retval){
    int fd = a0;
    size_t num_bytes = a2;
    //char* data = kmalloc(num_bytes * sizeof(char));

    int err;

    int ft_index;

    // throw EBADF if invalid flag or invalid fd
    if (fd < 0 || fd >= FD_TABLE_SIZE || (ft_index = curproc->fd_table[fd]) == FD_UNUSED || ((file_table[ft_index].fp.flag & 3) == O_RDONLY) || check_ft_unused(ft_index)){
        return EBADF;
    }
    
    struct iovec cur_iovec;
    struct uio cur_uio;
    uio_uinit(&cur_iovec, &cur_uio, (userptr_t)a1, num_bytes, file_table[ft_index].fp.offset, UIO_WRITE);
    
    err = VOP_WRITE(file_table[ft_index].vnode_ptr, &cur_uio);
    
    // pass on error from VOP_WRITE()
    if (err){
        return err;
    }

    *retval = num_bytes - cur_uio.uio_resid;
    file_table[ft_index].fp.offset += *retval;

    return 0;
}

int sys_close(int a0){
    int fd = a0;

    int ft_index;

    // we assume that no error is thrown if fd points to an already-closed file pointer.
    if (fd < 0 || fd >= FD_TABLE_SIZE || (ft_index = curproc->fd_table[fd]) == FD_UNUSED){
        return EBADF;
    }

    // I think EIO is supposed to be a possible return value from vfs_close (that seems to be the only place where it can appear)
    //   but in this case, the vfs is confident that vfs_close returns no errors.  
    ft_decrease_count(ft_index);

    curproc->fd_table[fd] = FD_UNUSED;

    return 0;
}

int sys_read(int a0, int a1, int a2, ssize_t *retval){
    int fd = a0;
    size_t num_bytes = a2;
    //char* data = kmalloc(num_bytes * sizeof(char));

    int err;

    int ft_index;

    // throw EBADF if invalid flag or invalid fd
    if (fd < 0 || fd >= FD_TABLE_SIZE || (ft_index = curproc->fd_table[fd]) == FD_UNUSED || (file_table[ft_index].fp.flag & 3) == O_WRONLY || check_ft_unused(ft_index)){
        return EBADF;
    }
    
    struct iovec cur_iovec;
    struct uio cur_uio;
    uio_uinit(&cur_iovec, &cur_uio, (userptr_t)a1, num_bytes, file_table[ft_index].fp.offset, UIO_READ);
    
    err = VOP_READ(file_table[ft_index].vnode_ptr, &cur_uio);
    
    // pass on error from VOP_READ()
    if (err){
        return err;
    }

    *retval = num_bytes - cur_uio.uio_resid;
    file_table[ft_index].fp.offset += *retval;

    return 0;
}
int sys_dup2(int oldfd, int newfd, ssize_t *retval){
    
    /**
     * intinalize variables
     */
    int *fd_table = curproc->fd_table;
    int ft_index;
    /**
     * oldfd invalid the call should fail
     */
    if (oldfd >= FD_TABLE_SIZE || oldfd < 0)
    {
        return EBADF;
    }
    if (fd_table[oldfd] == FD_UNUSED)
    {
        return EBADF;
    }
    
    if (newfd == oldfd)
    {
        return newfd;
    }
    /**
     * if newfd points to a open file, close it
     */
    ft_index = fd_table[newfd];
    if (!check_ft_unused(ft_index))
    {
        ft_decrease_count(ft_index);
        curproc->fd_table[newfd] = FD_UNUSED;
    }
    /**
     * redirect ofptr of newfd to oldfd's ofptr
     */
    ft_index = fd_table[oldfd];
    file_table[ft_index].fp.fd_count++;
    fd_table[newfd] = fd_table[oldfd];
    *retval = newfd;
    return 0;
    /**
     * note on problem: the dup should not share the fd flag but they share offset this implementation is redirect ofptr to fp
     */
}

int sys_lseek(int fd, off_t offset, int whence, off_t *retval){
    
    /**
     * initial variables
     */
    int ofptr;
    
    if (fd < 0 || fd >= FD_TABLE_SIZE || (ofptr = curproc->fd_table[fd]) == FD_UNUSED || check_ft_unused(ofptr))
    {
        return EBADF;
    }
    
    if (!VOP_ISSEEKABLE(file_table[ofptr].vnode_ptr))
    {
        return ESPIPE;
    }
    
    off_t sum;
    switch (whence)
    {
    case SEEK_SET:
        if (offset < 0)
        {
            return EINVAL;
        }
        file_table[ofptr].fp.offset = offset;
        break;
    case SEEK_CUR:
        sum = file_table[ofptr].fp.offset + offset;
        if (sum < 0)
        {
            return EINVAL;
        }
        file_table[ofptr].fp.offset = sum;
        break;
    case SEEK_END:
    {
        struct stat st;
        int err = VOP_STAT(file_table[ofptr].vnode_ptr, &st);
        
        // pass on error from VOP_STAT
        if (err){
            return err;
        }
        sum = st.st_size + offset;
        if (sum < 0)
        {
            return EINVAL;
        }
        file_table[ofptr].fp.offset = sum;
        break;
    }
    default:
        return EINVAL;
    }
    
    *retval = file_table[ofptr].fp.offset;
    return 0;
}
