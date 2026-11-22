### Fixed

- **The i.MX 95 SCMI mailbox stays inside its channel** (#640). A channel
  shorter than its 0x1C-byte header is refused before any access, and the
  payload room is whole words only, so neither a command's header nor a
  reply's last word is read or written past the channel's end.
