### Fixed

- **The AVR USART init refuses a baud it cannot run** (#640). `tfw_hal_uart_init` in `src/hal/avr/hal_uart.c` returned `TFW_OK` for a rate whose divisor does not fit UBRRn's 12 bits (300 baud programmed 0x1A09, which the part reads as 0x0A09, about 778 baud), wrapped the divisor to 0xFFFF above F_CPU/8 and divided by zero at baud 0. It now returns `TFW_ERR_RANGE` for these, as every Cortex-M HAL does, and writes no register; at 16 MHz the accepted range is 489 to 2000000 baud.
