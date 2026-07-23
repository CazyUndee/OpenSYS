/*
 * icmp.h - ICMP Protocol Interface
 */

#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>
#include "net.h"

/* Start a ping — resets reply tracker */
void icmp_ping_start(uint16_t seq);

/* Poll whether a reply has been received for the current ping */
int icmp_ping_has_reply(void);

/* Send an ICMP echo request */
int icmp_send_echo_request(ip_addr_t dst, uint16_t id, uint16_t seq);

/* Handle a received ICMP packet (called from ip.c) */
int icmp_handle_packet(const ipv4_header_t* ip, const void* data, uint16_t len);

#endif
