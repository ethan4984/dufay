#include <fs/fd.h>
#include <types.h>
#include <debug.h>

static_hash_table(struct fd, fd_list);

int open(char *path, int flags) {
    if(vfs_open(path, flags) == -1)
        return -1;

    struct vfs_node *node = vfs_absolute_path(path);

    if(node == NULL) {
        return -1;
    }

    struct fd fd = { .vfs_node = node,
                .flags = kmalloc(sizeof(int)),
                .loc = kmalloc(sizeof(size_t))
              };

    *fd.flags = flags;
    *fd.loc = 0;

    return hash_push(struct fd, fd_list, fd);
}

void syscall_open(struct regs *regs) {
    int fd = open((void*)regs->rdi, (int)regs->rsi);
    regs->rax = fd;
}

int read(int fd, void *buf, size_t cnt) {
    struct fd *fd_entry = hash_search(struct fd, fd_list, (size_t)fd);
    if(fd_entry == NULL) {
        set_errno(EBADF);
        return -1;
    }

    int ret = vfs_read(fd_entry->vfs_node, *fd_entry->loc, cnt, buf);
    if(ret == -1) {
        return -1;
    }

    *fd_entry->loc += cnt;

    return ret;
}

void syscall_read(struct regs *regs) {
    regs->rax = read((int)regs->rdi, (void*)regs->rsi, regs->rdx);
}

int write(int fd, void *buf, size_t cnt) {
    struct fd *fd_entry = hash_search(struct fd, fd_list, (size_t)fd);
    if(fd_entry == NULL) {
        set_errno(EBADF);
        return -1;
    }

    int ret = vfs_write(fd_entry->vfs_node, *fd_entry->loc, cnt, buf);
    if(ret == -1) 
        return -1;

    *fd_entry->loc += cnt;

    return ret;
}

void syscall_write(struct regs *regs) {
    regs->rax = write((int)regs->rdi, (void*)regs->rsi, regs->rdx);
}

int lseek(int fd, off_t off, int whence) {
    struct fd *fd_entry = hash_search(struct fd, fd_list, (size_t)fd);
    if(fd_entry == NULL) {
        set_errno(EBADF);
        return -1;
    }

    switch(whence) {
        case SEEK_SET:
            return (*fd_entry->loc = off); 
        case SEEK_CUR:
            return (*fd_entry->loc += off);
        case SEEK_END:
            return (*fd_entry->loc = fd_entry->vfs_node->stat.st_size + off);
    }

    return -1;
}

void syscall_lseek(struct regs *regs) {
    regs->rax = lseek((int)regs->rdi, (off_t)regs->rsi, (int)regs->rdx);
}

int close(int fd) {
    struct fd *fd_entry = hash_search(struct fd, fd_list, (size_t)fd);
    if(fd_entry == NULL) {
        set_errno(EBADF);
        return -1;
    }
    
    return hash_remove(struct fd, fd_list, (size_t)fd);
}

void syscall_close(struct regs *regs) {
    regs->rax = close((int)regs->rdi);
}

int dup(int fd) {
    struct fd *fd_entry = hash_search(struct fd, fd_list, (size_t)fd);
    if(fd_entry == NULL) {
        set_errno(EBADF);
        return -1;
    }

    struct fd new_fd = *fd_entry;

    return hash_push(struct fd, fd_list, new_fd);
}

void syscall_dup(struct regs *regs) {
    regs->rax = dup((int)regs->rdi);
}

int dup2(int old_fd, int new_fd) {
    struct fd *old_fd_entry = hash_search(struct fd, fd_list, (size_t)old_fd);
    if(old_fd_entry == NULL) { 
        set_errno(EBADF);
        return -1;
    }

    struct fd *new_fd_entry = hash_search(struct fd, fd_list, (size_t)new_fd);
    if(new_fd_entry != NULL) {
        close(new_fd);
    }

    struct fd fd = *old_fd_entry;

    return hash_push(struct fd, fd_list, fd);
}

void syscall_dup2(struct regs *regs) {
    regs->rax = dup2((int)regs->rdi, (int)regs->rsi);
}

void syscall_stat(struct regs *regs) {
    char *path = (void*)regs->rdi;
    struct stat *stat = (void*)regs->rsi;

    int fd = open(path, 0);
    if(fd == -1) {
        regs->rax = -1;
        return;
    }

    struct fd *fd_struct = hash_search(struct fd, fd_list, (size_t)fd);
    *stat = fd_struct->vfs_node->stat;
    regs->rax = 0;
}

void syscall_fstat(struct regs *regs) {
    int fd = (int)regs->rdi;
    struct stat *stat = (void*)regs->rsi;

    struct fd *fd_struct = hash_search(struct fd, fd_list, (size_t)fd);
    if(fd_struct == NULL) {
        regs->rax = -1;
        return;
    }
    
    *stat = fd_struct->vfs_node->stat; 
    regs->rax = 0;
}

#define F_DUPFD 1
#define F_DUPFD_CLOEXEC 2
#define F_GETFD 3
#define F_SETFD 4
#define F_GETFL 5
#define F_SETFL 6
#define F_GETLK 7
#define F_SETLK 8
#define F_SETLKW 9
#define F_GETOWN 10
#define F_SETOWN 11

#define FD_CLOEXEC 1

void syscall_fcntl(struct regs *regs) {
    int fd = (int)regs->rdi;
    int cmd = (int)regs->rsi;

    struct fd *fd_struct = hash_search(struct fd, fd_list, fd);
    if(fd_struct == NULL) {
        regs->rax = -1;
        return;
    }

    switch(cmd) {
        case F_DUPFD: 
            regs->rax = (size_t)dup2(fd, (int)regs->rdx);
            return;
        case F_GETFD:
            regs->rax = (size_t)((regs->rdx & FD_CLOEXEC) ? O_CLOEXEC : 0);
            return; 
        case F_SETFD:
            *fd_struct->flags = (int)((regs->rdx & FD_CLOEXEC) ? O_CLOEXEC : 0);
            break;
        case F_GETFL:
            regs->rax = (size_t)fd_struct->flags; 
            return;
        case F_SETFL:
            *fd_struct->flags = (int)regs->rdx;
            break;
        default:
            regs->rax = -1;
            return; 
    }

    regs->rax = 0;
    return;
}
