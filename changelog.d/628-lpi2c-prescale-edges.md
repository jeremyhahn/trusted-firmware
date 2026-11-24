### Added

- **The FRDM-MCXN947's I2C test covers the two edges of the timing search** (#628): when two prescales give the same error the smaller one is kept, and a CLKLO + CLKHI of exactly 120 - SCL_LATENCY is accepted while one past it moves to the next prescale.
