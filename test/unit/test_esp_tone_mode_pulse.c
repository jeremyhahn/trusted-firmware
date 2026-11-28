/* SPDX-License-Identifier: Apache-2.0 */
/*
 * #561: TONE, then MODE on the same pin, then PULSE_CAPTURE. MODE's
 * gpio_reset_pin takes the pin off the LEDC channel, so the tone hal_tone.c
 * tracked has ended and the capture must not loop back a pin that is now an
 * input. hal_tone.c, hal_gpio.c and hal_pulse_rmt.c linked together over
 * stand-in gpio and LEDC drivers and the mock RMT driver.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_rmt_mock.h"

#include "hal_esp32.h"

#define SYM(l0, d0, l1, d1) \
    { { .duration0 = (d0), .level0 = (l0), .duration1 = (d1), .level1 = (l1) } }

static const tfw_pinref_t pin   = { 21u };
static const tfw_pinref_t other = { 22u };
/* GPIO20, ADC1 channel 4 on the P4: a pin MODE and an analog read share. */
static const tfw_pinref_t adc_pin = { 20u };
/* The profile's SPI MISO pad here (mk/host.mk), and a CS for the transfer. */
static const tfw_pinref_t miso_pin = { 33u };
static const tfw_pinref_t spi_cs   = { 35u };
static uint16_t iv[4];
static uint8_t  n;
static unsigned stops;

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    (void)gpio_num;
    (void)mode;
    return ESP_OK;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    (void)gpio_num;
    (void)pull;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return 0;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
    (void)timer_conf;
    return ESP_OK;
}

/* The fake LEDC: a configured channel toggles its pin until a stop that
 * works. A stop can fail (stop_err), or report success and keep toggling
 * (stop_sticks): the two ways NOTONE can leave the output running. */
static uint8_t   toggling;
static esp_err_t stop_err;
static uint8_t   stop_sticks;

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    (void)ledc_conf;
    toggling = 1u;
    return ESP_OK;
}

esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level)
{
    (void)speed_mode;
    (void)channel;
    (void)idle_level;
    stops++;
    if (stop_err == ESP_OK && !stop_sticks) {
        toggling = 0u;
    }
    return stop_err;
}

/* hal_pwm.c's, which this test does not link: no pin is PWM-driven here. */
int tfw_esp_pwm_drives(int g)
{
    (void)g;
    return 0;
}

void tfw_esp_pwm_release(int g)
{
    (void)g;
}

/* The oneshot ADC: every call succeeds, so a read configures its pad. */
struct adc_oneshot_unit_ctx_t { int unused; };
static struct adc_oneshot_unit_ctx_t the_unit;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    (void)init_config;
    *ret_unit = &the_unit;
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config)
{
    (void)handle;
    (void)channel;
    (void)config;
    return ESP_OK;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw)
{
    (void)handle;
    (void)chan;
    *out_raw = 0;
    return ESP_OK;
}

/* hal_bus.c's I2C and SPI master drivers, all succeeding: the SPI bus init
 * takes the MISO pad, which the capture after it must see (#48). */
static int bus_handle;

void tfw_hal_delay_us(uint16_t us)
{
    (void)us;
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus_handle)
{
    (void)bus_config;
    *ret_bus_handle = (i2c_master_bus_handle_t)&bus_handle;
    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle_)
{
    (void)bus_handle_;
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle_, uint16_t address,
                           int xfer_timeout_ms)
{
    (void)bus_handle_;
    (void)address;
    (void)xfer_timeout_ms;
    return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle_,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle)
{
    (void)bus_handle_;
    (void)dev_config;
    *ret_handle = (i2c_master_dev_handle_t)&bus_handle;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                      const uint8_t *write_buffer, size_t write_size,
                                      uint8_t *read_buffer, size_t read_size,
                                      int xfer_timeout_ms)
{
    (void)i2c_dev;
    (void)write_buffer;
    (void)write_size;
    (void)read_buffer;
    (void)read_size;
    (void)xfer_timeout_ms;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms)
{
    (void)i2c_dev;
    (void)write_buffer;
    (void)write_size;
    (void)xfer_timeout_ms;
    return ESP_OK;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev, uint8_t *read_buffer,
                             size_t read_size, int xfer_timeout_ms)
{
    (void)i2c_dev;
    (void)read_buffer;
    (void)read_size;
    (void)xfer_timeout_ms;
    return ESP_OK;
}

esp_err_t spi_bus_initialize(spi_host_device_t host_id, const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_chan)
{
    (void)host_id;
    (void)bus_config;
    (void)dma_chan;
    return ESP_OK;
}

esp_err_t spi_bus_add_device(spi_host_device_t host_id,
                             const spi_device_interface_config_t *dev_config,
                             spi_device_handle_t *handle)
{
    (void)host_id;
    (void)dev_config;
    *handle = (spi_device_handle_t)&bus_handle;
    return ESP_OK;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc)
{
    (void)handle;
    (void)trans_desc;
    return ESP_OK;
}

void setUp(void)
{
    stop_err    = ESP_OK;
    stop_sticks = 0u;
    tfw_esp_tone_release(tfw_esp_tone_gpio());
    (void)tfw_hal_tone_stop();
    /* No test inherits a pin MODE left an output (#48). */
    (void)tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT);
    (void)tfw_hal_gpio_mode(&other, TFW_PIN_INPUT);
    (void)tfw_hal_gpio_mode(&adc_pin, TFW_PIN_INPUT);
    (void)tfw_hal_gpio_mode(&miso_pin, TFW_PIN_INPUT);
    mock_rmt_reset();
    stops    = 0u;
    toggling = 0u;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.misuse);
    TEST_ASSERT_EQUAL_UINT(mock_rmt.created, mock_rmt.deleted);
}

static void capture_loop_back(unsigned want)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 80, 0, 0) };

    mock_rmt_reset();
    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(want, mock_rmt.cfg.flags.io_loop_back);
}

/* The Codex P1 on #561: MODE input after TONE leaves no tone behind. */
static void test_mode_input_after_tone_ends_the_loop_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    capture_loop_back(1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    capture_loop_back(0u);
}

/* An RX channel without loopback makes the pad input-only, so a capture on
 * a MODE OUTPUT pin must loop it back or its writes never reach it (#48). */
static void test_a_mode_output_pin_is_looped_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_OUTPUT));
    capture_loop_back(1u);
}

static void test_a_mode_input_pin_is_not_looped_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT_PULLUP));
    capture_loop_back(0u);
}

/* MODE on another pin leaves the tone, and its loop back, as they were. */
static void test_mode_on_another_pin_keeps_the_tone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&other, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, stops);
    capture_loop_back(1u);
}

/* A refused MODE touches nothing, the tone included. */
static void test_a_refused_mode_keeps_the_tone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_gpio_mode(&pin, (tfw_pin_mode_t)0x7F));
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, stops);
}

/* #561: a stopped tone still owns its pin; MODE takes it back with no second stop. */
static void test_mode_input_after_notone_takes_the_pin_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    capture_loop_back(0u);
}

/* #561: MODE on another pin after NOTONE leaves the stopped tone's pin owned. */
static void test_mode_on_another_pin_after_notone_keeps_the_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&other, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    capture_loop_back(1u);
}

/* #48: MODE's tone release past the plain running-tone case. The release
 * after NOTONE is test_mode_input_after_notone_takes_the_pin_back (it kills
 * `if (running && owned_gpio == gpio)`; an inner `if (1)` is equivalent, as
 * tfw_hal_tone_stop checks `running` itself). The two below:
 * - a tone moved to another pin is released only on the pin it now owns:
 *   kills a release that drops the `owned_gpio == gpio` compare (MODE on the
 *   old pin would stop the tone) or keys it on the first pin the tone took;
 * - MODE over a stop the driver refuses still takes the pin back, and the
 *   channel stays running so NOTONE retries the stop: kills a release that
 *   keeps owned_gpio on a failed stop (the loop back on an input pin, #561)
 *   and a NOTONE that marks the channel stopped when ledc_stop failed. */
static void test_a_tone_moved_to_another_pin_is_released_only_there(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&other, 1000u));
    TEST_ASSERT_EQUAL_INT(22, tfw_esp_tone_gpio());
    capture_loop_back(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(22, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, stops);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&other, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
}

/* MODE OUTPUT on 21, a tone on 21, then the tone moved to 22: the move
 * resets 21 to an input, so a capture there must not loop it back and drive
 * it again (#48, Codex P1). */
static void test_a_mode_output_pin_a_moved_tone_resets_is_not_looped_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&other, 1000u));
    TEST_ASSERT_EQUAL_INT(22, tfw_esp_tone_gpio());
    capture_loop_back(0u);
}

/* MODE OUTPUT on GPIO20, then an analog read on ADC1 channel 4, its pad: the
 * ADC takes the pad for analog input, so a capture there must be input-only
 * and not drive the sensor it measures (#48, Codex P1). */
static void test_a_mode_output_pin_an_adc_read_takes_is_not_looped_back(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 80, 0, 0) };
    uint16_t raw = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&adc_pin, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(4u, &raw));

    mock_rmt_reset();
    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&adc_pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.cfg.flags.io_loop_back);
}

static void test_a_mode_output_pin_the_spi_bus_takes_for_miso_is_not_looped_back(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 80, 0, 0) };
    uint8_t w[2] = { 0x5au, 0xa5u };
    uint8_t r[2] = { 0u, 0u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&miso_pin, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&spi_cs, w, r, 2u));

    mock_rmt_reset();
    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&miso_pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.cfg.flags.io_loop_back);
}

static void test_mode_over_a_refused_stop_releases_the_pin_and_a_retry_stops_the_tone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    stop_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    capture_loop_back(0u);

    stop_err = ESP_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(2u, stops);
    TEST_ASSERT_EQUAL_UINT(0u, toggling);
}

/* The edges a capture after NOTONE reads on the pin: the LEDC waveform when
 * the channel still toggles and the capture kept the pad's output
 * (io_loop_back); none otherwise, since an input-only RMT pad disables the
 * output -- the measurement would silence the pin itself (Codex P1, #561). */
static uint8_t edges_after_notone(void)
{
    static const rmt_symbol_word_t wave[] = {
        SYM(1, 500, 0, 500), SYM(1, 500, 0, 0),
    };

    mock_rmt_reset();
    mock_rmt_chunk(wave, 2u, 1u);
    n = 0u;
    (void)tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n);
    TEST_ASSERT_EQUAL_UINT(1u, mock_rmt.cfg.flags.io_loop_back);
    return (toggling && mock_rmt.cfg.flags.io_loop_back) ? n : 0u;
}

/* Fault injection: a stop that fails is reported, and the capture after it
 * still sees the edges, so TestFirmwareTone's silence check fails. */
static void test_a_failed_stop_is_reported_and_its_edges_seen(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    stop_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_TRUE(edges_after_notone() > 0u);
}

/* Fault injection: a stop the driver accepts but the output ignores. */
static void test_an_ineffective_stop_shows_its_edges(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    stop_sticks = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_TRUE(edges_after_notone() > 0u);
}

/* A stop that works: the same capture, loop back kept, reads silence. */
static void test_a_working_stop_reads_silence(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(0u, edges_after_notone());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_stop_is_reported_and_its_edges_seen);
    RUN_TEST(test_an_ineffective_stop_shows_its_edges);
    RUN_TEST(test_a_working_stop_reads_silence);
    RUN_TEST(test_mode_input_after_tone_ends_the_loop_back);
    RUN_TEST(test_a_mode_output_pin_is_looped_back);
    RUN_TEST(test_a_mode_input_pin_is_not_looped_back);
    RUN_TEST(test_mode_on_another_pin_keeps_the_tone);
    RUN_TEST(test_a_refused_mode_keeps_the_tone);
    RUN_TEST(test_mode_input_after_notone_takes_the_pin_back);
    RUN_TEST(test_mode_on_another_pin_after_notone_keeps_the_pin);
    RUN_TEST(test_a_tone_moved_to_another_pin_is_released_only_there);
    RUN_TEST(test_a_mode_output_pin_a_moved_tone_resets_is_not_looped_back);
    RUN_TEST(test_a_mode_output_pin_an_adc_read_takes_is_not_looped_back);
    RUN_TEST(test_a_mode_output_pin_the_spi_bus_takes_for_miso_is_not_looped_back);
    RUN_TEST(test_mode_over_a_refused_stop_releases_the_pin_and_a_retry_stops_the_tone);
    return UNITY_END();
}
