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

#ifndef HW_STM_I2S_H
#define HW_STM_I2S_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define STM_I2S_KR      0x00
#define STM_I2S_PR      0x04
#define STM_I2S_RLR     0x08
#define STM_I2S_SR      0x0C

#define TYPE_STM32F4XX_I2S "stm32f4xx-I2S"
OBJECT_DECLARE_SIMPLE_TYPE(STM32F4xxI2SState, STM32F4XX_I2S)

#define SYSCFG_NUM_EXTICR 4

struct STM32F4xxI2SState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t i2s_kr;
    uint32_t i2s_pr;
    uint32_t i2s_rlr;
    uint32_t i2s_sr;

};

#endif
