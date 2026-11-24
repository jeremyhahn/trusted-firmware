### Security

- **The ESP32 hmac-p256 tier's operator_bind tag wipe is pinned** (#23). A
  host test now fails if `tfw_crypto_verify_operator` leaves the tag it
  computed for a presented key in RAM, where it would bind that key.
