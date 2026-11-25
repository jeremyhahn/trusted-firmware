### Fixed

- **The link suite pins that a DHCPNAK to the first request keeps the link
  down and backs off** (#55).
  `test_a_nak_to_the_first_request_keeps_the_link_down_and_backs_off` in
  `test/unit/test_link_dhcp.c` answers the first DHCPREQUEST with a DHCPNAK:
  the socket is given back, nothing is dialled, nothing is asked again for at
  least 1 s, and the next attempt is a DHCPDISCOVER from no address. Before, a
  link that re-asked on the very next poll after a refused lease
  (`src/proto/link_eth.c:72` read as `if (st != TFW_OK) {`), hammering the
  server, or that dialled without a lease (`st == TFW_OK &&` dropped from
  `src/proto/link_eth.c:77`), left every link suite that runs the real DHCP
  client green.
