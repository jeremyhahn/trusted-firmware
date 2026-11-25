### Added

- **A test that a peer which keeps reading holds tfwsim's Ethernet link through
  a full socket buffer** (row `ethernet-transport`). `test_eth_socket_mutate`
  sends 64 KB through 4 KB socket buffers to a forked reader that drains 64
  bytes at a time, and checks that every byte arrives in order and the link
  stays up, so a send stall that drops the link the moment the buffer fills no
  longer passes.
