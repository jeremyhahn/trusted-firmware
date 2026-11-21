/* PWM_WRITE on the FRDM-MCXN947 through its command handler (src/caps/pwm.c)
 * and the SoC's own HAL files, hal_gpio.c and hal_pwm.c, unchanged. The SoC's
 * register addresses are backed by host memory mapped at those addresses, so
 * every register the HAL writes is a word the test reads back.
 *
 * The handler makes the pin a GPIO output before each write, which sets its
 * PCR MUX to 0; the write must hand it back to the timer every time (#371). */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "unity.h"
#include "reader.h"
#include "pwm.h"
#include "soc_regs.h"
#include "board_profile.h"
#include "ctimer_pwm.h"

#define PERIPH_BASE 0x50000000u
#define PERIPH_SIZE 0x00200000u

#define RED_LED 0u      /* P0_10, CTIMER0 match 0 */
#define RED_PORT 0u
#define RED_BIT 10u
#define GREEN_LED 1u    /* P0_27, no compare unit */
#define GREEN_BIT 27u

static volatile uint32_t *ct0(void)
{
    return (volatile uint32_t *)(uintptr_t)TFW_MCXN_CTIMER_BASE(0u);
}

static uint32_t pcr_mux(void)
{
    return (TFW_MCXN_PORT_PCR(RED_PORT, RED_BIT) >> TFW_MCXN_PCR_MUX_SHIFT) & 0xFu;
}

static tfw_status_t pwm_write(uint8_t pin, uint8_t duty)
{
    const uint8_t args[2] = { pin, duty };
    tfw_reader_t r;

    tfw_reader_init(&r, args, (uint8_t)sizeof args);
    return tfw_pwm_cmd_write(&r, 0);
}

void setUp(void) {}
void tearDown(void) {}

/* Duty 128 then 64: the second write returned OK with the pin left on GPIO. */
static void test_every_nonzero_write_leaves_the_pin_on_the_timer(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 128u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_PWM_PIN_FN, pcr_mux());
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 64u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_PWM_PIN_FN, pcr_mux());
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 64u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_PWM_PIN_FN, pcr_mux());
}

/* Zero gives the pin back to GPIO; the next nonzero write takes it again. */
static void test_zero_returns_the_pin_to_gpio_and_a_write_takes_it_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 200u));
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, pcr_mux());
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 10u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_PWM_PIN_FN, pcr_mux());
}

/* Zero halts CTIMER0 (counter held in reset, no PWM channel); the next nonzero
 * write must start it again, or the pin is on a timer that never counts. */
static void test_zero_halts_the_timer_and_the_next_write_starts_it_again(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 128u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_CT_TCR_CEN, ct0()[TFW_MCXN_CT_TCR]);
    TEST_ASSERT_BITS_HIGH(1u, ct0()[TFW_MCXN_CT_PWMC]);

    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 0u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_CT_TCR_CRST, ct0()[TFW_MCXN_CT_TCR]);
    TEST_ASSERT_EQUAL_UINT32(0u, ct0()[TFW_MCXN_CT_PWMC]);

    TEST_ASSERT_EQUAL(TFW_OK, pwm_write(RED_LED, 50u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_CT_TCR_CEN, ct0()[TFW_MCXN_CT_TCR]);
    TEST_ASSERT_BITS_HIGH(1u, ct0()[TFW_MCXN_CT_PWMC]);
    TEST_ASSERT_EQUAL_UINT32(mcxn_ctimer_pwm_match(50u), ct0()[TFW_MCXN_CT_MR(0u)]);
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_PWM_PIN_FN, pcr_mux());
}

/* A pin with no compare unit, or past the table, is refused before the
 * handler touches its PCR. */
static void test_a_pin_without_a_compare_unit_is_refused_and_left_alone(void)
{
    TFW_MCXN_PORT_PCR(0u, GREEN_BIT) = 0xA5u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, pwm_write(GREEN_LED, 128u));
    TEST_ASSERT_EQUAL_UINT32(0xA5u, TFW_MCXN_PORT_PCR(0u, GREEN_BIT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, pwm_write(TFW_PIN_COUNT, 128u));
    TEST_ASSERT_EQUAL_UINT32(0xA5u, TFW_MCXN_PORT_PCR(0u, GREEN_BIT));
}

int main(void)
{
    void *regs = mmap((void *)(uintptr_t)PERIPH_BASE, PERIPH_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (regs != (void *)(uintptr_t)PERIPH_BASE) {
        return 1;   /* the peripheral window is not free in this process */
    }
    memset(regs, 0, PERIPH_SIZE);
    UNITY_BEGIN();
    RUN_TEST(test_every_nonzero_write_leaves_the_pin_on_the_timer);
    RUN_TEST(test_zero_returns_the_pin_to_gpio_and_a_write_takes_it_back);
    RUN_TEST(test_zero_halts_the_timer_and_the_next_write_starts_it_again);
    RUN_TEST(test_a_pin_without_a_compare_unit_is_refused_and_left_alone);
    return UNITY_END();
}
