/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stand-in tfw_app_command: an application with no wire commands. Linked
 * beside src/core/app_runner.c when the application's app.mk does not set
 * APP_COMMANDS := yes (mk/app.mk), so APP_CMD reaches a running application and
 * every sub-op answers TFW_ERR_UNSUPPORTED with an empty reply. It reads and
 * writes nothing. See docs/application-sdk/adr-application-wire-commands.md.
 */
#include "tfw/app.h"

tfw_status_t tfw_app_command(uint8_t op, tfw_reader_t *r, tfw_writer_t *w)
{
    (void)op;
    (void)r;
    (void)w;
    return TFW_ERR_UNSUPPORTED;
}
