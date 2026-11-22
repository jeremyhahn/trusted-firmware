### Fixed

- **The HSM self-defence docs drift batch** (#640). `docs/hsm-self-defence/`
  now says what main does: slice 3 runs the engine on the mps2-an505 QEMU
  image, the channel carries `0x7C`, lockdown sends A3, the port has four
  functions and three implementations, and the two ADRs cite `suit.h`'s
  lines, the A6 store's status returns, the ATECC data helpers and the
  model's two counters.
