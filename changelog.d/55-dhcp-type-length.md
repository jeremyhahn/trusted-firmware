### Added

- `test_a_message_type_of_any_length_but_one_is_refused` in `test/unit/test_dhcp_robust.c`: a DHCP reply whose message type (option 53) is any length but one, first or after a good one, in every phase, is refused whole, as `src/net/dhcp.c:417` refuses it (Refs #55).
