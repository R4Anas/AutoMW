#include "someip.h"
#include "someip_sd.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/*
 * VehicleSpeed SOME/IP client
 *
 * Phase 1 — Service Discovery
 *   Broadcasts FindService on the SD multicast group.
 *   Waits up to 5 seconds for an OfferService reply to learn
 *   the server's IP and port. This is the Adaptive AUTOSAR way:
 *   no hardcoded addresses, services are found at runtime.
 *
 * Phase 2 — Method Calls
 *   Opens a SOME/IP socket and calls GetSpeed and GetOdometer.
 *   Each call is a REQUEST message; server replies with a RESPONSE
 *   that echoes session_id so we can match reply to request.
 *
 * Start the server (someip_server) before running this.
 */

static int call_method(int sock,
                        const char *server_ip, uint16_t server_port,
                        uint16_t method_id, uint16_t session_id,
                        const char *label)
{
    someip_msg_t req;
    someip_build_request(&req,
                         AUTOMW_SERVICE_VEHICLE,
                         method_id,
                         0x0042,      /* client_id — identifies this ECU  */
                         session_id,  /* incremented per call              */
                         NULL, 0);    /* no request payload for get-methods*/

    if (someip_send(sock, server_ip, server_port, &req) < 0) return -1;

    someip_msg_t resp;
    char     src_ip[INET_ADDRSTRLEN];
    uint16_t src_port;

    if (someip_receive(sock, &resp, src_ip, &src_port) < 0) {
        printf("[CLIENT] %s — no response (timeout?)\n", label);
        return -1;
    }

    if (resp.header.return_code != SOMEIP_RET_OK) {
        printf("[CLIENT] %s — error 0x%02X\n", label, resp.header.return_code);
        return -1;
    }

    uint32_t value = 0;
    memcpy(&value, resp.payload, sizeof(value));
    printf("[CLIENT] %-22s session=%u → %u\n", label, resp.header.session_id, value);
    return 0;
}

int main(void) {
    /* ── Phase 1: Service Discovery ── */
    printf("[CLIENT] Searching for VehicleSpeed service (0x%04X)...\n",
           AUTOMW_SERVICE_VEHICLE);

    int sd_sock = someip_sd_create_socket();
    if (sd_sock < 0) return 1;

    /* If the server doesn't reply within 5 seconds, fall back to localhost */
    struct timeval sd_timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sd_sock, SOL_SOCKET, SO_RCVTIMEO, &sd_timeout, sizeof(sd_timeout));

    someip_sd_send_find(sd_sock, AUTOMW_SERVICE_VEHICLE);

    char     server_ip[INET_ADDRSTRLEN] = "127.0.0.1"; /* fallback address */
    uint16_t server_port = SOMEIP_SERVER_PORT;

    /*
     * Loop over incoming SD messages — skip our own FindService echo
     * (IP_MULTICAST_LOOP means we receive what we send), wait for OFFER.
     */
    for (int i = 0; i < 10; i++) {
        uint8_t  sd_type       = 0;
        uint16_t sd_service_id = 0;
        uint32_t sd_ip         = 0;
        uint16_t sd_port       = 0;

        if (someip_sd_receive(sd_sock, &sd_type, &sd_service_id,
                              &sd_ip, &sd_port) < 0) {
            /* Timeout — server might not be running, use fallback */
            printf("[CLIENT] SD timeout — using fallback %s:%u\n",
                   server_ip, server_port);
            break;
        }

        if (sd_type == SOMEIP_SD_ENTRY_OFFER &&
            sd_service_id == AUTOMW_SERVICE_VEHICLE) {
            inet_ntop(AF_INET, &sd_ip, server_ip, sizeof(server_ip));
            server_port = sd_port;
            printf("[CLIENT] Service discovered at %s:%u\n", server_ip, server_port);
            break;
        }
        /* sd_type == SOMEIP_SD_ENTRY_FIND is our own echo — keep looping */
    }

    someip_close_socket(sd_sock);

    /* ── Phase 2: Method Calls ── */
    int sock = someip_create_socket(SOMEIP_CLIENT_PORT);
    if (sock < 0) return 1;

    /* 3-second receive timeout per call */
    struct timeval resp_timeout = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &resp_timeout, sizeof(resp_timeout));

    printf("\n[CLIENT] Calling service at %s:%u\n\n", server_ip, server_port);

    call_method(sock, server_ip, server_port, AUTOMW_METHOD_GET_SPEED, 0x0001, "GetSpeed (km/h)");
    call_method(sock, server_ip, server_port, AUTOMW_METHOD_GET_ODO,   0x0002, "GetOdometer (m)");

    someip_close_socket(sock);
    return 0;
}
