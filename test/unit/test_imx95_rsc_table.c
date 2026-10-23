/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7 image's remoteproc resource table, checked byte by byte on
 * x86 against the file the M7 image links (src/hal/cortex-m/imx95/rsc_table.c).
 * x86 is little-endian, as the M7 is, so the object's bytes here are the
 * image's bytes.
 *
 * The expected bytes are the format's, written out by hand from the Linux
 * kernel's "Binary Firmware Structure" (docs/reference/README.md): struct
 * resource_table is u32 ver, u32 num, u32 reserved[2], then u32 offset[num],
 * packed. A computed vector would move with the code it pins.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "rsc_table.h"

void setUp(void) {}
void tearDown(void) {}

/* ver 1, num 0, reserved 0 and 0; no offset array, as num is 0. */
static const uint8_t expected[16] = {
    0x01u, 0x00u, 0x00u, 0x00u,     /* ver */
    0x00u, 0x00u, 0x00u, 0x00u,     /* num */
    0x00u, 0x00u, 0x00u, 0x00u,     /* reserved[0] */
    0x00u, 0x00u, 0x00u, 0x00u,     /* reserved[1] */
};

static void test_the_table_is_the_formats_bytes(void)
{
    uint8_t got[sizeof expected];

    TEST_ASSERT_EQUAL_UINT(sizeof expected, sizeof tfw_imx95_rsc_table);
    memcpy(got, &tfw_imx95_rsc_table, sizeof got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, got, sizeof expected);
}

static void test_the_header_fields_sit_at_the_formats_offsets(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, offsetof(struct tfw_imx95_rsc_table, ver));
    TEST_ASSERT_EQUAL_UINT(4u, offsetof(struct tfw_imx95_rsc_table, num));
    TEST_ASSERT_EQUAL_UINT(8u, offsetof(struct tfw_imx95_rsc_table, reserved));
}

/* The kernel's loader takes the table only if it holds the header and num
 * offsets: 16 + 4 * num bytes. */
static void test_the_table_holds_its_header_and_offsets(void)
{
    TEST_ASSERT_TRUE(sizeof tfw_imx95_rsc_table >=
                     16u + 4u * (size_t)tfw_imx95_rsc_table.num);
}

static void test_the_table_is_word_aligned(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, (uintptr_t)&tfw_imx95_rsc_table & 3u);
}

/* ---- the table as Linux's loader reads it
 *
 * The checks drivers/remoteproc/remoteproc_elf_loader.c
 * (rproc_elf_find_rsc_table) and remoteproc_core.c (rproc_handle_resources
 * and the carveout, devmem, trace and vdev handlers) make, in their order,
 * on the bytes alone. Each refusal has its own code so a mutation that
 * fails for the wrong reason fails the test. Entry sizes are the packed
 * structs of include/linux/remoteproc.h: struct fw_rsc_hdr is u32 type;
 * fw_rsc_carveout and fw_rsc_devmem are five u32 and char name[32] (52
 * bytes), fw_rsc_trace three u32 and the name (44), fw_rsc_vdev five u32,
 * u8 status, u8 num_of_vrings and u8 reserved[2] (24), each
 * fw_rsc_vdev_vring five u32 (20). */

enum rsc_verdict {
    RSC_OK,
    RSC_TOO_SMALL,          /* "Image is too small" */
    RSC_BAD_VERSION,        /* "unsupported fw ver" */
    RSC_RESERVED_SET,       /* "non zero reserved bytes" */
    RSC_INCOMPLETE,         /* "resource table incomplete" */
    RSC_TRUNCATED,          /* "rsc table is truncated" */
    RSC_ENTRY_TRUNCATED,    /* "<type> rsc is truncated" */
    RSC_VDEV_RESERVED_SET,  /* "vdev rsc has non zero reserved bytes" */
};

#define RSC_CARVEOUT 0u
#define RSC_DEVMEM   1u
#define RSC_TRACE    2u
#define RSC_VDEV     3u

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static enum rsc_verdict linux_parse(const uint8_t *t, size_t size)
{
    uint32_t num;
    uint32_t i;

    if (size < 16u) {
        return RSC_TOO_SMALL;
    }
    if (le32(t) != 1u) {
        return RSC_BAD_VERSION;
    }
    if (le32(t + 8) != 0u || le32(t + 12) != 0u) {
        return RSC_RESERVED_SET;
    }
    num = le32(t + 4);
    if (16u + 4u * (uint64_t)num > size) {
        return RSC_INCOMPLETE;
    }
    for (i = 0u; i < num; i++) {
        uint32_t       off = le32(t + 16u + 4u * i);
        const uint8_t *rsc;
        uint64_t       avail;

        if ((uint64_t)off + 4u > size) {
            return RSC_TRUNCATED;
        }
        avail = size - off - 4u;
        rsc   = t + off + 4u;
        switch (le32(t + off)) {
        case RSC_CARVEOUT:
        case RSC_DEVMEM:
            if (avail < 52u) {
                return RSC_ENTRY_TRUNCATED;
            }
            break;
        case RSC_TRACE:
            if (avail < 44u) {
                return RSC_ENTRY_TRUNCATED;
            }
            break;
        case RSC_VDEV:
            if (avail < 24u ||
                24u + 20u * (uint64_t)rsc[21] + le32(rsc + 16) > avail) {
                return RSC_ENTRY_TRUNCATED;
            }
            if (rsc[22] != 0u || rsc[23] != 0u) {
                return RSC_VDEV_RESERVED_SET;
            }
            break;
        default:
            break;          /* the kernel warns and skips an unknown type */
        }
    }
    return RSC_OK;
}

static size_t image_table(uint8_t *buf, size_t cap)
{
    TEST_ASSERT_TRUE(cap >= sizeof tfw_imx95_rsc_table);
    memcpy(buf, &tfw_imx95_rsc_table, sizeof tfw_imx95_rsc_table);
    return sizeof tfw_imx95_rsc_table;
}

static void test_linux_takes_the_images_table(void)
{
    uint8_t buf[64];
    size_t  n = image_table(buf, sizeof buf);

    TEST_ASSERT_EQUAL(RSC_OK, linux_parse(buf, n));
}

/* The image declares no carveout and no vdev (rsc_table.c says why), so the
 * parser is shown a table that has both, built by hand, to prove it reads
 * entries at all; a parser that took anything would pass the test above. */
static void test_the_parser_reads_a_carveout_and_a_vdev(void)
{
    uint8_t t[16 + 8 + 56 + 24 + 4 + 40];
    size_t  vdev = 16u + 8u + 56u;

    memset(t, 0, sizeof t);
    put32(t, 1u);
    put32(t + 4, 2u);
    put32(t + 16, 24u);                     /* carveout */
    put32(t + 20, (uint32_t)vdev);          /* vdev */
    put32(t + 24, RSC_CARVEOUT);
    put32(t + vdev, RSC_VDEV);
    t[vdev + 4u + 21u] = 2u;                /* two vrings */
    TEST_ASSERT_EQUAL(RSC_OK, linux_parse(t, sizeof t));

    t[vdev + 4u + 21u] = 3u;                /* a vring past the end */
    TEST_ASSERT_EQUAL(RSC_ENTRY_TRUNCATED, linux_parse(t, sizeof t));
    t[vdev + 4u + 21u] = 2u;
    t[vdev + 4u + 22u] = 1u;
    TEST_ASSERT_EQUAL(RSC_VDEV_RESERVED_SET, linux_parse(t, sizeof t));
    t[vdev + 4u + 22u] = 0u;
    TEST_ASSERT_EQUAL(RSC_ENTRY_TRUNCATED, linux_parse(t, 24u + 4u + 51u));
}

/* One field of the image's table corrupted at a time: each is refused, and
 * for the reason that field gives. */
static void test_a_corrupted_field_is_refused(void)
{
    uint8_t buf[64];
    size_t  n;

    n = image_table(buf, sizeof buf);
    put32(buf, 2u);
    TEST_ASSERT_EQUAL(RSC_BAD_VERSION, linux_parse(buf, n));

    n = image_table(buf, sizeof buf);
    put32(buf, 0u);
    TEST_ASSERT_EQUAL(RSC_BAD_VERSION, linux_parse(buf, n));

    n = image_table(buf, sizeof buf);
    buf[8] = 0x01u;
    TEST_ASSERT_EQUAL(RSC_RESERVED_SET, linux_parse(buf, n));

    n = image_table(buf, sizeof buf);
    buf[15] = 0x80u;
    TEST_ASSERT_EQUAL(RSC_RESERVED_SET, linux_parse(buf, n));

    n = image_table(buf, sizeof buf);
    put32(buf + 4, 1u);                     /* an entry with no offset word */
    TEST_ASSERT_EQUAL(RSC_INCOMPLETE, linux_parse(buf, n));

    n = image_table(buf, sizeof buf);
    put32(buf + 4, 0x40000000u);            /* 16 + 4 * num past 2^32 */
    TEST_ASSERT_EQUAL(RSC_INCOMPLETE, linux_parse(buf, n));

    n = image_table(buf, sizeof buf);
    put32(buf + 4, 1u);
    put32(buf + 16, 0xFFFFFFF0u);           /* an offset past the table */
    TEST_ASSERT_EQUAL(RSC_TRUNCATED, linux_parse(buf, n + 4u));

    n = image_table(buf, sizeof buf);
    TEST_ASSERT_EQUAL(RSC_TOO_SMALL, linux_parse(buf, n - 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_table_is_the_formats_bytes);
    RUN_TEST(test_the_header_fields_sit_at_the_formats_offsets);
    RUN_TEST(test_the_table_holds_its_header_and_offsets);
    RUN_TEST(test_the_table_is_word_aligned);
    RUN_TEST(test_linux_takes_the_images_table);
    RUN_TEST(test_the_parser_reads_a_carveout_and_a_vdev);
    RUN_TEST(test_a_corrupted_field_is_refused);
    return UNITY_END();
}
