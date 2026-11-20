### Added

- **The i.MX 95 Cortex-M7 board page** (row `imx95`, #537):
  `docs/boards/frdm-imx95-m7.md` records owner ruling 771 (the A55 secure world
  is TF-A and OP-TEE from NXP's BSP, the M33 System Manager is NXP's, the
  EdgeLock Enclave is the hardware root, the M7 is this project's) and a
  feature matrix of every SPEC row: each row that applies is not run and
  queued after #535 and #536 (ruling 768), and each other row says why it is
  not this board's. The design and the board-id
  registry link it.
