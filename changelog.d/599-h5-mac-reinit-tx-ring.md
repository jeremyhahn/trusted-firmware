### Added

- **A host test for the STM32H5 MAC init over a used TX ring**
  (`test_h5_eth_mac`, #599). The netif inits the MAC again at every link-up
  over the same rings: the test sends two frames, marks every TX descriptor
  word 0xA5A5A5A5 (OWN set), inits again and checks that every TX descriptor
  is zeroed, the TX tail pointer is back at `tx[0]`, and the next frame goes
  out on `tx[0]`.
