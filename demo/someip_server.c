#include "someip.h"
#include "someip_sd.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/select.h>
#include <arpa/inet.h>

/*
 * VehicleSpeed SOME/IP server
 *
 * Shows three concepts together:
 *
 *  1. Service Discovery (SOME/IP-SD)
 *     Server announces itself via UDP multicast at startup.
 *     When a client sends FindService, server re-announces so late-joining
 *     clients can always discover it.
 *
 *  2. Method calls (SOME/IP REQUEST / RESPONSE)
 *     Handles GetSpeed (0x0001) and GetOdometer (0x0002).
 *     Each response echoes client_id + session_id so the client can
 *     match the reply to its original request.
 *
 *  3. Socket multiplexing (select)
 *     Watches the SD socket (port 30490) and the SOME/IP socket (port 30000)
 *     simultaneously — same technique used in real Adaptive AUTOSAR stacks.
 *
 * Start this before the client.
 */
int main(void) {
    /* Our address — 127.0.0.1 for localhost demo */
    uint32_t my_ip;
    inet_pton(AF_INET, "127.0.0.1", &my_ip);

    int sd_sock = someip_sd_create_socket();
    if (sd_sock < 0) return 1;

    int sock = someip_create_socket(SOMEIP_SERVER_PORT);
    if (sock < 0) return 1;

    /* Announce service — all ECUs on the multicast group will hear this */
    someip_sd_send_offer(sd_sock, AUTOMW_SERVICE_VEHICLE, my_ip, SOMEIP_SERVER_PORT);

    printf("[SERVER] VehicleSpeed service is up on port %d\n", SOMEIP_SERVER_PORT);
    printf("[SERVER] Waiting for clients...\n\n");

    uint32_t speed_kph  = 120;
    uint32_t odometer_m = 54321;
    int requests_done   = 0;

    while (requests_done < 4) {
        /*
         * select() — block until activity on either socket.
         * SD socket:      respond to FindService with OfferService.
         * SOME/IP socket: respond to GetSpeed / GetOdometer calls.
         */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sd_sock, &rfds);
        FD_SET(sock, &rfds);
        int max_fd = (sd_sock > sock) ? sd_sock : sock;

        struct timeval timeout = { .tv_sec = 15, .tv_usec = 0 };
        if (select(max_fd + 1, &rfds, NULL, NULL, &timeout) <= 0) {
            printf("[SERVER] Timed out. Shutting down.\n");
            break;
        }

        /* SD socket: client is searching for us — re-announce */
        if (FD_ISSET(sd_sock, &rfds)) {
            uint8_t  sd_type;
            uint16_t sd_service_id;
            uint32_t dummy_ip   = 0;
            uint16_t dummy_port = 0;

            if (someip_sd_receive(sd_sock, &sd_type, &sd_service_id,
                                  &dummy_ip, &dummy_port) == 0) {
                if (sd_type == SOMEIP_SD_ENTRY_FIND &&
                    sd_service_id == AUTOMW_SERVICE_VEHICLE) {
                    printf("[SERVER] FindService received — re-announcing\n");
                    someip_sd_send_offer(sd_sock, AUTOMW_SERVICE_VEHICLE,
                                         my_ip, SOMEIP_SERVER_PORT);
                }
                /* Ignore: our own OfferService echo, unrelated SD messages */
            }
        }

        /* SOME/IP socket: client is calling a method — respond */
        if (FD_ISSET(sock, &rfds)) {
            someip_msg_t req;
            char     client_ip[INET_ADDRSTRLEN];
            uint16_t client_port;

            if (someip_receive(sock, &req, client_ip, &client_port) < 0)
                continue;

            printf("[SERVER] method=0x%04X from %s:%u (session=%u)\n",
                   req.header.method_id, client_ip, client_port,
                   req.header.session_id);

            uint32_t value    = 0;
            uint8_t  ret_code = SOMEIP_RET_OK;

            if (req.header.service_id != AUTOMW_SERVICE_VEHICLE) {
                ret_code = SOMEIP_RET_UNKNOWN_SERVICE;
            } else if (req.header.method_id == AUTOMW_METHOD_GET_SPEED) {
                value = speed_kph;
                printf("[SERVER] GetSpeed → %u km/h\n", speed_kph);
            } else if (req.header.method_id == AUTOMW_METHOD_GET_ODO) {
                value = odometer_m;
                printf("[SERVER] GetOdometer → %u m\n", odometer_m);
            } else {
                ret_code = SOMEIP_RET_UNKNOWN_METHOD;
            }

            someip_msg_t resp;
            someip_build_response(&resp, &req, ret_code, &value, sizeof(value));
            someip_send(sock, client_ip, client_port, &resp);

            requests_done++;
        }
    }

    printf("\n[SERVER] Handled %d requests. Done.\n", requests_done);
    someip_close_socket(sock);
    someip_close_socket(sd_sock);
    return 0;
}
