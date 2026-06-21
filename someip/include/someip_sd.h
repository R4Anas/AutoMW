#ifndef SOMEIP_SD_H
#define SOMEIP_SD_H

#include <stdint.h>
#include "someip.h"

/*
 * SOME/IP-SD — Service Discovery
 *
 * Runs on UDP multicast 224.224.224.245:30490
 * Every ECU joins this multicast group at startup
 *
 * Three entry types we implement:
 * OFFER    — server announcing a service is available
 * FIND     — client looking for a service
 * SUBSCRIBE — client subscribing to event notifications
 *
 * AUTOSAR parallel:
 * In Classic AUTOSAR service availability is static — generated at build time
 * In Adaptive AUTOSAR / SOME/IP it is dynamic — discovered at runtime
 * This is the key architectural difference between Classic and Adaptive
 */

/* SD Header flags */
#define SOMEIP_SD_FLAG_REBOOT       0x80
#define SOMEIP_SD_FLAG_UNICAST      0x40

/* SD Entry types */
#define SOMEIP_SD_ENTRY_FIND        0x00  /* client looking for service */
#define SOMEIP_SD_ENTRY_OFFER       0x01  /* server announcing service  */
#define SOMEIP_SD_ENTRY_SUBSCRIBE   0x06  /* client subscribing to events */
#define SOMEIP_SD_ENTRY_SUBSCRIBE_ACK 0x07 /* server confirming subscription */

/*
 * SD Entry — fixed 16 bytes
 * Carried inside a SOME/IP message as payload
 * Service ID 0xFFFF = SD protocol identifier
 * Method  ID 0x8100 = SD method identifier
 */
typedef struct __attribute__((packed)) {
    uint8_t  type;            /* FIND, OFFER, SUBSCRIBE etc    */
    uint8_t  index_first;     /* option index — 0 for us       */
    uint8_t  index_second;    /* option index — 0 for us       */
    uint8_t  num_options;     /* number of options — 1 for us  */
    uint16_t service_id;      /* which service                 */
    uint16_t instance_id;     /* 0xFFFF = any instance         */
    uint8_t  major_ver;       /* service major version         */
    uint8_t  ttl[3];          /* time to live in seconds       */
    uint32_t minor_ver;       /* service minor version         */
} someip_sd_entry_t;

/*
 * SD IPv4 Endpoint Option — 12 bytes
 * Tells clients where to reach the service (IP + port + protocol)
 */
typedef struct __attribute__((packed)) {
    uint16_t length;          /* 0x0009 — fixed for IPv4 option */
    uint8_t  type;            /* 0x04 = IPv4 endpoint option    */
    uint8_t  reserved;        /* always 0                       */
    uint32_t ip_address;      /* server IP in network byte order */
    uint8_t  reserved2;       /* always 0                       */
    uint8_t  protocol;        /* 0x11 = UDP, 0x06 = TCP         */
    uint16_t port;            /* server port                    */
} someip_sd_option_ipv4_t;

#define SOMEIP_SD_PROTO_UDP         0x11
#define SOMEIP_SD_PROTO_TCP         0x06

/* SD Service IDs — fixed by SOME/IP spec */
#define SOMEIP_SD_SERVICE_ID        0xFFFF
#define SOMEIP_SD_METHOD_ID         0x8100
#define SOMEIP_SD_CLIENT_ID         0x0000
#define SOMEIP_SD_INSTANCE_ANY      0xFFFF

/* TTL helpers — TTL is 3 bytes big-endian */
#define SD_TTL_FOREVER              0xFFFFFF
#define SD_TTL_STOP                 0x000000

/* API */
int  someip_sd_create_socket      (void);
int  someip_sd_send_offer         (int sock,
                                   uint16_t service_id,
                                   uint32_t server_ip,
                                   uint16_t server_port);
int  someip_sd_send_find          (int sock,
                                   uint16_t service_id);
int  someip_sd_send_subscribe     (int sock,
                                   uint16_t service_id,
                                   uint32_t client_ip,
                                   uint16_t client_event_port);
int  someip_sd_send_subscribe_ack (int sock,
                                   uint16_t service_id,
                                   const char *client_ip);
int  someip_sd_receive            (int sock,
                                   uint8_t *out_type,
                                   uint16_t *out_service_id,
                                   uint32_t *out_ip,
                                   uint16_t *out_port);

#endif /* SOMEIP_SD_H */
