### Added

- **A host test sweeps the i.MX 95 GPT tone arithmetic** (#473).
  test_imx95_gpt_half checks tfw_imx95_gpt_half at 65535 Hz (OCR1 182), at
  37 Hz where the half period is not a whole count (OCR1 324323, rounded
  down), at the one-count edge, at a 0xFFFFFFFF Hz clock (no prescaler), and
  that every tone from 1 to 65535 Hz at 24 MHz gives PR 0 and OCR1 + 1 equal
  to 24000000 / (2 * hz).
