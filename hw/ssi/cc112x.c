/*
 * Maxim MAX1110/1111 ADC chip emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPLv2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/ssi/cc112x.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"

/* Control-byte bitfields */
#define CB_PD0		(1 << 0)
#define CB_PD1		(1 << 1)
#define CB_SGL		(1 << 2)
#define CB_UNI		(1 << 3)
#define CB_SEL0		(1 << 4)
#define CB_SEL1		(1 << 5)
#define CB_SEL2		(1 << 6)
#define CB_START	(1 << 7)

#define CHANNEL_NUM(v, b0, b1, b2)	\
                        ((((v) >> (2 + (b0))) & 4) |	\
                         (((v) >> (3 + (b1))) & 2) |	\
                         (((v) >> (4 + (b2))) & 1))

static uint32_t cc112x_read(CC112xState *s)
{
    if (!s->tb1)
        return 0;
	printf("cc112x_read\n");

    switch (s->cycle ++) {
    case 1:
        return s->rb2;
    case 2:
        return s->rb3;
    }

    return 0;
}

/* Interpret a control-byte */
static void cc112x_write(CC112xState *s, uint32_t value)
{
    int measure, chan;
	printf("cc112x_write %x\n",value);

    /* Ignore the value if START bit is zero */
    if (!(value & CB_START))
        return;

    s->cycle = 0;

    if (!(value & CB_PD1)) {
        s->tb1 = 0;
        return;
    }

    s->tb1 = value;

    if (s->inputs == 8)
        chan = CHANNEL_NUM(value, 1, 0, 2);
    else
        chan = CHANNEL_NUM(value & ~CB_SEL0, 0, 1, 2);

    if (value & CB_SGL)
        measure = s->input[chan] - s->com;
    else
        measure = s->input[chan] - s->input[chan ^ 1];

    if (!(value & CB_UNI))
        measure ^= 0x80;

    s->rb2 = (measure >> 2) & 0x3f;
    s->rb3 = (measure << 6) & 0xc0;

    /* FIXME: When should the IRQ be lowered?  */
    qemu_irq_raise(s->interrupt);
}

static uint32_t cc112x_transfer(SSIPeripheral *dev, uint32_t value)
{
    CC112xState *s = CC_112X(dev);
    cc112x_write(s, value);
	printf("cc112x_transfer %x\n",value);
    return cc112x_read(s);
}

static const VMStateDescription vmstate_cc112x = {
    .name = "cc112x",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_SSI_PERIPHERAL(parent_obj, CC112xState),
        VMSTATE_UINT8(tb1, CC112xState),
        VMSTATE_UINT8(rb2, CC112xState),
        VMSTATE_UINT8(rb3, CC112xState),
        VMSTATE_INT32_EQUAL(inputs, CC112xState, NULL),
        VMSTATE_INT32(com, CC112xState),
        VMSTATE_ARRAY_INT32_UNSAFE(input, CC112xState, inputs,
                                   vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static void cc112x_input_set(void *opaque, int line, int value)
{
    CC112xState *s = CC_112X(opaque);

    assert(line >= 0 && line < s->inputs);
    s->input[line] = value;
}

static int cc112x_init(SSIPeripheral *d, int inputs)
{
    DeviceState *dev = DEVICE(d);
    CC112xState *s = CC_112X(dev);

    qdev_init_gpio_out(dev, &s->interrupt, 1);
    qdev_init_gpio_in(dev, cc112x_input_set, inputs);
	printf("cc112x_init\n");
    s->inputs = inputs;

    return 0;
}


// Once device is init'ed, it will be realized
// 
static void cc1120_realize(SSIPeripheral *dev, Error **errp)
{
	printf("cc1120_realize\n");
    cc112x_init(dev, 4);
}

static void cc112x_reset(DeviceState *dev)
{
    CC112xState *s = CC_112X(dev);
    int i;

    for (i = 0; i < s->inputs; i++) {
        s->input[i] = s->reset_input[i];
    }
    s->com = 0;
    s->tb1 = 0;
    s->rb2 = 0;
    s->rb3 = 0;
    s->cycle = 0;
}


static Property cc1120_properties[] = {
    /* Reset values for ADC inputs */
    DEFINE_PROP_UINT8("input0", CC112xState, reset_input[0], 0xf0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cc112x_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->transfer = cc112x_transfer;
    dc->reset = cc112x_reset;
    dc->vmsd = &vmstate_cc112x;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo cc112x_info = {
    .name          = TYPE_CC_112X,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(CC112xState),
    .class_init    = cc112x_class_init,
    .abstract      = true,
};


static void cc1120_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = cc1120_realize;
    device_class_set_props(dc, cc1120_properties);
}

static const TypeInfo cc1120_info = {
    // Just here for describing relationship
    .name          = TYPE_CC_1120,
    .parent        = TYPE_CC_112X,
    .class_init    = cc1120_class_init,
};

// For registering object type
static void cc112x_register_types(void)
{
    // Supplying pointer to descriptor structure
    type_register_static(&cc112x_info);
    type_register_static(&cc1120_info);
}

// Creating object by passing in fn for init of this type
type_init(cc112x_register_types)
