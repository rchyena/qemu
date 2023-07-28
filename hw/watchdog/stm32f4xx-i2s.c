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
//#include "hw/arm/stm32f407vgtx_soc.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

struct stm32f2xx_i2s_s {
    qemu_irq handler[16];

    uint16_t kr;
    uint16_t pr;
    uint16_t rlr;
    uint16_t sr;
};

static void stm32f2xx_i2s_reset(DeviceState *dev)
{
    STM32F2XXI2SState *s = STM32F2XX_I2S(dev);

    printf("stm32f2xx_i2s_reset\n");
// TODO how to reset?

}

static uint64_t stm32f2xx_i2s_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct stm32f2xx_i2s_s *s = (struct stm32f2xx_i2s_s *) opaque;


    switch (addr) {
    case 0x00: 
        return s->kr;

    case 0x04:
        // Here is where the counter might be updated?
        printf("we're in i2s");
        return s->pr;

    case 0x08:
        return s->rlr;

    case 0x0c:
        return s->sr;
    }

    stm32f2xx_BAD_REG(addr);
    return 0;
}

static void stm32f2xx_i2s_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct stm32f2xx_i2s_s *s = (struct stm32f2xx_i2s_s *) opaque;

    // here is where we are writing - register changes probably happen here
    switch (addr) {
    case 0x00:
        s->kr = value;
        break;

    case 0x04:
        s->pr = value;
        break;

    case 0x08:
        s->rlr = value;
        break;

    case 0x0c:
        s->sr = value;
        break;

    default:
        stm32f2xx_BAD_REG(addr);
        return;
    }
}

/* *Some* sources say the memory region is 32-bit.  */
static const MemoryRegionOps stm32f2xx_i2s_ops = {
    .read = stm32f2xx_i2s_read,
    .write = stm32f2xx_i2s_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stm32f2xx_i2s = {
    .name = TYPE_STM32F2XX_I2S,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(i2s_kr, STM32F2XXI2SState),
        VMSTATE_UINT32(i2s_pr, STM32F2XXI2SState),
        VMSTATE_UINT32(i2s_rlr, STM32F2XXI2SState),
        VMSTATE_UINT32(i2s_sr, STM32F2XXI2SState),
        VMSTATE_END_OF_LIST()
    }
};
static void stm32f2xx_i2s_init(Object *obj)
{
    STM32F2XXI2SState *s = STM32F2XX_I2S(obj);
    DeviceState *dev = DEVICE(obj);

    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f2xx_i2s_ops, s,
                          TYPE_STM32F2XX_I2S, 0x400);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void stm32f2xx_i2s_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_i2s_reset;
    dc->vmsd = &vmstate_stm32f2xx_i2s;
}

static const TypeInfo stm32f2xx_i2s_info = {
    .name          = TYPE_STM32F2XX_I2S,
    .parent        = TYPE_SYS_BUS_DEVICE, // is it?
    .instance_size = sizeof(STM32F2XXI2SState),
    .instance_init = stm32f2xx_i2s_init,
    .class_init    = stm32f2xx_i2s_class_init,
};

static void stm32f2xx_i2s_register_types(void)
{
    type_register_static(&stm32f2xx_i2s_info);
}

type_init(stm32f2xx_i2s_register_types)