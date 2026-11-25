### Changed

- An i.MX 95 S3 measurement's GET_INFO asks for the info buffer at its full 64-bit address and length, and an unanswered GET_INFO is a timeout that sends nothing more and leaves no stale info; tests pin it (#50).
