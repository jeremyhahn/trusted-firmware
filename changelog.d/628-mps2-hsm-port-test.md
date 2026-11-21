### Changed

- **A host test pins the mps2 tamper-response port** (#628). `make host-test`
  now builds src/hal/cortex-m/mps2/hsm_port.c unchanged and checks that power
  off and shutdown light LED 0 and the wipe LED 1, each keeping what is lit,
  that the other actions touch no register, that button 0 is the tamper pin
  and button 1 hot-plug, and that the key is the test envelopes' tenant key.
