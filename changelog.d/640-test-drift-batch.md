### Fixed

- **Test comments cite the code they pin** (#640). The simavr boot harness
  and ten unit suites named mk/avr.mk, which no longer exists, or source lines
  that have moved; they now cite mk/arch/avr.mk, mk/firmware.mk, mk/boot.mk,
  boards/atmega2560/profile.mk and the lines that hold the code on main.
  Comments only.
