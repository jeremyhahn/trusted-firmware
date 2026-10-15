/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The EEPROM capability and the platform block, on flash partitions.
 *
 * An ESP32 has no EEPROM. Both stores are data partitions the board's
 * partitions.csv names, read and written through esp_partition -- so flash
 * encryption, when the board turns it on, covers them with no code here: a
 * partition marked `encrypted` is decrypted by the read and encrypted by the
 * write, in ESP-IDF.
 *
 *   tfw_eeprom  the host-visible EEPROM (TFW_EEPROM_SIZE bytes of it)
 *   tfw_plat    the platform block (TFW_BOOT_EE_*), READ ONLY here; it is
 *               written once by the provisioning jig and never by this image,
 *               which is the AVR's rule for the same block for the same
 *               reason -- see include/tfw/hal.h at tfw_hal_nvm_read
 *
 * A write rewrites each 4 KB sector it touches: read, erase, write back. NOR
 * flash wears where an EEPROM does not, by roughly two orders of magnitude,
 * so a host that writes the EEPROM in a loop will wear this one out first.
 */

#include <string.h>
#include "esp_partition.h"

#include "tfw/hal.h"
#include "tfw/config.h"
#include "hal_esp32.h"

#define SECTOR 4096u

static uint8_t sector[SECTOR];

static const esp_partition_t *find(const char *label)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
}

tfw_status_t tfw_esp_part_read(const char *label, uint32_t off, uint8_t *buf, uint32_t len)
{
    const esp_partition_t *p = find(label);

    if (p == NULL) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (off > p->size || len > p->size - off) {
        return TFW_ERR_RANGE;
    }
    return esp_partition_read(p, off, buf, len) == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

tfw_status_t tfw_esp_part_write(const char *label, uint32_t off, const uint8_t *buf, uint32_t len)
{
    const esp_partition_t *p = find(label);

    if (p == NULL) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (off > p->size || len > p->size - off) {
        return TFW_ERR_RANGE;
    }
    while (len) {
        uint32_t base  = off - (off % SECTOR);
        uint32_t at    = off - base;
        uint32_t chunk = (SECTOR - at < len) ? SECTOR - at : len;

        if (esp_partition_read(p, base, sector, SECTOR) != ESP_OK) {
            return TFW_ERR_INTERNAL;
        }
        if (memcmp(&sector[at], buf, chunk) != 0) {
            memcpy(&sector[at], buf, chunk);
            if (esp_partition_erase_range(p, base, SECTOR) != ESP_OK ||
                esp_partition_write(p, base, sector, SECTOR) != ESP_OK) {
                return TFW_ERR_INTERNAL;
            }
        }
        off += chunk;
        buf += chunk;
        len -= chunk;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    return tfw_esp_part_read(TFW_ESP_PART_EEPROM, addr, buf, len);
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    return tfw_esp_part_write(TFW_ESP_PART_EEPROM, addr, buf, len);
}

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_esp_part_read(TFW_ESP_PART_PLATFORM, addr, buf, len);
}
