/* newlib-nano 所需的最小系统调用适配。
 * 裸机没有文件系统或进程；本工程用 snprintf 写 RAM 字符串，不向串口输出。
 * (void)参数表示有意不用该参数，避免编译器未使用参数警告。 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include "main.h"
/* 当前没有标准输出后端，返回错误而不是声称数据已经发送。 */
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
    /* 为 C 库提供一个有边界的小堆。算法和帧缓冲均静态分配，不依赖 malloc。
     * 返回的是扩展前的堆尾；越界时返回 (void*)-1 并设置 ENOMEM。 */
    static unsigned char heap[1024];
    static ptrdiff_t used;
    if (increment < -used || increment > (ptrdiff_t)sizeof(heap)-used) {
        errno=ENOMEM; return (void *)-1;
    }
    void *previous=heap+used; used+=increment; return previous;
}
