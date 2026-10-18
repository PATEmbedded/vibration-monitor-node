/*
 * Minimal system calls for newlib-nano. The firmware has no file system or console, and all
 * dynamic memory comes from the FreeRTOS heap, so these only report failure.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#undef errno
extern int errno;

int _close(int fd);
int _fstat(int fd, struct stat *st);
int _getpid(void);
int _isatty(int fd);
int _kill(int pid, int sig);
int _lseek(int fd, int offset, int whence);
int _read(int fd, char *buf, int len);
int _write(int fd, const char *buf, int len);
void *_sbrk(ptrdiff_t increment);
void _exit(int status);

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = EBADF;
    return -1;
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    (void)buf;
    errno = EBADF;
    return len > 0 ? -1 : 0;
}

void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM; /* malloc is not used: FreeRTOS owns the heap */
    return (void *)-1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}
