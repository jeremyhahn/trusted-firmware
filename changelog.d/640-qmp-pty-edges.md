### Added

- **scripts/test_qmp_pty_edges.py pins the edges of the Cortex-M wire rows'
  qmp_pty.py** (#640): a QMP error with no desc prints the whole error, a
  chardev with no filename fails as not a pty, a monitor that hangs up before
  its greeting and one that greets and then goes silent fail naming what was
  awaited, an event before the greeting is passed over, and the first of two
  chardevs with one label wins.
