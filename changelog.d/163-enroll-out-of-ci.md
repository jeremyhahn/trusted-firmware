### Changed

- **CI no longer enrolls tfwsim through `TestHardwareEnrollment`** (#163
  decision 2). `make xkms-test` deselects it by name (`go test -skip`) and
  still runs every other `xkms/` suite, `xkms/e2e`'s other tests included. It
  no longer builds `tfwsim` or `tfwsim-atecc608a`, since nothing it runs starts
  a simulator. `make hw-enroll` is unchanged: a board only, `DEVICE` required.
  Run by hand with no board named, the test still enrolls the simulator.
