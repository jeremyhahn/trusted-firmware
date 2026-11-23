### Changed

- **The wire protocol architecture puts the link under the transport**: `link_serial.c` or `link_eth.c` behind `include/tfw/link.h` touches the wire, binds a frame to the wire of its first byte, admits only the `tfw_link_eth_allows` commands over Ethernet and sends events on serial only.
