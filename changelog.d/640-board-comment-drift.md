### Fixed

- **Six board and driver comments now match the code beside them** (#640):
  `boards/atmega32u4/board_profile.h` names `src/hal/avr/avr_usart_regs.h` as
  where the USART register names are pasted, `src/crypto/optiga/optiga_crypto.c`
  names mps2-an505's `optiga:I2C=1` row beside the ATmega2560's,
  `boards/nrf54l15-dk/board_profile.h` and `boards/mps2-an500/board_profile.h`
  list the capabilities that are on instead of "every other capability is
  off", and `boards/mps2-an500/board.c` and `boards/mps2-an505/board.c` say
  the SPI select carries `TFW_PIN_DIGITAL` though no digital command drives it.
