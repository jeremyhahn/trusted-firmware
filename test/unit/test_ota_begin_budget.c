/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The per-boot OTA_BEGIN budget (#232, the peer-binding ADR's decision 1 B)
 * as a board that overrides it builds: mk/host.mk compiles this suite with
 * -DTFW_OTA_BEGIN_BUDGET=2u, as a boards/<board>/board_profile.h would set it,
 * and tfw/config.h's default of 4 must give way to it.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "tfw/protocol.h"

void setUp(void)    { tfw_nor_model_reset(); tfw_stage_init(); }
void tearDown(void) { TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations()); }

static tfw_status_t begin(uint32_t total)
{
    uint8_t      in[4], out[8];
    tfw_reader_t r;
    tfw_writer_t w;

    in[0] = (uint8_t)total; in[1] = (uint8_t)(total >> 8);
    in[2] = (uint8_t)(total >> 16); in[3] = (uint8_t)(total >> 24);
    tfw_reader_init(&r, in, 4u);
    tfw_writer_init(&w, out, sizeof out);
    return tfw_stage_cmd_begin(&r, &w);
}

static void test_a_board_override_of_the_budget_is_honoured(void)
{
    uint32_t erases;

    TEST_ASSERT_EQUAL_UINT(2u, TFW_OTA_BEGIN_BUDGET);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    erases = tfw_nor_model_erases();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(512u));
    TEST_ASSERT_EQUAL_UINT32(erases, tfw_nor_model_erases());
    tfw_stage_init();
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_board_override_of_the_budget_is_honoured);
    return UNITY_END();
}
