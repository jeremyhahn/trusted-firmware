### Changed

- **Cortex-M requirement audit** (row `cortex-m`, #458): `docs/cortex-m/engineering.md` maps each requirement to its code and test, and lists six remaining slices.
- **cppcheck names each run** (#458): `make lint` prints `cppcheck <hal>:<board> clean` per run, so the log shows the Cortex-M HAL is analysed.
