/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* tfwsim's Ethernet controller: include/tfw/eth.h over a host TCP socket. See
 * eth_socket.c. */
#ifndef TFWSIM_ETH_SOCKET_H
#define TFWSIM_ETH_SOCKET_H

/* Puts the controller on the bus. Until then tfw_eth_start() answers as an
 * absent one does. */
void tfwsim_eth_enable(void);

/* The socket, for the run loop to wait on; -1 while there is none. */
int  tfwsim_eth_fd(void);

#endif /* TFWSIM_ETH_SOCKET_H */
