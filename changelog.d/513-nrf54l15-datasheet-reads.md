### Changed

- **The nrf54l15 datasheet reads** (#513): `docs/boards/nrf54l15-dk.md` gains "Datasheet reads (#513)", one row per item of the audit's slice 2. The UICR's address is settled from the pinned nrfx MDK; the KMU's slots, `BOOTCONF`, `RRAMC.REGION` and the CRACEN driver's curves are recorded from the MDK and sdk-nrf; the rest stays open because the nRF54L15 datasheet could not be retrieved (Nordic's hosts answer a Cloudflare challenge), and `docs/reference/README.md` records it as not pinned, with the sdk-nrf documents read and every file's SHA-256.
