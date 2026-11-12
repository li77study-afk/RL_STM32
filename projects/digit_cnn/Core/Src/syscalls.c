/**
 ******************************************************************************
 * @file    syscalls.c
 * @brief   newlib-nano 系统调用最小实现
 *
 * 用途: 让 printf 重定向到 USART1 (与原 usart.c 中 fputc 的目的一致)
 * 说明: Keil 使用 microlib 时 printf 走 fputc,GCC newlib 走 _write,
 *       因此这里实现 _write 转发到 HAL_UART_Transmit(&huart1)。
 ******************************************************************************
 */

#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/unistd.h>
#include "stm32h7xx_hal.h"

extern UART_HandleTypeDef huart1;

/* ------------------------- 输出: printf -> USART1 ------------------------- */
int _write(int file, char *ptr, int len)
{
  (void)file;
  if (HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY) == HAL_OK)
  {
    return len;
  }
  return 0;
}

/* ------------------------- 输入: 暂无,返回 0 ------------------------------ */
int _read(int file, char *ptr, int len)
{
  (void)file;
  (void)ptr;
  (void)len;
  return 0;
}

/* ------------------------- 其余 syscall 桩 -------------------------------- */
void _exit(int status)
{
  (void)status;
  while (1) { }
}

int _close(int file)
{
  (void)file;
  return -1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

/* 堆: 从链接脚本的 end 符号向上增长 (供 malloc 使用,当前工程未用到) */
caddr_t _sbrk(int incr)
{
  extern char end asm("end");
  static char *heap_end;
  char *prev_heap_end;

  if (heap_end == 0)
  {
    heap_end = &end;
  }
  prev_heap_end = heap_end;
  heap_end += incr;
  return (caddr_t)prev_heap_end;
}
