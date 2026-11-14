### Added

- **FRDM-IMX95-PRO bench record** (row `imx95`,
  `docs/development/bench/frdm-imx95.md`). The board's state before any load,
  the frdm-imx95-m7 image build, and the M7 console port found. Every RAM-only
  route to load our M7 image is closed on the board's System Manager
  configuration (mSel 0): Linux remoteproc only attaches, TRDC blocks the A55
  from the M7 TCM, and the SM monitor neither reaches memory nor sets the M7
  vector. Loading our image needs a boot-container change, an owner decision.
