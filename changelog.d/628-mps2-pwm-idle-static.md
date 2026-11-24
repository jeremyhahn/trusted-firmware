### Fixed

- **The MPS2 PWM tests for duty 0 and 255 on an idle timer can fail** (row `cortex-m`). They checked only `Timer1Control == 0` and no NVIC enable, which a stop of the idle timer also leaves; they now also check no `Timer1IntClr` write, no NVIC disable and, for duty 0, no `Timer1Load` write, so a `ch_static` that stops a timer with no channel running fails both.
