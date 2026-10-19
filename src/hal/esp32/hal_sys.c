/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Lifecycle, time and the system queries, on ESP-IDF.
 *
 * ESP-IDF owns the reset vector, the C runtime, the clocks, the watchdogs and
 * FreeRTOS. By the time anything here runs, its startup has brought the part
 * up and called app_main() from the main task. So app_main() below is the
 * whole of the bridge: it lowers its own task to the idle priority and calls
 * the firmware's main(), the same superloop every other architecture runs.
 *
 * WHY THE IDLE PRIORITY. The superloop never blocks and must never sleep --
 * there is no delay() in this firmware. At the idle priority every ESP-IDF
 * task (lwIP, the Ethernet driver, esp_timer) preempts it the moment it has
 * work, and FreeRTOS time-slices it against the idle task, so the task
 * watchdog that watches the idle task is still fed. Nothing in core learns
 * that an RTOS exists.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_private/system_internal.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "tfw/hal.h"
#include "tfw/config.h"
#include "hal_esp32.h"
#include "../../core/handoff.h"

int main(void);

void app_main(void)
{
    vTaskPrioritySet(NULL, tskIDLE_PRIORITY);
    (void)main();
}

/* A board's own bring-up -- holding a co-processor in reset, say -- happens
 * here, first, before any capability can drive a pin. */
tfw_status_t tfw_hal_init(void)
{
    return tfw_esp_board_init();
}

/*
 * ESP-IDF's watchdogs are its own and were armed by its startup, so there is
 * nothing to disarm here; the cause is read from the reset-reason latch the
 * ROM keeps, which no earlier code has cleared. The bootloader's handoff, when
 * there is one, carries the same cause as the bootloader saw it, and is
 * preferred for the same reason the AVR prefers it: it is the first observer.
 *
 * The one exception is a handed-off software reset (#48). The ROM, which is all
 * the bootloader can read, ends a requested restart, a panic and a task or
 * interrupt watchdog alike in a software reset; ESP-IDF's own record of why it
 * restarted, which esp_reset_reason() reads, tells them apart, so a handed-off
 * TFW_RESET_SOFTWARE is refined by it. Reasons with no value of their own,
 * such as a deep-sleep wake, report TFW_RESET_UNKNOWN rather than a cause they
 * are not.
 */
tfw_reset_cause_t tfw_hal_reset_cause(void)
{
    uint8_t handed_off;
    tfw_reset_cause_t refined;

    if (!tfw_boot_handoff_reset_cause(&handed_off)) {
        return tfw_esp_reset_decode(esp_reset_reason());
    }
    if (handed_off != (uint8_t)TFW_RESET_SOFTWARE) {
        return (tfw_reset_cause_t)handed_off;
    }
    refined = tfw_esp_reset_decode(esp_reset_reason());
    return refined == TFW_RESET_UNKNOWN ? TFW_RESET_SOFTWARE : refined;
}

tfw_reset_cause_t tfw_esp_reset_decode(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:  return TFW_RESET_POWER_ON;
    case ESP_RST_EXT:      return TFW_RESET_EXTERNAL;
    case ESP_RST_BROWNOUT: return TFW_RESET_BROWNOUT;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return TFW_RESET_WATCHDOG;
    case ESP_RST_SW:       return TFW_RESET_SOFTWARE;
    case ESP_RST_PANIC:    return TFW_RESET_PANIC;
    default:               return TFW_RESET_UNKNOWN;
    }
}

/* A reset leaves nothing of the old boot behind. On the ESP32, esp_restart()
 * resets only the CPUs (SW_CPU_RESET): the peripherals keep running through
 * it, and a timer alarm that fires on the way down is still pending when the
 * next boot enables that interrupt. So the ESP32 takes the system reset
 * (SW_RESET) that ESP-IDF uses for its own panics, which clears the digital
 * peripherals too. On the later chips esp_restart() resets the peripherals
 * itself, and that system-reset routine does not exist. */
void tfw_hal_reset(void)
{
#if CONFIG_IDF_TARGET_ESP32
    esp_restart_noos_dig();
#else
    esp_restart();
#endif
}

/* FreeRTOS enabled interrupts before app_main() was called; the ISRs this HAL
 * relies on are ESP-IDF's own and are already live. */
void tfw_hal_irq_enable(void)
{
}

/* esp_timer runs from boot; there is no tick of ours to start. */
tfw_status_t tfw_hal_tick_init(void)
{
    return TFW_OK;
}

/* esp_timer is a 64-bit microsecond counter, read atomically by ESP-IDF. The
 * millisecond count wraps at 2^32 exactly as the AVR's does. */
tfw_ms_t tfw_hal_millis(void)
{
    return (tfw_ms_t)(esp_timer_get_time() / 1000);
}

/* For bus bit-banging only -- 1-Wire slots -- as on every architecture. */
void tfw_hal_delay_us(uint16_t us)
{
    esp_rom_delay_us(us);
}

/* ESP-IDF allocates (lwIP, the Ethernet driver), so the figure a host can
 * watch fall is the free internal heap, not stack headroom. */
uint32_t tfw_hal_free_ram(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

/* No-ops: ESP-IDF's task and interrupt watchdogs are its own, armed by its
 * startup, and a trial here is its bootloader's (CONFIG_BOOTLOADER_APP_ROLLBACK_
 * ENABLE): a trial that never confirms falls back at the next reset, which the
 * trial deadline in src/core/ota_trial.c asks for. */
void tfw_hal_watchdog_arm(void)
{
}

void tfw_hal_watchdog_pet(void)
{
}
