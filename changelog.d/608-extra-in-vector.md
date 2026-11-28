### Fixed

- **The DICE dispatcher refuses a third or fourth in-vector** (#608). A
  psa_call carries up to four in-vectors and no DICE operation takes more
  than two: a message with a third or fourth now answers
  `TFW_ERR_BAD_ARGC` before the operation runs, and nothing is written,
  instead of being served with the extra vector unread.
