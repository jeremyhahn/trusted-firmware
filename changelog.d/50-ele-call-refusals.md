### Added

- test(imx95): the ELE MU client's own refusals are pinned by host tests: a
  request header of no words, a response shorter than two words or longer
  than the client takes, and an MU with no transmit register each return
  their error with nothing sent (`test/unit/test_imx95_ele_mu.c`) (Refs #50).
