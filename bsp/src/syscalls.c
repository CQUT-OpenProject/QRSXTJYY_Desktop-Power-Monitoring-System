/**
 * @file syscalls.c
 * @brief 裸机固件使用的最小 newlib 系统调用桩函数。
 *
 * ARM 嵌入式 C 库在链接时可能会引用 _write、_read、_close、_fstat 等
 * 类 POSIX 系统调用符号。STM32 裸机工程没有操作系统提供这些符号，
 * 这里补充最小桩函数实现，用于避免链接阶段出现未定义符号错误。
 */
#include <sys/stat.h>

int _close(int file)
{
    (void)file;
    return -1;
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

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}
