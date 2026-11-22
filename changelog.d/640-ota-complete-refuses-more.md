### Fixed

- **A unit test pins that a completed OTA image takes no more writes** (#640). `test_a_completed_image_takes_no_more_data_and_no_second_end` in `test/unit/boot/test_boot_ota.c` finishes an image, then sends a chunk past its end, a chunk at offset 0 and a second `OTA_END`: each is refused `TFW_ERR_BUSY`, the state stays `COMPLETE` and no page is written over the image the bootloader is about to measure. Before it, a `tfw_ota_data` or `tfw_ota_end` guard that refused only IDLE and FAILED passed every boot test. `boot/boot_ota.c` does not change.
