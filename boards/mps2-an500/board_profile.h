/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Arm MPS2+ AN500 under QEMU (`-M mps2-an500`): a Cortex-M7 with no
 * TrustZone, the generic Cortex-M port's second CI target (row cortex-m,
 * docs/boards/mps2-an500.md).
 *
 * A test target, like mps2-an505. QEMU models the FPGA's two user LEDs and no
 * converter and no PWM, so the pins are the two LEDs and the shield 0 SPI
 * select, and every other capability is off by file selection. The two
 * buses, I2C over the shield 0 SBCon and SPI over its PL022, are linked only in
 * their rows (profile.mk).
 */
#ifndef TFW_BOARD_MPS2_AN500_H
#define TFW_BOARD_MPS2_AN500_H

#define TFW_BOARD_NAME "mps2-an500"
#define TFW_BOARD_ID   0x42u   /* docs/boards/board-id-registry.md */

/* The ONEWIRE=1 row adds GPIO block 0 as port 2 and its bit 0 as pin 3, the
 * 1-Wire line (board.c, src/hal/cortex-m/mps2/gpio_line.h). */
#if defined(TFW_CM_ONEWIRE)
#define TFW_PIN_COUNT  4u
#define TFW_PORT_COUNT 3u      /* the FPGA I/O block's LED and MISC, GPIO 0 */
#else
#define TFW_PIN_COUNT  3u
#define TFW_PORT_COUNT 2u      /* the FPGA I/O block's LED and MISC registers */
#endif
#define TFW_PWM_COUNT  0u
#define TFW_ADC_COUNT  0u
#define TFW_EEPROM_SIZE TFW_CM_PLATFORM_SIZE  /* the platform window: src/hal/cortex-m/hal_eeprom.c */

#define TFW_FEATURE_ANALOG  0
#define TFW_FEATURE_PWM     0
#define TFW_FEATURE_TONE    0
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  1
/* I2C over the shield 0 SBCon (src/hal/cortex-m/mps2/hal_bus.c) only in the
 * I2C=1 row (profile.mk): no element is on this board, and QEMU puts no device
 * on the bus, so the row's image is the soft root's with the bus linked. */
#if defined(TFW_CM_I2C)
#define TFW_FEATURE_I2C     1
#else
#define TFW_FEATURE_I2C     0
#endif
/* SPI over the shield 0 PL022, the same file, only in the
 * SPI=1 row (profile.mk): its select is pin 2 (board.c), the image's one
 * route to the shield device. */
#if defined(TFW_CM_SPI)
#define TFW_FEATURE_SPI     1
#else
#define TFW_FEATURE_SPI     0
#endif
/* 1-Wire bit-banged on GPIO block 0 bit 0 (hal_bus.c), only in the ONEWIRE=1
 * row: QEMU puts no device on the line. */
#if defined(TFW_CM_ONEWIRE)
#define TFW_FEATURE_ONEWIRE 1
#else
#define TFW_FEATURE_ONEWIRE 0
#endif

/* The soft root: the UDS in tfw-boot's region, the CDI handed over in SRAM. */
#define TFW_FEATURE_ATTEST 1

/* The system clock the FPGA image runs the core and the APB at: QEMU's
 * mps2.c SYSCLK_FRQ. SysTick, the UART divisor and the watchdog count it. */
#define TFW_CM_CPU_HZ 25000000uL

/* The MPS2 peripherals src/hal/cortex-m/mps2/ drives (QEMU hw/arm/mps2.c,
 * Arm AN500): UART0 raises its receive interrupt on external interrupt 0 and
 * its transmit interrupt on 1; the CMSDK APB watchdog's interrupt is the NMI. */
#define TFW_MPS2_UART0_BASE   0x40004000u
#define TFW_MPS2_UART0_IRQ_A  0
#define TFW_MPS2_UART0_IRQ_B  1

/* UART0's rings (src/hal/cortex-m/mps2/hal_uart.c), which asserts that the
 * receive ring holds a whole request at the longest payload: 70 bytes. QEMU's
 * CMSDK UART hands the guest the next byte as soon as it has read the last,
 * with no baud rate between them, so nothing bounds how far a frame can run
 * ahead of the main loop. At 64 the ring held 63, and every OTA_DATA and
 * OTA_AUTH request is 70. */
#define TFW_UART_RX_RING 128u
#define TFW_UART_TX_RING 64u
#define TFW_MPS2_FPGAIO_BASE  0x40028000u
#define TFW_MPS2_WDOG_BASE    0x40008000u

/* The shield 0 SBCon, the I2C bus src/hal/cortex-m/mps2/hal_bus.c clocks:
 * AN500 (Arm DAI 0500B), its memory map's 0x4002_9000; QEMU's mps2.c puts an
 * SBCon there and attaches nothing to its bus. Unused while TFW_FEATURE_I2C is
 * 0, outside the I2C=1 row. The SBCon has no select: no pin in board.c. */
#define TFW_MPS2_I2C_BASE     0x40029000u

/* The shield 0 SPI, a PL022 whose chip select is the FPGA I/O block's MISC bit
 * 8 (src/hal/cortex-m/mps2/pl022.h): AN500 (Arm DAI 0500B), Table 3-1,
 * 0x4002_6000. Unused while TFW_FEATURE_SPI is 0, outside the SPI=1 row.
 *
 * The select is pin 2 of board.c's table, port 1 (MISC) bit 8:
 * *Application Note AN500: ARM Cortex-M7 SMM on V2M-MPS2+* (Arm DAI 0500B),
 * 3.7 "SPI" and 3.13 Table 3-6, MISC bit 8, SHIELD0_SPI_nCS. The image does
 * not wire the PL022's own select to the shield. */
#define TFW_MPS2_SPI_BASE     0x40026000u

/* The CMSDK AHB GPIO block 0, on the shield header: AN500 (Arm DAI 0500B),
 * 0x4001_0000; QEMU's mps2.c maps it as an unimplemented device, which reads
 * 0. Its bit 0 is the ONEWIRE=1 row's line (src/hal/cortex-m/mps2/
 * gpio_line.h); unused outside that row. */
#define TFW_MPS2_GPIO0_BASE   0x40010000u

/* Flash geometry for tfw-boot, as addresses: TFW_FLASH_TOTAL is the end of
 * SSRAM1, the span both regions sit in (include/tfw/boot.h asserts they fit
 * below it and do not overlap). tfw-boot is at the base, where the core
 * fetches its vector table at reset; the application follows it. */
#define TFW_BOOT_REGION_BASE  0x00000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x00400000uL   /* the end of SSRAM1 */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 4 KiB of SSRAM1, which tfw-boot writes and
 * the application reads. */
#define TFW_CM_PLATFORM_BASE  0x003FF000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* Where QEMU loads the application and the platform block, and where the
 * image runs: one slot, or two by swap-move with factory copies QEMU puts
 * back on every reset, as boards/mps2-an505/board_profile.h explains.
 *
 *   0x00008000  factory application, 128 KiB   loaded, put back on reset
 *   0x00040000  primary slot, 128 KiB + 512    the image that runs
 *   0x00060200  secondary slot, 128 KiB        TFW_OTA_STAGE_DEST
 *   0x00200000  image store, 264 KiB           below
 *   0x003FE000  factory platform block, 4 KiB  loaded, put back on reset
 *   0x003FF000  platform block, 4 KiB          swap record, confirm request
 */
#if defined(TFW_CM_AB)
/* The primary, profile.mk's APP_BASE; the rest of the geometry is tfw-boot's
 * (boot/hal/cortex-m/boot_slots.h), as mps2-an505's. */
#define TFW_APP_REGION_BASE       0x00040000uL
#define TFW_SWAP_RECORD_ADDR      0x0E0u
#define TFW_BOOT_CONFIRM_ADDR     0x0EAu
#define TFW_BOOT_EE_EXTRA_BASE    TFW_SWAP_RECORD_ADDR
#define TFW_BOOT_EE_EXTRA_LEN     21u
#define TFW_APP_WRITE_END         TFW_BOOT_SLOT_END
#define TFW_OTA_STAGE_DEST        TFW_BOOT_SLOT_SECONDARY
#define TFW_CM_LOAD_APP_AT        0x00008000uL
#define TFW_CM_LOAD_PLATFORM_AT   0x003FE000uL
#define TFW_OTA_TRIAL_TIMEOUT_S   60u    /* as mps2-an505's */
#else
#define TFW_APP_REGION_BASE       0x00008000uL
#define TFW_CM_LOAD_APP_AT        TFW_APP_REGION_BASE
#define TFW_CM_LOAD_PLATFORM_AT   TFW_CM_PLATFORM_BASE
#endif

/* The image store (OTA_STORE := mem, src/store/mem.c), as on mps2-an505: a
 * span of SSRAM1 that no raw image covers, so QEMU's reset, which copies only
 * the loaded images back in (hw/core/loader.c, rom_reset), leaves it as the
 * platform block is left; the single-slot layout of
 * boards/mps2-an505/board_profile.h, 264 KiB at 2 MiB into SSRAM1. */
#define TFW_OTA_STORE_MEM_BASE    0x00200000uL
#define TFW_OTA_STORE_MEM_SIZE    0x00042000uL
#define TFW_OTA_STORE_STAGE_ADDR  0x000000ul
#define TFW_OTA_STORE_STAGE_LEN   0x020000ul  /* 128 KiB: TFW_APP_REGION_SIZE */
#define TFW_OTA_STORE_MANIFEST    0x020000ul
#define TFW_OTA_STORE_PREV_ADDR   0x021000ul
#define TFW_OTA_STORE_MARK_ADDR   0x041000ul

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_MPS2_AN500_H */
