/**
 * @file syscalls.c
 * @brief 裸机固件使用的最小 newlib 系统调用桩函数。
 *
 * ARM 嵌入式 C 库在链接时可能会引用 _write、_read、_close、_fstat 等
 * 类 POSIX 系统调用符号。STM32 裸机工程没有操作系统提供这些符号，
 * 这里补上最小桩函数，防止链接时报未定义符号。
 */
#include <sys/stat.h>

int _close(int file)
{
    /* 裸机没有文件描述符可关闭，返回 -1 表示不支持。 */
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    /* 告诉 C 库这是字符设备，printf 这类函数就不会按普通文件处理。 */
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    /* 当作终端设备处理，满足 newlib 对 stdout/stderr 的判断。 */
    (void)file;
    return 1;
}

int _getpid(void)
{
    /* 没有进程概念，返回一个固定的假 PID 即可。 */
    return 1;
}

int _kill(int pid, int sig)
{
    /* 没有操作系统，也就没有 kill 信号可发。 */
    (void)pid;
    (void)sig;
    return -1;
}

int _lseek(int file, int ptr, int dir)
{
    /* 字符设备不支持 seek，这里只返回 0 让 C 库继续运行。 */
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    /* 当前没有把 stdin 接到串口，读操作直接返回 0 表示没有数据。 */
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _write(int file, char *ptr, int len)
{
    /*
     * 这里不负责输出字符，只告诉 C 库“写入成功”。
     * 项目自己的串口输出走 app_protocol_send_line。
     */
    (void)file;
    (void)ptr;
    return len;
}
