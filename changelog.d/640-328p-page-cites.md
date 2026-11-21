### Fixed

- **The ATmega328P page cites the lines the build reads again** (#640).
  `docs/boards/atmega328p.md` pointed at `board_profile.h` lines from before
  the SUIT policy and script class ids landed, so the EEPROM cite named a blank
  line and the flash, boot, budget, scheduler, feature-block and pin cites were
  8 to 18 lines off. Every `board_profile.h:N` cite now names the define,
  assert or comment it stands for; no value on the page changed.
