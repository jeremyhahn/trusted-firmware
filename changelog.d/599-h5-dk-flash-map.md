### Added

- **A host test for the STM32H573I-DK's flash map** (`test_board_stm32h573i-dk`,
  #599). It pins tfw-boot, the application, the platform block and the user
  EEPROM to their bases and sizes, and checks that each sits on whole 8 KiB
  sectors inside the part, clear of the others, with the platform block in
  bank 1's last sector and the user EEPROM at the end of bank 2. A larger
  application region that ran over the platform block now fails the test.
