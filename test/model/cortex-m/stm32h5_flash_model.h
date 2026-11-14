/* stm32h5_flash_model.h: the host stand-in for ee_flash_port.c's
 * tfw_h5_ee_flash, the flash interface src/hal/cortex-m/stm32h5/ee_flash.c
 * writes through. It hands the real ee_flash.c and h5_flash.c the test's
 * register model. tfw-boot's tfw_cm_boot_flash_write is not defined here: the
 * application does not link it, and the suite links without it.
 *
 * Included by exactly one test, test/unit/test_stm32h5_eeprom.c, after that
 * test's flash model (its statics regs and MEM and the h5_flash declarations)
 * is in scope; it defines the function, so it is never included twice. It
 * lives under test/model/cortex-m/ because the name is a Cortex-M one the
 * arch-leak gate keeps out of generic code. */
#ifndef TFW_TEST_STM32H5_FLASH_MODEL_H
#define TFW_TEST_STM32H5_FLASH_MODEL_H

/* ee_flash_port.c's, on the model's registers. */
const struct tfw_h5_flash *tfw_h5_ee_flash(void)
{
    static const struct tfw_h5_flash flash = { regs, MEM };

    return &flash;
}

#endif /* TFW_TEST_STM32H5_FLASH_MODEL_H */
