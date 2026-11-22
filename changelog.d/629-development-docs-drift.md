### Fixed

- **docs/development matches main** (#629). The ISP route says `make boot-flash` refuses an erase that would take the Mega's EEPROM (`BOOT_EEPROM_RESTORE=1` or EESAVE) and that the programmer flags are `ARCH_ISP`'s, and cites `mk/` by name instead of line numbers main no longer has; the Mega page's secure-boot link resolves; the bench inventory records the STM32H573I-DK run of 2026-11-17 (#535).
