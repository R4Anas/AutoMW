#ifndef SOMEIP_H
#define SOMEIP_H

#include <stdint.h>
#include <stddef.h>

/*
 * SOME/IP fixed header — 16 bytes, always present
 * All multi-byte fields are big-endian (network byte order)
 * Use htons()/htonl() before sending, ntohs()/ntohl() after receiving
 *
 * AUTOSAR parallel: like a PDU header in COM stack
 * but standardized across vendors and carried over UDP/TCP
 */
typedef struct __attribute__((packed)) {
    uint16_t service_id;      /* which service e.g. 0x0101 VehicleSpeed */
    uint16_t method_id;       /* which method  e.g. 0x0001 GetSpeed     */
    uint32_t length;          /* bytes after this field (header+payload-8)*/
    uint16_t client_id;       /* identifies the client — echoed in response*/
    uint16_t session_id;      /* increments per request — correlation ID  */
    uint8_t  proto_ver;       /* always 0x01                              */
    uint8_t  iface_ver;       /* interface version — always 0x01 here     */
    uint8_t  msg_type;        /* REQUEST, RESPONSE, NOTIFICATION etc      */
    uint8_t  return_code;     /* 0x00 = OK, non-zero = error              */
} someip_header_t;

/* SOME/IP header is always 16 bytes */
#define SOMEIP_HEADER_SIZE  16

/*
 * Message types
 * Maps to AUTOSAR COM transfer kinds:
 * REQUEST        = C/S synchronous call
 * NOTIFICATION   = sender/receiver event
 * REQUEST_NO_RETURN = fire and forget
 */
#define SOMEIP_MSG_REQUEST          0x00
#define SOMEIP_MSG_REQUEST_NO_RETURN 0x01
#define SOMEIP_MSG_NOTIFICATION     0x02
#define SOMEIP_MSG_RESPONSE         0x80
#define SOMEIP_MSG_ERROR            0x81

/* Return codes */
#define SOMEIP_RET_OK               0x00
#define SOMEIP_RET_ERROR            0x01
#define SOMEIP_RET_UNKNOWN_SERVICE  0x02
#define SOMEIP_RET_UNKNOWN_METHOD   0x03

/* Protocol and interface version */
#define SOMEIP_PROTO_VER            0x01
#define SOMEIP_IFACE_VER            0x01

/*
 * AutoMW service definitions
 * In production these come from the ARXML service interface definition
 * Here we define them manually — same concept as AUTOSAR service IDs
 */
#define AUTOMW_SERVICE_VEHICLE      0x0101  /* VehicleSpeed service */
#define AUTOMW_METHOD_GET_SPEED     0x0001  /* GetSpeed method      */
#define AUTOMW_METHOD_GET_ODO       0x0002  /* GetOdometer method   */
#define AUTOMW_EVENT_SPEED_NOTIFY   0x8001  /* Speed notification event
                                               events use 0x8000+ range */

/* Max payload size */
#define SOMEIP_MAX_PAYLOAD          256

/* Full SOME/IP message — header + payload */
typedef struct {
    someip_header_t header;
    uint8_t         payload[SOMEIP_MAX_PAYLOAD];
    size_t          payload_len;
} someip_msg_t;

/*
 * SOME/IP over UDP port assignments
 * In production defined in ARXML network binding
 */
#define SOMEIP_SERVER_PORT          30000
#define SOMEIP_CLIENT_PORT          30001
#define SOMEIP_SD_PORT              30490   /* SOME/IP-SD standard port */
#define SOMEIP_SD_MULTICAST         "224.224.224.245" /* standard SD multicast */

/* API */
int  someip_create_socket (uint16_t port);
int  someip_send          (int sock,
                           const char *dest_ip,
                           uint16_t dest_port,
                           const someip_msg_t *msg);
int  someip_receive       (int sock,
                           someip_msg_t *out_msg,
                           char *out_src_ip,
                           uint16_t *out_src_port);
void someip_build_request (someip_msg_t *msg,
                           uint16_t service_id,
                           uint16_t method_id,
                           uint16_t client_id,
                           uint16_t session_id,
                           const void *payload,
                           size_t payload_len);
void someip_build_response(someip_msg_t *msg,
                           const someip_msg_t *req,
                           uint8_t return_code,
                           const void *payload,
                           size_t payload_len);
void someip_build_notification(someip_msg_t *msg,
                               uint16_t service_id,
                               uint16_t event_id,
                               const void *payload,
                               size_t payload_len);
void someip_close_socket  (int sock);

#endif /* SOMEIP_H */
