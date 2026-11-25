### Fixed

- **`tfw_ele_get_event`'s event bounds are pinned by tests** (#50). Two host
  tests now cover the i.MX 95 enclave's event poll: a fourteen-word response
  with room for ten events returns at most `TFW_ELE_EVENTS_MAX` (eight) even
  to a caller with room for sixteen, and a layout whose count word follows
  the events never returns the count word as an event. The test harness's
  reply buffer grows to `TFW_ELE_MSG_WORDS`. Test only; no code change.
