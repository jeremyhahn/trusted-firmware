### Fixed

- **The Cortex-M and Ethernet pages say what the code does** (#640). The
  Cortex-M user guide's QEMU run boots four machines, not three, and on an
  image with the tamper-response engine six `hsm-*` cases besides; the
  Ethernet API page names `tfw_link_eth_allows` in `src/proto/link_eth.h` as
  the network allow-list, which `tfw_link_admits` applies.
