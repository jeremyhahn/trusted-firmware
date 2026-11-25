# esp32p4-poe: provisioning (bootloader, partition table, `tfw_plat`)

Provisioning writes everything below the app: the second-stage bootloader
with layer 0 in it, the partition table, and the `tfw_plat` platform block,
then a sealed app on top. It is its own procedure and never
part of a feature PR's hardware evidence: an Ethernet or feature run flashes
the app alone onto a board already provisioned. The board page is
[`esp32p4-poe.md`](esp32p4-poe.md); the bench record with each dated run is
[`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md)
("HIL-0", step 3).

## When it is needed

- **A new board**, or one still on its factory image: the factory bootloader
  holds no layer 0 and the factory table is not this firmware's
  (`boards/esp32p4-poe/partitions.csv`).
- **A new image key**: the bootloader holds the public half of layer 0's
  image key and `tfw_plat` holds its pin, so a build with another key needs
  both rewritten before its app will boot.

Anything else is an app-only flash of `ota_0` (`0x20000`), which needs no
provisioning as long as the app is sealed with the key the board already
holds.

## The image key

`make esp32-build` generates two keys into `build/esp32/keys/`, each only if
the file is absent (`mk/arch/esp32-targets.mk`, `esp32-build`):

- `image_key_p256.pem`, layer 0's ES256 image key
  (`espsecure.py generate_signing_key --version 2 --scheme ecdsa256`).
  `l0_seal.py keyslot` compiles its public half into the bootloader and
  prints its pin, which `tfwprov --bake-eeprom --pubkey-hash` bakes into
  `tfw_plat.bin`; `l0_seal.py seal --image-key` signs the app's trailer v1
  with it.
- `secure_boot_signing_key.pem`, the RSA-3072 key the app is signed with
  (`CONFIG_SECURE_BOOT_SIGNING_KEY`), re-signed by `l0_seal.py seal
  --sign-key` after the trailer is added.

Neither is committed. Every ESP32 image in that build directory shares them,
and a fresh clone or a removed `build/esp32/keys/` gets new ones on its next
build. Keep a copy of `build/esp32/keys/` outside the tree for each
provisioned board, and put it back before building an app for that board.

Every later app-only flash must be sealed with the same image key. Layer 0
checks the trailer against the key compiled into the bootloader on the
board; an app sealed with any other key is refused (`Signature Check Failed`,
then `tfw-l0: app at 0x20000 refused by the trailer gate`, `no app passed
the gate` and a reset loop: the 2026-11-17 run of #539 in
[`esp32p4-poe.md`](esp32p4-poe.md), "Ethernet DHCP lease"). The cure is not a
bootloader rewrite in a feature run: it is this procedure, done once, with
the key then kept.

## What a build produces

From `make esp32-build BOARD=esp32p4-poe CRYPTO=none
ESP_SDKCONFIG_EXTRA=boards/esp32p4-poe/sdkconfig.hil0`, in
`build/esp32/esp32p4-poe-esp32p4-none/`:

| File | Written to | What it is |
|---|---|---|
| `idf/bootloader/bootloader.bin` | `0x2000` | the second-stage bootloader, layer 0 and the image key's slot in it |
| `idf/partition_table/partition-table.bin` | `0x10000` | `boards/esp32p4-poe/partitions.csv` (`CONFIG_PARTITION_TABLE_OFFSET`) |
| `tfw_plat.bin` | `0x11000` (`tfw_plat`) | the platform block: the image key's pin and platform-bake's neutral test serial unless `SERIAL=` is given (never a device's in this tree) |
| `trusted-firmware.bin` | `0x20000` (`ota_0`) | the app sealed with trailer v1 and the SUIT envelope and re-signed; `idf/trusted-firmware.bin` has no trailer and layer 0 refuses it |

The build's `sdkconfig` must have `CONFIG_SECURE_BOOT` and
`CONFIG_SECURE_FLASH_ENC_ENABLED` not set, which `sdkconfig.hil0` gives.

## The procedure

As hw-568f did it on 2026-11-18 (esptool v5.6.0, ESP-IDF v5.5.5), from the
build directory, `$ESP_PORT` being the board's USB serial port (check its USB
id first):

1. **Before.** `espefuse summary`, and a read of the flash about to be
   written (the first `0xA2000` bytes), both kept outside the tree:

   ```sh
   espefuse --chip esp32p4 --port $ESP_PORT summary > efuse-before.txt
   esptool --chip esp32p4 --port $ESP_PORT read-flash 0x0 0xA2000 flash-before.bin
   ```

2. **Erase the data sectors**, `tfw_id`, `tfw_eeprom`, `otadata` and
   `tfw_auth` (`0x12000` to `0x16FFF`), so `otadata` boots `ota_0` and no
   stale EEPROM or `OTA_AUTH` record is left:

   ```sh
   esptool --chip esp32p4 --port $ESP_PORT erase-region 0x12000 0x5000
   ```

3. **Write the bootloader, table, `tfw_plat` and app**, in that order, one
   command; esptool prints `Hash of data verified.` after each of the four:

   ```sh
   esptool --chip esp32p4 --port $ESP_PORT write-flash \
     0x2000  idf/bootloader/bootloader.bin \
     0x10000 idf/partition_table/partition-table.bin \
     0x11000 tfw_plat.bin \
     0x20000 trusted-firmware.bin
   ```

4. **After.** `espefuse summary` again; it must differ from the first in no
   line, since `esptool write-flash` touches no eFuse:

   ```sh
   espefuse --chip esp32p4 --port $ESP_PORT summary > efuse-after.txt
   ```

5. **Boot check.** One reset (EN through RTS, IO0 high; no serial break) and
   the console: `tfw-l0: app at 0x20000 verified, measurement …` and `boot:
   Loaded app from partition at offset 0x20000`.

The summaries and the flash read hold the part's MAC, chip id and key
blocks: they stay in the bench record outside the tree, and a document quotes
them only by hash or by "no line differs".

## Out of scope

- **eFuse burns** of any kind, and no `espefuse` command but `summary`.
- **Enabling secure boot v2 or flash encryption**: each burns eFuses
  (CA 583), and so does the UDS the `hmac-p256` tier needs.
- **`erase-flash`** (CA 583): only the region in step 2 is erased.
- **Owner-only steps**: the eFuse work above and any step the bench record
  leaves to the owner. Putting a board back on its factory image is the
  bench record's "Restore", not this procedure.
