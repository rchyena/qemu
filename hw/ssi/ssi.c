/*
 * QEMU Synchronous Serial Interface support
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qom/object.h"

struct SSIBus {
    BusState parent_obj;
};

#define TYPE_SSI_BUS "SSI"
OBJECT_DECLARE_SIMPLE_TYPE(SSIBus, SSI_BUS)

static const TypeInfo ssi_bus_info = {
    .name = TYPE_SSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SSIBus),
};

static void ssi_cs_default(void *opaque, int n, int level)
{
    SSIPeripheral *s = SSI_PERIPHERAL(opaque);
    bool cs = !!level;
    assert(n == 0);
	//printf("ssi_cs_default\n");
    if (s->cs != cs) {
        SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(s);
        if (ssc->set_cs) {
            ssc->set_cs(s, cs);
        }
    }
    s->cs = cs;
}

static uint32_t ssi_transfer_raw_default(SSIPeripheral *dev, uint32_t val)
{
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(dev);

	//printf("ssi_transfer_raw_default\n");
    if ((dev->cs && ssc->cs_polarity == SSI_CS_HIGH) ||
            (!dev->cs && ssc->cs_polarity == SSI_CS_LOW) ||
            ssc->cs_polarity == SSI_CS_NONE) {
        return ssc->transfer(dev, val);
    }
    return 0;
}

static void ssi_peripheral_realize(DeviceState *dev, Error **errp)
{
    SSIPeripheral *s = SSI_PERIPHERAL(dev);
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(s);

	//printf("ssi_peripheral_realize\n");
    if (ssc->transfer_raw == ssi_transfer_raw_default &&
            ssc->cs_polarity != SSI_CS_NONE) {
        qdev_init_gpio_in_named(dev, ssi_cs_default, SSI_GPIO_CS, 1);
    }

    ssc->realize(s, errp);
}

static void ssi_peripheral_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

	//printf("ssi_peripheral_class_init\n");
    dc->realize = ssi_peripheral_realize;
    dc->bus_type = TYPE_SSI_BUS;
    if (!ssc->transfer_raw) {
        ssc->transfer_raw = ssi_transfer_raw_default;
    }
}

static const TypeInfo ssi_peripheral_info = {
    .name = TYPE_SSI_PERIPHERAL,
    .parent = TYPE_DEVICE,
    .class_init = ssi_peripheral_class_init,
    .class_size = sizeof(SSIPeripheralClass),
    .abstract = true,
};

bool ssi_realize_and_unref(DeviceState *dev, SSIBus *bus, Error **errp)
{
    return qdev_realize_and_unref(dev, &bus->parent_obj, errp);
}

DeviceState *ssi_create_peripheral(SSIBus *bus, const char *name)
{
    DeviceState *dev = qdev_new(name);

	//printf("ssi_create_peripheral\n");
    ssi_realize_and_unref(dev, bus, &error_fatal);
    return dev;
}

SSIBus *ssi_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
	//printf("ssi_create_bus\n");
    bus = qbus_new(TYPE_SSI_BUS, parent, name);
    return SSI_BUS(bus);
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    BusState *b = BUS(bus);
    BusChild *kid;
    SSIPeripheralClass *ssc;
    uint32_t r = 0;

	//printf("ssi_transfer val %ld b->name %s b->children %p b->num_children %d\n", val, b->name, &b->children, b->num_children);
	

    QTAILQ_FOREACH(kid, &b->children, sibling) {
		//printf("Entering ssi_transfer loop\n");
        SSIPeripheral *peripheral = SSI_PERIPHERAL(kid->child);
		//printf("ssi_transfer peripheral %p\n", peripheral);
        ssc = SSI_PERIPHERAL_GET_CLASS(peripheral);
        r |= ssc->transfer_raw(peripheral, val);
    }

    //printf("r: %d\n", r);
    return r;
}

const VMStateDescription vmstate_ssi_peripheral = {
    .name = "SSISlave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(cs, SSIPeripheral),
        VMSTATE_END_OF_LIST()
    }
};

static void ssi_peripheral_register_types(void)
{
    type_register_static(&ssi_bus_info);
    type_register_static(&ssi_peripheral_info);
}

type_init(ssi_peripheral_register_types)
