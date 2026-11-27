### Added

- **A host test for the STM32H5 RNG bring-up when HSI48 never readies**
  (`test_h5_netif`, #599). With HSI48RDY never set, `tfw_h5_rng_init()` turns
  HSI48 on and gives up: the test checks that the RNG clock (RCC_AHB2ENR
  RNGEN) and the RNG (RNG_CR RNGEN) stay off and the netif stays down with no
  fallback key, and that the next retry, with HSI48 ready, enables both and
  brings the netif up.
