### Fixed

- **The capability handlers' comments match the code** (#627): src/caps cites mk/firmware.mk, not a mk/avr.mk that does not exist, gives the EEPROM boundary as 0x140, the ALIAS page as 1..TFW_ALIAS_PAGE bytes and a signed document as 365 to 492 bytes, and names the callers and boards the code actually has.
