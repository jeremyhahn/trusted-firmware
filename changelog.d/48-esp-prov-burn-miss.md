### Added

- test/unit/test_esp_prov_uds_mutate.c pins the ESP32 UDS burn's post-burn find check: when the confirming `esp_efuse_find_purpose` misses, `tfw_prov_uds_burn` refuses with TFW_ERR_CRYPTO even if the miss leaves the burned block's number behind, so a burn the part cannot confirm never reads as done (Refs #48).
