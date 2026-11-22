### Fixed

- **docs/boards/atmega2560.md agrees with the board files again** (#640). `make firmware-matrix` builds nine on this board, not six: the six tier rows plus the two OPTIGA Trust M measurement rows and the DHCP measurement (`profile.mk` `BOARD_ROWS`); the shipped boot rows name the soft tier's (`atmega2560:soft:soft:p256-soft:0:0`) beside the element tier's three; and every `board_profile.h`, `profile.mk`, `board.c`, `mk/boot.mk` and board-test line cite points at the line that now holds it. No value on the page changes.
