/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_PWM_H
#define TFW_CAPS_PWM_H
#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
tfw_status_t tfw_pwm_cmd_write(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_tone_cmd_start(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_tone_cmd_stop(tfw_reader_t *r, tfw_writer_t *w);
#endif
