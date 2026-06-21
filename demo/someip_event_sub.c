#include "someip.h"
#include "someip_sd.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

/*
 * VehicleSpeed event subscriber — the "consumer" side of SOME/IP events.
 *
 * Two-phase operation:
 *
 *  Phase 1 — Subscribe (SD multicast)
 *    Broadcast to the SD multicast group: "I want VehicleSpeed events,
 *    push them to MY_IP:30003."
 *    Wait for SubscribeAck from the server on the same SD socket.
 *
 *  Phase 2 — Receive NOTIFICATION (SOME/IP unicast)
 *    The server now knows our address. It will push NOTIFICATION datagrams
 *    directly to our event socket (port 30003). We block on recvfrom()
 *    and print each speed update as it arrives.
 *
 * AUTOSAR parallel:
 *  Classic: receiver port reads signal from COM buffer, triggered by RTE.
 *  Adaptive: ara::com event.Subscribe() then event.GetNewSamples().
 *  Here:    SD subscribe → recvfrom() on a UDP socket.
 *
 * Start someip_notifier before running this.
 */

/* Port where this subscriber receives event notifications */
#define EVENT_SUB_PORT  30003

int main(void) {
    printf("[SUB] Subscribing to VehicleSpeed events (0x%04X)...\n\n",
           AUTOMW_SERVICE_VEHICLE);

    /* SD socket — used for Subscribe and SubscribeAck exchange */
    int sd_sock = someip_sd_create_socket();
    if (sd_sock < 0) return 1;

    /*
     * Event socket — must be open BEFORE we send Subscribe,
     * so the server can start pushing immediately after it sends Ack.
     * If we opened it after, the first notification might be lost.
     */
    int event_sock = someip_create_socket(EVENT_SUB_PORT);
    if (event_sock < 0) return 1;

    /* Phase 1a: send Subscribe carrying our event endpoint */
    uint32_t my_ip;
    inet_pton(AF_INET, "127.0.0.1", &my_ip);

    someip_sd_send_subscribe(sd_sock,
                              AUTOMW_SERVICE_VEHICLE,
                              my_ip,
                              EVENT_SUB_PORT);

    /* Phase 1b: wait for SubscribeAck — 5-second window */
    struct timeval ack_timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sd_sock, SOL_SOCKET, SO_RCVTIMEO, &ack_timeout, sizeof(ack_timeout));

    printf("[SUB] Waiting for SubscribeAck...\n");

    int acked = 0;
    for (int i = 0; i < 15 && !acked; i++) {
        uint8_t  sd_type       = 0;
        uint16_t sd_service_id = 0;
        uint32_t dummy_ip      = 0;
        uint16_t dummy_port    = 0;

        if (someip_sd_receive(sd_sock, &sd_type, &sd_service_id,
                              &dummy_ip, &dummy_port) < 0)
            break;   /* timeout */

        if (sd_type == SOMEIP_SD_ENTRY_SUBSCRIBE_ACK &&
            sd_service_id == AUTOMW_SERVICE_VEHICLE) {
            printf("[SUB] SubscribeAck received — events incoming!\n\n");
            acked = 1;
        }
        /*
         * Other messages seen here due to IP_MULTICAST_LOOP:
         *   - Our own Subscribe echo (type=0x06) — skip
         *   - Notifier's OfferService (type=0x01) — skip
         */
    }

    if (!acked)
        printf("[SUB] No Ack received — continuing anyway (may miss first event)\n\n");

    someip_close_socket(sd_sock);

    /*
     * Phase 2: receive NOTIFICATION datagrams pushed by the notifier.
     *
     * The server sends these directly (unicast) to 127.0.0.1:30003 —
     * not to multicast. We just block on recvfrom() and print.
     *
     * 3-second timeout per event — if notifier is slow or done, we exit.
     */
    struct timeval event_timeout = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(event_sock, SOL_SOCKET, SO_RCVTIMEO,
               &event_timeout, sizeof(event_timeout));

    for (int i = 0; i < 5; i++) {
        someip_msg_t notif;
        char     src_ip[INET_ADDRSTRLEN];
        uint16_t src_port;

        if (someip_receive(event_sock, &notif, src_ip, &src_port) < 0) {
            printf("[SUB] Timeout — no more events.\n");
            break;
        }

        if (notif.header.method_id == AUTOMW_EVENT_SPEED_NOTIFY &&
            notif.header.msg_type  == SOMEIP_MSG_NOTIFICATION) {

            uint32_t speed = 0;
            memcpy(&speed, notif.payload, sizeof(speed));

            printf("[SUB] SPEED EVENT  session=%-4u  speed = %u km/h\n",
                   notif.header.session_id, speed);
        }
    }

    someip_close_socket(event_sock);
    return 0;
}
