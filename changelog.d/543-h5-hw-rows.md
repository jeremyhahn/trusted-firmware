### Changed

- **`make hw-test BOARD=stm32h573i-dk`** (row `stm32h5`, #543). The board has
  a row list (`boardImages`) and pin facts (`peerBoards`) from #535's bench
  run: LD1 for the GPIO rows, PWM on D6, D3 (the tone pin) as the pin with no
  compare unit, A0 as the analog pin, and the I2C, SPI and 1-Wire refusals on
  the default image. The PWM rows take each board's PWM pin and skip the
  tick-pin refusal where the tick is SysTick. DTR does not reset the board
  through the ST-LINK VCOM, so `hw-test` says the rows run without a reset
  between them.
