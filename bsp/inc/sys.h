/**
 * @file sys.h
 * @brief ALIENTEK 风格驱动兼容头文件。
 *
 * ALIENTEK LCD 驱动依赖 u8/u16/u32 类型别名和 PAout/PBin 等位带 IO 宏。
 * 当前工程使用标准库和 CMake 组织源码，因此在这里提供兼容定义，减少
 * 对原 LCD 驱动主体的侵入式修改。
 */
#ifndef SYS_H
#define SYS_H

#include "stm32f10x.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define SYSTEM_SUPPORT_OS 0

#define BITBAND(addr, bitnum) \
    ((addr & 0xF0000000U) + 0x02000000U + ((addr & 0x000FFFFFU) << 5) + ((bitnum) << 2))
#define MEM_ADDR(addr) (*((volatile unsigned long *)(addr)))
#define BIT_ADDR(addr, bitnum) MEM_ADDR(BITBAND(addr, bitnum))

#define GPIOA_ODR_Addr (GPIOA_BASE + 12)
#define GPIOB_ODR_Addr (GPIOB_BASE + 12)
#define GPIOC_ODR_Addr (GPIOC_BASE + 12)
#define GPIOD_ODR_Addr (GPIOD_BASE + 12)
#define GPIOE_ODR_Addr (GPIOE_BASE + 12)

#define GPIOA_IDR_Addr (GPIOA_BASE + 8)
#define GPIOB_IDR_Addr (GPIOB_BASE + 8)
#define GPIOC_IDR_Addr (GPIOC_BASE + 8)
#define GPIOD_IDR_Addr (GPIOD_BASE + 8)
#define GPIOE_IDR_Addr (GPIOE_BASE + 8)

#define PAout(n) BIT_ADDR(GPIOA_ODR_Addr, n)
#define PAin(n) BIT_ADDR(GPIOA_IDR_Addr, n)
#define PBout(n) BIT_ADDR(GPIOB_ODR_Addr, n)
#define PBin(n) BIT_ADDR(GPIOB_IDR_Addr, n)
#define PCout(n) BIT_ADDR(GPIOC_ODR_Addr, n)
#define PCin(n) BIT_ADDR(GPIOC_IDR_Addr, n)
#define PDout(n) BIT_ADDR(GPIOD_ODR_Addr, n)
#define PDin(n) BIT_ADDR(GPIOD_IDR_Addr, n)
#define PEout(n) BIT_ADDR(GPIOE_ODR_Addr, n)
#define PEin(n) BIT_ADDR(GPIOE_IDR_Addr, n)

#endif
