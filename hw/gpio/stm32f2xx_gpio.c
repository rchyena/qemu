/*
 * TI stm32f2xx processors GPIO emulation.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
//#include "hw/arm/stm32f407vgtx_soc.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"

struct stm32f2xx_gpio_s {
    qemu_irq irq;
    qemu_irq handler[16];

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;
    uint16_t pins;
};

struct stm32f2xx_gpif_s {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int mpu_model;
    void *clk;
    struct stm32f2xx_gpio_s stm32f2xx1;
};

/* General-Purpose I/O of stm32f2xx1 */
static void stm32f2xx_gpio_set(void *opaque, int line, int level)
{
    trace_stm32f2xx_gpio_set("STM32F2XX_GPIO_SET\n");
    struct stm32f2xx_gpio_s *s = &((struct stm32f2xx_gpif_s *) opaque)->stm32f2xx1;
    uint16_t prev = s->inputs;

    if (level)
        s->inputs |= 1 << line;
    else
        s->inputs &= ~(1 << line);

    if (((s->edge & s->inputs & ~prev) | (~s->edge & ~s->inputs & prev)) &
                    (1 << line) & s->dir & ~s->mask) {
        s->ints |= 1 << line;
        qemu_irq_raise(s->irq);
    }
}

static uint64_t stm32f2xx_gpio_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    trace_stm32f2xx_gpio_read("STM32F2XX_GPIO_READ\n");
    struct stm32f2xx_gpio_s *s = (struct stm32f2xx_gpio_s *) opaque;
    int offset = addr & stm32f2xx_MPUI_REG_MASK;

    if (size != 2) {
        return stm32f2xx_badwidth_read16(opaque, addr);
    }

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        return s->inputs & s->pins;

    case 0x04:	/* DATA_OUTPUT */
        return s->outputs;

    case 0x08:	/* DIRECTION_CONTROL */
        return s->dir;

    case 0x0c:	/* INTERRUPT_CONTROL */
        return s->edge;

    case 0x10:	/* INTERRUPT_MASK */
        return s->mask;

    case 0x14:	/* INTERRUPT_STATUS */
        return s->ints;

    case 0x18:	/* PIN_CONTROL (not in stm32f2xx310) */
        stm32f2xx_BAD_REG(addr);
        return s->pins;
    }

    stm32f2xx_BAD_REG(addr);
    return 0;
}

static void stm32f2xx_gpio_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    trace_stm32f2xx_gpio_write("STM32F2XX_GPIO_WRITE\n");
    struct stm32f2xx_gpio_s *s = (struct stm32f2xx_gpio_s *) opaque;
    int offset = addr & stm32f2xx_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    if (size != 2) {
        stm32f2xx_badwidth_write16(opaque, addr, value);
        return;
    }

    switch (offset) {
    case 0x00:	/* DATA_INPUT */
        stm32f2xx_RO_REG(addr);
        return;

    case 0x04:	/* DATA_OUTPUT */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ctz32(diff)) != 32) {
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:	/* DIRECTION_CONTROL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ctz32(diff)) != 32) {
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x0c:	/* INTERRUPT_CONTROL */
        s->edge = value;
        break;

    case 0x10:	/* INTERRUPT_MASK */
        s->mask = value;
        break;

    case 0x14:	/* INTERRUPT_STATUS */
        s->ints &= ~value;
        if (!s->ints)
            qemu_irq_lower(s->irq);
        break;

    case 0x18:	/* PIN_CONTROL (not in stm32f2xx310 TRM) */
        stm32f2xx_BAD_REG(addr);
        s->pins = value;
        break;

    default:
        stm32f2xx_BAD_REG(addr);
        return;
    }
}

/* *Some* sources say the memory region is 32-bit.  */
static const MemoryRegionOps stm32f2xx_gpio_ops = {
    .read = stm32f2xx_gpio_read,
    .write = stm32f2xx_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f2xx_gpio_reset(struct stm32f2xx_gpio_s *s)
{
    trace_stm32f2xx_gpio_reset("STM32F2XX_GPIO_RESET\n");
    s->inputs = 0;
    s->outputs = ~0;
    s->dir = ~0;
    s->edge = ~0;
    s->mask = ~0;
    s->ints = 0;
    s->pins = ~0;
}

struct stm32f2xx2_gpio_s {
    qemu_irq irq[2];
    qemu_irq wkup;
    qemu_irq *handler;
    MemoryRegion iomem;

    uint8_t revision;
    uint8_t config[2];
    uint32_t inputs;
    uint32_t outputs;
    uint32_t dir;
    uint32_t level[2];
    uint32_t edge[2];
    uint32_t mask[2];
    uint32_t wumask;
    uint32_t ints[2];
    uint32_t debounce;
    uint8_t delay;
};

struct stm32f2xx2_gpif_s {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int mpu_model;
    void *iclk;
    void *fclk[6];
    int modulecount;
    struct stm32f2xx2_gpio_s *modules;
    qemu_irq *handler;
    int autoidle;
    int gpo;
};

/* General-Purpose Interface of stm32f2xx2/3 */
static inline void stm32f2xx2_gpio_module_int_update(struct stm32f2xx2_gpio_s *s,
                                                int line)
{
    trace_stm32f2xx2_gpio_module_int_update("STM32F2XX2_GPIO_MODULE_INT_UPDATE\n");
    qemu_set_irq(s->irq[line], s->ints[line] & s->mask[line]);
}

static void stm32f2xx2_gpio_module_wake(struct stm32f2xx2_gpio_s *s, int line)
{
    trace_stm32f2xx2_gpio_module_wake("STM32F2XX2_GPIO_MODULE_WAKE\n");
    if (!(s->config[0] & (1 << 2)))			/* ENAWAKEUP */
        return;
    if (!(s->config[0] & (3 << 3)))			/* Force Idle */
        return;
    if (!(s->wumask & (1 << line)))
        return;

    qemu_irq_raise(s->wkup);
}

static inline void stm32f2xx2_gpio_module_out_update(struct stm32f2xx2_gpio_s *s,
                uint32_t diff)
{
    trace_stm32f2xx2_gpio_module_out_update("STM32F2XX2_GPIO_MODULE_OUT_UPDATE\n");
    int ln;

    s->outputs ^= diff;
    diff &= ~s->dir;
    while ((ln = ctz32(diff)) != 32) {
        qemu_set_irq(s->handler[ln], (s->outputs >> ln) & 1);
        diff &= ~(1 << ln);
    }
}

static void stm32f2xx2_gpio_module_level_update(struct stm32f2xx2_gpio_s *s, int line)
{
    trace_stm32f2xx2_gpio_module_level_update("STM32F2XX2_GPIO_MODULE_LEVEL_UPDATE\n");
    s->ints[line] |= s->dir &
            ((s->inputs & s->level[1]) | (~s->inputs & s->level[0]));
    stm32f2xx2_gpio_module_int_update(s, line);
}

static inline void stm32f2xx2_gpio_module_int(struct stm32f2xx2_gpio_s *s, int line)
{
    trace_stm32f2xx2_gpio_module_int("STM32F2XX2_GPIO_MODULE_INT\n");
    s->ints[0] |= 1 << line;
    stm32f2xx2_gpio_module_int_update(s, 0);
    s->ints[1] |= 1 << line;
    stm32f2xx2_gpio_module_int_update(s, 1);
    stm32f2xx2_gpio_module_wake(s, line);
}

static void stm32f2xx2_gpio_set(void *opaque, int line, int level)
{
    trace_stm32f2xx2_gpio_set("STM32F2XX2_GPIO_SET\n");
    struct stm32f2xx2_gpif_s *p = opaque;
    struct stm32f2xx2_gpio_s *s = &p->modules[line >> 5];

    line &= 31;
    if (level) {
        if (s->dir & (1 << line) & ((~s->inputs & s->edge[0]) | s->level[1]))
            stm32f2xx2_gpio_module_int(s, line);
        s->inputs |= 1 << line;
    } else {
        if (s->dir & (1 << line) & ((s->inputs & s->edge[1]) | s->level[0]))
            stm32f2xx2_gpio_module_int(s, line);
        s->inputs &= ~(1 << line);
    }
}

static void stm32f2xx2_gpio_module_reset(struct stm32f2xx2_gpio_s *s)
{
    trace_stm32f2xx2_gpio_module_reset("STM32F2XX2_GPIO_MODULE_RESET\n");
    s->config[0] = 0;
    s->config[1] = 2;
    s->ints[0] = 0;
    s->ints[1] = 0;
    s->mask[0] = 0;
    s->mask[1] = 0;
    s->wumask = 0;
    s->dir = ~0;
    s->level[0] = 0;
    s->level[1] = 0;
    s->edge[0] = 0;
    s->edge[1] = 0;
    s->debounce = 0;
    s->delay = 0;
}

static uint32_t stm32f2xx2_gpio_module_read(void *opaque, hwaddr addr)
{
    trace_stm32f2xx2_gpio_module_read("STM32F2XX2_GPIO_MODULE_READ\n");
    struct stm32f2xx2_gpio_s *s = (struct stm32f2xx2_gpio_s *) opaque;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
        return s->revision;

    case 0x10:	/* GPIO_SYSCONFIG */
        return s->config[0];

    case 0x14:	/* GPIO_SYSSTATUS */
        return 0x01;

    case 0x18:	/* GPIO_IRQSTATUS1 */
        return s->ints[0];

    case 0x1c:	/* GPIO_IRQENABLE1 */
    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
    case 0x64:	/* GPIO_SETIRQENABLE1 */
        return s->mask[0];

    case 0x20:	/* GPIO_WAKEUPENABLE */
    case 0x80:	/* GPIO_CLEARWKUENA */
    case 0x84:	/* GPIO_SETWKUENA */
        return s->wumask;

    case 0x28:	/* GPIO_IRQSTATUS2 */
        return s->ints[1];

    case 0x2c:	/* GPIO_IRQENABLE2 */
    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
    case 0x74:	/* GPIO_SETIREQNEABLE2 */
        return s->mask[1];

    case 0x30:	/* GPIO_CTRL */
        return s->config[1];

    case 0x34:	/* GPIO_OE */
        return s->dir;

    case 0x38:	/* GPIO_DATAIN */
        return s->inputs;

    case 0x3c:	/* GPIO_DATAOUT */
    case 0x90:	/* GPIO_CLEARDATAOUT */
    case 0x94:	/* GPIO_SETDATAOUT */
        return s->outputs;

    case 0x40:	/* GPIO_LEVELDETECT0 */
        return s->level[0];

    case 0x44:	/* GPIO_LEVELDETECT1 */
        return s->level[1];

    case 0x48:	/* GPIO_RISINGDETECT */
        return s->edge[0];

    case 0x4c:	/* GPIO_FALLINGDETECT */
        return s->edge[1];

    case 0x50:	/* GPIO_DEBOUNCENABLE */
        return s->debounce;

    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        return s->delay;
    }

    stm32f2xx_BAD_REG(addr);
    return 0;
}

static void stm32f2xx2_gpio_module_write(void *opaque, hwaddr addr,
                uint32_t value)
{
    trace_stm32f2xx2_gpio_module_write("STM32F2XX2_GPIO_MODULE_WRITE\n");
    struct stm32f2xx2_gpio_s *s = (struct stm32f2xx2_gpio_s *) opaque;
    uint32_t diff;
    int ln;

    switch (addr) {
    case 0x00:	/* GPIO_REVISION */
    case 0x14:	/* GPIO_SYSSTATUS */
    case 0x38:	/* GPIO_DATAIN */
        stm32f2xx_RO_REG(addr);
        break;

    case 0x10:	/* GPIO_SYSCONFIG */
        if (((value >> 3) & 3) == 3) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Illegal IDLEMODE value: 3\n", __func__);
        }
        if (value & 2)
            stm32f2xx2_gpio_module_reset(s);
        s->config[0] = value & 0x1d;
        break;

    case 0x18:	/* GPIO_IRQSTATUS1 */
        if (s->ints[0] & value) {
            s->ints[0] &= ~value;
            stm32f2xx2_gpio_module_level_update(s, 0);
        }
        break;

    case 0x1c:	/* GPIO_IRQENABLE1 */
        s->mask[0] = value;
        stm32f2xx2_gpio_module_int_update(s, 0);
        break;

    case 0x20:	/* GPIO_WAKEUPENABLE */
        s->wumask = value;
        break;

    case 0x28:	/* GPIO_IRQSTATUS2 */
        if (s->ints[1] & value) {
            s->ints[1] &= ~value;
            stm32f2xx2_gpio_module_level_update(s, 1);
        }
        break;

    case 0x2c:	/* GPIO_IRQENABLE2 */
        s->mask[1] = value;
        stm32f2xx2_gpio_module_int_update(s, 1);
        break;

    case 0x30:	/* GPIO_CTRL */
        s->config[1] = value & 7;
        break;

    case 0x34:	/* GPIO_OE */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ctz32(diff)) != 32) {
            diff &= ~(1 << ln);
            qemu_set_irq(s->handler[ln], (value >> ln) & 1);
        }

        stm32f2xx2_gpio_module_level_update(s, 0);
        stm32f2xx2_gpio_module_level_update(s, 1);
        break;

    case 0x3c:	/* GPIO_DATAOUT */
        stm32f2xx2_gpio_module_out_update(s, s->outputs ^ value);
        break;

    case 0x40:	/* GPIO_LEVELDETECT0 */
        s->level[0] = value;
        stm32f2xx2_gpio_module_level_update(s, 0);
        stm32f2xx2_gpio_module_level_update(s, 1);
        break;

    case 0x44:	/* GPIO_LEVELDETECT1 */
        s->level[1] = value;
        stm32f2xx2_gpio_module_level_update(s, 0);
        stm32f2xx2_gpio_module_level_update(s, 1);
        break;

    case 0x48:	/* GPIO_RISINGDETECT */
        s->edge[0] = value;
        break;

    case 0x4c:	/* GPIO_FALLINGDETECT */
        s->edge[1] = value;
        break;

    case 0x50:	/* GPIO_DEBOUNCENABLE */
        s->debounce = value;
        break;

    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        s->delay = value;
        break;

    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
        s->mask[0] &= ~value;
        stm32f2xx2_gpio_module_int_update(s, 0);
        break;

    case 0x64:	/* GPIO_SETIRQENABLE1 */
        s->mask[0] |= value;
        stm32f2xx2_gpio_module_int_update(s, 0);
        break;

    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
        s->mask[1] &= ~value;
        stm32f2xx2_gpio_module_int_update(s, 1);
        break;

    case 0x74:	/* GPIO_SETIREQNEABLE2 */
        s->mask[1] |= value;
        stm32f2xx2_gpio_module_int_update(s, 1);
        break;

    case 0x80:	/* GPIO_CLEARWKUENA */
        s->wumask &= ~value;
        break;

    case 0x84:	/* GPIO_SETWKUENA */
        s->wumask |= value;
        break;

    case 0x90:	/* GPIO_CLEARDATAOUT */
        stm32f2xx2_gpio_module_out_update(s, s->outputs & value);
        break;

    case 0x94:	/* GPIO_SETDATAOUT */
        stm32f2xx2_gpio_module_out_update(s, ~s->outputs & value);
        break;

    default:
        stm32f2xx_BAD_REG(addr);
        return;
    }
}

static uint64_t stm32f2xx2_gpio_module_readp(void *opaque, hwaddr addr,
                                        unsigned size)
{
    trace_stm32f2xx2_gpio_module_readp("STM32F2XX2_GPIO_MODULE_READP\n");
    return stm32f2xx2_gpio_module_read(opaque, addr & ~3) >> ((addr & 3) << 3);
}

static void stm32f2xx2_gpio_module_writep(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    trace_stm32f2xx2_gpio_module_writep("STM32F2XX2_GPIO_MODULE_WRITEP\n");
    uint32_t cur = 0;
    uint32_t mask = 0xffff;

    if (size == 4) {
        stm32f2xx2_gpio_module_write(opaque, addr, value);
        return;
    }

    switch (addr & ~3) {
    case 0x00:	/* GPIO_REVISION */
    case 0x14:	/* GPIO_SYSSTATUS */
    case 0x38:	/* GPIO_DATAIN */
        stm32f2xx_RO_REG(addr);
        break;

    case 0x10:	/* GPIO_SYSCONFIG */
    case 0x1c:	/* GPIO_IRQENABLE1 */
    case 0x20:	/* GPIO_WAKEUPENABLE */
    case 0x2c:	/* GPIO_IRQENABLE2 */
    case 0x30:	/* GPIO_CTRL */
    case 0x34:	/* GPIO_OE */
    case 0x3c:	/* GPIO_DATAOUT */
    case 0x40:	/* GPIO_LEVELDETECT0 */
    case 0x44:	/* GPIO_LEVELDETECT1 */
    case 0x48:	/* GPIO_RISINGDETECT */
    case 0x4c:	/* GPIO_FALLINGDETECT */
    case 0x50:	/* GPIO_DEBOUNCENABLE */
    case 0x54:	/* GPIO_DEBOUNCINGTIME */
        cur = stm32f2xx2_gpio_module_read(opaque, addr & ~3) &
                ~(mask << ((addr & 3) << 3));

        /* Fall through.  */
    case 0x18:	/* GPIO_IRQSTATUS1 */
    case 0x28:	/* GPIO_IRQSTATUS2 */
    case 0x60:	/* GPIO_CLEARIRQENABLE1 */
    case 0x64:	/* GPIO_SETIRQENABLE1 */
    case 0x70:	/* GPIO_CLEARIRQENABLE2 */
    case 0x74:	/* GPIO_SETIREQNEABLE2 */
    case 0x80:	/* GPIO_CLEARWKUENA */
    case 0x84:	/* GPIO_SETWKUENA */
    case 0x90:	/* GPIO_CLEARDATAOUT */
    case 0x94:	/* GPIO_SETDATAOUT */
        value <<= (addr & 3) << 3;
        stm32f2xx2_gpio_module_write(opaque, addr, cur | value);
        break;

    default:
        stm32f2xx_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps stm32f2xx2_gpio_module_ops = {
    .read = stm32f2xx2_gpio_module_readp,
    .write = stm32f2xx2_gpio_module_writep,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f2xx_gpif_reset(DeviceState *dev)
{
    trace_stm32f2xx_gpif_reset("STM32F2XX_GPIF_RESET\n");
    struct stm32f2xx_gpif_s *s = stm32f2xx1_GPIO(dev);

    stm32f2xx_gpio_reset(&s->stm32f2xx1);
}

static void stm32f2xx2_gpif_reset(DeviceState *dev)
{
    trace_stm32f2xx2_gpif_reset("STM32F2XX2_GPIF_RESET\n");
    struct stm32f2xx2_gpif_s *s = stm32f2xx2_GPIO(dev);
    int i;

    for (i = 0; i < s->modulecount; i++) {
        stm32f2xx2_gpio_module_reset(&s->modules[i]);
    }
    s->autoidle = 0;
    s->gpo = 0;
}

static uint64_t stm32f2xx2_gpif_top_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    trace_stm32f2xx2_gpif_top_read("STM32F2XX2_GPIF_TOP_READ\n");
    struct stm32f2xx2_gpif_s *s = (struct stm32f2xx2_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
        return 0x18;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        return s->autoidle;

    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
        return 0x01;

    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
        return 0x00;

    case 0x40:	/* IPGENERICOCPSPL_GPO */
        return s->gpo;

    case 0x50:	/* IPGENERICOCPSPL_GPI */
        return 0x00;
    }

    stm32f2xx_BAD_REG(addr);
    return 0;
}

static void stm32f2xx2_gpif_top_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    trace_stm32f2xx2_gpif_top_write("STM32F2XX2_GPIF_TOP_WRITE\n");
    struct stm32f2xx2_gpif_s *s = (struct stm32f2xx2_gpif_s *) opaque;

    switch (addr) {
    case 0x00:	/* IPGENERICOCPSPL_REVISION */
    case 0x14:	/* IPGENERICOCPSPL_SYSSTATUS */
    case 0x18:	/* IPGENERICOCPSPL_IRQSTATUS */
    case 0x50:	/* IPGENERICOCPSPL_GPI */
        stm32f2xx_RO_REG(addr);
        break;

    case 0x10:	/* IPGENERICOCPSPL_SYSCONFIG */
        if (value & (1 << 1))					/* SOFTRESET */
            stm32f2xx2_gpif_reset(DEVICE(s));
        s->autoidle = value & 1;
        break;

    case 0x40:	/* IPGENERICOCPSPL_GPO */
        s->gpo = value & 1;
        break;

    default:
        stm32f2xx_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps stm32f2xx2_gpif_top_ops = {
    .read = stm32f2xx2_gpif_top_read,
    .write = stm32f2xx2_gpif_top_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f2xx_gpio_init(Object *obj)
{
    trace_stm32f2xx_gpio_init("STM32F2XX_GPIO_INIT\n");
    DeviceState *dev = DEVICE(obj);
    struct stm32f2xx_gpif_s *s = stm32f2xx1_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, stm32f2xx_gpio_set, 16);
    qdev_init_gpio_out(dev, s->stm32f2xx1.handler, 16);
    sysbus_init_irq(sbd, &s->stm32f2xx1.irq);
    memory_region_init_io(&s->iomem, obj, &stm32f2xx_gpio_ops, &s->stm32f2xx1,
                          "stm32f2xx.gpio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void stm32f2xx_gpio_realize(DeviceState *dev, Error **errp)
{
    trace_stm32f2xx_gpio_realize("STM32F2XX_GPIO_REALIZE\n");
    struct stm32f2xx_gpif_s *s = stm32f2xx1_GPIO(dev);

    if (!s->clk) {
        error_setg(errp, "stm32f2xx-gpio: clk not connected");
    }
}

static void stm32f2xx2_gpio_realize(DeviceState *dev, Error **errp)
{
    trace_stm32f2xx2_gpio_realize("STM32F2XX2_GPIO_REALIZE\n");
    struct stm32f2xx2_gpif_s *s = stm32f2xx2_GPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (!s->iclk) {
        error_setg(errp, "stm32f2xx2-gpio: iclk not connected");
        return;
    }

    s->modulecount = s->mpu_model < stm32f2xx2430 ? 4
        : s->mpu_model < stm32f2xx3430 ? 5
        : 6;

    if (s->mpu_model < stm32f2xx3430) {
        memory_region_init_io(&s->iomem, OBJECT(dev), &stm32f2xx2_gpif_top_ops, s,
                              "stm32f2xx2.gpio", 0x1000);
        sysbus_init_mmio(sbd, &s->iomem);
    }

    s->modules = g_new0(struct stm32f2xx2_gpio_s, s->modulecount);
    s->handler = g_new0(qemu_irq, s->modulecount * 32);
    qdev_init_gpio_in(dev, stm32f2xx2_gpio_set, s->modulecount * 32);
    qdev_init_gpio_out(dev, s->handler, s->modulecount * 32);

    for (i = 0; i < s->modulecount; i++) {
        struct stm32f2xx2_gpio_s *m = &s->modules[i];

        if (!s->fclk[i]) {
            error_setg(errp, "stm32f2xx2-gpio: fclk%d not connected", i);
            return;
        }

        m->revision = (s->mpu_model < stm32f2xx3430) ? 0x18 : 0x25;
        m->handler = &s->handler[i * 32];
        sysbus_init_irq(sbd, &m->irq[0]); /* mpu irq */
        sysbus_init_irq(sbd, &m->irq[1]); /* dsp irq */
        sysbus_init_irq(sbd, &m->wkup);
        memory_region_init_io(&m->iomem, OBJECT(dev), &stm32f2xx2_gpio_module_ops, m,
                              "stm32f2xx.gpio-module", 0x1000);
        sysbus_init_mmio(sbd, &m->iomem);
    }
}

void stm32f2xx_gpio_set_clk(stm32f2xx_gpif *gpio, stm32f2xx_clk clk)
{
    trace_stm32f2xx_gpio_set_clk("STM32F2XX_GPIO_SET_CLK\n");
    gpio->clk = clk;
}

static Property stm32f2xx_gpio_properties[] = {
    DEFINE_PROP_INT32("mpu_model", struct stm32f2xx_gpif_s, mpu_model, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f2xx_gpio_class_init(ObjectClass *klass, void *data)
{
    trace_stm32f2xx_gpio_class_init("STM32F2XX_GPIO_CLASS_INIT\n");
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f2xx_gpio_realize;
    dc->reset = stm32f2xx_gpif_reset;
    device_class_set_props(dc, stm32f2xx_gpio_properties);
    /* Reason: pointer property "clk" */
    dc->user_creatable = false;
}

static const TypeInfo stm32f2xx_gpio_info = {
    .name          = TYPE_stm32f2xx1_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32f2xx_gpif_s),
    .instance_init = stm32f2xx_gpio_init,
    .class_init    = stm32f2xx_gpio_class_init,
};

void stm32f2xx2_gpio_set_iclk(stm32f2xx2_gpif *gpio, stm32f2xx_clk clk)
{
    trace_stm32f2xx2_gpio_set_iclk("STM32F2XX2_GPIO_SET_ICLK\n");
    gpio->iclk = clk;
}

void stm32f2xx2_gpio_set_fclk(stm32f2xx2_gpif *gpio, uint8_t i, stm32f2xx_clk clk)
{
    trace_stm32f2xx2_gpio_set_fclk("STM32F2XX2_GPIO_SET_FCLK\n");
    assert(i <= 5);
    gpio->fclk[i] = clk;
}

static Property stm32f2xx2_gpio_properties[] = {
    DEFINE_PROP_INT32("mpu_model", struct stm32f2xx2_gpif_s, mpu_model, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f2xx2_gpio_class_init(ObjectClass *klass, void *data)
{
    trace_stm32f2xx2_gpio_class_init("STM32F2XX2_GPIO_CLASS_INIT\n");
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f2xx2_gpio_realize;
    dc->reset = stm32f2xx2_gpif_reset;
    device_class_set_props(dc, stm32f2xx2_gpio_properties);
    /* Reason: pointer properties "iclk", "fclk0", ..., "fclk5" */
    dc->user_creatable = false;
}

static const TypeInfo stm32f2xx2_gpio_info = {
    .name          = TYPE_stm32f2xx2_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct stm32f2xx2_gpif_s),
    .class_init    = stm32f2xx2_gpio_class_init,
};

static void stm32f2xx_gpio_register_types(void)
{
    trace_stm32f2xx_gpio_register_types("STM32F2XX_GPIO_REGISTER_TYPES\n");
    type_register_static(&stm32f2xx_gpio_info);
    type_register_static(&stm32f2xx2_gpio_info);
}

type_init(stm32f2xx_gpio_register_types)
