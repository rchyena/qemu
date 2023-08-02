/*
 * STM32F4xx SYSCFG
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_STM_IWDG_H
#define HW_STM_IWDG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define STM_IWDG_KR      0x00
#define STM_IWDG_PR      0x04
#define STM_IWDG_RLR     0x08
#define STM_IWDG_SR      0x0C

#define TYPE_STM32F4XX_IWDG "stm32f4xx-iwdg"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F4xxIWDGState, STM32F4XX_IWDG)

#define SYSCFG_NUM_EXTICR 4

struct STM32F4xxIWDGState {
    /* <private> */
    SysBusDevice parent_obj;

    int reboot_enabled;         /* "Reboot" on timer expiry.  The real action
                                * performed depends on the -watchdog-action
                                * param passed on QEMU command line.
                                */  

    /* <public> */
    MemoryRegion mmio;

    int enabled;                /* If true, watchdog is enabled. */
    QEMUTimer *timer;           /* The actual watchdog timer. */
    uint32_t timer_reload;      /* Values preloaded into timer1 */
    uint32_t prescaler;

    int unlock_state;

    uint32_t iwdg_kr;
    uint32_t iwdg_pr;
    uint32_t iwdg_rlr;
    uint32_t iwdg_sr;

};

#endif
