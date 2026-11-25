### Fixed

- **A directory under a simulator's name is reported as not built** (#629).
  `simproc.Locate` took any path that `os.Stat` found, so `TFWSIM_DIR`
  pointed at `tools/` returned the source directory `tools/tfwsim` as the
  binary and `Start` failed with exec's bare "permission denied". It now
  returns the `NotBuiltError` that names the directory and says how to build
  the simulator.
