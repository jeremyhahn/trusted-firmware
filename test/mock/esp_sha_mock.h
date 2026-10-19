#ifndef TFW_ESP_SHA_MOCK_H
#define TFW_ESP_SHA_MOCK_H

/* ESP-IDF's bootloader SHA-256 and the classic ESP32's ROM SHA on a host
 * (esp_sha_mock.c), over src/crypto/sha256_small.c, each holding its caller
 * to the contract of the silicon it stands in for. */

/* Forget every call. With words_only, bootloader_sha256_data is the classic
 * ESP32's word loop: a length or an address that is not whole words is a
 * fault. Without it, it is the C3's and the P4's ROM, which takes any. */
void tfw_esp_sha_mock_reset(int words_only);

/* Calls that broke the contract since the reset: bootloader_sha256_data
 * given what the target refuses, or a ROM call before ets_sha_enable, of a
 * type other than SHA-256, of a length not whole bytes, from an address not
 * whole words, or after a call that was not whole blocks. */
unsigned tfw_esp_sha_mock_faults(void);

/* Calls into the ROM's ets_sha_update, and digests bootloader_sha256_finish
 * gave, since the reset. */
unsigned tfw_esp_sha_mock_rom_updates(void);
unsigned tfw_esp_sha_mock_boot_digests(void);

#endif
