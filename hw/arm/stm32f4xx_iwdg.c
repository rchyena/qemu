/*
 * TI stm32f4xx processors IWDG emulation.
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
#include "hw/misc/stm32f4xx_iwdg.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "sysemu/watchdog.h"

const char *iwdg_addrs[] = {
    "kr",
    "0x01",
    "0x02",
    "0x03",
    "pr",
    "0x05",
    "0x06",
    "0x07",
    "rlr",
    "0x09",
    "0xa",
    "0xb",
    "sr"
};


/**
 * @fn uint32_t tim_period(Stm32Iwdg *s)
 * @brief Calculate the equivalent time in nanoseconds
 * @param s pointer to struct Stm32Iwdg
 * @return time calculated
 * @remarks This function calculate the equivalent recharge time of IWDG in nanoseconds
 * The calculated time depends on the frequency LSI, prescaler value and
 * the reload Register RLR.
 * 
 * Next expiration is computed in ns based on new counter value and the timer frequency
 * (period). This might be computed as follows: period = (1 / TIMER_FREQ_MHZ) * 1000 * scale.
*/    
static uint32_t tim_period(STM32F4xxIWDGState *s)
{   
    printf("tim_period: prescaler %lu\n", s->prescaler);
    /* LSI frequency = 37~40kHz 
     * LSI frequency can range from 37kHz to 40kHz.
     * This frequency can be measured on the board, through the Timer10.
     * When the measurement is made, the value is near to 38KHz. 
     * However, with 40kHz, the watchdog timer accuracy is closer 
     * to the real value. 
     */
    uint32_t period = (1000000 * s->prescaler) / 40;
    printf("Multiplying period %lu by s->iwdg_rlr %lu to get period of %lu\n", period, s->iwdg_rlr, period * s->iwdg_rlr);
    // Extending period long enough (e.g. this value) prevents reset
    // Meaning reset may be happening because period isn't extended long enough for some reason
    //return 2158981120000;
    return ((period * s->iwdg_rlr)); // time in nanoseconds
}

/**
 * @fn uint32_t tim_next_transition(Stm32Iwdg *s, int64_t current_time)
 * @brief Calculate the equivalent time in nanoseconds
 * @param s pointer to struct Stm32Iwdg
 * @param current_time Current time of system
 * @return time calculated
 * @remarks This function return the equivalent recharge time of IWDG
 * to prevent a reset.
*/
static int64_t tim_next_transition(STM32F4xxIWDGState *s, int64_t current_time)
{   
    printf("tim_next_transition current_time %lld + period %lu\n", current_time, tim_period(s));
    return current_time + tim_period(s);
}


/**
 * @fn void iwdg_restart_timer(Stm32Iwdg *s)
 * @brief Restart watchdog timer to prevent reset
 * @param s pointer to struct Stm32Iwdg
 * @return none
 * @remarks This function is called when the watchdog has either been enabled
 * (hence it starts counting down) or has been keep-alived.
*/
static void iwdg_restart_timer(STM32F4xxIWDGState *d)
{
    printf("iwdg_restart_timer at time %lld\n", qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    if (!d->enabled)
        return;
    
    timer_mod(d->timer, tim_next_transition(d, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)));
    printf("Updating expiration time of timer to %lld\n",tim_next_transition(d, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)));
}

/**
 * @fn void iwdg_disable_timer(Stm32Iwdg *s)
 * @brief Disable watchdog timer
 * @param s pointer to struct Stm32Iwdg
 * @return none
 * @remarks This is called when the guest disables the watchdog.
*/
static void iwdg_disable_timer(STM32F4xxIWDGState *d)
{
    printf("iwdg_disable_timer\n");
    timer_del(d->timer);

}

/**
 * @fn void iwdg_timer_expired(void *vp)
 * @brief Reset function
 * @param vp pointer to void
 * @return none
 * @remarks This function is called when the watchdog expires.
*/
static void stm32f4xx_iwdg_reset(DeviceState *dev)
{
    STM32F4xxIWDGState *s = STM32F4XX_IWDG(dev);

    printf("stm32f4xx_iwdg_reset\n");
    iwdg_disable_timer(s);

    s->reboot_enabled = 0;
    s->enabled = 0;
    s->prescaler = 4;
    s->timer_reload = 0xfff;
    s->iwdg_rlr = 0xfff;
    s->unlock_state = 0;
}

/**
* @fn void iwdg_timer_expired(void *vp)
* @brief Reset function
* @param vp pointer to void
* @return none
* @remarks This function is called when the watchdog expires.
*/
static void iwdg_timer_expired(void *vp)
{
    printf("iwdg_timer_expired at time %lld\n", qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    STM32F4xxIWDGState *d = vp;
    
    if (d->reboot_enabled) {
        d->previous_reboot_flag = 1;
        /* Set bit indicating reset reason (IWDG) */
        // stm32_RCC_CSR_write((Stm32Rcc *)d->stm32_rcc, 1<<RCC_CSR_IWDGRSTF_BIT, 0);
        /* This reboots, exits, etc */
        //watchdog_perform_action();
        stm32f4xx_iwdg_reset((DeviceState *)d);
    }
}

static uint64_t stm32f4xx_iwdg_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    printf("stm32f2xx_iwdg_read: 0x%" HWADDR_PRIx " %s\n", addr, iwdg_addrs[addr]);
    struct STM32F4xxIWDGState *s = (struct STM32F4xxIWDGState *) opaque;

    switch (addr) {
    case STM_IWDG_KR: 
        return 0;

    case STM_IWDG_PR:
        // Here is where the counter might be updated?
        return s->iwdg_pr;

    case STM_IWDG_RLR:
        return s->iwdg_rlr;

    case STM_IWDG_SR:
        return 0;
    }

    // stm32f4xx_BAD_REG(addr);
    return 0;
}

static void stm32f4xx_iwdg_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    printf("stm32f2xx_iwdg_write: 0x%" HWADDR_PRIx " %s, Value: 0x%x\n", addr, iwdg_addrs[addr], value);
    struct STM32F4xxIWDGState *s = (struct STM32F4xxIWDGState *) opaque;
    printf("Entering: kr: %u, pr: %u, rlr: %u, sr: %u\n", s->iwdg_kr, s->iwdg_pr, s->iwdg_rlr, s->iwdg_sr);

    switch (addr) {
        case STM_IWDG_KR:
            s->iwdg_kr = value & 0xFFFF;
            if (s->iwdg_kr == 0xCCCC) {
                s->enabled = 1;
                s->reboot_enabled = 1;
                // Looks to be where watchdog counter is extended to tim_next_transition
                timer_mod(s->timer, tim_next_transition(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)));
                printf("Updating expiration time of timer to %lld\n",tim_next_transition(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)));
            } else if (s->iwdg_kr == 0xAAAA) { /* IWDG_RLR value is reloaded in the counter */
                printf("refresh triggered\n");
                s->timer_reload = s->iwdg_rlr;
                iwdg_restart_timer(s);
            } else if (s->iwdg_kr == 0x5555) { /* Enable write access to the IWDG_PR and IWDG_RLR registers */
                s->unlock_state = 1;
            }
            printf("Changed s->iwdg_kr to: %u\n", s->iwdg_kr);
            break;

        case STM_IWDG_PR:
            if (s->unlock_state == 1) {
                s->iwdg_pr = value & 0x07;
                s->prescaler = 4 << s->iwdg_pr;
            }
            printf("Changed s->iwdg_pr to: %u\n", s->iwdg_pr);
            break;

        case STM_IWDG_RLR:
            if (s->unlock_state == 1) {
                s->iwdg_rlr = value & 0x07FF;
            }
            printf("Changed s->iwdg_rlr to: %u\n", s->iwdg_rlr);
            break;

        case STM_IWDG_SR:
            break;

    }
}

/* *Some* sources say the memory region is 32-bit.  */
static const MemoryRegionOps stm32f4xx_iwdg_ops = {
    .read = stm32f4xx_iwdg_read,
    .write = stm32f4xx_iwdg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4, /* XXX actually 1 */
        .max_access_size = 4
    }
};

/* With this VMSD's introduction, version_id/minimum_version_id were
 * erroneously set to sizeof(Stm32Iwdg), causing a somewhat random
 * version_id to be set for every build. This eventually broke
 * migration.
 *
 * To correct this without breaking old->new migration for older
 * versions of QEMU, we've set version_id to a value high enough
 * to exceed all past values of sizeof(Stm32Iwdg) across various
 * build environments, and have reset minimum_version_id to 1,
 * since this VMSD has never changed and thus can accept all past
 * versions.
 *
 * For future changes we can treat these values as we normally would.
 */
static const VMStateDescription vmstate_stm32f4xx_iwdg = {
    .name = TYPE_STM32F4XX_IWDG,
    .version_id = 10000,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(reboot_enabled, STM32F4xxIWDGState),
        VMSTATE_INT32(enabled, STM32F4xxIWDGState),
        //VMSTATE_TIMER(timer, STM32F4xxIWDGState),
        VMSTATE_UINT32(timer_reload, STM32F4xxIWDGState),
        VMSTATE_INT32(unlock_state, STM32F4xxIWDGState),
        VMSTATE_INT32(previous_reboot_flag, STM32F4xxIWDGState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f4xx_iwdg_init(Object *obj)
{
    printf("stm32f4xx_iwdg_init\n");
    STM32F4xxIWDGState *s = STM32F4XX_IWDG(obj);
    // s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;
    // Seems to be where counter is linked to expire function (triggering reset)
    // Creates a nanosecond timer using qemu virtual clock that calls expired when it hits 0
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, iwdg_timer_expired, s);
    s->previous_reboot_flag = 0;

    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f4xx_iwdg_ops, s,
                          TYPE_STM32F4XX_IWDG, 0x400);
    sysbus_init_mmio(sbd, &s->mmio);
}

static WatchdogTimerModel model = {
    .wdt_name = TYPE_STM32F4XX_IWDG,
    .wdt_description = "Independent watchdog",
};

static Property stm32f4xx_iwdg_properties[] = {
    //DEFINE_PROP_PERIPH_T("periph", STM32F4xxIWDGState, periph, STM32_PERIPH_UNDEFINED),
    // DEFINE_PROP_PTR("stm32_rcc", STM32F4xxIWDGState, stm32_rcc_prop),
    DEFINE_PROP_END_OF_LIST(),
};


static void stm32f4xx_iwdg_class_init(ObjectClass *klass, void *data)
{
    printf("iwdg_class_init\n");
    DeviceClass *dc = DEVICE_CLASS(klass);
    //SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);    
    
    // sc->init = stm32f4xx_iwdg_init;
    dc->reset = stm32f4xx_iwdg_reset;
    dc->vmsd = &vmstate_stm32f4xx_iwdg;
    dc->props_ = stm32f4xx_iwdg_properties;
}

static const TypeInfo stm32f4xx_iwdg_info = {
    .name          = TYPE_STM32F4XX_IWDG,
    .parent        = TYPE_SYS_BUS_DEVICE, // TODO what is it
    .instance_size = sizeof(STM32F4xxIWDGState),
    .instance_init = stm32f4xx_iwdg_init,
    .class_init    = stm32f4xx_iwdg_class_init,
};

static void stm32f4xx_iwdg_register_types(void)
{
    watchdog_add_model(&model);
    type_register_static(&stm32f4xx_iwdg_info);
}

type_init(stm32f4xx_iwdg_register_types)