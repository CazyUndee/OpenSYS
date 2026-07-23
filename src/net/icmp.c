/*
 * icmp.c - Minimal ICMP Support (ping)
 */

#include <stdint.h>
#include "net.h"
#include "ip.h"

/* ICMP header */
typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint8_t  code;
  uint16_t checksum;
  uint16_t id;
  uint16_t seq;
} icmp_header_t;

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO       8

/* Reply tracking (single outstanding ping) */
static uint8_t  ping_reply_received = 0;
static uint16_t ping_reply_seq       = 0;

static uint16_t icmp_checksum(const void* data, uint16_t len) {
  uint32_t sum = 0;
  const uint16_t* ptr = (const uint16_t*)data;
  for (int i = 0; i < len / 2; i++) {
    sum += ptr[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  }
  if (len & 1) sum += ((const uint8_t*)data)[len - 1] << 8;
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return (uint16_t)(~sum);
}

/* Reset reply state before issuing a new ping */
void icmp_ping_start(uint16_t seq) {
  ping_reply_received = 0;
  ping_reply_seq = seq;
}

/* Return 1 if a reply was received since the last ping_start */
int icmp_ping_has_reply(void) {
  return ping_reply_received;
}

int icmp_send_echo_request(ip_addr_t dst, uint16_t id, uint16_t seq) {
  /* Build an ICMP echo request packet with IP header */
  uint8_t packet[64]; /* Enough for ICMP echo request */

  /* IP header (20 bytes) */
  ipv4_header_t* ip_hdr = (ipv4_header_t*)packet;
  ip_hdr->version_ihl = 0x45;   /* IPv4, 5 words (20 bytes) */
  ip_hdr->tos = 0;
  ip_hdr->total_len = 20 + 8;   /* IP header + ICMP header only */
  ip_hdr->id = 0;
  ip_hdr->frag_off = 0;
  ip_hdr->ttl = 64;
  ip_hdr->proto = NET_PROTO_ICMP;
  ip_hdr->checksum = 0;
  ip_hdr->src = 0; /* Filled in by sender */
  ip_hdr->dst = (uint32_t)dst.addr[0] << 24
              | (uint32_t)dst.addr[1] << 16
              | (uint32_t)dst.addr[2] << 8
              | (uint32_t)dst.addr[3];
  ip_hdr->checksum = ip_checksum16(ip_hdr, 20);

  /* ICMP echo request (8 bytes) */
  icmp_header_t* icmp = (icmp_header_t*)(packet + 20);
  icmp->type = ICMP_ECHO;
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->id   = id;
  icmp->seq  = seq;
  icmp->checksum = icmp_checksum(icmp, 8);

  return net_send_packet(packet, 28);
}

int icmp_handle_packet(const ipv4_header_t* ip, const void* data, uint16_t len) {
  (void)ip;
  if (len < sizeof(icmp_header_t)) return -1;
  const icmp_header_t* icmp = (const icmp_header_t*)data;

  if (icmp->type == ICMP_ECHO_REPLY) {
    /* Accept only the first reply matching our outstanding ping */
    if (!ping_reply_received && icmp->seq == ping_reply_seq) {
      ping_reply_received = 1;
    }
    return 0;
  }
  return -1;
}
