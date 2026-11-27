### Fixed

- **The i.MX 95 M7 tone start drives a new tone pin low** (#474). Before the
  GPT runs, `tfw_hal_tone_start` writes a new (or retargeted) tone pin low,
  as the MPS2 tone does, so a pin left high starts with a full half period
  low and the first compare is a rise. A retune of the active pin keeps its
  level and the wave continues.
