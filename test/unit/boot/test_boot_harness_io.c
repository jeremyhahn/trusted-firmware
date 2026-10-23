/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What the simavr harness listens on, read from the board profile it is built
 * with (test/integration/simavr/boot_harness_io.h). Built once under each
 * profile with a store (mk/boot.mk), each build given the part's own answer
 * from its data sheet's pin mapping and register summary, so a profile and the
 * harness cannot drift from the pin the board is wired to or the USART it
 * speaks on.
 */
#include "unity.h"
#include "boot_harness_io.h"

void setUp(void) {}
void tearDown(void) {}

/* D8, the store's select: the port letter simavr names the port by. */
static void test_the_store_select_is_the_profiles_port(void)
{
    TEST_ASSERT_EQUAL_CHAR(EXPECT_STORE_CS_PORT, STORE_CS_PORT);
}

static void test_the_store_select_is_the_profiles_bit(void)
{
    TEST_ASSERT_EQUAL_UINT(EXPECT_STORE_CS_BIT, STORE_CS_BIT);
}

/* The USART the application speaks on, as simavr names it. */
static void test_the_uart_is_the_one_the_application_speaks_on(void)
{
    TEST_ASSERT_EQUAL_CHAR(EXPECT_UART, HARNESS_UART);
}

/* UCSRnA, the register the harness paces each byte on, at the part's address. */
static void test_the_uart_status_register_is_the_parts(void)
{
    TEST_ASSERT_EQUAL_HEX16(EXPECT_UCSRA, HARNESS_UCSRA);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_store_select_is_the_profiles_port);
    RUN_TEST(test_the_store_select_is_the_profiles_bit);
    RUN_TEST(test_the_uart_is_the_one_the_application_speaks_on);
    RUN_TEST(test_the_uart_status_register_is_the_parts);
    return UNITY_END();
}
