### Added

- **Host tests for the STM32H5 console UART's error and re-init paths**
  (`test_stm32h5_uart`, #599). An overrun with no byte beside it queues
  nothing, a TXE the driver did not enable sends nothing and keeps the queued
  byte, a re-init takes the new rate, empties both rings and drops TXEIE, and
  the pin setup keeps the mode and alternate function of the bank's other
  pins.
