/* No UART / host filesystem is required. snprintf only writes into RAM. */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include "main.h"
int _write(int fd, char *buffer, int len) { (void)fd; (void)buffer; (void)len; errno=EBADF; return -1; }
int _read(int fd, char *buffer, int len) { (void)fd; (void)buffer; (void)len; return 0; }
int _close(int fd) { (void)fd; errno=EBADF; return -1; }
int _lseek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; errno=ESPIPE; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode=S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno=EINVAL; return -1; }
void _exit(int status) { (void)status; Error_Handler(); while (1) { } }
void *_sbrk(ptrdiff_t increment)
{
    static unsigned char heap[1024];
    static ptrdiff_t used;
    if (increment < -used || increment > (ptrdiff_t)sizeof(heap)-used) {
        errno=ENOMEM; return (void *)-1;
    }
    void *previous=heap+used; used+=increment; return previous;
}
