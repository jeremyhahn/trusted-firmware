/* stm32h5_flash_model.h: the host stand-in for tfw-boot's boot_flash.c call
 * tfw_cm_boot_flash_write, which src/hal/cortex-m/stm32h5/hal_eeprom.c
 * reaches. It drives the real h5_flash.c on the test's register model.
 *
 * Included by exactly one test, test/unit/test_stm32h5_eeprom.c, after that
 * test's flash model (its statics regs and MEM and the h5_flash declarations)
 * is in scope; it defines the function, so it is never included twice. It
 * lives under test/model/cortex-m/ because the name is a Cortex-M one the
 * arch-leak gate keeps out of generic code. */
#ifndef TFW_TEST_STM32H5_FLASH_MODEL_H
#define TFW_TEST_STM32H5_FLASH_MODEL_H

/* boot_flash.c's, on the model's registers. */
tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    static const struct tfw_h5_flash flash = { regs, MEM };

    if (addr < TFW_H5_FLASH_MEM) {
        return TFW_ERR_RANGE;
    }
    return tfw_h5_flash_update(&flash, addr - TFW_H5_FLASH_MEM, data, len);
}

#endif /* TFW_TEST_STM32H5_FLASH_MODEL_H */
