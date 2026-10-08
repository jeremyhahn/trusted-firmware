/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The element-tier simulator's view of the host board profile: the real one,
 * with the operator-signature auth session switched on.
 *
 * boards/atmega328p/board_profile.h turns TFW_FEATURE_AUTH_SESSION off because
 * the feature does not fit that part's flash (see the measurement there). That
 * is a fact about silicon, and tfwsim-atecc608a runs on a host. The element tier
 * is the only tier whose AUTH_CHALLENGE/AUTH_RESPONSE can verify anything, and
 * the simulator is for exercising what that tier does, so it builds with the
 * session on. This file is found BEFORE the board's, by -Itools/tfwsim/element,
 * and includes the board's own for everything else.
 *
 * It is on the include path of tfwsim-atecc608a only. No AVR image, no unit
 * suite and no software-tier simulator sees it.
 */
#ifndef TFWSIM_ELEMENT_BOARD_PROFILE_H
#define TFWSIM_ELEMENT_BOARD_PROFILE_H

#include_next "board_profile.h"

#undef  TFW_FEATURE_AUTH_SESSION
#define TFW_FEATURE_AUTH_SESSION 1

#endif /* TFWSIM_ELEMENT_BOARD_PROFILE_H */
