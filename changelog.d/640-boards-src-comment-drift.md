### Fixed

- **Board header and profile.mk comments match the code** (#640): the
  STM32H573I-DK, Pico 2 W, FRDM-MCXN947 and mps2-an505 ids are no longer called
  provisional (the board-id registry records them), mps2-an505 is CI's first
  Cortex-M target rather than its only one, the STM32H573I-DK's tfw-boot writes
  go through h5_flash.c, the FRDM-IMX95's header names its four RGPIO pins and
  the capabilities it turns on, and its profile.mk loses a repeated F_CPU comment.
