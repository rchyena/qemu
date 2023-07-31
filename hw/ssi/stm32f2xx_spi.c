/*
 * STM32F405 SPI
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/stm32f2xx_spi.h"
#include "migration/vmstate.h"

#ifndef STM_SPI_ERR_DEBUG
#define STM_SPI_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_SPI_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

/* Passthrough Implemenation */
// Tells write to discard the 0 value.
int radio_addr = 0;
// Check for if we've seen data byte.
int seen_value = 0;
int toWrite = 0;
int toRead = 0;

/* Registers 
 * Keeps track of registers and their values
 * e.g. [reg_addr][reg_value]
 *      cc_tx_wr_rg(FS_VCO2, 0xFF) -> registers[FS_VC02][0xFF]
 *      cc_tx_rd_rg(FS_VCO2, &value) -> value = 0xFF
 */
uint8_t registers[217][1];

  
static void stm32f2xx_spi_reset(DeviceState *dev)
{
    STM32F2XXSPIState *s = STM32F2XX_SPI(dev);

	printf("stm32f2xx_spi_reset\n");
    s->spi_cr1 = 0x00000000;
    s->spi_cr2 = 0x00000000;
    s->spi_sr = 0x0000000A;
    s->spi_dr = 0x0000000C;
    s->spi_crcpr = 0x00000007;
    s->spi_rxcrcr = 0x00000000;
    s->spi_txcrcr = 0x00000000;
    s->spi_i2scfgr = 0x00000000;
    s->spi_i2spr = 0x00000002;
}

/*
 * Pull something from SSI onto DR, then telling DR we received.
 * Goes out, updates DR.
 * "~~compare~~ and swap".
 *
 * Normally another device would sit on the other end of SPIBUS,
 * but it doesn't exist here. So we can't swap data bytes.
 */
static void stm32f2xx_spi_transfer(STM32F2XXSPIState *s)
{
    DB_PRINT("Data to send: 0x%x\n", s->spi_dr);
    printf("Data to send: 0x%x\n", s->spi_dr);
    printf("ssi_transfer before s->ssi: 0x%x\n", s->ssi);

    //s->spi_dr = ssi_transfer(s->ssi, s->spi_dr);
    // Assuming status register needs to be updated to reflect some sort of status?
    s->spi_sr |= STM_SPI_SR_RXNE;

    // Not finding device and just receiving 0.
    DB_PRINT("Data received: 0x%x\n", s->spi_dr);
    printf("Data received: 0x%x\n", s->spi_dr);
    printf("ssi_transfer after s->ssi: 0x%x\n", s->ssi);

}

const char *spi_addrs[] = {
"cr1",
"0x01",
"0x02",
"0x03",
"cr2",
"0x05",
"0x06",
"0x07",
"sr",
"0x09",
"0x0a",
"0x0b",
"dr",
"0x0d",
"0x0e",
"0x0f",
"crcpr",
"0x11",
"0x12",
"0x13",
"rxcrcr",
"0x15",
"0x16",
"0x17",
"txcrcr",
"0x19",
"0x1a",
"0x1b",
"i2scfgr",
"0x1d",
"0x1e",
"0x1f",
"0x20",
};

static uint64_t stm32f2xx_spi_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F2XXSPIState *s = opaque;

    DB_PRINT("Address: 0x%" HWADDR_PRIx "\n", addr);
    printf("stm32f2xx_spi_read register: 0x%" HWADDR_PRIx " %s\n", addr, spi_addrs[addr]);
    printf("cr1: %llx, sr: %llx, dr: %llx\n", s->spi_cr1, s->spi_sr, s->spi_dr);

    switch (addr) {
    case STM_SPI_CR1:
        return s->spi_cr1;
    case STM_SPI_CR2:
        qemu_log_mask(LOG_UNIMP, "%s: Interrupts and DMA are not implemented\n",
                      __func__);
        return s->spi_cr2;
    case STM_SPI_SR:
        return s->spi_sr;
    case STM_SPI_DR:
        stm32f2xx_spi_transfer(s);
        s->spi_sr &= ~STM_SPI_SR_RXNE;
        /*
         * We've passed check 1, but we need another check (i.e. seen_value), because
         * otherwise we return 0x41 too soon. We need the next read to be 0x41.
         */
        // Reads from marcstate register
        // Separate case, because we're expecting a return that wasn't a previous register write?
        if (radio_addr == 0x73) {
            radio_addr = 0;
            seen_value = 1;
        } else if (seen_value == 1) {
            seen_value = 0;
            s->spi_dr = 0x41;
        } else if ((radio_addr != 0) && (toRead == 1)) {
            seen_value = registers[radio_addr][0];
            printf("Read 0x%x from 0x%x\n", seen_value, radio_addr);
            toRead = 2;
        } else if (toRead == 2) {
            s->spi_dr = seen_value;
            seen_value = 0;
            toRead = 0;
        }

        printf("returning from spi_read with s->spi_dr: 0x%x\n", s->spi_dr);
        return s->spi_dr;
    case STM_SPI_CRCPR:
        qemu_log_mask(LOG_UNIMP, "%s: CRC is not implemented, the registers " \
                      "are included for compatibility\n", __func__);
        return s->spi_crcpr;
    case STM_SPI_RXCRCR:
        qemu_log_mask(LOG_UNIMP, "%s: CRC is not implemented, the registers " \
                      "are included for compatibility\n", __func__);
        return s->spi_rxcrcr;
    case STM_SPI_TXCRCR:
        qemu_log_mask(LOG_UNIMP, "%s: CRC is not implemented, the registers " \
                      "are included for compatibility\n", __func__);
        return s->spi_txcrcr;
    case STM_SPI_I2SCFGR:
        qemu_log_mask(LOG_UNIMP, "%s: I2S is not implemented, the registers " \
                      "are included for compatibility\n", __func__);
        return s->spi_i2scfgr;
    case STM_SPI_I2SPR:
        qemu_log_mask(LOG_UNIMP, "%s: I2S is not implemented, the registers " \
                      "are included for compatibility\n", __func__);
        return s->spi_i2spr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }

    return 0;
}

static void stm32f2xx_spi_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    STM32F2XXSPIState *s = opaque;
    uint32_t value = val64;

    DB_PRINT("Address: 0x%" HWADDR_PRIx ", Value: 0x%x\n", addr, value);
    printf("stm32f2xx_spi_write: 0x%" HWADDR_PRIx " %s, Value: 0x%x\n", addr, spi_addrs[addr], value);
    
    /*
     * Scripting (passthrough) attempt.
     * Switch statement here is interpreting bytes as the come in from the Tx Buffer
     * Value can be anything coming from TxBuffer. Below are key values to make note of.
     * Radio reg addresses > 0x2F00 have 0x2F masked away on reads/writes
     * Radio reg addresses < 0x2F00 are not altered for writes
     *     but for reads, the address is AND'ed with 0x0080, then stripped of first 2 bytes
     *
     * If spi_write() is called AND (reg addr >= CC_EXT_ADD (0x2F00)), then first byte is a "header" value of 0x2F or 0xAF
     * 0x2F: Signals register write - write data byte into register variable. (2F + XX)
     * 0xAF: Signals register read - return data from register variable. (AF + XX)
     * 0x73: Marcstate address (set radio_addr to indicate we're about to read marcstate).
     * 0x25: FS_VCO2 register address (2F25)
     * 0x15: FS_CAL2 register address (2F15)
     *
     * If spi_write() is called AND (reg addr < CC_EXT_ADD (0x2F00)), then it will NOT have a header value,
     * first byte is addr, and read/write is determined by value of address
     * 0x00: IOCFG3 register address
     */
    switch (value) {
        case 0x2f:
            printf("Case 2f\n");
            toWrite = 1;
            break;
        case 0xaf:
            printf("Case af\n");
            toRead = 1;
            break;
        /*
        case 0x73:
            radio_addr = value;
            break;
        case 0x25:
            radio_addr = value;
            break;
        case 0x15:
            radio_addr = value;
            break;
        */
        default:
            radio_addr = value;
            break;
    }
    
    /* Reads and writes with no header (i.e., radio address < 0x2F)
     * Read: Address was AND'ed with 0x0080 before coming here, so
     *       anything > 0x2F is a read (anything < 0x2F & 0x0080 = >0x2F)
     * Write: Address was left intact
    */
    if ((toWrite == 0) && (toRead == 0) && (radio_addr < 0x2f)) {
        toWrite = 1;
    // READ with no header, interested in address value (>0x2F = read)
    } else if ((toWrite == 0) && (toRead == 0) && (radio_addr > 0x2f)) {
        toRead = 1;
    }
    
    /* Reads and writes with a header (i.e., radio address > 0x2F)
     * This is a wr_rg call and we've seen address of register to write to (here, value should be byte to write)
    */
    if ((toWrite == 1) && (radio_addr != 0) && (radio_addr != value)) {
        // Referencing register using extended reg address (i.e. lower 2 bytes as referenced by upsat)
        printf("Write 0x%x to 0x%x\n", value, radio_addr);
        registers[radio_addr][0] = value;
        printf("Confirming value was written: 0x%x\n", registers[radio_addr][0]);
        toWrite = 0;
        radio_addr = 0;
    }

    switch (addr) {
    case STM_SPI_CR1:
        s->spi_cr1 = value;
        return;
    case STM_SPI_CR2:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Interrupts and DMA are not implemented\n", __func__);
        s->spi_cr2 = value;
        return;
    case STM_SPI_SR:
        /* Read only register, except for clearing the CRCERR bit, which
         * is not supported.
         */
        return;
    case STM_SPI_DR:
        /* If spi_dr gets set to 0, the RxBuffer receives this as last byte instead of marcstate (value of 0 results in the Rx[len-1]).
         * Why even Tx a 0 if we are placing that into RxBuffer? Is shipping out 0 supposed to return something else?
         * It seems yes, shipping out 0 is supposed to return something non-zero, but we have to work around that here since no device.
         */
        if ((value == 0) && (radio_addr != 0)) {
            stm32f2xx_spi_transfer(s);
            return;
        }
        s->spi_dr = value;
        stm32f2xx_spi_transfer(s);
        return;
    case STM_SPI_CRCPR:
        qemu_log_mask(LOG_UNIMP, "%s: CRC is not implemented\n", __func__);
        return;
    case STM_SPI_RXCRCR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Read only register: " \
                      "0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    case STM_SPI_TXCRCR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Read only register: " \
                      "0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    case STM_SPI_I2SCFGR:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "I2S is not implemented\n", __func__);
        return;
    case STM_SPI_I2SPR:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "I2S is not implemented\n", __func__);
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f2xx_spi_ops = {
    .read = stm32f2xx_spi_read,
    .write = stm32f2xx_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_stm32f2xx_spi = {
    .name = TYPE_STM32F2XX_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(spi_cr1, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_cr2, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_sr, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_dr, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_crcpr, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_rxcrcr, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_txcrcr, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_i2scfgr, STM32F2XXSPIState),
        VMSTATE_UINT32(spi_i2spr, STM32F2XXSPIState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f2xx_spi_init(Object *obj)
{
    STM32F2XXSPIState *s = STM32F2XX_SPI(obj);
    DeviceState *dev = DEVICE(obj);

	printf("stm32f2xx_spi_init\n");
    memory_region_init_io(&s->mmio, obj, &stm32f2xx_spi_ops, s,
                          TYPE_STM32F2XX_SPI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->ssi = ssi_create_bus(dev, "ssi");
}

static void stm32f2xx_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_spi_reset;
    dc->vmsd = &vmstate_stm32f2xx_spi;
}

static const TypeInfo stm32f2xx_spi_info = {
    .name          = TYPE_STM32F2XX_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F2XXSPIState),
    .instance_init = stm32f2xx_spi_init,
    .class_init    = stm32f2xx_spi_class_init,
};

static void stm32f2xx_spi_register_types(void)
{
    type_register_static(&stm32f2xx_spi_info);
}

type_init(stm32f2xx_spi_register_types)
