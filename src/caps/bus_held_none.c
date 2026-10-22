/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The I2C address list for a build that links no driver holding an address
 * of its own -- every build today but the OPTIGA Trust M transport's.
 *
 * bus.c asks tfw_bus_i2c_held() with no #if: the gate is which of this file
 * and src/crypto/optiga/optiga_i2c.c the object list carries (mk/firmware.mk's
 * BUS_HELD_SRCS), the same mechanism bus_none.c uses for tfw_bus_init(). The
 * element's own address and the reserved one are not here: bus.c refuses those
 * on every build.
 */

#include "bus.h"

uint8_t tfw_bus_i2c_held(uint8_t addr)
{
    (void)addr;
    return 0u;
}
