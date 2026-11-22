### Fixed

- **The mps2-an500 page states PWM's interrupt rate** (row `cortex-m`). It said the dual timer interrupts 256 times a period; the 256 steps are the duty's resolution, and `arm_next()` loads the distance to the next edge, so a period takes one interrupt per distinct running duty plus one, three at most with the two LEDs.
