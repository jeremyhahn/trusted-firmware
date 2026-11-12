/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Arm MPS2+ AN505 under QEMU (`-M mps2-an505`): the generic Cortex-M port's
 * reference board (#161, docs/cortex-m/adr-reference-part-and-emulation.md).
 *
 * A test target, like esp32-qemu. QEMU models the FPGA's two user LEDs and no
 * switch, no converter and no PWM, so the pins are the two LEDs and the shield
 * 0 SPI select, and every other capability is off by file selection. The two
 * buses, I2C over the shield 0 SBCon and SPI over its PL022, are linked only in
 * their rows (profile.mk).
 *
 * The board id is PROVISIONAL (boards/mps2-an505/profile.mk).
 */
#ifndef TFW_BOARD_MPS2_AN505_H
#define TFW_BOARD_MPS2_AN505_H

#define TFW_BOARD_NAME "mps2-an505"
#define TFW_BOARD_ID   0x40u   /* docs/boards/board-id-registry.md */

/* The policy-set and script class ids, UUIDv5(TFW_SUIT_VENDOR_ID,
 * "mps2-an505/policy") and (..., "mps2-an505/script")
 * (docs/hsm-self-defence/adr-response-policy-format.md D2): the image links
 * the tamper-response engine (profile.mk HSM).
 * b46b3e2f-0849-56df-9556-e94d0d85c91f, 6847ff2a-0362-5538-a231-fc5af3c40c4b. */
#define TFW_SUIT_POLICY_CLASS_ID { 0xb4u, 0x6bu, 0x3eu, 0x2fu, 0x08u, 0x49u, 0x56u, 0xdfu, \
                                   0x95u, 0x56u, 0xe9u, 0x4du, 0x0du, 0x85u, 0xc9u, 0x1fu }
#define TFW_SUIT_SCRIPT_CLASS_ID { 0x68u, 0x47u, 0xffu, 0x2au, 0x03u, 0x62u, 0x55u, 0x38u, \
                                   0xa2u, 0x31u, 0xfcu, 0x5au, 0xf3u, 0xc4u, 0x0cu, 0x4bu }

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
#define TFW_EEPROM_SIZE 0u

#define TFW_FEATURE_ANALOG  0
#define TFW_FEATURE_PWM     0
#define TFW_FEATURE_TONE    0
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  0
/* I2C over the shield 0 SBCon (src/hal/cortex-m/mps2/hal_bus.c) only in the
 * I2C=1 row (profile.mk), the element tier's measurement: the image's default
 * has no device on the bus to talk to. */
#if defined(TFW_CM_I2C)
#define TFW_FEATURE_I2C     1
#else
#define TFW_FEATURE_I2C     0
#endif
/* SPI over the shield 0 PL022, the same file, only in the SPI=1 row: its
 * select is pin 2 (board.c), the image's one route to the shield device. */
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

/* The soft root: the UDS in tfw-boot's region, the CDI handed over in SRAM,
 * HMAC evidence (src/crypto/soft/). */
#define TFW_FEATURE_ATTEST 1

/* The system clock the FPGA image runs the core and the APB at: QEMU's
 * an505 sysclk_frq and apb_periph_frq. SysTick and the UART divisor count it. */
#define TFW_CM_CPU_HZ 20000000uL

/* The MPS2 peripherals src/hal/cortex-m/mps2/ drives, at their Secure aliases
 * (the core is Secure and the SAU is off): QEMU hw/arm/mps2-tz.c and
 * hw/arm/armsse.c. UART0's two lines are external interrupts 32 and 33; the
 * watchdog is the IoT Kit's Secure one. */
#define TFW_MPS2_UART0_BASE   0x40200000u
#define TFW_MPS2_UART0_IRQ_A  32
#define TFW_MPS2_UART0_IRQ_B  33

/* UART0's rings (src/hal/cortex-m/mps2/hal_uart.c), which asserts that the
 * receive ring holds a whole request at the longest payload: 70 bytes. QEMU's
 * CMSDK UART hands the guest the next byte as soon as it has read the last,
 * with no baud rate between them, so nothing bounds how far a frame can run
 * ahead of the main loop. At 64 the ring held 63, and every OTA_DATA and
 * OTA_AUTH request is 70. */
#define TFW_UART_RX_RING 128u
#define TFW_UART_TX_RING 64u
#define TFW_MPS2_FPGAIO_BASE  0x40302000u
#define TFW_MPS2_WDOG_BASE    0x50081000u

/* The shield 0 SBCon, the I2C bus on the shield header's EXP[5] (SCL) and
 * EXP[15] (SDA): AN505 (Arm DAI 0505B), its memory map's first alias of
 * 0x4020_C000 and 0x5020_C000, the alias UART0 is reached at above; QEMU's
 * mps2-tz.c puts its i2c2 there. The registers are DUI 0224I's (soc_regs.h). */
#define TFW_MPS2_I2C_BASE     0x4020C000u

/* The shield 0 SPI, a PL022 whose chip select is the FPGA I/O block's MISC bit
 * 8 (src/hal/cortex-m/mps2/pl022.h): AN505 (Arm DAI 0505B), Table 3-7, the
 * first alias of 0x4020_A000 and 0x5020_A000, as for the SBCon above. Unused
 * while TFW_FEATURE_SPI is 0, outside the SPI=1 row.
 *
 * The select is pin 2 of board.c's table, port 1 (MISC) bit 8:
 * *Application Note AN505: Example IoT Subsystem design for V2M-MPS2+* (Arm
 * DAI 0505B), 4.6 "SPI" and 4.14 Table 4-5, MISC bit 8, SHIELD0_SPI_nCS. The
 * image does not wire the PL022's own select to the shield. */
#define TFW_MPS2_SPI_BASE     0x4020A000u

/* The CMSDK AHB GPIO block 0, on the shield header: AN505 (Arm DAI 0505B),
 * the first alias of 0x4010_0000 and 0x5010_0000; QEMU's mps2-tz.c maps it as
 * an unimplemented device, which reads 0. Its bit 0 is the ONEWIRE=1 row's
 * line (src/hal/cortex-m/mps2/gpio_line.h); unused outside that row. */
#define TFW_MPS2_GPIO0_BASE   0x40100000u

/* Flash geometry for tfw-boot, as addresses: on this part the regions are not
 * offsets from zero, so TFW_FLASH_TOTAL is the end of the address span both
 * sit in (include/tfw/boot.h asserts they fit below it and do not overlap).
 * The application region is the code budget, FLASH_BUDGET in profile.mk. */
#define TFW_BOOT_REGION_BASE  0x10000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x38400000uL   /* the end of SSRAM-2 */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 4 KiB of the code bank, which tfw-boot
 * writes and the application reads. */
#define TFW_CM_PLATFORM_BASE  0x383FF000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* Where QEMU loads the application and the platform block
 * (test/integration/cortex-m/qemu_wire_test.sh, from cm_image's addrs.env),
 * and where the image runs. QEMU copies every loaded image back in on each
 * system reset (hw/core/loader.c, rom_reset).
 *
 * One slot (BOOT_AB=0, profile.mk): the application slot at the bank's base
 * and the platform block are loaded where they run, so a reset puts both
 * back as they were loaded.
 *
 * Two slots (BOOT_AB=1, mk/boot.mk's A/B rows): swap-move as on the 2560
 * (ADR decision 7, boot/boot_swap.c), both slots and the platform block in
 * memory no image is loaded into, so a swap, a trial and its confirm survive
 * the reset. QEMU loads the application and the platform block at their
 * factory copies instead, and tfw-boot copies each in once while its home is
 * blank, as a programmer would (boot/hal/cortex-m/boot_factory.c):
 *
 *   0x38000000  factory application, 128 KiB   loaded, put back on reset
 *   0x38040000  primary slot, 128 KiB + 512    the image that runs
 *   0x38060200  secondary slot, 128 KiB        TFW_OTA_STAGE_DEST
 *   0x38200000  image store, 264 KiB           below
 *   0x383FE000  factory platform block, 4 KiB  loaded, put back on reset
 *   0x383FF000  platform block, 4 KiB          swap record, confirm request
 */
#if defined(TFW_CM_AB)
/* The primary slot, profile.mk's APP_BASE: the application links there. The
 * secondary, the move unit and the write end are tfw-boot's, derived from it
 * (boot/hal/cortex-m/boot_slots.h); the two below name them, so only a unit
 * that includes that header expands them, and every tfw-boot unit that does. */
#define TFW_APP_REGION_BASE       0x38040000uL
/* The 2560's offsets in the platform window (boards/atmega2560/
 * board_profile.h): the swap record, then OTA_CONFIRM's request, past the
 * platform block and refused to the application's EEPROM_WRITE as it is. */
#define TFW_SWAP_RECORD_ADDR      0x0E0u
#define TFW_BOOT_CONFIRM_ADDR     0x0EAu
#define TFW_BOOT_EE_EXTRA_BASE    TFW_SWAP_RECORD_ADDR
#define TFW_BOOT_EE_EXTRA_LEN     21u
#define TFW_APP_WRITE_END         TFW_BOOT_SLOT_END
#define TFW_OTA_STAGE_DEST        TFW_BOOT_SLOT_SECONDARY
#define TFW_CM_LOAD_APP_AT        0x38000000uL
#define TFW_CM_LOAD_PLATFORM_AT   0x383FE000uL
/* The one trial's deadline (decision 9, src/core/ota_trial.c), 60 s as on
 * every board with a trial: a boot, the connection, one attestation and the
 * confirm. tfw-boot's 8 s watchdog only catches a hang inside it. */
#define TFW_OTA_TRIAL_TIMEOUT_S   60u
#else
#define TFW_APP_REGION_BASE       0x38000000uL
#define TFW_CM_LOAD_APP_AT        TFW_APP_REGION_BASE
#define TFW_CM_LOAD_PLATFORM_AT   TFW_CM_PLATFORM_BASE
#endif

/* The image store (OTA_STORE := mem, src/store/mem.c): a span of the same code
 * bank that no ELF segment covers, so QEMU's reset, which copies only the
 * loaded images back in (hw/core/loader.c, rom_reset), leaves it as the
 * platform block is left. Its addresses are offsets from the base, laid out as
 * a single-slot NOR store is (the 328P's, boards/atmega328p/board_profile.h):
 * STAGE holds an application region, PREV the running image OTA_BEGIN backs
 * up, and the MARK sector is last. 264 KiB at 2 MiB into the bank: clear of
 * the application slot below and of the platform block above. */
#define TFW_OTA_STORE_MEM_BASE    0x38200000uL
#define TFW_OTA_STORE_MEM_SIZE    0x00042000uL
#define TFW_OTA_STORE_STAGE_ADDR  0x000000ul
#define TFW_OTA_STORE_STAGE_LEN   0x020000ul  /* 128 KiB: TFW_APP_REGION_SIZE */
#define TFW_OTA_STORE_MANIFEST    0x020000ul
#define TFW_OTA_STORE_PREV_ADDR   0x021000ul
#define TFW_OTA_STORE_MARK_ADDR   0x041000ul

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_MPS2_AN505_H */
