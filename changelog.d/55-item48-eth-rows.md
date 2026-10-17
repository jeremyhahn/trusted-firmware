### Changed

- **`ETH=1` is the ATmega2560's default** (#55, owner ruling item 48). A Mega
  built with no `ETH=` is the Ethernet image. The shieldless bench Mega is a
  named `ETH=0` row on both tiers, and `make firmware-matrix` builds and
  size-checks every board's extra rows (`BOARD_ROWS` in `profile.mk`). The boot
  harness's serial legs and `make bench-check` name `ETH=0`.
- **The ATmega328P soft-crypto Ethernet node is a supported row** (#55):
  `CRYPTO=soft ETH=1 OTA_STAGE=0`. It links no stager, so its update path is
  serial: one update path per image, and the 328P's boot cannot take an image
  from the external store.
- **The Ethernet allow-list's update commands are `0x68`–`0x6B`** (#55). The
  OTA ADR's proposed `0x61`, `0x62` and `0x6C`–`0x6E` stay on serial, and a
  host test pins the refusal. The firmware already admitted only these four.
  No status code or command id changed.
- **The ATmega2560's RAM gate is 2 048 bytes, a quarter of SRAM** (#55), the
  same fraction as its flash budget. It was 1 024, the 328P's figure in absolute
  terms, and the `ETH=1` element image measured 1 027 (`.bss` 946, `.noinit` 1,
  the handoff block 80). No static could be freed without a change of
  behaviour. The gate still fails the build on creep, and no other board's
  budget changed.

### Fixed

- **The SUIT gate's constants live in flash** (#55). `src/core/suit.c`'s
  `es256`, vendor id and class id had no `TFW_PROGMEM`, and the zero class id
  was a RAM array too. In the 2560's `ETH=1` element image this freed 0 bytes,
  measured: its `.data` is empty, so the constants were not in the app's RAM.
  The move does not bring that image under its RAM budget.
